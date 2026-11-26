#include "engine/MixerEngine.h"
#include <chrono>
#include <cmath>
#include <optional>

MixerEngine::MixerEngine()
{
    juce::addDefaultFormatsToManager (formatManager);
    scanner = std::make_unique<PluginScanner> (formatManager);

    for (auto& s : strips)
        s = std::make_unique<ChannelStrip>();
    for (int s = 0; s < kDefaultStrips; ++s)
        strips[(size_t) s]->active.store (true);

    int pair = 0;
    for (int b = 0; b < kNumBuses; ++b)
    {
        buses[(size_t) b].active.store (isDefaultBus (b));
        buses[(size_t) b].outputPair.store (isDefaultBus (b) ? pair++ : 0);   // A1 -> outs 1/2, A2 -> 3/4, B1 -> 5/6
    }
    buses[0].monitor.store (true);   // A1 (usually headphones) is where solo goes
}

MixerEngine::~MixerEngine()
{
    undoAllAppRedirects();
    deviceManager.removeAudioCallback (this);
    deviceManager.closeAudioDevice();

    // Stop secondary devices and app captures before anything else goes away.
    for (auto& bus : buses)
    {
        const juce::ScopedLock sl (bus.sinkLock);
        bus.sink.reset();
    }
    for (auto& s : strips)
        s->setExternalSource (nullptr);
}

void MixerEngine::start (const juce::XmlElement* savedDeviceState)
{
    const auto error = deviceManager.initialise (8, 8, savedDeviceState, true);
    if (error.isNotEmpty())
    {
        juce::Logger::writeToLog ("Audio device error: " + error);
    }

    deviceManager.addAudioCallback (this);
}

//==============================================================================
// Device lists

juce::StringArray MixerEngine::getActiveInputNames() const
{
    juce::StringArray result;
    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        const auto names = device->getInputChannelNames();
        const auto active = device->getActiveInputChannels();
        for (int i = 0; i < names.size(); ++i)
            if (active[i])
                result.add (names[i]);
    }
    return result;
}

juce::StringArray MixerEngine::getActiveOutputPairNames() const
{
    juce::StringArray result;
    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        const auto names = device->getOutputChannelNames();
        const auto active = device->getActiveOutputChannels();
        juce::StringArray activeNames;
        for (int i = 0; i < names.size(); ++i)
            if (active[i])
                activeNames.add (names[i]);

        for (int i = 0; i + 1 < activeNames.size(); i += 2)
            result.add (activeNames[i] + " + " + activeNames[i + 1]);
        if (activeNames.size() % 2 == 1)
            result.add (activeNames[activeNames.size() - 1] + " (mono)");
    }
    return result;
}

juce::AudioIODeviceType* MixerEngine::getWindowsAudioType()
{
    if (windowsType == nullptr)
        windowsType.reset (juce::AudioIODeviceType::createAudioIODeviceType_WASAPI (juce::WASAPIDeviceMode::shared));
    return windowsType.get();
}

juce::StringArray MixerEngine::getWindowsInputNames()
{
    if (auto* type = getWindowsAudioType())
    {
        type->scanForDevices();
        return type->getDeviceNames (true);
    }
    return {};
}

juce::StringArray MixerEngine::getWindowsOutputNames()
{
    if (auto* type = getWindowsAudioType())
    {
        type->scanForDevices();
        return type->getDeviceNames (false);
    }
    return {};
}

//==============================================================================
// Routing (message thread)

void MixerEngine::setStripSourceNone (int s)
{
    auto& strip = getStrip (s);
    undoAppRedirect (s);
    strip.sourceType.store ((int) SourceType::None);
    strip.setExternalSource (nullptr);
}

void MixerEngine::setStripSourceMain (int s, int channel, int rightChannel)
{
    auto& strip = getStrip (s);
    undoAppRedirect (s);
    strip.inputChannel.store (channel);
    strip.inputChannelRight.store (rightChannel);
    strip.sourceType.store ((int) SourceType::Main);
    strip.setExternalSource (nullptr);
}

bool MixerEngine::setStripSourceDevice (int s, const juce::String& name, juce::String& error)
{
    auto& strip = getStrip (s);
    undoAppRedirect (s);
    strip.sourceDeviceName = name;
    strip.sourceType.store ((int) SourceType::Device);

    auto* type = getWindowsAudioType();
    if (type == nullptr)
    {
        error = "Windows Audio isn't available";
        strip.setExternalSource (nullptr);
        return false;
    }

    type->scanForDevices();
    auto source = DeviceInputSource::open (*type, name, getSampleRate(), error);
    strip.setExternalSource (std::move (source));
    return strip.getExternalSource() != nullptr;
}

bool MixerEngine::setStripSourceApp (int s, const juce::String& exeName, juce::String& error)
{
    auto& strip = getStrip (s);
    if (! strip.sourceAppExe.equalsIgnoreCase (exeName))
    {
        undoAppRedirect (s);
        strip.appOutputDeviceId.clear();
        strip.appOutputDeviceName.clear();
    }
    strip.sourceAppExe = exeName;
    strip.sourceType.store ((int) SourceType::App);

    const auto pid = findAudioAppByExe (exeName.toWideCharPointer());
    if (pid == 0)
    {
        // Not running yet: maintainSources() connects it when it starts playing.
        strip.setExternalSource (nullptr);
        return true;
    }

    auto source = AppLoopbackSource::open (pid, error);
    strip.setExternalSource (std::move (source));

    if (strip.getExternalSource() != nullptr && strip.appOutputDeviceId.isNotEmpty())
    {
        juce::String routeError;
        setStripAppOutput (s, strip.appOutputDeviceId, strip.appOutputDeviceName, routeError);
    }
    return strip.getExternalSource() != nullptr;
}

bool MixerEngine::setStripSourceLoopback (int s, const juce::String& endpointId, const juce::String& name, juce::String& error)
{
    auto& strip = getStrip (s);
    undoAppRedirect (s);
    strip.loopbackId = endpointId;
    strip.loopbackName = name;
    strip.sourceType.store ((int) SourceType::Loopback);

    strip.setExternalSource (DeviceLoopbackSource::open (endpointId, error));
    return strip.getExternalSource() != nullptr;
}

juce::StringArray MixerEngine::getOwnOutputDeviceNames() const
{
    juce::StringArray names;
    if (auto* device = deviceManager.getCurrentAudioDevice())
        names.add (device->getName());
    for (auto& b : buses)
        if (b.active.load() && (BusTarget) b.target.load() == BusTarget::Device && b.deviceName.isNotEmpty())
            names.addIfNotAlreadyThere (b.deviceName);
    return names;
}

bool MixerEngine::setStripAppOutput (int s, const juce::String& deviceId, const juce::String& deviceName, juce::String& error)
{
    auto& strip = getStrip (s);
    strip.appOutputDeviceId = deviceId;
    strip.appOutputDeviceName = deviceName;

    auto* app = dynamic_cast<AppLoopbackSource*> (strip.getExternalSource());
    if (app == nullptr)
        return true;   // applied when the app is captured next

    if (deviceId.isEmpty())
    {
        undoAppRedirect (s);
        return true;
    }

    std::string err;
    if (! setAppOutputDevice (app->getPid(), deviceId.toWideCharPointer(), err))
    {
        error = juce::String (err);
        return false;
    }
    strip.redirectedPid = app->getPid();
    return true;
}

void MixerEngine::undoAppRedirect (int s)
{
    auto& strip = getStrip (s);
    if (strip.redirectedPid == 0)
        return;

    if (isProcessAlive (strip.redirectedPid))
    {
        std::string err;
        setAppOutputDevice (strip.redirectedPid, L"", err);   // back to the Windows default
    }
    strip.redirectedPid = 0;
}

void MixerEngine::undoAllAppRedirects()
{
    for (int s = 0; s < kNumStrips; ++s)
        undoAppRedirect (s);
}

int MixerEngine::addStrip()
{
    for (int s = 0; s < kNumStrips; ++s)
    {
        auto& strip = getStrip (s);
        if (! strip.active.load())
        {
            strip.resetToDefaults();
            strip.active.store (true);
            return s;
        }
    }
    return -1;
}

void MixerEngine::removeStrip (int s)
{
    auto& strip = getStrip (s);
    strip.active.store (false);
    undoAppRedirect (s);
    strip.resetToDefaults();

    // Strips that ducked under this one stop ducking.
    for (auto& other : strips)
        if (other->fx.duckSource.load() == s)
        {
            other->fx.duckSource.store (-1);
            other->fx.duckOn.store (false);
        }
}

void MixerEngine::addBus (int b)
{
    if (juce::isPositiveAndBelow (b, kNumBuses))
        buses[(size_t) b].active.store (true);
}

void MixerEngine::removeBus (int b)
{
    if (! juce::isPositiveAndBelow (b, kNumBuses))
        return;

    setBusOff (b);
    auto& bus = buses[(size_t) b];
    bus.active.store (false);
    bus.monitor.store (false);
    bus.faderDb.store (0.0f);
    bus.mute.store (false);
    bus.normOn.store (false);
    bus.normTargetLufs.store (-14.0f);
    bus.normGain.store (1.0f);
    bus.inserts.removeAllPlugins();
    bus.customName.clear();
    bus.fx.eqOn.store (false);   bus.fx.eqLowDb.store (0.0f); bus.fx.eqMidDb.store (0.0f); bus.fx.eqMidHz.store (1000.0f); bus.fx.eqHighDb.store (0.0f);
    bus.fx.compOn.store (false); bus.fx.compThresholdDb.store (-18.0f); bus.fx.compRatio.store (3.0f);
    for (auto& s : strips)
        s->busOn[b].store (false);
}

void MixerEngine::setMonitorBus (int b)
{
    for (int i = 0; i < kNumBuses; ++i)
        buses[(size_t) i].monitor.store (i == b);
}

int MixerEngine::getNumActiveStrips() const
{
    int n = 0;
    for (auto& s : strips)
        n += s->active.load() ? 1 : 0;
    return n;
}

int MixerEngine::getNumActiveBuses() const
{
    int n = 0;
    for (auto& b : buses)
        n += b.active.load() ? 1 : 0;
    return n;
}

//==============================================================================
// Solo + named strip presets (see MixerEngine.h)

int MixerEngine::getMonitorBus() const noexcept
{
    for (int b = 0; b < kNumBuses; ++b)
        if (buses[(size_t) b].monitor.load())
            return b;
    return -1;
}

bool MixerEngine::isSoloActive() const noexcept
{
    for (auto& s : strips)
        if (s->active.load() && s->solo.load())
            return true;
    return false;
}

bool MixerEngine::isStripMutedBySolo (int strip) const noexcept
{
    if (! juce::isPositiveAndBelow (strip, kNumStrips) || ! isSoloActive())
        return false;

    const int monitor = getMonitorBus();
    if (monitor < 0)
        return false;   // no monitor bus: solo silences nothing

    const auto& s = *strips[(size_t) strip];
    if (s.solo.load())
        return false;
    // Solo-safe strips routed to the monitor bus keep playing (see StripLogic.h).
    return ! omx::stripAudibleOnMonitorBus (false, s.soloSafe.load(), s.busOn[(size_t) monitor].load());
}

//==============================================================================
// Bus names / groups (see MixerEngine.h)

juce::String MixerEngine::getBusName (int bus) const
{
    if (! juce::isPositiveAndBelow (bus, kNumBuses))
        return {};
    const auto& custom = buses[(size_t) bus].customName;
    return custom.isNotEmpty() ? custom : juce::String (kBusNames[bus]);
}

void MixerEngine::setBusName (int bus, const juce::String& name)
{
    if (! juce::isPositiveAndBelow (bus, kNumBuses))
        return;
    buses[(size_t) bus].customName = name.trim();
}

int MixerEngine::findNextFreeSlotInGroup (bool virtualGroup) const noexcept
{
    const int first = omx::firstSlotInGroup (virtualGroup);
    for (int b = first; b < first + 3; ++b)
        if (! buses[(size_t) b].active.load())
            return b;
    return -1;
}

juce::StringArray MixerEngine::getStripPresetNames() const
{
    juce::StringArray names;
    for (const auto& p : stripPresets)
        names.add (p.name);
    return names;
}

const omx::StripPreset* MixerEngine::findStripPreset (const juce::String& name) const
{
    for (const auto& p : stripPresets)
        if (p.name == name)
            return &p.preset;
    return nullptr;
}

bool MixerEngine::saveStripPreset (const juce::String& name, int strip)
{
    if (name.trim().isEmpty() || ! juce::isPositiveAndBelow (strip, kNumStrips))
        return false;

    auto preset = getStrip (strip).savePreset();
    for (auto& p : stripPresets)
        if (p.name == name)
        {
            p.preset = std::move (preset);
            return true;
        }

    stripPresets.push_back ({ name, std::move (preset) });
    return true;
}

int MixerEngine::applyStripPreset (const juce::String& name, int strip)
{
    for (int i = 0; i < (int) stripPresets.size(); ++i)
        if (stripPresets[(size_t) i].name == name)
            return applyStripPreset (i, strip);
    return -1;
}

int MixerEngine::applyStripPreset (int presetIndex, int strip)
{
    if (! juce::isPositiveAndBelow (presetIndex, (int) stripPresets.size())
        || ! juce::isPositiveAndBelow (strip, kNumStrips))
        return -1;

    auto& target = getStrip (strip);
    const int missing = target.loadPreset (stripPresets[(size_t) presetIndex].preset, formatManager,
                                           [this] (const juce::String& file) { return scanner != nullptr && scanner->isBlacklisted (file); });

    // A preset saved on another strip may duck under this very strip: a strip can't
    // duck under itself, so that part is dropped.
    if (target.fx.duckSource.load() == strip)
    {
        target.fx.duckSource.store (-1);
        target.fx.duckOn.store (false);
    }
    return missing;
}

bool MixerEngine::deleteStripPreset (const juce::String& name)
{
    for (auto it = stripPresets.begin(); it != stripPresets.end(); ++it)
        if (it->name == name)
        {
            stripPresets.erase (it);
            return true;
        }
    return false;
}

void MixerEngine::maintainSources()
{
    // Device lists are only scanned if something actually needs reconnecting.
    std::optional<juce::StringArray> inputNames, outputNames;

    for (int i = 0; i < kNumStrips; ++i)
    {
        auto& s = *strips[(size_t) i];
        if (! s.active.load())
            continue;
        auto* current = s.getExternalSource();

        switch ((SourceType) s.sourceType.load())
        {
            case SourceType::Device:
            {
                if (current != nullptr && current->isHealthy())
                    break;

                if (current != nullptr)
                    s.setExternalSource (nullptr);   // unplugged / stalled

                if (! inputNames)
                    inputNames = getWindowsInputNames();

                if (inputNames->contains (s.sourceDeviceName))
                {
                    juce::String error;
                    setStripSourceDevice (i, s.sourceDeviceName, error);
                }
                break;
            }

            case SourceType::App:
            {
                if (s.sourceAppExe.isEmpty())
                    break;

                auto* app = dynamic_cast<AppLoopbackSource*> (current);
                if (app != nullptr && app->isHealthy() && isProcessAlive (app->getPid()))
                    break;

                if (current != nullptr)
                    s.setExternalSource (nullptr);   // app exited or stream died

                if (const auto pid = findAudioAppByExe (s.sourceAppExe.toWideCharPointer()); pid != 0)
                {
                    juce::String error;
                    s.redirectedPid = 0;   // the old process is gone
                    s.setExternalSource (AppLoopbackSource::open (pid, error));
                    if (s.getExternalSource() != nullptr && s.appOutputDeviceId.isNotEmpty())
                        setStripAppOutput (i, s.appOutputDeviceId, s.appOutputDeviceName, error);
                }
                break;
            }

            case SourceType::Loopback:
            {
                if (current != nullptr && current->isHealthy())
                    break;
                if (current != nullptr)
                    s.setExternalSource (nullptr);   // device removed or stream invalidated

                // Reconnect by endpoint ID, or by name if Windows gave the device a new ID.
                for (const auto& d : listRenderDevices())
                {
                    const juce::String id (d.id.c_str()), name (d.name.c_str());
                    if (id == s.loopbackId || name == s.loopbackName)
                    {
                        juce::String error;
                        setStripSourceLoopback (i, id, name, error);
                        break;
                    }
                }
                break;
            }

            case SourceType::None:
            case SourceType::Main:
                break;
        }
    }

    for (int b = 0; b < kNumBuses; ++b)
    {
        auto& bus = buses[(size_t) b];
        if ((BusTarget) bus.target.load() != BusTarget::Device || bus.deviceName.isEmpty())
            continue;

        if (bus.sink != nullptr && bus.sink->isHealthy())
            continue;

        if (bus.sink != nullptr)
        {
            std::unique_ptr<DeviceOutputSink> dead;
            {
                const juce::ScopedLock sl (bus.sinkLock);
                std::swap (dead, bus.sink);
            }
        }

        if (! outputNames)
            outputNames = getWindowsOutputNames();

        if (outputNames->contains (bus.deviceName))
        {
            juce::String error;
            const auto name = bus.deviceName;
            setBusDevice (b, name, error);
        }
    }
}

void MixerEngine::setBusOff (int b)
{
    auto& bus = getBus (b);
    bus.target.store ((int) BusTarget::Off);

    std::unique_ptr<DeviceOutputSink> old;
    {
        const juce::ScopedLock sl (bus.sinkLock);
        std::swap (old, bus.sink);
    }
}

void MixerEngine::setBusMainPair (int b, int pair)
{
    setBusOff (b);
    auto& bus = getBus (b);
    bus.outputPair.store (pair);
    bus.target.store ((int) BusTarget::MainPair);
}

bool MixerEngine::setBusDevice (int b, const juce::String& name, juce::String& error)
{
    setBusOff (b);
    auto& bus = getBus (b);
    bus.deviceName = name;

    auto* type = getWindowsAudioType();
    if (type == nullptr)
    {
        error = "Windows Audio isn't available";
        return false;
    }

    type->scanForDevices();
    auto sink = DeviceOutputSink::open (*type, name, getSampleRate(), error);
    if (sink == nullptr)
        return false;

    {
        const juce::ScopedLock sl (bus.sinkLock);
        bus.sink = std::move (sink);
    }
    bus.target.store ((int) BusTarget::Device);
    return true;
}

int MixerEngine::getLinkDropouts() const
{
    int total = 0;
    for (auto& s : strips)
        if (auto* src = s->getExternalSource())
            total += src->bridge.underruns.load() + src->bridge.overruns.load();

    for (auto& bus : buses)
        if (bus.sink != nullptr)
            total += bus.sink->bridge.underruns.load() + bus.sink->bridge.overruns.load();

    return total;
}

bool MixerEngine::mainDeviceHasSplitClocks() const
{
    auto* device = deviceManager.getCurrentAudioDevice();
    if (device == nullptr || device->getTypeName().containsIgnoreCase ("ASIO"))
        return false;

    const auto setup = deviceManager.getAudioDeviceSetup();
    return setup.inputDeviceName.isNotEmpty() && setup.outputDeviceName.isNotEmpty()
        && setup.inputDeviceName != setup.outputDeviceName
        && ! device->getActiveInputChannels().isZero();
}

//==============================================================================
// Audio

void MixerEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    const double sr = device->getCurrentSampleRate();
    currentSampleRate.store (sr);
    maxBlock = juce::jmax (32, device->getCurrentBufferSizeSamples());

    stripBuffer.setSize (2, maxBlock);
    preBuffer.setSize (2, maxBlock);
    soundBuffer.setSize (2, maxBlock);
    soundboard.prepare (sr);
    if (replay.isEnabled())
        replay.configure (sr, replay.getSeconds());
    for (auto& g : soundSend)
    {
        g.reset (sr, 0.02);
        g.setCurrentAndTargetValue (0.0f);
    }
    sourceBuffer.setSize (2, maxBlock);
    busBuffer.setSize (kNumBuses * 2, maxBlock);
    busDry.setSize (2, maxBlock);

    for (int b = 0; b < kNumBuses; ++b)
    {
        busGain[(size_t) b].reset (sr, 0.02);
        busGain[(size_t) b].setCurrentAndTargetValue (dbToGain (buses[(size_t) b].faderDb.load()));
        busLimiter[(size_t) b].prepare (sr);
        busNorm[(size_t) b].prepare (sr);
        busRmsL[(size_t) b].prepare (sr);
        busRmsR[(size_t) b].prepare (sr);
        busEq[(size_t) b].prepare (sr);
        busComp[(size_t) b].prepare (sr);
        for (auto* mix : { &busEqMix[(size_t) b], &busCompMix[(size_t) b] })
            mix->reset (sr, 0.02);
        busEqMix[(size_t) b].setCurrentAndTargetValue (buses[(size_t) b].fx.eqOn.load() ? 1.0f : 0.0f);
        busCompMix[(size_t) b].setCurrentAndTargetValue (buses[(size_t) b].fx.compOn.load() ? 1.0f : 0.0f);
        buses[(size_t) b].inserts.prepare (sr, maxBlock);
    }

    for (int s = 0; s < kNumStrips; ++s)
    {
        strips[(size_t) s]->prepare (sr, maxBlock);
        for (int b = 0; b < kNumBuses; ++b)
        {
            auto& g = sendGain[(size_t) s][(size_t) b];
            g.reset (sr, 0.02);
            g.setCurrentAndTargetValue (0.0f);   // sends fade in on start
        }
    }
}

void MixerEngine::audioDeviceStopped()
{
    for (auto& s : strips)
        s->release();
    for (auto& b : buses)
        b.inserts.release();
}

void MixerEngine::audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                                    float* const* outputs, int numOutputs,
                                                    int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    juce::ScopedNoDenormals noDenormals;

    for (int ch = 0; ch < numOutputs; ++ch)
        if (outputs[ch] != nullptr)
            juce::FloatVectorOperations::clear (outputs[ch], numSamples);

    // CPU meter: one clock read per block, around the whole callback.
    const auto cpuStart = std::chrono::steady_clock::now();

    // Some drivers deliver more samples than announced; process in chunks.
    for (int offset = 0; offset < numSamples; offset += maxBlock)
        processChunk (inputs, numInputs, outputs, numOutputs, offset, juce::jmin (maxBlock, numSamples - offset));

    const double cpuSeconds = std::chrono::duration<double> (
        std::chrono::steady_clock::now() - cpuStart).count();
    cpuMeter.addSample (cpuSeconds, numSamples / currentSampleRate.load());
    cpuUsagePercent.store (cpuMeter.getPercent(), std::memory_order_relaxed);
}

void MixerEngine::processChunk (const float* const* inputs, int numInputs,
                                float* const* outputs, int numOutputs, int offset, int n) noexcept
{
    const double sr = currentSampleRate.load();
    busBuffer.clear (0, n);

    // Plugin delay compensation: every strip is delayed to match the slowest one, so strips
    // that meet on a bus stay in time (e.g. a mic with a lookahead denoiser + its own echo).
    int maxLatency = 0;
    bool anySolo = false;
    for (auto& st : strips)
        if (st->active.load())
        {
            maxLatency = juce::jmax (maxLatency, st->latencySamples.load (std::memory_order_relaxed));
            anySolo = anySolo || st->solo.load();
        }
    for (auto& st : strips)
        st->alignDelay.store (maxLatency - st->latencySamples.load (std::memory_order_relaxed), std::memory_order_relaxed);

    // Ducking: a strip gets quieter while its source strip had signal in the last block.
    for (auto& st : strips)
    {
        const int src = st->fx.duckSource.load();
        float target = 1.0f;
        if (st->fx.duckOn.load() && juce::isPositiveAndBelow (src, kNumStrips) && strips[(size_t) src]->active.load())
        {
            const float level = strips[(size_t) src]->sidechainLevel.load (std::memory_order_relaxed);
            if (juce::Decibels::gainToDecibels (level, -100.0f) > st->fx.duckThresholdDb.load())
                target = juce::Decibels::decibelsToGain (st->fx.duckDepthDb.load());
        }
        st->duckTarget.store (target, std::memory_order_relaxed);
    }

    for (int s = 0; s < kNumStrips; ++s)
    {
        auto& strip = *strips[(size_t) s];
        if (! strip.active.load())
            continue;

        const float* inL = nullptr;
        const float* inR = nullptr;

        switch ((SourceType) strip.sourceType.load())
        {
            case SourceType::Main:
            {
                const int in = strip.inputChannel.load();
                const int inRight = strip.inputChannelRight.load();
                if (in >= 0 && in < numInputs && inputs[in] != nullptr)
                {
                    inL = inR = inputs[in] + offset;
                    if (inRight >= 0 && inRight < numInputs && inputs[inRight] != nullptr)
                        inR = inputs[inRight] + offset;   // stereo pair
                }
                break;
            }

            case SourceType::Device:
            case SourceType::App:
            case SourceType::Loopback:
            {
                auto* l = sourceBuffer.getWritePointer (0);
                auto* r = sourceBuffer.getWritePointer (1);
                if (strip.pullExternal (l, r, n, sr))
                {
                    inL = l;
                    inR = r;
                }
                break;
            }

            case SourceType::None:
                break;
        }

        strip.process (inL, inR, stripBuffer, preBuffer, n);
        recorder.writeStrip (s, stripBuffer.getReadPointer (0), stripBuffer.getReadPointer (1), n);

        for (int b = 0; b < kNumBuses; ++b)
        {
            auto& bus = buses[(size_t) b];
            auto& g = sendGain[(size_t) s][(size_t) b];

            // The monitor bus plays only soloed strips while anything is soloed (after-fader listen).
            // Other buses (stream, Discord) are never affected by solo. Solo-safe strips
            // routed to the monitor bus keep playing at their normal send level.
            const bool soloing = anySolo && bus.monitor.load();
            const bool soloed = strip.solo.load();
            const bool on = bus.active.load()
                && (soloing ? omx::stripAudibleOnMonitorBus (soloed, strip.soloSafe.load(), strip.busOn[b].load())
                            : strip.busOn[b].load());
            const float level = soloing ? (soloed ? 1.0f : juce::Decibels::decibelsToGain (strip.sendDb[b].load(), -60.0f))
                                        : juce::Decibels::decibelsToGain (strip.sendDb[b].load(), -60.0f);
            g.setTargetValue (on ? level : 0.0f);

            if (! g.isSmoothing() && g.getTargetValue() == 0.0f)
                continue;

            // Soloed strips are heard post-fader (after-fader listen); everything else,
            // including a solo-safe strip, honours its pre/post-fader send setting.
            const auto& src = (soloing && soloed) ? stripBuffer
                              : (strip.sendPre[b].load() ? preBuffer : stripBuffer);
            const float startGain = g.getCurrentValue();
            g.skip (n);
            const float endGain = g.getCurrentValue();
            busBuffer.addFromWithRamp (b * 2,     0, src.getReadPointer (0), n, startGain, endGain);
            busBuffer.addFromWithRamp (b * 2 + 1, 0, src.getReadPointer (1), n, startGain, endGain);
        }
    }

    // Soundboard: mixed into its buses like a strip, with smoothed sends.
    soundBuffer.clear (0, n);
    const bool soundPlaying = soundboard.render (soundBuffer.getWritePointer (0), soundBuffer.getWritePointer (1), n);
    for (int b = 0; b < kNumBuses; ++b)
    {
        auto& g = soundSend[(size_t) b];
        g.setTargetValue (soundPlaying && buses[(size_t) b].active.load() && soundboard.busOn[b].load() ? 1.0f : 0.0f);
        if (! g.isSmoothing() && g.getTargetValue() == 0.0f)
            continue;
        const float startGain = g.getCurrentValue();
        g.skip (n);
        const float endGain = g.getCurrentValue();
        busBuffer.addFromWithRamp (b * 2,     0, soundBuffer.getReadPointer (0), n, startGain, endGain);
        busBuffer.addFromWithRamp (b * 2 + 1, 0, soundBuffer.getReadPointer (1), n, startGain, endGain);
    }

    for (int b = 0; b < kNumBuses; ++b)
    {
        auto& bus = buses[(size_t) b];
        if (! bus.active.load())
            continue;

        auto* L = busBuffer.getWritePointer (b * 2);
        auto* R = busBuffer.getWritePointer (b * 2 + 1);

        // Bus processing (before the fader, so the fader stays a plain level control).
        // Switching EQ or compressor on/off crossfades with the dry signal over ~20 ms.
        auto runFaded = [&] (juce::SmoothedValue<float>& mix, bool on, auto&& process)
        {
            mix.setTargetValue (on ? 1.0f : 0.0f);
            if (! mix.isSmoothing())
            {
                if (on)
                    process();
                return;
            }
            busDry.copyFrom (0, 0, L, n);
            busDry.copyFrom (1, 0, R, n);
            const float start = mix.getCurrentValue();
            mix.skip (n);
            const float end = mix.getCurrentValue();
            process();
            for (int ch = 0; ch < 2; ++ch)
            {
                busBuffer.applyGainRamp (b * 2 + ch, 0, n, start, end);
                busBuffer.addFromWithRamp (b * 2 + ch, 0, busDry.getReadPointer (ch), n, 1.0f - start, 1.0f - end);
            }
        };

        auto& eq = busEq[(size_t) b];
        if (bus.fx.eqOn.load() && busEqMix[(size_t) b].getCurrentValue() == 0.0f)
            eq.reset();   // starting from off: no stale filter state
        runFaded (busEqMix[(size_t) b], bus.fx.eqOn.load(), [&]
        {
            eq.lowDb = bus.fx.eqLowDb.load();
            eq.midDb = bus.fx.eqMidDb.load();
            eq.midHz = bus.fx.eqMidHz.load();
            eq.highDb = bus.fx.eqHighDb.load();
            eq.process (L, R, n);
        });

        auto& comp = busComp[(size_t) b];
        if (bus.fx.compOn.load() && busCompMix[(size_t) b].getCurrentValue() == 0.0f)
            comp.reset();
        runFaded (busCompMix[(size_t) b], bus.fx.compOn.load(), [&]
        {
            comp.thresholdDb = bus.fx.compThresholdDb.load();
            comp.ratio = bus.fx.compRatio.load();
            const float gr = comp.process (L, R, n);
            if (gr < bus.fx.compGain.load (std::memory_order_relaxed))
                bus.fx.compGain.store (gr, std::memory_order_relaxed);
        });
        int busLatency = 0;
        bus.inserts.process (L, R, n, busLatency);

        // Loudness normalisation, before the fader so the fader still trims the result.
        // Always measured; when off it glides back to unity instead of jumping.
        {
            auto& norm = busNorm[(size_t) b];
            const float normStart = norm.currentGain();
            const float normEnd = norm.process (L, R, n, bus.normTargetLufs.load(), bus.normOn.load());
            bus.normGain.store (normEnd);
            if (normStart != 1.0f || normEnd != 1.0f)
            {
                busBuffer.applyGainRamp (b * 2,     0, n, normStart, normEnd);
                busBuffer.applyGainRamp (b * 2 + 1, 0, n, normStart, normEnd);
            }
        }

        auto& g = busGain[(size_t) b];
        g.setTargetValue (bus.mute.load() ? 0.0f : dbToGain (bus.faderDb.load()));

        const float start = g.getCurrentValue();
        g.skip (n);
        const float end = g.getCurrentValue();
        busBuffer.applyGainRamp (b * 2,     0, n, start, end);
        busBuffer.applyGainRamp (b * 2 + 1, 0, n, start, end);

        // RMS meters (sustained level, post-fader).
        bus.rmsL.store (busRmsL[(size_t) b].push (L, n));
        bus.rmsR.store (busRmsR[(size_t) b].push (R, n));

        // Guard against any non-finite samples from bus EQ / comp / inserts / norm before the limiter.
        if (AudioSafety::containsNonFinite (L, n) || AudioSafety::containsNonFinite (R, n))
        {
            busBuffer.clear (b * 2, 0, n);
            busBuffer.clear (b * 2 + 1, 0, n);
            busEq[(size_t) b].reset();
            busComp[(size_t) b].reset();
            busNorm[(size_t) b].reset();
        }

        // Safety limiter: nothing leaves a bus above -0.3 dBFS.
        const float lim = busLimiter[(size_t) b].process (busBuffer.getWritePointer (b * 2),
                                                          busBuffer.getWritePointer (b * 2 + 1), n);
        if (lim < bus.limiterGain.load())
            bus.limiterGain.store (lim);

        const float pl = busBuffer.getMagnitude (b * 2, 0, n);
        const float pr = busBuffer.getMagnitude (b * 2 + 1, 0, n);
        if (pl > bus.peakL.load()) bus.peakL.store (pl);
        if (pr > bus.peakR.load()) bus.peakR.store (pr);

        const auto* busL = busBuffer.getReadPointer (b * 2);
        const auto* busR = busBuffer.getReadPointer (b * 2 + 1);

        // Taps after the limiter: recorder, replay buffer, analysis for the UI.
        recorder.writeBus (b, busL, busR, n);
        if (replay.sourceBus.load (std::memory_order_relaxed) == b)
            replay.push (busL, busR, n);
        {
            const auto scope = bus.analysisFifo.write (n);
            if (scope.blockSize1 > 0)
            {
                bus.analysisBuffer.copyFrom (0, scope.startIndex1, busL, scope.blockSize1);
                bus.analysisBuffer.copyFrom (1, scope.startIndex1, busR, scope.blockSize1);
            }
            if (scope.blockSize2 > 0)
            {
                bus.analysisBuffer.copyFrom (0, scope.startIndex2, busL + scope.blockSize1, scope.blockSize2);
                bus.analysisBuffer.copyFrom (1, scope.startIndex2, busR + scope.blockSize1, scope.blockSize2);
            }
        }

        switch ((BusTarget) bus.target.load())
        {
            case BusTarget::MainPair:
            {
                const int outL = bus.outputPair.load() * 2, outR = outL + 1;
                if (outL < 0 || outL >= numOutputs || outputs[outL] == nullptr)
                    break;

                if (outR < numOutputs && outputs[outR] != nullptr)
                {
                    juce::FloatVectorOperations::add (outputs[outL] + offset, busL, n);
                    juce::FloatVectorOperations::add (outputs[outR] + offset, busR, n);
                }
                else // mono output: fold down
                {
                    juce::FloatVectorOperations::addWithMultiply (outputs[outL] + offset, busL, 0.5f, n);
                    juce::FloatVectorOperations::addWithMultiply (outputs[outL] + offset, busR, 0.5f, n);
                }
                break;
            }

            case BusTarget::Device:
            {
                const juce::ScopedTryLock sl (bus.sinkLock);
                if (sl.isLocked() && bus.sink != nullptr)
                {
                    const float* chans[2] { busL, busR };
                    bus.sink->bridge.write (chans, 2, n, sr);
                }
                break;
            }

            case BusTarget::Off:
                break;
        }
    }
}
