#include "MixerEngine.h"

static float dbToGain (float db) noexcept
{
    return db <= -60.0f ? 0.0f : juce::Decibels::decibelsToGain (db);
}

MixerEngine::MixerEngine()
{
    juce::addDefaultFormatsToManager (formatManager);

    for (auto& s : strips)
        s = std::make_unique<ChannelStrip>();

    for (int b = 0; b < kNumBuses; ++b)
        buses[(size_t) b].outputPair.store (b);   // A1 -> outs 1/2, A2 -> 3/4, B1 -> 5/6
}

MixerEngine::~MixerEngine()
{
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
        DBG ("Audio device error: " << error);
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
    strip.sourceType.store ((int) SourceType::None);
    strip.setExternalSource (nullptr);
}

void MixerEngine::setStripSourceMain (int s, int channel)
{
    auto& strip = getStrip (s);
    strip.inputChannel.store (channel);
    strip.sourceType.store ((int) SourceType::Main);
    strip.setExternalSource (nullptr);
}

bool MixerEngine::setStripSourceDevice (int s, const juce::String& name, juce::String& error)
{
    auto& strip = getStrip (s);
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
    strip.sourceAppExe = exeName;
    strip.sourceType.store ((int) SourceType::App);

    const auto pid = findAudioAppByExe (exeName.toWideCharPointer());
    if (pid == 0)
    {
        // Not running yet: maintainAppSources() connects it when it starts playing.
        strip.setExternalSource (nullptr);
        return true;
    }

    auto source = AppLoopbackSource::open (pid, error);
    strip.setExternalSource (std::move (source));
    return strip.getExternalSource() != nullptr;
}

void MixerEngine::maintainAppSources()
{
    for (auto& s : strips)
    {
        if (s->sourceType.load() != (int) SourceType::App || s->sourceAppExe.isEmpty())
            continue;

        if (auto* current = dynamic_cast<AppLoopbackSource*> (s->getExternalSource()))
        {
            if (isProcessAlive (current->getPid()))
                continue;

            s->setExternalSource (nullptr);   // app exited
        }

        if (const auto pid = findAudioAppByExe (s->sourceAppExe.toWideCharPointer()); pid != 0)
        {
            juce::String error;
            s->setExternalSource (AppLoopbackSource::open (pid, error));
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
    sourceBuffer.setSize (2, maxBlock);
    busBuffer.setSize (kNumBuses * 2, maxBlock);

    for (int b = 0; b < kNumBuses; ++b)
    {
        busGain[(size_t) b].reset (sr, 0.02);
        busGain[(size_t) b].setCurrentAndTargetValue (dbToGain (buses[(size_t) b].faderDb.load()));
    }

    for (int s = 0; s < kNumStrips; ++s)
    {
        strips[(size_t) s]->prepare (sr, maxBlock);
        for (int b = 0; b < kNumBuses; ++b)
        {
            auto& g = sendGain[(size_t) s][(size_t) b];
            g.reset (sr, 0.02);
            g.setCurrentAndTargetValue (strips[(size_t) s]->busOn[b].load() ? 1.0f : 0.0f);
        }
    }
}

void MixerEngine::audioDeviceStopped()
{
    for (auto& s : strips)
        s->release();
}

void MixerEngine::audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                                    float* const* outputs, int numOutputs,
                                                    int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    juce::ScopedNoDenormals noDenormals;

    for (int ch = 0; ch < numOutputs; ++ch)
        if (outputs[ch] != nullptr)
            juce::FloatVectorOperations::clear (outputs[ch], numSamples);

    // Some drivers deliver more samples than announced; process in chunks.
    for (int offset = 0; offset < numSamples; offset += maxBlock)
        processChunk (inputs, numInputs, outputs, numOutputs, offset, juce::jmin (maxBlock, numSamples - offset));
}

void MixerEngine::processChunk (const float* const* inputs, int numInputs,
                                float* const* outputs, int numOutputs, int offset, int n) noexcept
{
    const double sr = currentSampleRate.load();
    busBuffer.clear (0, n);

    for (int s = 0; s < kNumStrips; ++s)
    {
        auto& strip = *strips[(size_t) s];
        const float* inL = nullptr;
        const float* inR = nullptr;

        switch ((SourceType) strip.sourceType.load())
        {
            case SourceType::Main:
            {
                const int in = strip.inputChannel.load();
                if (in >= 0 && in < numInputs && inputs[in] != nullptr)
                    inL = inR = inputs[in] + offset;
                break;
            }

            case SourceType::Device:
            case SourceType::App:
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

        strip.process (inL, inR, stripBuffer, n);

        for (int b = 0; b < kNumBuses; ++b)
        {
            auto& g = sendGain[(size_t) s][(size_t) b];
            g.setTargetValue (strip.busOn[b].load() ? 1.0f : 0.0f);

            if (! g.isSmoothing() && g.getTargetValue() == 0.0f)
                continue;

            const float start = g.getCurrentValue();
            g.skip (n);
            const float end = g.getCurrentValue();
            busBuffer.addFromWithRamp (b * 2,     0, stripBuffer.getReadPointer (0), n, start, end);
            busBuffer.addFromWithRamp (b * 2 + 1, 0, stripBuffer.getReadPointer (1), n, start, end);
        }
    }

    for (int b = 0; b < kNumBuses; ++b)
    {
        auto& bus = buses[(size_t) b];
        auto& g = busGain[(size_t) b];
        g.setTargetValue (bus.mute.load() ? 0.0f : dbToGain (bus.faderDb.load()));

        const float start = g.getCurrentValue();
        g.skip (n);
        const float end = g.getCurrentValue();
        busBuffer.applyGainRamp (b * 2,     0, n, start, end);
        busBuffer.applyGainRamp (b * 2 + 1, 0, n, start, end);

        const float pl = busBuffer.getMagnitude (b * 2, 0, n);
        const float pr = busBuffer.getMagnitude (b * 2 + 1, 0, n);
        if (pl > bus.peakL.load()) bus.peakL.store (pl);
        if (pr > bus.peakR.load()) bus.peakR.store (pr);

        const auto* busL = busBuffer.getReadPointer (b * 2);
        const auto* busR = busBuffer.getReadPointer (b * 2 + 1);

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
