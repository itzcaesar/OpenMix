#pragma once

// Small, allocation-free building blocks that keep the signal path safe.
// Plain C++ (no JUCE) so they can be unit-tested on their own.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace AudioSafety
{
    // True if any sample is NaN or infinite (e.g. a misbehaving plugin). Checks the exponent
    // bits directly, so fast-math compiler settings can't optimize the test away.
    inline bool containsNonFinite (const float* data, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
        {
            std::uint32_t bits;
            std::memcpy (&bits, data + i, sizeof (bits));
            if ((bits & 0x7f800000u) == 0x7f800000u)
                return true;
        }
        return false;
    }

    // One-pole DC blocker (high-pass at ~5 Hz): removes offset from cheap mics/interfaces
    // without touching anything audible.
    class DcBlocker
    {
    public:
        void prepare (double sampleRate) noexcept
        {
            r = (float) std::exp (-2.0 * 3.14159265358979323846 * 5.0 / sampleRate);
            reset();
        }

        void reset() noexcept { x1[0] = x1[1] = y1[0] = y1[1] = 0.0f; }

        void process (float* data, int numSamples, int channel) noexcept
        {
            float px = x1[channel], py = y1[channel];
            for (int i = 0; i < numSamples; ++i)
            {
                const float x = data[i];
                const float y = x - px + r * py;
                px = x;
                py = y;
                data[i] = y;
            }
            x1[channel] = px;
            y1[channel] = std::abs (py) < 1.0e-15f ? 0.0f : py;   // flush tiny tails (no denormals)
        }

    private:
        float r = 0.9993f;
        float x1[2] {}, y1[2] {};
    };

    // Stereo-linked safety limiter for buses. Instant attack (the gain is applied to the same
    // sample that triggered it, so the output can never exceed the ceiling), smooth release.
    // It's a safety net for ears, speakers and the apps fed by virtual buses, not a mastering tool.
    class SafetyLimiter
    {
    public:
        static constexpr float defaultCeiling = 0.9661f;   // -0.3 dBFS

        void prepare (double sampleRate, float releaseSeconds = 0.15f) noexcept
        {
            release = (float) std::exp (-1.0 / (releaseSeconds * sampleRate));
            gain = 1.0f;
        }

        // Returns the smallest gain used in this block (1 = no limiting).
        float process (float* left, float* right, int numSamples) noexcept
        {
            float minGain = 1.0f;
            for (int i = 0; i < numSamples; ++i)
            {
                const float l = std::isfinite (left[i])  ? left[i]  : 0.0f;
                const float r = std::isfinite (right[i]) ? right[i] : 0.0f;
                const float peak = std::max (std::abs (l), std::abs (r));
                const float target = peak > ceiling ? ceiling / peak : 1.0f;

                gain = target < gain ? target : target + (gain - target) * release;

                left[i]  = std::clamp (l * gain, -ceiling, ceiling);
                right[i] = std::clamp (r * gain, -ceiling, ceiling);
                minGain = std::min (minGain, gain);
            }
            return minGain;
        }

        float ceiling = defaultCeiling;

    private:
        float release = 0.9998f;
        float gain = 1.0f;
    };
}
