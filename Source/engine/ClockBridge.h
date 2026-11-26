#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

#include "dsp/SincTable.h"

// Carries stereo audio from one clock domain to another (e.g. a WASAPI device or an app
// capture thread -> the main ASIO callback, or the main callback -> a second output device).
//
// One writer thread, one reader thread, lock-free. The reader resamples with a 16-tap
// windowed sinc (clean enough for music, not just voice). Its rate = writerRate / readerRate, nudged by a few hundred ppm so the
// buffer fill stays near a target: that cancels clock drift between devices that both
// claim "48 kHz" but aren't exactly 48 kHz.
class ClockBridge
{
public:
    explicit ClockBridge (int capacityPow2 = 1 << 15)
        : capacity ((uint64_t) capacityPow2), mask ((uint64_t) capacityPow2 - 1)
    {
        for (auto& b : buf)
            b.assign ((size_t) capacityPow2, 0.0f);
    }

    // Writer thread. numChannels 1 (duplicated to both sides) or 2+ (first two used).
    void write (const float* const* data, int numChannels, int numFrames, double rate) noexcept
    {
        if (numChannels <= 0 || numFrames <= 0)
            return;

        writerRate.store (rate, std::memory_order_relaxed);
        if (numFrames > writerBlock.load (std::memory_order_relaxed))
            writerBlock.store (numFrames, std::memory_order_relaxed);   // largest block seen

        const uint64_t w = written.load (std::memory_order_relaxed);
        const uint64_t used = w - readFloor.load (std::memory_order_acquire);
        if (used + (uint64_t) numFrames + 8 > capacity)
        {
            overruns.fetch_add (1, std::memory_order_relaxed);
            return;   // reader stalled or gone; drop this block
        }

        const float* l = data[0];
        const float* r = numChannels > 1 ? data[1] : data[0];
        for (int i = 0; i < numFrames; ++i)
        {
            const auto idx = (w + (uint64_t) i) & mask;
            buf[0][idx] = l != nullptr ? l[i] : 0.0f;
            buf[1][idx] = r != nullptr ? r[i] : 0.0f;
        }

        written.store (w + (uint64_t) numFrames, std::memory_order_release);
    }

    // Reader thread. Always fills both outputs (with silence if there's nothing to play).
    void read (float* outL, float* outR, int numFrames, double rate) noexcept
    {
        const double wr = writerRate.load (std::memory_order_relaxed);
        const uint64_t w = written.load (std::memory_order_acquire);

        if (wr <= 0.0 || rate <= 0.0)
            return silence (outL, outR, numFrames);

        const double step = wr / rate;
        readerBlock = std::max (readerBlock, numFrames);

        // Anti-alias cutoff follows the conversion ratio (only recomputed when it changes).
        if (std::abs (step - designedStep) > 0.01)
        {
            sinc.design (0.45 * std::min (1.0, 1.0 / step));
            designedStep = step;
        }

        // Enough to cover one block from each side plus scheduling jitter. Every underrun
        // widens the safety margin, so a jittery device settles on a margin that works.
        const double target = writerBlock.load (std::memory_order_relaxed) + readerBlock * step + margin;
        double fill = (double) w - readPos;

        if (! primed)
        {
            if ((double) w < target || fill < target)
                return silence (outL, outR, numFrames);

            primed = true;
            readPos = (double) w - target;
            avgFill = target;
            fill = target;
            fadeInPos = 0;   // resume with a short fade instead of a click
        }

        if (fill < numFrames * step * 1.01 + 12.0)   // + the sinc's look-ahead
        {
            underruns.fetch_add (1, std::memory_order_relaxed);
            margin = std::min (margin + 256.0, 8192.0);
            framesSinceUnderrun = 0;
            primed = false;
            return silence (outL, outR, numFrames);
        }

        // After 10 s without trouble, give back some of the extra margin (= latency).
        framesSinceUnderrun += numFrames;
        if (framesSinceUnderrun > (int64_t) (rate * 10.0) && margin > 256.0)
        {
            margin = std::max (256.0, margin - 64.0);
            framesSinceUnderrun = 0;
        }

        if (fill > target * 4.0 + 4096.0 || fill > (double) capacity * 0.75)
        {
            readPos = (double) w - target;   // way behind (e.g. after a stall): jump forward
            avgFill = target;
        }

        // Slow, gentle correction: real clock drift is well under 0.1%, so a small, smoothed
        // nudge keeps the fill steady without audible pitch wobble.
        avgFill += 0.002 * (fill - avgFill);
        const double correction = std::clamp (1.0 + (avgFill - target) * 1.0e-5, 0.997, 1.003);
        const double s = step * correction;

        for (int i = 0; i < numFrames; ++i)
        {
            const auto idx = (int64_t) std::floor (readPos);
            const float t = (float) (readPos - (double) idx);
            outL[i] = sinc.at (buf[0].data(), mask, idx, t);
            outR[i] = sinc.at (buf[1].data(), mask, idx, t);
            readPos += s;

            if (fadeInPos < fadeInLength)
            {
                const float g = (float) fadeInPos++ / (float) fadeInLength;
                outL[i] *= g;
                outR[i] *= g;
            }
        }

        readFloor.store ((uint64_t) std::max<int64_t> (0, (int64_t) std::floor (readPos) - 10),
                         std::memory_order_release);
    }

    // Diagnostics
    std::atomic<int> underruns { 0 }, overruns { 0 };

private:
    static void silence (float* l, float* r, int n) noexcept
    {
        std::fill (l, l + n, 0.0f);
        std::fill (r, r + n, 0.0f);
    }

    const uint64_t capacity, mask;
    std::vector<float> buf[2];

    std::atomic<uint64_t> written { 0 };
    std::atomic<uint64_t> readFloor { 0 };
    std::atomic<double> writerRate { 0.0 };
    std::atomic<int> writerBlock { 0 };

    // Reader-only state
    omx::SincTable sinc;
    double designedStep = 1.0;
    double readPos = 0.0;
    double avgFill = 0.0;
    double margin = 256.0;
    int readerBlock = 0;
    bool primed = false;
    int64_t framesSinceUnderrun = 0;
    static constexpr int fadeInLength = 96;   // 2 ms at 48 kHz
    int fadeInPos = fadeInLength;
};
