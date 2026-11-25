#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>

// Allocation-free stereo biquad (RBJ cookbook), safe to retune on the audio thread.
struct Biquad
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float z1[2] {}, z2[2] {};

    void reset() noexcept { z1[0] = z1[1] = z2[0] = z2[1] = 0.0f; }

    float process (float x, int ch) noexcept
    {
        const float y = b0 * x + z1[ch];
        z1[ch] = b1 * x - a1 * y + z2[ch];
        z2[ch] = b2 * x - a2 * y;
        return y;
    }

    void setPeak (double fs, double freq, double q, double gainDb) noexcept
    {
        const double rate = fs > 0.0 ? fs : 48000.0;
        const double f = std::clamp (freq, 10.0, rate * 0.45);
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w = 2.0 * std::numbers::pi * f / rate;
        const double cw = std::cos (w), alpha = std::sin (w) / (2.0 * std::max (0.1, q));
        set (1.0 + alpha * A, -2.0 * cw, 1.0 - alpha * A,
             1.0 + alpha / A, -2.0 * cw, 1.0 - alpha / A);
    }

    void setLowShelf (double fs, double freq, double gainDb, double q = 0.707) noexcept
    {
        const double rate = fs > 0.0 ? fs : 48000.0;
        const double f = std::clamp (freq, 10.0, rate * 0.45);
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w = 2.0 * std::numbers::pi * f / rate;
        const double cw = std::cos (w), alpha = std::sin (w) / (2.0 * std::max (0.1, q));
        const double k = 2.0 * std::sqrt (A) * alpha;
        set (A * ((A + 1) - (A - 1) * cw + k),
             2 * A * ((A - 1) - (A + 1) * cw),
             A * ((A + 1) - (A - 1) * cw - k),
             (A + 1) + (A - 1) * cw + k,
             -2 * ((A - 1) + (A + 1) * cw),
             (A + 1) + (A - 1) * cw - k);
    }

    void setHighShelf (double fs, double freq, double gainDb, double q = 0.707) noexcept
    {
        const double rate = fs > 0.0 ? fs : 48000.0;
        const double f = std::clamp (freq, 10.0, rate * 0.45);
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w = 2.0 * std::numbers::pi * f / rate;
        const double cw = std::cos (w), alpha = std::sin (w) / (2.0 * std::max (0.1, q));
        const double k = 2.0 * std::sqrt (A) * alpha;
        set (A * ((A + 1) + (A - 1) * cw + k),
             -2 * A * ((A - 1) + (A + 1) * cw),
             A * ((A + 1) + (A - 1) * cw - k),
             (A + 1) - (A - 1) * cw + k,
             2 * ((A - 1) - (A + 1) * cw),
             (A + 1) - (A - 1) * cw - k);
    }

    // Band-pass with 0 dB peak gain (RBJ "constant 0 dB peak gain"), used as a detector.
    void setBandPass (double fs, double freq, double q) noexcept
    {
        const double rate = fs > 0.0 ? fs : 48000.0;
        const double f = std::clamp (freq, 10.0, rate * 0.45);
        const double w = 2.0 * std::numbers::pi * f / rate;
        const double cw = std::cos (w), alpha = std::sin (w) / (2.0 * std::max (0.1, q));
        set (alpha, 0.0, -alpha, 1.0 + alpha, -2.0 * cw, 1.0 - alpha);
    }

    // RBJ high-pass / low-pass (Q = 0.707 is Butterworth, -3 dB at the cutoff).
    // The cutoff is clamped inside (0, Nyquist) so retuning can never go unstable.
    void setHighPass (double fs, double freq, double q = 0.707) noexcept
    {
        const double rate = fs > 0.0 ? fs : 48000.0;
        const double f = std::clamp (freq, 10.0, rate * 0.45);
        const double w = 2.0 * std::numbers::pi * f / rate;
        const double cw = std::cos (w), alpha = std::sin (w) / (2.0 * std::max (0.1, q));
        set ((1.0 + cw) / 2.0, -(1.0 + cw), (1.0 + cw) / 2.0,
             1.0 + alpha, -2.0 * cw, 1.0 - alpha);
    }

    void setLowPass (double fs, double freq, double q = 0.707) noexcept
    {
        const double rate = fs > 0.0 ? fs : 48000.0;
        const double f = std::clamp (freq, 10.0, rate * 0.45);
        const double w = 2.0 * std::numbers::pi * f / rate;
        const double cw = std::cos (w), alpha = std::sin (w) / (2.0 * std::max (0.1, q));
        set ((1.0 - cw) / 2.0, 1.0 - cw, (1.0 - cw) / 2.0,
             1.0 + alpha, -2.0 * cw, 1.0 - alpha);
    }

private:
    void set (double nb0, double nb1, double nb2, double na0, double na1, double na2) noexcept
    {
        b0 = (float) (nb0 / na0); b1 = (float) (nb1 / na0); b2 = (float) (nb2 / na0);
        a1 = (float) (na1 / na0); a2 = (float) (na2 / na0);
    }
};
