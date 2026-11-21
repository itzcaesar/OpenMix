#pragma once

// Windowed-sinc interpolation for resampling (16 taps, Blackman window, 128 phases with linear
// interpolation between phases). Used by the clock bridge and the noise remover.
// design() computes coefficients without allocating, so it may run on the audio thread.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace omx
{
    class SincTable
    {
    public:
        static constexpr int taps = 16;      // samples idx-7 .. idx+8
        static constexpr int phases = 128;

        SincTable() { design (0.45); }

        // cutoff: in cycles per *source* sample (0.5 = source Nyquist). Use
        // 0.45 * min (1, targetRate / sourceRate) so downsampling doesn't alias.
        void design (double cutoff) noexcept
        {
            constexpr double half = taps / 2.0;
            for (int p = 0; p <= phases; ++p)
            {
                const double t = (double) p / phases;
                double sum = 0.0;
                for (int k = 0; k < taps; ++k)
                {
                    const double x = (k - (taps / 2 - 1)) - t;   // distance from the read position
                    const double arg = 2.0 * cutoff * x;
                    const double sinc = std::abs (arg) < 1.0e-9 ? 1.0 : std::sin (std::numbers::pi * arg) / (std::numbers::pi * arg);
                    const double w = std::abs (x) >= half ? 0.0
                                   : 0.42 + 0.5 * std::cos (std::numbers::pi * x / half) + 0.08 * std::cos (2.0 * std::numbers::pi * x / half);
                    coef[p][k] = (float) (2.0 * cutoff * sinc * w);
                    sum += coef[p][k];
                }
                for (int k = 0; k < taps; ++k)          // unity gain at DC for every phase
                    coef[p][k] = (float) (coef[p][k] / sum);
            }
        }

        // Value at position idx + t (0 <= t < 1) in a power-of-two ring buffer.
        float at (const float* ring, uint64_t mask, int64_t idx, float t) const noexcept
        {
            const float pf = t * (float) phases;
            const int p = std::min ((int) pf, phases - 1);
            const float frac = pf - (float) p;
            const float* c0 = coef[p];
            const float* c1 = coef[p + 1];

            float acc = 0.0f;
            for (int k = 0; k < taps; ++k)
            {
                const float s = ring[(uint64_t) (idx - (taps / 2 - 1) + k) & mask];
                acc += s * (c0[k] + frac * (c1[k] - c0[k]));
            }
            return acc;
        }

    private:
        float coef[phases + 1][taps] {};
    };
}
