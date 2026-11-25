#pragma once

// Stereo delay line for latency compensation. Allocates only in prepare(); process() is
// real-time safe. Plain C++ so it can be unit-tested without JUCE.

#include <algorithm>
#include <vector>

class DelayLine
{
public:
    void prepare (int maxDelaySamples)
    {
        size = std::max (1, maxDelaySamples + 1);
        for (auto& b : buf)
            b.assign ((size_t) size, 0.0f);
        pos = 0;
        lastDelay = -1;
    }

    void clear() noexcept
    {
        for (auto& b : buf)
            std::fill (b.begin(), b.end(), 0.0f);
        lastDelay = -1;
    }

    int getMaxDelay() const noexcept { return size - 1; }

    // Delays left/right in place by `delay` samples (clamped to the prepared maximum).
    // Crossfades across the block when `delay` changes so latency jumps never click.
    void process (float* left, float* right, int numSamples, int delay) noexcept
    {
        if (buf[0].empty() || numSamples <= 0)
            return;

        delay = std::clamp (delay, 0, size - 1);
        if (lastDelay < 0)
            lastDelay = delay;

        float* data[2] { left, right };
        const bool crossfade = delay != lastDelay;
        const float invN = crossfade ? 1.0f / (float) numSamples : 0.0f;

        int p = pos;
        for (int i = 0; i < numSamples; ++i)
        {
            const int readNew = (p - delay + size) % size;
            const int readOld = crossfade ? (p - lastDelay + size) % size : readNew;
            const float mix = crossfade ? (float) i * invN : 1.0f;

            for (int ch = 0; ch < 2; ++ch)
            {
                buf[ch][(size_t) p] = data[ch][i];
                const float yNew = buf[ch][(size_t) readNew];
                if (crossfade)
                {
                    const float yOld = buf[ch][(size_t) readOld];
                    data[ch][i] = yOld + mix * (yNew - yOld);
                }
                else
                {
                    data[ch][i] = yNew;
                }
            }
            p = (p + 1) % size;
        }
        pos = p;
        lastDelay = delay;
    }

private:
    std::vector<float> buf[2];
    int size = 0, pos = 0, lastDelay = -1;
};
