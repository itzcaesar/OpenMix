#include "dsp/BuiltInFx.h"

#include <numbers>

namespace omx
{

namespace
{

double sanitiseSampleRate (double sampleRate) noexcept
{
    if (! std::isfinite (sampleRate) || sampleRate <= 0.0)
        return 48000.0;
    return std::clamp (sampleRate, 8000.0, 192000.0);
}

float clamp01 (float v) noexcept { return std::clamp (v, 0.0f, 1.0f); }

} // namespace

//==============================================================================
// BuiltInReverb

float BuiltInReverb::Comb::process (float input) noexcept
{
    const float output = buf[(size_t) pos];
    // One-pole low-pass in the feedback loop: damp1 = damping amount,
    // damp2 = 1 - damp1. Higher damping keeps more of the previous filtered
    // sample, which rolls the highs off each time round the loop.
    filterStore = detail::flushDenormal (output * damp2 + filterStore * damp1);
    buf[(size_t) pos] = detail::flushDenormal (input + filterStore * feedback);
    if (++pos >= (int) buf.size())
        pos = 0;
    return output;
}

float BuiltInReverb::Allpass::process (float input) noexcept
{
    const float bufOut = buf[(size_t) pos];
    const float output = -input + bufOut;
    buf[(size_t) pos] = detail::flushDenormal (input + bufOut * 0.5f);
    if (++pos >= (int) buf.size())
        pos = 0;
    return output;
}

void BuiltInReverb::updateCombCoefficients() noexcept
{
    // Comb feedback 0.70..0.98: long enough for a hall at roomSize 1, but
    // always below 1 so the network is stable for every parameter value.
    const float feedback = 0.70f + 0.28f * roomSize;
    const float damp1 = damping; // 0 = bright, 1 = fully damped
    for (auto& channelCombs : combs)
        for (auto& comb : channelCombs)
        {
            comb.feedback = feedback;
            comb.damp1 = damp1;
            comb.damp2 = 1.0f - damp1;
        }
}

void BuiltInReverb::prepareToPlay (double sampleRateIn, int /*blockSize*/)
{
    sampleRate = sanitiseSampleRate (sampleRateIn);

    // Freeverb tunings at 44.1 kHz, scaled to the real sample rate. The
    // right channel is spread by +23 samples so the two sides decorrelate.
    static constexpr int combTunings[kNumCombs] { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
    static constexpr int allpassTunings[kNumAllpasses] { 556, 441, 341, 225 };
    const double scale = sampleRate / 44100.0;

    const int preDelaySize = (int) std::ceil (sampleRate * 0.1) + 2; // 100 ms max
    for (int ch = 0; ch < 2; ++ch)
    {
        preDelayBuf[ch].assign ((size_t) preDelaySize, 0.0f);
        for (int i = 0; i < kNumCombs; ++i)
            combs[ch][i].buf.assign ((size_t) std::max (1, (int) std::lround (combTunings[i] * scale) + (ch == 1 ? 23 : 0)), 0.0f);
        for (int i = 0; i < kNumAllpasses; ++i)
            allpasses[ch][i].buf.assign ((size_t) std::max (1, (int) std::lround (allpassTunings[i] * scale) + (ch == 1 ? 23 : 0)), 0.0f);
    }

    mix.prepare (sampleRate, 20.0, mixTarget);
    updateCombCoefficients();
    reset();
    prepared = true;
}

void BuiltInReverb::reset() noexcept
{
    for (int ch = 0; ch < 2; ++ch)
    {
        std::fill (preDelayBuf[ch].begin(), preDelayBuf[ch].end(), 0.0f);
        for (auto& comb : combs[ch])
        {
            std::fill (comb.buf.begin(), comb.buf.end(), 0.0f);
            comb.pos = 0;
            comb.filterStore = 0.0f;
        }
        for (auto& ap : allpasses[ch])
        {
            std::fill (ap.buf.begin(), ap.buf.end(), 0.0f);
            ap.pos = 0;
        }
    }
    preDelayPos = 0;
    mix.snapToTarget();
}

void BuiltInReverb::setRoomSize (float roomSizeIn) noexcept
{
    roomSize = clamp01 (roomSizeIn);
    updateCombCoefficients();
}

void BuiltInReverb::setDamping (float dampingIn) noexcept
{
    damping = clamp01 (dampingIn);
    updateCombCoefficients();
}

void BuiltInReverb::setMix (float mixIn) noexcept
{
    mixTarget = clamp01 (mixIn);
    mix.setTarget (mixTarget);
}

void BuiltInReverb::setPreDelayMs (float preDelayMsIn) noexcept
{
    preDelayMs = std::clamp (preDelayMsIn, 0.0f, 100.0f);
}

void BuiltInReverb::processBlock (juce::AudioBuffer<float>& buffer)
{
    if (! prepared || buffer.getNumSamples() <= 0 || buffer.getNumChannels() <= 0)
        return;
    float* channels[2] { buffer.getWritePointer (0),
                         buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr };
    process (channels, std::min (2, buffer.getNumChannels()), buffer.getNumSamples());
}

void BuiltInReverb::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    if (! prepared || channels == nullptr || channels[0] == nullptr || numSamples <= 0)
        return;
    numChannels = std::clamp (numChannels, 1, 2);

    const int preDelaySize = (int) preDelayBuf[0].size();
    const int preDelaySamples = std::clamp ((int) std::lround (preDelayMs * sampleRate * 0.001),
                                            0, preDelaySize - 1);

    for (int n = 0; n < numSamples; ++n)
    {
        const float mixNow = mix.next();
        const float dryGain = 1.0f - mixNow;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float dry = channels[ch][n];

            // Pre-delay ring buffer.
            const int readPos = (preDelayPos - preDelaySamples + preDelaySize) % preDelaySize;
            preDelayBuf[ch][(size_t) preDelayPos] = dry;
            const float delayed = preDelayBuf[ch][(size_t) readPos];

            float wet = 0.0f;
            for (auto& comb : combs[ch])
                wet += comb.process (delayed);
            wet *= 0.125f; // sum of 8 combs back to ~unity
            for (auto& ap : allpasses[ch])
                wet = ap.process (wet);

            channels[ch][n] = dry * dryGain + wet * mixNow;
        }

        if (++preDelayPos >= preDelaySize)
            preDelayPos = 0;
    }
}

//==============================================================================
// BuiltInDelay

void BuiltInDelay::updateTempoTime() noexcept
{
    if (! tempoSync)
        return;
    // division * (ms per beat): 1.0 = quarter note at the current bpm.
    timeMsTarget = std::clamp (noteDivision * 60000.0f / bpm, 0.0f, (float) (kMaxDelaySeconds * 1000.0));
    delaySamples.setTarget ((float) (timeMsTarget * sampleRate * 0.001));
}

void BuiltInDelay::updateFeedbackFilter() noexcept
{
    const double fc = std::clamp ((double) feedbackLowpassHz, 200.0, 20000.0);
    feedbackFilterAlpha = (float) (1.0 - std::exp (-2.0 * std::numbers::pi * fc / sampleRate));
}

void BuiltInDelay::prepareToPlay (double sampleRateIn, int /*blockSize*/)
{
    sampleRate = sanitiseSampleRate (sampleRateIn);

    const int size = (int) std::ceil (sampleRate * kMaxDelaySeconds) + 2;
    for (auto& b : buf)
        b.assign ((size_t) size, 0.0f);

    // The delay-time smoother starts at the current target, so the first
    // block after prepare is already at the right time; only later moves
    // ramp (which is what kills the clicks).
    delaySamples.prepare (sampleRate, 30.0, (float) (timeMsTarget * sampleRate * 0.001));
    feedback.prepare (sampleRate, 20.0, feedbackTarget);
    mix.prepare (sampleRate, 20.0, mixTarget);
    updateFeedbackFilter();
    reset();
    prepared = true;
}

void BuiltInDelay::reset() noexcept
{
    for (auto& b : buf)
        std::fill (b.begin(), b.end(), 0.0f);
    writePos = 0;
    feedbackFilterState[0] = feedbackFilterState[1] = 0.0f;
    delaySamples.snapToTarget();
    feedback.snapToTarget();
    mix.snapToTarget();
}

void BuiltInDelay::setTimeMs (float timeMsIn) noexcept
{
    tempoSync = false;
    timeMsTarget = std::clamp (timeMsIn, 0.0f, (float) (kMaxDelaySeconds * 1000.0));
    delaySamples.setTarget ((float) (timeMsTarget * sampleRate * 0.001));
}

void BuiltInDelay::setTempoBpm (float bpmIn) noexcept
{
    bpm = std::clamp (bpmIn, 20.0f, 300.0f);
    tempoSync = true;
    updateTempoTime();
}

void BuiltInDelay::setNoteDivision (float divisionIn) noexcept
{
    noteDivision = std::clamp (divisionIn, 0.125f, 4.0f);
    tempoSync = true;
    updateTempoTime();
}

void BuiltInDelay::setFeedback (float feedbackIn) noexcept
{
    // Hard cap at 0.95: with the feedback low-pass in the loop the gain
    // stays below unity, so the delay can never run away.
    feedbackTarget = std::clamp (feedbackIn, 0.0f, 0.95f);
    feedback.setTarget (feedbackTarget);
}

void BuiltInDelay::setMix (float mixIn) noexcept
{
    mixTarget = clamp01 (mixIn);
    mix.setTarget (mixTarget);
}

void BuiltInDelay::setFeedbackLowpassHz (float hzIn) noexcept
{
    feedbackLowpassHz = std::clamp (hzIn, 200.0f, 20000.0f);
    if (prepared)
        updateFeedbackFilter();
}

void BuiltInDelay::processBlock (juce::AudioBuffer<float>& buffer)
{
    if (! prepared || buffer.getNumSamples() <= 0 || buffer.getNumChannels() <= 0)
        return;
    float* channels[2] { buffer.getWritePointer (0),
                         buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr };
    process (channels, std::min (2, buffer.getNumChannels()), buffer.getNumSamples());
}

void BuiltInDelay::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    if (! prepared || channels == nullptr || channels[0] == nullptr || numSamples <= 0)
        return;
    numChannels = std::clamp (numChannels, 1, 2);

    const int size = (int) buf[0].size();
    const float maxDelay = (float) (size - 2);

    for (int n = 0; n < numSamples; ++n)
    {
        // Per-sample smoothed values: one next() per sample, shared across
        // channels, so L/R stay in step.
        const float delayNow = std::clamp (delaySamples.next(), 0.0f, maxDelay);
        const float feedbackNow = feedback.next();
        const float mixNow = mix.next();
        const float dryGain = 1.0f - mixNow;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float dry = channels[ch][n];
            const float delayed = detail::readFractional (buf[ch], writePos, delayNow);

            // Feedback path: low-pass the repeat before feeding it back, so
            // successive repeats get darker instead of accumulating highs.
            feedbackFilterState[ch] += feedbackFilterAlpha * (delayed - feedbackFilterState[ch]);
            const float toWrite = detail::flushDenormal (
                dry + feedbackNow * detail::flushDenormal (feedbackFilterState[ch]));
            buf[ch][(size_t) writePos] = toWrite;

            channels[ch][n] = dry * dryGain + delayed * mixNow;
        }

        if (++writePos >= size)
            writePos = 0;
    }
}

//==============================================================================
// BuiltInChorus

void BuiltInChorus::prepareToPlay (double sampleRateIn, int /*blockSize*/)
{
    sampleRate = sanitiseSampleRate (sampleRateIn);

    const int size = (int) std::ceil (sampleRate * kMaxDelaySeconds) + 2;
    for (auto& b : buf)
        b.assign ((size_t) size, 0.0f);

    rate.prepare (sampleRate, 20.0, rateTarget);
    depth.prepare (sampleRate, 20.0, depthTarget);
    mix.prepare (sampleRate, 20.0, mixTarget);
    feedback.prepare (sampleRate, 20.0, feedbackTarget);
    reset();
    prepared = true;
}

void BuiltInChorus::reset() noexcept
{
    for (auto& b : buf)
        std::fill (b.begin(), b.end(), 0.0f);
    writePos = 0;
    lfoPhase = 0.0;
    rate.snapToTarget();
    depth.snapToTarget();
    mix.snapToTarget();
    feedback.snapToTarget();
}

void BuiltInChorus::setRateHz (float rateHzIn) noexcept
{
    rateTarget = std::clamp (rateHzIn, 0.05f, 10.0f);
    rate.setTarget (rateTarget);
}

void BuiltInChorus::setDepthMs (float depthMsIn) noexcept
{
    depthTarget = std::clamp (depthMsIn, 0.0f, 10.0f);
    depth.setTarget (depthTarget);
}

void BuiltInChorus::setMix (float mixIn) noexcept
{
    mixTarget = clamp01 (mixIn);
    mix.setTarget (mixTarget);
}

void BuiltInChorus::setFeedback (float feedbackIn) noexcept
{
    feedbackTarget = std::clamp (feedbackIn, 0.0f, 0.5f);
    feedback.setTarget (feedbackTarget);
}

void BuiltInChorus::processBlock (juce::AudioBuffer<float>& buffer)
{
    if (! prepared || buffer.getNumSamples() <= 0 || buffer.getNumChannels() <= 0)
        return;
    float* channels[2] { buffer.getWritePointer (0),
                         buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr };
    process (channels, std::min (2, buffer.getNumChannels()), buffer.getNumSamples());
}

void BuiltInChorus::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    if (! prepared || channels == nullptr || channels[0] == nullptr || numSamples <= 0)
        return;
    numChannels = std::clamp (numChannels, 1, 2);

    const int size = (int) buf[0].size();
    const float maxDelay = (float) (size - 2);
    const double twoPi = 2.0 * std::numbers::pi;

    for (int n = 0; n < numSamples; ++n)
    {
        const float rateNow = rate.next();
        const float depthNow = depth.next();
        const float mixNow = mix.next();
        const float feedbackNow = feedback.next();
        const float dryGain = 1.0f - mixNow;

        // Advance the LFO once per sample. Right channel reads at +pi so
        // the two sides move in opposite directions (stereo width).
        lfoPhase += twoPi * (double) rateNow / sampleRate;
        if (lfoPhase >= twoPi)
            lfoPhase -= twoPi;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const double phase = lfoPhase + (ch == 1 ? std::numbers::pi : 0.0);
            const float delayMs = kCentreDelayMs + depthNow * (float) std::sin (phase);
            const float delayNow = std::clamp (delayMs * (float) (sampleRate * 0.001), 0.0f, maxDelay);

            const float dry = channels[ch][n];
            const float delayed = detail::readFractional (buf[ch], writePos, delayNow);
            buf[ch][(size_t) writePos] = detail::flushDenormal (dry + feedbackNow * delayed);
            channels[ch][n] = dry * dryGain + delayed * mixNow;
        }

        if (++writePos >= size)
            writePos = 0;
    }
}

} // namespace omx
