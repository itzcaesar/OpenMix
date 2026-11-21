#pragma once

// Built-in strip processors: gate, compressor, 3-band EQ, de-esser.
// Plain C++, allocation-free in process(), so they are real-time safe and unit-testable.
// Each process() works in place on a stereo pair and returns its lowest gain in the block
// (1 = untouched), which the UI shows as a gain-reduction meter.

#include "dsp/Biquad.h"
#include <algorithm>
#include <cmath>

namespace omx
{
    inline float toDb (float gain) noexcept    { return 20.0f * std::log10 (std::max (gain, 1.0e-6f)); }
    inline float toGain (float db) noexcept    { return std::pow (10.0f, db / 20.0f); }
    inline float coeff (double fs, double ms)  { return (float) std::exp (-1.0 / (0.001 * ms * fs)); }

    //==========================================================================
    // Noise gate with hold and 4 dB hysteresis (so it doesn't chatter on a decaying word).
    class Gate
    {
    public:
        float thresholdDb = -50.0f;   // opens above this
        float rangeDb     = -40.0f;   // attenuation while closed (not full silence: sounds natural)

        void prepare (double fs)
        {
            envAttack  = coeff (fs, 0.5);
            envRelease = coeff (fs, 60.0);
            openCoef   = coeff (fs, 1.0);
            closeCoef  = coeff (fs, 120.0);
            holdSamples = (int) (fs * 0.08);
            reset();
        }

        void reset() noexcept { env = 0.0f; gain = 1.0f; holdCount = 0; open = true; }

        float process (float* l, float* r, int n) noexcept
        {
            const float openLevel = toGain (thresholdDb), closeLevel = toGain (thresholdDb - 4.0f);
            const float floorGain = toGain (rangeDb);
            float minGain = 1.0f;

            for (int i = 0; i < n; ++i)
            {
                const float peak = std::max (std::abs (l[i]), std::abs (r[i]));
                const float c = peak > env ? envAttack : envRelease;
                env = c * env + (1.0f - c) * peak;

                if (env > openLevel)        { open = true; holdCount = holdSamples; }
                else if (env < closeLevel)  { if (holdCount > 0) --holdCount; else open = false; }

                const float target = open ? 1.0f : floorGain;
                const float g = target > gain ? openCoef : closeCoef;
                gain = g * gain + (1.0f - g) * target;

                l[i] *= gain;
                r[i] *= gain;
                minGain = std::min (minGain, gain);
            }
            return minGain;
        }

    private:
        float envAttack = 0, envRelease = 0, openCoef = 0, closeCoef = 0;
        float env = 0, gain = 1;
        int holdSamples = 0, holdCount = 0;
        bool open = true;
    };

    //==========================================================================
    // Feed-forward compressor, stereo-linked, 6 dB soft knee, smoothing in the dB domain.
    // Make-up gain is automatic (half the reduction at 0 dBFS), capped at +12 dB.
    class Compressor
    {
    public:
        float thresholdDb = -18.0f;
        float ratio       = 3.0f;

        void prepare (double fs)
        {
            attack  = coeff (fs, 5.0);
            release = coeff (fs, 120.0);
            reset();
        }

        void reset() noexcept { grDb = 0.0f; }

        static float staticReductionDb (float inputDb, float thresholdDb, float ratio, float kneeDb = 6.0f) noexcept
        {
            const float over = inputDb - thresholdDb;
            const float slope = 1.0f - 1.0f / std::max (1.0f, ratio);
            if (2.0f * over < -kneeDb)               return 0.0f;
            if (2.0f * std::abs (over) <= kneeDb)    return slope * (over + kneeDb * 0.5f) * (over + kneeDb * 0.5f) / (2.0f * kneeDb);
            return slope * over;
        }

        float makeupDb() const noexcept
        {
            return std::clamp (-thresholdDb * (1.0f - 1.0f / std::max (1.0f, ratio)) * 0.5f, 0.0f, 12.0f);
        }

        float process (float* l, float* r, int n) noexcept
        {
            const float makeup = makeupDb();
            float maxGr = 0.0f;

            for (int i = 0; i < n; ++i)
            {
                const float peak = std::max (std::abs (l[i]), std::abs (r[i]));
                const float target = staticReductionDb (toDb (peak), thresholdDb, ratio);
                const float c = target > grDb ? attack : release;
                grDb = c * grDb + (1.0f - c) * target;

                const float g = toGain (makeup - grDb);
                l[i] *= g;
                r[i] *= g;
                maxGr = std::max (maxGr, grDb);
            }
            return toGain (-maxGr);
        }

    private:
        float attack = 0, release = 0, grDb = 0;
    };

    //==========================================================================
    // 3-band EQ: low shelf 120 Hz, sweepable mid bell, high shelf 8 kHz (all +/-12 dB).
    class Eq3
    {
    public:
        float lowDb = 0, midDb = 0, midHz = 1000, highDb = 0;

        void prepare (double sampleRate)
        {
            fs = sampleRate;
            low.reset(); mid.reset(); high.reset();
            last[0] = 1.0e9f;   // force a coefficient update
        }

        void reset() noexcept { low.reset(); mid.reset(); high.reset(); }

        void process (float* l, float* r, int n) noexcept
        {
            if (lowDb != last[0] || midDb != last[1] || midHz != last[2] || highDb != last[3])
            {
                low.setLowShelf (fs, 120.0, lowDb);
                mid.setPeak (fs, std::clamp (midHz, 100.0f, 10000.0f), 1.0, midDb);
                high.setHighShelf (fs, 8000.0, highDb);
                last[0] = lowDb; last[1] = midDb; last[2] = midHz; last[3] = highDb;
            }

            for (int i = 0; i < n; ++i)
            {
                l[i] = high.process (mid.process (low.process (l[i], 0), 0), 0);
                r[i] = high.process (mid.process (low.process (r[i], 1), 1), 1);
            }
        }

    private:
        double fs = 48000.0;
        Biquad low, mid, high;
        float last[4] {};
    };

    //==========================================================================
    // De-esser: listens around 6.5 kHz and, only while sibilance is there, turns the top end
    // down with a high shelf. Leaves the rest of the voice alone.
    class DeEsser
    {
    public:
        float thresholdDb = -30.0f;
        float maxReductionDb = 10.0f;

        void prepare (double sampleRate)
        {
            fs = sampleRate;
            detector.setBandPass (fs, 6500.0, 1.4);
            attack = coeff (fs, 1.0);
            release = coeff (fs, 60.0);
            reset();
        }

        void reset() noexcept
        {
            detector.reset(); shelf.reset();
            env = 0.0f; reductionDb = 0.0f; appliedDb = -1.0f;
        }

        float process (float* l, float* r, int n) noexcept
        {
            float maxRed = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                const float band = std::abs (detector.process (0.5f * (l[i] + r[i]), 0));
                const float c = band > env ? attack : release;
                env = c * env + (1.0f - c) * band;

                reductionDb = std::clamp (toDb (env) - thresholdDb, 0.0f, maxReductionDb);
                if ((i & 31) == 0 && std::abs (reductionDb - appliedDb) > 0.1f)
                {
                    shelf.setHighShelf (fs, 4500.0, -reductionDb);
                    appliedDb = reductionDb;
                }

                l[i] = shelf.process (l[i], 0);
                r[i] = shelf.process (r[i], 1);
                maxRed = std::max (maxRed, appliedDb);
            }
            return toGain (-maxRed);
        }

    private:
        double fs = 48000.0;
        Biquad detector, shelf;
        float attack = 0, release = 0, env = 0, reductionDb = 0, appliedDb = -1;
    };
}
