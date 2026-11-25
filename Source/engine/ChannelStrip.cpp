#include "engine/ChannelStrip.h"

static float dbToGain (float db) noexcept
{
    return db <= -60.0f ? 0.0f : juce::Decibels::decibelsToGain (db);
}

ChannelStrip::ChannelStrip()
{
    busOn[0].store (true);   // new strips go to A1
}

ChannelStrip::~ChannelStrip() = default;

void ChannelStrip::prepare (double newSampleRate, int maxBlockSize)
{
    sampleRate = newSampleRate;
    dcBlocker.prepare (sampleRate);

    denoiser.prepare (sampleRate);
    rmsMeterL.prepare (sampleRate);
    rmsMeterR.prepare (sampleRate);
    fx.denoiseUnavailable.store (! denoiser.isAvailable());
    fx.denoiseLatencyMs.store ((int) std::lround (1000.0 * denoiser.getLatency() / sampleRate));
    gate.prepare (sampleRate);
    hpf.reset(); lpf.reset();
    lastHpfHz = -1.0f; lastLpfHz = -1.0f;
    hpfMix.reset (sampleRate, 0.02);
    lpfMix.reset (sampleRate, 0.02);
    hpfMix.setCurrentAndTargetValue (fx.hpfOn.load() ? 1.0f : 0.0f);
    lpfMix.setCurrentAndTargetValue (fx.lpfOn.load() ? 1.0f : 0.0f);
    eq.prepare (sampleRate);
    comp.prepare (sampleRate);
    deesser.prepare (sampleRate);
    denoiseWasOn = false;

    // Latency compensation: up to 500 ms per strip.
    alignLine.prepare ((int) (sampleRate * 0.5));

    for (auto* g : { &inGain, &muteGain, &faderGain })
        g->reset (sampleRate, 0.02);
    duckGain.reset (sampleRate, 0.05);
    inGain.setCurrentAndTargetValue (dbToGain (gainDb.load()));
    muteGain.setCurrentAndTargetValue (mute.load() ? 0.0f : 1.0f);
    faderGain.setCurrentAndTargetValue (0.0f);   // fade in on start
    duckGain.setCurrentAndTargetValue (1.0f);
    padProcessor.prepare (sampleRate, maxBlockSize);
    inserts.prepare (sampleRate, maxBlockSize);
}

void ChannelStrip::release()
{
    inserts.release();
}

void ChannelStrip::resetToDefaults()
{
    inserts.removeAllPlugins();
    setExternalSource (nullptr);

    name.clear();
    colour = 0;
    sourceDeviceName.clear();
    sourceAppExe.clear();
    loopbackId.clear();
    loopbackName.clear();
    appOutputDeviceId.clear();
    appOutputDeviceName.clear();
    sourceType.store ((int) SourceType::Main);
    inputChannel.store (-1);
    inputChannelRight.store (-1);
    gainDb.store (0.0f);
    faderDb.store (0.0f);
    mute.store (false);
    solo.store (false);
    soloSafe.store (false);
    mono.store (false);
    invertPolarity.store (false);
    for (int b = 0; b < kNumBuses; ++b)
    {
        busOn[b].store (b == 0);
        sendDb[b].store (0.0f);
        sendPre[b].store (false);
    }
    pad.x.store (0.0f);
    pad.y.store (-1.0f);
    pad.mode.store (VoicePadParams::Voice);

    fx.denoiseOn.store (false);  fx.denoiseAmount.store (1.0f);
    fx.gateOn.store (false);     fx.gateThresholdDb.store (-50.0f); fx.gateRangeDb.store (-40.0f);
    fx.hpfOn.store (false);      fx.hpfHz.store (80.0f);
    fx.lpfOn.store (false);      fx.lpfHz.store (18000.0f);
    fx.eqOn.store (false);       fx.eqLowDb.store (0.0f); fx.eqMidDb.store (0.0f); fx.eqMidHz.store (1000.0f); fx.eqHighDb.store (0.0f);
    fx.compOn.store (false);     fx.compThresholdDb.store (-18.0f); fx.compRatio.store (3.0f);
    fx.deessOn.store (false);    fx.deessThresholdDb.store (-30.0f); fx.deessMaxDb.store (10.0f);
    fx.duckOn.store (false);     fx.duckSource.store (-1); fx.duckDepthDb.store (-12.0f); fx.duckThresholdDb.store (-40.0f);
}

//==============================================================================
// Strip presets (message thread)

omx::StripPreset ChannelStrip::savePreset() const
{
    static_assert (omx::kPresetNumBuses == kNumBuses, "preset bus count must match the engine");
    static_assert (omx::kPresetNumSlots == kNumSlots, "preset slot count must match the chain");

    omx::StripPreset p;
    p.gainDb = gainDb.load();   p.faderDb = faderDb.load();
    p.mono = mono.load();       p.invertPolarity = invertPolarity.load();
    p.soloSafe = soloSafe.load();

    p.denoiseOn = fx.denoiseOn.load();       p.denoiseAmount = fx.denoiseAmount.load();
    p.gateOn = fx.gateOn.load();             p.gateThresholdDb = fx.gateThresholdDb.load(); p.gateRangeDb = fx.gateRangeDb.load();
    p.hpfOn = fx.hpfOn.load();               p.hpfHz = fx.hpfHz.load();
    p.lpfOn = fx.lpfOn.load();               p.lpfHz = fx.lpfHz.load();
    p.eqOn = fx.eqOn.load();                 p.eqLowDb = fx.eqLowDb.load(); p.eqMidDb = fx.eqMidDb.load();
    p.eqMidHz = fx.eqMidHz.load();           p.eqHighDb = fx.eqHighDb.load();
    p.compOn = fx.compOn.load();             p.compThresholdDb = fx.compThresholdDb.load(); p.compRatio = fx.compRatio.load();
    p.deessOn = fx.deessOn.load();           p.deessThresholdDb = fx.deessThresholdDb.load(); p.deessMaxDb = fx.deessMaxDb.load();
    p.duckOn = fx.duckOn.load();             p.duckSource = fx.duckSource.load();
    p.duckDepthDb = fx.duckDepthDb.load();   p.duckThresholdDb = fx.duckThresholdDb.load();

    for (int b = 0; b < kNumBuses; ++b)
    {
        p.busOn[(size_t) b] = busOn[b].load();
        p.sendDb[(size_t) b] = sendDb[b].load();
        p.sendPre[(size_t) b] = sendPre[b].load();
    }

    p.padX = pad.x.load();   p.padY = pad.y.load();   p.padMode = pad.mode.load();
    for (int m = 0; m < VoicePadParams::numModes; ++m)
    {
        p.padModeX[(size_t) m] = pad.modeX[(size_t) m].load();
        p.padModeY[(size_t) m] = pad.modeY[(size_t) m].load();
    }

    p.numSlots = inserts.getNumPlugins();
    for (int s = 0; s < p.numSlots; ++s)
    {
        auto& slot = p.slots[(size_t) s];
        slot = {};
        if (auto* plugin = inserts.getPlugin (s))
        {
            const auto desc = plugin->getPluginDescription();
            slot.identifier = desc.fileOrIdentifier.toStdString();
            slot.name = desc.name.toStdString();
            slot.format = desc.pluginFormatName.toStdString();
        }
        slot.bypass = inserts.slotBypass[(size_t) s].load();
        slot.mix = inserts.slotMix[(size_t) s].load();
    }
    return p;
}

int ChannelStrip::loadPreset (const omx::StripPreset& p, juce::AudioPluginFormatManager& formats,
                              const std::function<bool (const juce::String&)>& isBlocked)
{
    gainDb.store (p.gainDb);   faderDb.store (p.faderDb);
    mono.store (p.mono);       invertPolarity.store (p.invertPolarity);
    soloSafe.store (p.soloSafe);

    fx.denoiseOn.store (p.denoiseOn);       fx.denoiseAmount.store (juce::jlimit (0.0f, 1.0f, p.denoiseAmount));
    fx.gateOn.store (p.gateOn);             fx.gateThresholdDb.store (p.gateThresholdDb); fx.gateRangeDb.store (p.gateRangeDb);
    setHpfEnabled (p.hpfOn);                setHpfCutoff (p.hpfHz);
    setLpfEnabled (p.lpfOn);                setLpfCutoff (p.lpfHz);
    fx.eqOn.store (p.eqOn);                 fx.eqLowDb.store (p.eqLowDb); fx.eqMidDb.store (p.eqMidDb);
    fx.eqMidHz.store (p.eqMidHz);           fx.eqHighDb.store (p.eqHighDb);
    fx.compOn.store (p.compOn);             fx.compThresholdDb.store (p.compThresholdDb); fx.compRatio.store (std::max (1.0f, p.compRatio));
    fx.deessOn.store (p.deessOn);           fx.deessThresholdDb.store (p.deessThresholdDb); fx.deessMaxDb.store (p.deessMaxDb);
    fx.duckOn.store (p.duckOn);
    fx.duckSource.store (juce::isPositiveAndBelow (p.duckSource, kNumStrips) ? p.duckSource : -1);
    fx.duckDepthDb.store (p.duckDepthDb);   fx.duckThresholdDb.store (p.duckThresholdDb);

    for (int b = 0; b < kNumBuses; ++b)
    {
        busOn[b].store (p.busOn[(size_t) b]);
        sendDb[b].store (p.sendDb[(size_t) b]);
        sendPre[b].store (p.sendPre[(size_t) b]);
    }

    pad.mode.store (juce::jlimit (0, VoicePadParams::numModes - 1, p.padMode));
    for (int m = 0; m < VoicePadParams::numModes; ++m)
        pad.setModePosition (m, p.padModeX[(size_t) m], p.padModeY[(size_t) m]);
    pad.setPosition (p.padX, p.padY);

    // Inserts: replace the chain. Each plugin is re-loaded by identifier and starts
    // with its defaults (no state is stored in a preset); a plugin that is not
    // installed any more is skipped and counted, never fatal.
    inserts.removeAllPlugins();
    int missing = 0;
    const int n = juce::jlimit (0, kNumSlots, p.numSlots);
    for (int s = 0; s < n; ++s)
    {
        const auto& slot = p.slots[(size_t) s];
        if (slot.identifier.empty() || (isBlocked && isBlocked (juce::String (slot.identifier))))
        {
            ++missing;
            continue;
        }

        juce::PluginDescription desc;
        desc.fileOrIdentifier = slot.identifier;
        desc.name = slot.name;
        desc.pluginFormatName = slot.format;

        juce::String error;
        if (! inserts.loadPlugin (s, desc, formats, error))
        {
            juce::Logger::writeToLog ("Strip preset: could not load " + desc.name + " (" + desc.fileOrIdentifier + "): " + error);
            ++missing;
            continue;
        }

        // Slots are packed, so the plugin may sit lower than its saved index.
        const int installed = inserts.getNumPlugins() - 1;
        inserts.slotBypass[(size_t) installed].store (slot.bypass);
        inserts.slotMix[(size_t) installed].store (juce::jlimit (0.0f, 1.0f, slot.mix));
    }
    return missing;
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

    // HPF/LPF, before the EQ. The filters always run (warm state, no zipper when
    // the cutoff moves); enabling/disabling crossfades wet/dry over ~20 ms via
    // the smoothed mixes, so toggling never clicks.
    {
        const float hpfHz = fx.hpfHz.load(), lpfHz = fx.lpfHz.load();
        if (hpfHz != lastHpfHz) { hpf.setHighPass (sampleRate, hpfHz); lastHpfHz = hpfHz; }
        if (lpfHz != lastLpfHz) { lpf.setLowPass (sampleRate, lpfHz);  lastLpfHz = lpfHz; }
        hpfMix.setTargetValue (fx.hpfOn.load() ? 1.0f : 0.0f);
        lpfMix.setTargetValue (fx.lpfOn.load() ? 1.0f : 0.0f);
        for (int i = 0; i < numSamples; ++i)
        {
            const float hm = hpfMix.getNextValue(), lm = lpfMix.getNextValue();
            const float lDry = L[i], rDry = R[i];
            const float lHp = hpf.process (lDry, 0), rHp = hpf.process (rDry, 1);
            L[i] = lDry + hm * (lHp - lDry);
            R[i] = rDry + hm * (rHp - rDry);
            const float lLp = lpf.process (L[i], 0), rLp = lpf.process (R[i], 1);
            L[i] += lm * (lLp - L[i]);
            R[i] += lm * (rLp - R[i]);
        }
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
    inserts.process (L, R, numSamples, latency);
    latencySamples.store (latency, std::memory_order_relaxed);

    // 4. VoicePad
    padProcessor.process (out, numSamples);

    // Last line of defence before the buses.
    if (AudioSafety::containsNonFinite (L, numSamples) || AudioSafety::containsNonFinite (R, numSamples))
    {
        out.clear (0, numSamples);
        padProcessor.reset();
        dcBlocker.reset();
        hpf.reset(); lpf.reset();
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
    rmsL.store (rmsMeterL.push (out.getReadPointer (0), numSamples));
    rmsR.store (rmsMeterR.push (out.getReadPointer (1), numSamples));
}
