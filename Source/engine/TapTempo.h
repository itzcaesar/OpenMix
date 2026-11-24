#pragma once

// Tap tempo: computes a BPM from tap timestamps. JUCE-free, so it can be
// unit-tested by tests/CoreTests.cpp (plain C++) and used by MixerEngine.
//
// Behaviour: the BPM is 60 / mean of the recent tap intervals (up to the
// last 8 intervals, i.e. 9 taps). A pause longer than kResetAfterSeconds
// (2 s) restarts the tap sequence, so an old tempo never drags a new one
// down. The result is clamped to 40..240 BPM; a single tap (or no taps)
// leaves the current BPM untouched. tap() reads the steady clock; tapAt()
// takes an explicit timestamp in seconds, for tests and for callers that
// already have one.

#include <array>
#include <chrono>

namespace omx
{

class TapTempo
{
public:
    static constexpr double kMinBpm = 40.0;
    static constexpr double kMaxBpm = 240.0;
    static constexpr double kDefaultBpm = 120.0;
    static constexpr double kResetAfterSeconds = 2.0;
    static constexpr int kMaxIntervals = 8;   // last 8 intervals (9 taps)

    static double clampBpm (double bpm) noexcept
    {
        if (bpm < kMinBpm)
            return kMinBpm;
        if (bpm > kMaxBpm)
            return kMaxBpm;
        return bpm;
    }

    static double bpmFromInterval (double intervalSeconds) noexcept
    {
        return intervalSeconds > 0.0 ? clampBpm (60.0 / intervalSeconds) : kDefaultBpm;
    }

    // Tap now (steady clock). Returns the current BPM.
    double tap()
    {
        const double now = std::chrono::duration<double> (
            std::chrono::steady_clock::now().time_since_epoch()).count();
        tapAt (now);
        return bpm;
    }

    // Tap at an explicit timestamp in seconds. Non-positive or zero-length
    // intervals are ignored (same timestamp twice, or a clock going back).
    void tapAt (double nowSeconds) noexcept
    {
        if (lastTap >= 0.0)
        {
            const double dt = nowSeconds - lastTap;
            if (dt <= 0.0)
                return;   // duplicate / out-of-order tap: ignore it

            if (dt > kResetAfterSeconds)
            {
                // Long pause: start a fresh sequence from this tap.
                numIntervals = 0;
                nextInterval = 0;
                tapCount = 1;
                lastTap = nowSeconds;
                return;
            }

            intervals[(size_t) nextInterval] = dt;
            nextInterval = (nextInterval + 1) % kMaxIntervals;
            if (numIntervals < kMaxIntervals)
                ++numIntervals;

            double sum = 0.0;
            for (int i = 0; i < numIntervals; ++i)
                sum += intervals[(size_t) i];
            bpm = clampBpm (60.0 / (sum / (double) numIntervals));
            ++tapCount;
        }
        else
        {
            tapCount = 1;
        }

        lastTap = nowSeconds;
    }

    double getBpm() const noexcept { return bpm; }
    int getTapCount() const noexcept { return tapCount; }

    // Set the BPM directly (e.g. from a session file or a typed value).
    // Clamped to 40..240; clears the tap history so stale taps cannot
    // override the value on the next tap.
    void setBpm (double bpmIn) noexcept
    {
        bpm = clampBpm (bpmIn);
        resetTaps();
    }

    void reset() noexcept
    {
        bpm = kDefaultBpm;
        resetTaps();
    }

private:
    void resetTaps() noexcept
    {
        numIntervals = 0;
        nextInterval = 0;
        tapCount = 0;
        lastTap = -1.0;
    }

    double bpm = kDefaultBpm;
    std::array<double, kMaxIntervals> intervals {};
    int numIntervals = 0, nextInterval = 0;
    double lastTap = -1.0;   // < 0 = no tap yet
    int tapCount = 0;
};

} // namespace omx
