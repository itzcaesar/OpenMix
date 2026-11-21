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
    }

    void clear() noexcept
    {
        for (auto& b : buf)
            std::fill (b.begin(), b.end(), 0.0f);
    }

    int getMaxDelay() const noexcept { return size - 1; }

    // Delays left/right in place by `delay` samples (clamped to the prepared maximum).
    void process (float* left, float* right, int numSamples, int delay) noexcept
    {
        if (buf[0].empty())
            return;

        delay = std::clamp (delay, 0, size - 1);
        float* data[2] { left, right };

        int p = pos;
        for (int i = 0; i < numSamples; ++i)
        {
            const int read = (p - delay + size) % size;
            for (int ch = 0; ch < 2; ++ch)
            {
                buf[ch][(size_t) p] = data[ch][i];
                data[ch][i] = buf[ch][(size_t) read];
            }
            p = (p + 1) % size;
        }
        pos = p;
    }

private:
    std::vector<float> buf[2];
    int size = 0, pos = 0;
};
