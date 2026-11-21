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
    fx.denoiseUnavailable.store (! denoiser.isAvailable());
    fx.denoiseLatencyMs.store ((int) std::lround (1000.0 * denoiser.getLatency() / sampleRate));
    gate.prepare (sampleRate);
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
