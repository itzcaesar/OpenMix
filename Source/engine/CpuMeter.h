#pragma once

// CPU meter: audio-thread CPU usage as a percentage of real time, smoothed
// with an exponential moving average. JUCE-free and header-only, so it can
// be unit-tested by tests/CoreTests.cpp (plain C++).
//
// Use: once per audio block, call addSample() with the time the block took
// to process and the block's duration in seconds (numSamples / sampleRate).
// The measurement itself is one clock read per block in the caller, plus
// one division and one multiply here -- near-zero overhead. getPercent()
// is safe to call from the UI thread (the smoothed value is a plain float;
// a torn read can only be off by one update).
//
// Smoothing: EMA with alpha 0.1 per block, so the reading settles in
// ~25 blocks (~0.25 s at 512 samples / 48 kHz) and does not jitter with
// every block. A block that takes longer than real time reads > 100%.

namespace omx
{

class CpuMeter
{
public:
    static constexpr float kAlpha = 0.1f;   // EMA weight per block

    static float smooth (float current, float sample, float alpha = kAlpha) noexcept
    {
        return current + alpha * (sample - current);
    }

    // processSeconds: wall time spent processing this block.
    // bufferSeconds: the block's duration (numSamples / sampleRate).
    // A zero or negative buffer duration is ignored (no sample to measure
    // against); usage above 100% is reported as-is, so overload is visible.
    void addSample (double processSeconds, double bufferSeconds) noexcept
    {
        if (bufferSeconds <= 0.0 || processSeconds < 0.0)
            return;

        const float sample = (float) (100.0 * processSeconds / bufferSeconds);
        percent = smooth (percent, sample);
    }

    float getPercent() const noexcept { return percent; }
    void reset() noexcept { percent = 0.0f; }

private:
    float percent = 0.0f;
};

} // namespace omx
