#include "engine/ChannelStrip.h"

static float dbToGain (float db) noexcept
{
    return db <= -60.0f ? 0.0f : juce::Decibels::decibelsToGain (db);
}

ChannelStrip::ChannelStrip()
{
    busOn[0].store (true);   // new strips go to A1
    for (auto& m : slotMix)
        m.store (1.0f);
}

ChannelStrip::~ChannelStrip()
{
    const juce::ScopedLock sl (slotLock);
    for (auto& p : slots)
        if (p != nullptr)
            p->releaseResources();
}

void ChannelStrip::prepare (double newSampleRate, int maxBlockSize)
{
    const juce::ScopedLock sl (slotLock);

    sampleRate = newSampleRate;
    blockSize = maxBlockSize;

    scratch.setSize (kMaxPluginChannels, maxBlockSize);
    dryBuffer.setSize (2, maxBlockSize);
    mixDryBuffer.setSize (2, maxBlockSize);
    midi.ensureSize (256);
    dcBlocker.prepare (sampleRate);
    wasActive.fill (false);

    denoiser.prepare (sampleRate);
    fx.denoiseUnavailable.store (! denoiser.isAvailable());
    fx.denoiseLatencyMs.store ((int) std::lround (1000.0 * denoiser.getLatency() / sampleRate));
    gate.prepare (sampleRate);
    eq.prepare (sampleRate);
    comp.prepare (sampleRate);
    deesser.prepare (sampleRate);
    denoiseWasOn = false;

    // Latency compensation: up to 250 ms per bypassed plugin, 500 ms per strip.
    for (auto& d : bypassDelay)
        d.prepare ((int) (sampleRate * 0.25));
    for (auto& d : mixDelay)
        d.prepare ((int) (sampleRate * 0.25));
    alignLine.prepare ((int) (sampleRate * 0.5));

    for (auto* g : { &inGain, &muteGain, &faderGain })
        g->reset (sampleRate, 0.02);
    duckGain.reset (sampleRate, 0.05);
    inGain.setCurrentAndTargetValue (dbToGain (gainDb.load()));
    muteGain.setCurrentAndTargetValue (mute.load() ? 0.0f : 1.0f);
    faderGain.setCurrentAndTargetValue (0.0f);   // fade in on start
    duckGain.setCurrentAndTargetValue (1.0f);
    padProcessor.prepare (sampleRate, maxBlockSize);

    for (auto& p : slots)
        if (p != nullptr)
            preparePlugin (*p);
}

void ChannelStrip::release()
{
    const juce::ScopedLock sl (slotLock);
    for (auto& p : slots)
        if (p != nullptr)
            p->releaseResources();
}

void ChannelStrip::preparePlugin (juce::AudioPluginInstance& plugin)
{
    // Ask for stereo in / stereo out on the main buses; keep the plugin's default if refused.
    auto layout = plugin.getBusesLayout();
    if (! layout.inputBuses.isEmpty())  layout.inputBuses.getReference (0)  = juce::AudioChannelSet::stereo();
    if (! layout.outputBuses.isEmpty()) layout.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
    plugin.setBusesLayout (layout);

    plugin.setNonRealtime (false);
    plugin.prepareToPlay (sampleRate, blockSize);
}

//==============================================================================
// Plugin slots (message thread)

bool ChannelStrip::loadPlugin (int slot, const juce::PluginDescription& desc,
                               juce::AudioPluginFormatManager& formats, juce::String& error)
{
    auto instance = formats.createPluginInstance (desc, sampleRate, blockSize, error);
    if (instance == nullptr)
        return false;

    installPlugin (slot, std::move (instance));
    return true;
}

int ChannelStrip::getNumPlugins() const noexcept
{
    int n = 0;
    while (n < kNumSlots && slots[(size_t) n] != nullptr)
        ++n;
    return n;
}

void ChannelStrip::installPlugin (int slot, std::unique_ptr<juce::AudioPluginInstance> instance)
{
    if (instance == nullptr || ! juce::isPositiveAndBelow (slot, kNumSlots))
        return;

    // Keep the chain packed: a new plugin past the end goes right after the last one.
    slot = juce::jmin (slot, getNumPlugins());

    std::unique_ptr<juce::AudioPluginInstance> old;
    {
        const juce::ScopedLock sl (slotLock);
        preparePlugin (*instance);
        old = std::move (slots[(size_t) slot]);
        slots[(size_t) slot] = std::move (instance);
        wasActive[(size_t) slot] = false;   // fade the new plugin in
        bypassDelay[(size_t) slot].clear();
        mixDelay[(size_t) slot].clear();
    }
    slotBypass[(size_t) slot].store (false);
    slotMix[(size_t) slot].store (1.0f);
    slotFault[(size_t) slot].store (false);

    if (old != nullptr)
        old->releaseResources();
}

void ChannelStrip::removePlugin (int slot)
{
    if (! juce::isPositiveAndBelow (slot, kNumSlots))
        return;

    std::unique_ptr<juce::AudioPluginInstance> old;
    {
        const juce::ScopedLock sl (slotLock);
        old = std::move (slots[(size_t) slot]);

        // Move later plugins up so the chain stays packed.
        for (int i = slot; i < kNumSlots - 1; ++i)
        {
            std::swap (slots[(size_t) i], slots[(size_t) i + 1]);
            slotBypass[(size_t) i].store (slotBypass[(size_t) i + 1].load());
            slotFault[(size_t) i].store (slotFault[(size_t) i + 1].load());
            std::swap (wasActive[(size_t) i], wasActive[(size_t) i + 1]);
            std::swap (bypassDelay[(size_t) i], bypassDelay[(size_t) i + 1]);
            std::swap (mixDelay[(size_t) i], mixDelay[(size_t) i + 1]);
            slotMix[(size_t) i].store (slotMix[(size_t) i + 1].load());
        }
        slotBypass[kNumSlots - 1].store (false);
        slotMix[kNumSlots - 1].store (1.0f);
        slotFault[kNumSlots - 1].store (false);
        wasActive[kNumSlots - 1] = false;
    }

    if (old != nullptr)
        old->releaseResources();
}

void ChannelStrip::removeAllPlugins()
{
    while (getNumPlugins() > 0)
        removePlugin (getNumPlugins() - 1);
}

void ChannelStrip::movePlugin (int from, int to)
{
    const int n = getNumPlugins();
    if (! juce::isPositiveAndBelow (from, n) || ! juce::isPositiveAndBelow (to, n) || from == to)
        return;

    const juce::ScopedLock sl (slotLock);
    const int step = to > from ? 1 : -1;
    for (int i = from; i != to; i += step)
    {
        const auto a = (size_t) i, b = (size_t) (i + step);
        std::swap (slots[a], slots[b]);
        const bool bypassA = slotBypass[a].load(), faultA = slotFault[a].load();
        slotBypass[a].store (slotBypass[b].load()); slotBypass[b].store (bypassA);
        slotFault[a].store (slotFault[b].load());   slotFault[b].store (faultA);
        std::swap (wasActive[a], wasActive[b]);
        std::swap (bypassDelay[a], bypassDelay[b]);
        std::swap (mixDelay[a], mixDelay[b]);
        const float mixA = slotMix[a].load();
        slotMix[a].store (slotMix[b].load()); slotMix[b].store (mixA);
    }
}

void ChannelStrip::resetToDefaults()
{
    removeAllPlugins();
    setExternalSource (nullptr);

    name.clear();
    colour = 0;
    sourceDeviceName.clear();
    sourceAppExe.clear();
    appOutputDeviceId.clear();
    appOutputDeviceName.clear();
    sourceType.store ((int) SourceType::Main);
    inputChannel.store (-1);
    inputChannelRight.store (-1);
    gainDb.store (0.0f);
    faderDb.store (0.0f);
    mute.store (false);
    solo.store (false);
    mono.store (false);
    invertPolarity.store (false);
    for (int b = 0; b < kNumBuses; ++b)
    {
        busOn[b].store (b == 0);
        sendDb[b].store (0.0f);
        sendPre[b].store (false);
    }
    for (auto& m : slotMix)
        m.store (1.0f);
    pad.x.store (0.0f);
    pad.y.store (-1.0f);
    pad.mode.store (VoicePadParams::Voice);

    fx.denoiseOn.store (false);  fx.denoiseAmount.store (1.0f);
    fx.gateOn.store (false);     fx.gateThresholdDb.store (-50.0f); fx.gateRangeDb.store (-40.0f);
    fx.eqOn.store (false);       fx.eqLowDb.store (0.0f); fx.eqMidDb.store (0.0f); fx.eqMidHz.store (1000.0f); fx.eqHighDb.store (0.0f);
    fx.compOn.store (false);     fx.compThresholdDb.store (-18.0f); fx.compRatio.store (3.0f);
    fx.deessOn.store (false);    fx.deessThresholdDb.store (-30.0f); fx.deessMaxDb.store (10.0f);
    fx.duckOn.store (false);     fx.duckSource.store (-1); fx.duckDepthDb.store (-12.0f); fx.duckThresholdDb.store (-40.0f);
}

//==============================================================================
// External source

void ChannelStrip::setExternalSource (std::unique_ptr<ExternalSource> source)
{
    {
        const juce::ScopedLock sl (sourceLock);
        std::swap (external, source);
    }
    // `source` now holds the old one; it stops its device/thread here, outside the lock.
}

bool ChannelStrip::pullExternal (float* l, float* r, int numSamples, double rate) noexcept
{
    const juce::ScopedTryLock sl (sourceLock);
    if (! sl.isLocked() || external == nullptr)
        return false;

    external->bridge.read (l, r, numSamples, rate);
    return true;
}

//==============================================================================
// Audio

void ChannelStrip::runInserts (float* L, float* R, juce::AudioBuffer<float>& out, int numSamples, int& chainLatency) noexcept
{
    // If the UI is swapping a plugin right now, skip the inserts for this block.
    const juce::ScopedTryLock sl (slotLock);
    if (! sl.isLocked() || numSamples > scratch.getNumSamples())
        return;

    for (size_t s = 0; s < slots.size(); ++s)
    {
        auto& p = slots[s];
        if (p == nullptr)
        {
            wasActive[s] = false;
            continue;
        }

        const int numCh = juce::jmax (2, p->getTotalNumInputChannels(), p->getTotalNumOutputChannels());
        if (numCh > kMaxPluginChannels || slotFault[s].load())
        {
            wasActive[s] = false;
            continue;   // unusable or faulted: out of the chain (and out of the latency sum)
        }

        const int pluginLatency = juce::jmax (0, p->getLatencySamples());
        chainLatency += pluginLatency;

        const bool slotOn = ! slotBypass[s].load() && ! p->isSuspended();
        const bool crossfade = slotOn != wasActive[s];

        // Steady bypass: keep the plugin's latency so timing never jumps.
        if (! slotOn && ! crossfade && p->getBypassParameter() == nullptr)
        {
            bypassDelay[s].process (L, R, numSamples, pluginLatency);
            continue;
        }

        if (crossfade)
        {
            dryBuffer.copyFrom (0, 0, L, numSamples);
            dryBuffer.copyFrom (1, 0, R, numSamples);
        }

        // Wet/dry: keep the dry input, delayed by the plugin's latency so the blend lines up.
        const float mix = slotMix[s].load();
        const bool blendDry = mix < 0.999f;
        if (blendDry)
        {
            mixDryBuffer.copyFrom (0, 0, L, numSamples);
            mixDryBuffer.copyFrom (1, 0, R, numSamples);
            mixDelay[s].process (mixDryBuffer.getWritePointer (0), mixDryBuffer.getWritePointer (1), numSamples, pluginLatency);
        }

        juce::AudioBuffer<float> view (scratch.getArrayOfWritePointers(), numCh, numSamples);
        view.clear();
        view.copyFrom (0, 0, L, numSamples);
        view.copyFrom (1, 0, R, numSamples);

        midi.clear();
        if (slotOn || crossfade)
            p->processBlock (view, midi);
        else
            p->processBlockBypassed (view, midi);   // the plugin's own latency-correct bypass

        const int outs = p->getTotalNumOutputChannels();
        const int rightSrc = outs == 1 ? 0 : 1;

        // A plugin that outputs NaN/Inf would poison everything after it permanently.
        // Drop its output, reset it and bypass it.
        if (AudioSafety::containsNonFinite (view.getReadPointer (0), numSamples)
            || AudioSafety::containsNonFinite (view.getReadPointer (rightSrc), numSamples))
        {
            slotFault[s].store (true);
            slotBypass[s].store (true);
            faultCount.fetch_add (1);
            p->reset();
            wasActive[s] = false;
            continue;   // keep the dry signal
        }

        out.copyFrom (0, 0, view, 0, 0, numSamples);
        out.copyFrom (1, 0, view, rightSrc, 0, numSamples);

        if (blendDry && (slotOn || crossfade))
        {
            out.applyGain (0, numSamples, mix);
            out.addFrom (0, 0, mixDryBuffer, 0, 0, numSamples, 1.0f - mix);
            out.addFrom (1, 0, mixDryBuffer, 1, 0, numSamples, 1.0f - mix);
        }
        else if (! blendDry)
        {
            mixDelay[s].clear();
        }

        // Bypass / enable / newly loaded: crossfade over this block instead of clicking.
        if (crossfade)
        {
            const float wetStart = slotOn ? 0.0f : 1.0f;
            const float wetEnd   = slotOn ? 1.0f : 0.0f;
            out.applyGainRamp (0, 0, numSamples, wetStart, wetEnd);
            out.applyGainRamp (1, 0, numSamples, wetStart, wetEnd);
            out.addFromWithRamp (0, 0, dryBuffer.getReadPointer (0), numSamples, 1.0f - wetStart, 1.0f - wetEnd);
            out.addFromWithRamp (1, 0, dryBuffer.getReadPointer (1), numSamples, 1.0f - wetStart, 1.0f - wetEnd);
            if (! slotOn)
                bypassDelay[s].clear();
        }

        wasActive[s] = slotOn;
    }
}

void ChannelStrip::process (const float* inL, const float* inR, juce::AudioBuffer<float>& out,
                            juce::AudioBuffer<float>& preOut, int numSamples) noexcept
{
    auto* L = out.getWritePointer (0);
    auto* R = out.getWritePointer (1);
    const bool monoSource = inL != nullptr && (inR == nullptr || inR == inL);

    // 1. Input gain, polarity, mono
    inGain.setTargetValue (dbToGain (gainDb.load()));
    if (inL != nullptr)
    {
        if (inR == nullptr)
            inR = inL;

        const float polarity = invertPolarity.load() ? -1.0f : 1.0f;
        const bool sumToMono = mono.load();

        for (int i = 0; i < numSamples; ++i)
        {
            const float g = inGain.getNextValue() * polarity;
            float l = inL[i] * g, r = inR[i] * g;   // read both before writing (in/out may alias)
            if (sumToMono)
                l = r = 0.5f * (l + r);
            L[i] = l;
            R[i] = r;
        }

        dcBlocker.process (L, numSamples, 0);
        dcBlocker.process (R, numSamples, 1);
    }
    else
    {
        inGain.skip (numSamples);
        out.clear (0, numSamples);
    }

    // 2. Built-in processing
    int latency = 0;

    const bool denoiseOn = fx.denoiseOn.load() && denoiser.isAvailable();
    if (denoiseOn != denoiseWasOn)
    {
        denoiser.reset();
        denoiseWasOn = denoiseOn;
    }
    if (denoiseOn)
    {
        fx.voiceProbability.store (denoiser.process (L, R, numSamples, fx.denoiseAmount.load(), monoSource || mono.load()));
        latency += denoiser.getLatency();
    }

    if (fx.gateOn.load())
    {
        gate.thresholdDb = fx.gateThresholdDb.load();
        gate.rangeDb = fx.gateRangeDb.load();
        storeMin (fx.gateGain, gate.process (L, R, numSamples));
    }

    if (fx.eqOn.load())
    {
        eq.lowDb = fx.eqLowDb.load();
        eq.midDb = fx.eqMidDb.load();
        eq.midHz = fx.eqMidHz.load();
        eq.highDb = fx.eqHighDb.load();
        eq.process (L, R, numSamples);
    }

    if (fx.compOn.load())
    {
        comp.thresholdDb = fx.compThresholdDb.load();
        comp.ratio = fx.compRatio.load();
        storeMin (fx.compGain, comp.process (L, R, numSamples));
    }

    if (fx.deessOn.load())
    {
        deesser.thresholdDb = fx.deessThresholdDb.load();
        deesser.maxReductionDb = fx.deessMaxDb.load();
        storeMin (fx.deessGain, deesser.process (L, R, numSamples));
    }

    // 3. VST3 inserts
    runInserts (L, R, out, numSamples, latency);
    latencySamples.store (latency, std::memory_order_relaxed);

    // 4. VoicePad
    padProcessor.process (out, numSamples);

    // Last line of defence before the buses.
    if (AudioSafety::containsNonFinite (L, numSamples) || AudioSafety::containsNonFinite (R, numSamples))
    {
        out.clear (0, numSamples);
        padProcessor.reset();
        dcBlocker.reset();
        gate.reset(); eq.reset(); comp.reset(); deesser.reset(); denoiser.reset();
        faultCount.fetch_add (1);
    }

    // 5. Ducking (target set by the engine from the source strip's level)
    duckGain.setTargetValue (fx.duckOn.load() ? duckTarget.load() : 1.0f);
    if (duckGain.isSmoothing() || duckGain.getTargetValue() < 1.0f)
    {
        const float start = duckGain.getCurrentValue();
        duckGain.skip (numSamples);
        const float end = duckGain.getCurrentValue();
        out.applyGainRamp (0, numSamples, start, end);
        storeMin (fx.duckGain, juce::jmin (start, end));
    }

    // 6. Latency alignment: delay this strip so it lines up with the slowest strip.
    alignLine.process (L, R, numSamples, alignDelay.load (std::memory_order_relaxed));

    // Level for other strips' ducking: taken before mute and fader on purpose
    // (muting the mic on the desk shouldn't stop it from ducking the music).
    sidechainLevel.store (juce::jmax (out.getMagnitude (0, 0, numSamples), out.getMagnitude (1, 0, numSamples)),
                          std::memory_order_relaxed);

    // 7. Mute (applies to pre- and post-fader sends), then the pre-fader tap, then the fader
    muteGain.setTargetValue (mute.load() ? 0.0f : 1.0f);
    {
        const float start = muteGain.getCurrentValue();
        muteGain.skip (numSamples);
        out.applyGainRamp (0, numSamples, start, muteGain.getCurrentValue());
    }

    preOut.copyFrom (0, 0, out, 0, 0, numSamples);
    preOut.copyFrom (1, 0, out, 1, 0, numSamples);

    faderGain.setTargetValue (dbToGain (faderDb.load()));
    {
        const float start = faderGain.getCurrentValue();
        faderGain.skip (numSamples);
        out.applyGainRamp (0, numSamples, start, faderGain.getCurrentValue());
    }

    // 8. Meters (post-fader; peak hold and decay are done by the UI)
    const float pl = out.getMagnitude (0, 0, numSamples);
    const float pr = out.getMagnitude (1, 0, numSamples);
    if (pl > peakL.load()) peakL.store (pl);
    if (pr > peakR.load()) peakR.store (pr);
}
