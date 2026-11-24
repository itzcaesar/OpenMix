#pragma once

// Standalone built-in DSP effects for OpenMix: reverb, tempo-synced delay and
// chorus. Plain JUCE audio-thread code, no dependencies on the app, the engine
// or rnnoise.
//
// Design notes for the integration agent:
//  - All three classes share the BuiltInFx interface below, so they can be
//    held, prepared and processed uniformly (e.g. in an insert slot).
//  - The DSP core of each class is plain C++ over float* channels
//    (process(float* const*, int, int)); processBlock(juce::AudioBuffer<float>&)
//    is a thin wrapper around it, so the engine can also call process() with
//    raw pointers when it already has them.
//  - Channel handling: channels 0 and 1 are processed (mono input processes
//    channel 0 only, with the left network). Any channels beyond the first
//    two are passed through untouched -- OpenMix buses are stereo, so this
//    only matters for exotic >2 channel buffers.
//  - No allocations happen in processBlock/process(): every buffer is
//    allocated in prepareToPlay(). Calling processBlock() before prepare is
//    a no-op (signal passes through dry).
//  - All parameters are clamped to their documented ranges in the setters,
//    so out-of-range values from a UI or preset can never destabilise the
//    DSP. Mix / feedback / delay-time moves are smoothed per-sample, so
//    parameter automation does not click or zipper.
//  - Denormal-safe: feedback states and delay-line writes are flushed to
//    zero below ~1e-18 (-360 dBFS), so a decaying tail cannot burn CPU in
//    denormal arithmetic.
//  - Sample rates 44100 / 48000 / 96000 are all handled (any sane rate in
//    8000..192000 Hz works; anything else falls back to 48000 Hz).
//
// This header includes only <juce_audio_basics/juce_audio_basics.h> (for
// juce::AudioBuffer), not the generated JuceHeader.h, so it can be compiled
// standalone -- see tests/BuiltInFxTests.cpp for the one-command build.

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace omx
{

enum class FxType
{
    Reverb,
    Delay,
    Chorus
};

// Common interface for the built-in effects.
class BuiltInFx
{
public:
    virtual ~BuiltInFx() = default;

    virtual FxType getType() const noexcept = 0;

    // Allocate all buffers for this sample rate. blockSize is accepted for
    // interface uniformity; the effects work with any block size.
    virtual void prepareToPlay (double sampleRate, int blockSize) = 0;

    // Process the buffer in place. Channels beyond the first two pass
    // through untouched. No-op until prepareToPlay() has been called.
    virtual void processBlock (juce::AudioBuffer<float>& buffer) = 0;

    // Clear all delay lines / filter states / LFO phase and snap smoothed
    // parameters to their targets (so the next block starts clean).
    virtual void reset() noexcept = 0;

    bool isPrepared() const noexcept { return prepared; }

protected:
    bool prepared = false;
};

namespace detail
{

// One-pole smoothed parameter: the target is set from the UI thread, the
// audio thread calls next() once per sample. Smoothing time constant is
// ~20 ms by default (30 ms for delay time, which moves further).
struct SmoothedFloat
{
    float current = 0.0f, target = 0.0f, alpha = 1.0f;

    void prepare (double sampleRate, double timeConstantMs, float initial) noexcept
    {
        const double fs = sampleRate > 0.0 ? sampleRate : 48000.0;
        const double tcSeconds = std::max (1.0, timeConstantMs) * 0.001;
        alpha = (float) (1.0 - std::exp (-1.0 / (fs * tcSeconds)));
        current = target = initial;
    }

    void setTarget (float value) noexcept { target = value; }
    void snapToTarget() noexcept { current = target; }
    float next() noexcept
    {
        current += alpha * (target - current);
        return current;
    }
};

// Flush denormals: values below -360 dBFS are inaudible but can make the
// CPU fall back to slow denormal arithmetic inside feedback loops.
inline float flushDenormal (float x) noexcept
{
    return std::abs (x) < 1e-18f ? 0.0f : x;
}

// Fractional delay-line read with linear interpolation. delaySamples must
// be in [0, size - 2]; the caller guarantees this by construction.
inline float readFractional (const std::vector<float>& buf, int writePos, float delaySamples) noexcept
{
    const int size = (int) buf.size();
    float readPos = (float) writePos - delaySamples;
    if (readPos < 0.0f)
        readPos += (float) size;
    const int i0 = (int) readPos;
    const float frac = readPos - (float) i0;
    int i1 = i0 + 1;
    if (i1 >= size)
        i1 -= size;
    return buf[(size_t) i0] * (1.0f - frac) + buf[(size_t) i1] * frac;
}

} // namespace detail

//==============================================================================
// BuiltInReverb: Freeverb-style algorithmic reverb -- 8 parallel comb
// filters (with a one-pole low-pass in each feedback loop for damping)
// feeding 4 series allpasses, per channel, plus a short pre-delay.
// Cheap enough for realtime (8 combs + 4 allpasses per channel per sample)
// and stable for every parameter combination: comb feedback is capped at
// 0.98 and allpass feedback is fixed at 0.5.
class BuiltInReverb final : public BuiltInFx
{
public:
    FxType getType() const noexcept override { return FxType::Reverb; }
    void prepareToPlay (double sampleRate, int blockSize) override;
    void processBlock (juce::AudioBuffer<float>& buffer) override;
    void reset() noexcept override;

    // Core DSP over raw channels; channels[1] may be null for mono.
    // numChannels is clamped to 2 internally; extra channels are untouched
    // by the caller (processBlock only passes the first two).
    void process (float* const* channels, int numChannels, int numSamples) noexcept;

    // Room size / decay, 0..1 (default 0.5). Maps to comb feedback
    // 0.70..0.98: 0 is a small dry room, 1 is a long hall. Always < 1,
    // so the network can never run away.
    void setRoomSize (float roomSize) noexcept;
    float getRoomSize() const noexcept { return roomSize; }

    // High-frequency damping in the comb feedback loops, 0..1 (default 0.5).
    // 0 is bright, 1 is very dark.
    void setDamping (float damping) noexcept;
    float getDamping() const noexcept { return damping; }

    // Wet/dry mix, 0..1 (default 0.3). 0 is fully dry, 1 is fully wet.
    // Smoothed, so moves do not click.
    void setMix (float mix) noexcept;
    float getMix() const noexcept { return mixTarget; }

    // Pre-delay, 0..100 ms (default 0). Applied before the comb network.
    void setPreDelayMs (float preDelayMs) noexcept;
    float getPreDelayMs() const noexcept { return preDelayMs; }

private:
    struct Comb
    {
        std::vector<float> buf;
        int pos = 0;
        float filterStore = 0.0f;
        float feedback = 0.84f, damp1 = 0.5f, damp2 = 0.5f;
        float process (float input) noexcept;
    };

    struct Allpass
    {
        std::vector<float> buf;
        int pos = 0;
        float process (float input) noexcept;
    };

    static constexpr int kNumCombs = 8;
    static constexpr int kNumAllpasses = 4;

    void updateCombCoefficients() noexcept;

    double sampleRate = 48000.0;
    float roomSize = 0.5f, damping = 0.5f, mixTarget = 0.3f, preDelayMs = 0.0f;
    detail::SmoothedFloat mix;

    // Pre-delay ring buffer, stereo, 100 ms maximum.
    std::vector<float> preDelayBuf[2];
    int preDelayPos = 0;

    Comb combs[2][kNumCombs];
    Allpass allpasses[2][kNumAllpasses];
};

//==============================================================================
// BuiltInDelay: stereo tempo-synced delay (one delay line per channel, no
// cross-feedback) with feedback, wet/dry mix and a one-pole low-pass in the
// feedback path so repeats get darker instead of building up harshly.
// Delay time is smoothed (~30 ms) and read with fractional interpolation,
// so changing the time -- by hand or from tap tempo -- glides instead of
// clicking or zippering.
class BuiltInDelay final : public BuiltInFx
{
public:
    FxType getType() const noexcept override { return FxType::Delay; }
    void prepareToPlay (double sampleRate, int blockSize) override;
    void processBlock (juce::AudioBuffer<float>& buffer) override;
    void reset() noexcept override;

    void process (float* const* channels, int numChannels, int numSamples) noexcept;

    // Direct delay time, 0..2000 ms (default 250). Switches off tempo sync:
    // after this call the time stays where it was put until setTempoBpm() /
    // setNoteDivision() is called again.
    void setTimeMs (float timeMs) noexcept;
    float getTimeMs() const noexcept { return timeMsTarget; }
    bool isTempoSynced() const noexcept { return tempoSync; }

    // Tap-tempo support. setTempoBpm() (20..300 bpm, default 120) and
    // setNoteDivision() (0.125..4.0, default 1.0) together set the delay
    // time to division * 60000 / bpm ms: 1.0 = quarter note, 0.5 = eighth,
    // 0.25 = sixteenth, 1.5 = dotted quarter, 2.0 = half note. Either call
    // re-enables tempo sync.
    void setTempoBpm (float bpm) noexcept;
    float getTempoBpm() const noexcept { return bpm; }
    void setNoteDivision (float division) noexcept;
    float getNoteDivision() const noexcept { return noteDivision; }

    // Feedback, 0..0.95 (default 0.35). Hard-capped at 0.95, so even a
    // mistake in a preset cannot make the delay run away. Smoothed.
    void setFeedback (float feedback) noexcept;
    float getFeedback() const noexcept { return feedbackTarget; }

    // Wet/dry mix, 0..1 (default 0.3). Smoothed.
    void setMix (float mix) noexcept;
    float getMix() const noexcept { return mixTarget; }

    // One-pole low-pass cutoff in the feedback path, 200..20000 Hz
    // (default 8000). Lower values make repeats progressively darker.
    void setFeedbackLowpassHz (float hz) noexcept;
    float getFeedbackLowpassHz() const noexcept { return feedbackLowpassHz; }

private:
    void updateTempoTime() noexcept;
    void updateFeedbackFilter() noexcept;

    static constexpr double kMaxDelaySeconds = 2.0;

    double sampleRate = 48000.0;
    float timeMsTarget = 250.0f;
    float bpm = 120.0f, noteDivision = 1.0f;
    bool tempoSync = false;
    float feedbackTarget = 0.35f, mixTarget = 0.3f, feedbackLowpassHz = 8000.0f;

    detail::SmoothedFloat delaySamples, feedback, mix;
    float feedbackFilterAlpha = 1.0f, feedbackFilterState[2] {};

    std::vector<float> buf[2];
    int writePos = 0;
};

//==============================================================================
// BuiltInChorus: classic chorus -- one modulated delay line per channel,
// LFO phase offset by 180 degrees between left and right for stereo width.
// The LFO itself is continuous, and mix / feedback / depth are smoothed,
// so there are no clicks on parameter moves. Delay time is
// centre (12 ms) +/- depth, so it never reaches zero.
class BuiltInChorus final : public BuiltInFx
{
public:
    FxType getType() const noexcept override { return FxType::Chorus; }
    void prepareToPlay (double sampleRate, int blockSize) override;
    void processBlock (juce::AudioBuffer<float>& buffer) override;
    void reset() noexcept override;

    void process (float* const* channels, int numChannels, int numSamples) noexcept;

    // LFO rate, 0.05..10 Hz (default 0.7). Smoothed.
    void setRateHz (float rateHz) noexcept;
    float getRateHz() const noexcept { return rateTarget; }

    // Modulation depth, 0..10 ms (default 3). Delay swings
    // 12 ms +/- depth, so the read position always stays >= 2 ms.
    void setDepthMs (float depthMs) noexcept;
    float getDepthMs() const noexcept { return depthTarget; }

    // Wet/dry mix, 0..1 (default 0.5). Smoothed.
    void setMix (float mix) noexcept;
    float getMix() const noexcept { return mixTarget; }

    // Feedback, 0..0.5 (default 0). Hard-capped at 0.5; higher values
    // would turn the chorus into a flanger/resonator. Smoothed.
    void setFeedback (float feedback) noexcept;
    float getFeedback() const noexcept { return feedbackTarget; }

private:
    static constexpr double kMaxDelaySeconds = 0.05; // 50 ms
    static constexpr float kCentreDelayMs = 12.0f;

    double sampleRate = 48000.0;
    float rateTarget = 0.7f, depthTarget = 3.0f, mixTarget = 0.5f, feedbackTarget = 0.0f;

    detail::SmoothedFloat rate, depth, mix, feedback;
    double lfoPhase = 0.0; // radians; right channel reads at +pi

    std::vector<float> buf[2];
    int writePos = 0;
};

} // namespace omx
