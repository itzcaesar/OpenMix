#pragma once

// Built-in strip processors: gate, compressor, 8-band EQ, de-esser.
// Plain C++, allocation-free in process(), so they are real-time safe and unit-testable.
// Each process() works in place on a stereo pair and returns its lowest gain in the block
// (1 = untouched), which the UI shows as a gain-reduction meter.

#include "dsp/Biquad.h"
#include "dsp/EqBands.h"
#include <algorithm>
#include <array>
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
        float attackMs    = 1.0f;     // gain rise time after opening
        float holdMs      = 80.0f;    // stay open after the detector falls below the close threshold
        float releaseMs   = 120.0f;   // gain fall time after closing

        void prepare (double fs)
        {
            sampleRate = fs > 0.0 ? fs : 48000.0;
            envAttack  = coeff (sampleRate, 0.5);
            envRelease = coeff (sampleRate, 60.0);
            configuredAttackMs = configuredHoldMs = configuredReleaseMs = -1.0f;
            updateTiming();
            reset();
        }

        void reset() noexcept { env = 0.0f; gain = 1.0f; holdCount = 0; open = true; }

        float process (float* l, float* r, int n) noexcept
        {
            updateTiming();
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
        void updateTiming() noexcept
        {
            const float nextAttackMs = std::clamp (std::isfinite (attackMs) ? attackMs : 1.0f, 0.1f, 100.0f);
            const float nextHoldMs = std::clamp (std::isfinite (holdMs) ? holdMs : 80.0f, 0.0f, 500.0f);
            const float nextReleaseMs = std::clamp (std::isfinite (releaseMs) ? releaseMs : 120.0f, 10.0f, 1000.0f);
            if (nextAttackMs != configuredAttackMs)
            {
                openCoef = coeff (sampleRate, nextAttackMs);
                configuredAttackMs = nextAttackMs;
            }
            if (nextHoldMs != configuredHoldMs)
            {
                holdSamples = (int) std::lround (sampleRate * 0.001 * nextHoldMs);
                holdCount = std::min (holdCount, holdSamples);
                configuredHoldMs = nextHoldMs;
            }
            if (nextReleaseMs != configuredReleaseMs)
            {
                closeCoef = coeff (sampleRate, nextReleaseMs);
                configuredReleaseMs = nextReleaseMs;
            }
        }

        double sampleRate = 48000.0;
        float configuredAttackMs = -1.0f, configuredHoldMs = -1.0f, configuredReleaseMs = -1.0f;
        float envAttack = 0, envRelease = 0, openCoef = 0, closeCoef = 0;
        float env = 0, gain = 1;
        int holdSamples = 0, holdCount = 0;
        bool open = true;
    };

    //==========================================================================
    // Feed-forward compressor, stereo-linked, adjustable soft knee, smoothing in the dB domain.
    // Make-up gain is automatic (half the reduction at 0 dBFS), capped at +12 dB.
    class Compressor
    {
    public:
        float thresholdDb = -18.0f;
        float ratio       = 3.0f;
        float kneeDb      = 6.0f;
        float attackMs    = 5.0f;
        float releaseMs   = 120.0f;

        void prepare (double fs)
        {
            sampleRate = fs > 0.0 ? fs : 48000.0;
            configuredAttackMs = configuredReleaseMs = -1.0f;
            updateTiming();
            reset();
        }

        void reset() noexcept { grDb = 0.0f; }

        static float staticReductionDb (float inputDb, float thresholdDb, float ratio, float kneeDb = 6.0f) noexcept
        {
            return reductionForKnee (inputDb, thresholdDb, ratio, safeKneeDb (kneeDb));
        }

        float makeupDb() const noexcept
        {
            return std::clamp (-thresholdDb * (1.0f - 1.0f / std::max (1.0f, ratio)) * 0.5f, 0.0f, 12.0f);
        }

        float process (float* l, float* r, int n) noexcept
        {
            updateTiming();
            const float makeup = makeupDb();
            const float activeKneeDb = safeKneeDb (kneeDb);
            float maxGr = 0.0f;

            for (int i = 0; i < n; ++i)
            {
                const float peak = std::max (std::abs (l[i]), std::abs (r[i]));
                const float target = reductionForKnee (toDb (peak), thresholdDb, ratio, activeKneeDb);
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
        static float safeKneeDb (float value) noexcept
        {
            return std::clamp (std::isfinite (value) ? value : 6.0f, 0.0f, 18.0f);
        }

        static float reductionForKnee (float inputDb, float thresholdDb, float ratio, float kneeDb) noexcept
        {
            const float over = inputDb - thresholdDb;
            const float slope = 1.0f - 1.0f / std::max (1.0f, ratio);
            if (kneeDb <= 0.0f)
                return std::max (0.0f, slope * over);
            if (2.0f * over < -kneeDb)               return 0.0f;
            if (2.0f * std::abs (over) <= kneeDb)    return slope * (over + kneeDb * 0.5f) * (over + kneeDb * 0.5f) / (2.0f * kneeDb);
            return slope * over;
        }

        void updateTiming() noexcept
        {
            const float safeAttackMs = std::clamp (std::isfinite (attackMs) ? attackMs : 5.0f, 0.1f, 100.0f);
            const float safeReleaseMs = std::clamp (std::isfinite (releaseMs) ? releaseMs : 120.0f, 10.0f, 1000.0f);
            if (safeAttackMs != configuredAttackMs)
            {
                attack = coeff (sampleRate, safeAttackMs);
                configuredAttackMs = safeAttackMs;
            }
            if (safeReleaseMs != configuredReleaseMs)
            {
                release = coeff (sampleRate, safeReleaseMs);
                configuredReleaseMs = safeReleaseMs;
            }
        }

        double sampleRate = 48000.0;
        float configuredAttackMs = -1.0f, configuredReleaseMs = -1.0f;
        float attack = 0, release = 0, grDb = 0;
    };

    //==========================================================================
    // 8-band parametric EQ (see dsp/EqBands.h): low shelf, six bells, high shelf.
    // A band at 0 dB is skipped; it starts from a clean state when it is turned up.
    class ParametricEq
    {
    public:
        std::array<float, kEqBands> db {}, hz = kEqDefaultHz, q = defaultQ();

        void prepare (double sampleRate)
        {
            fs = sampleRate;
            reset();
            for (auto& l : last) l = { 1.0e9f, 0.0f, 0.0f };   // force coefficient updates
        }

        void reset() noexcept
        {
            for (auto& b : bands) b.reset();
            active.fill (false);
        }

        void process (float* l, float* r, int n) noexcept
        {
            int on[kEqBands], numOn = 0;
            for (int b = 0; b < kEqBands; ++b)
            {
                if (std::abs (db[(size_t) b]) < 0.01f)
                {
                    active[(size_t) b] = false;
                    continue;
                }
                update (b);
                if (! active[(size_t) b])
                {
                    bands[(size_t) b].reset();
                    active[(size_t) b] = true;
                }
                on[numOn++] = b;
            }

            for (int i = 0; i < n; ++i)
            {
                float x = l[i], y = r[i];
                for (int k = 0; k < numOn; ++k)
                {
                    auto& f = bands[(size_t) on[k]];
                    x = f.process (x, 0);
                    y = f.process (y, 1);
                }
                l[i] = x;
                r[i] = y;
            }
        }

        // Coefficients for one band (also used by the UI to draw the curve).
        static void design (Biquad& f, int band, double fs, float gainDb, float freq, float bandQ) noexcept
        {
            const double hzSafe = std::clamp ((double) freq, 10.0, fs * 0.45);
            switch (eqBandType (band))
            {
                case EqBandType::lowShelf:  f.setLowShelf (fs, hzSafe, gainDb); break;
                case EqBandType::highShelf: f.setHighShelf (fs, hzSafe, gainDb); break;
                case EqBandType::bell:      f.setPeak (fs, hzSafe, std::clamp (bandQ, kEqMinQ, kEqMaxQ), gainDb); break;
            }
        }

    private:
        static std::array<float, kEqBands> defaultQ() { std::array<float, kEqBands> a; a.fill (kEqDefaultQ); return a; }

        void update (int b) noexcept
        {
            auto& l = last[(size_t) b];
            if (l[0] == db[(size_t) b] && l[1] == hz[(size_t) b] && l[2] == q[(size_t) b])
                return;
            design (bands[(size_t) b], b, fs, db[(size_t) b], hz[(size_t) b], q[(size_t) b]);
            l = { db[(size_t) b], hz[(size_t) b], q[(size_t) b] };
        }

        double fs = 48000.0;
        std::array<Biquad, kEqBands> bands;
        std::array<std::array<float, 3>, kEqBands> last {};
        std::array<bool, kEqBands> active {};
    };

    //==========================================================================
    // De-esser: listens around the configured sibilance frequency and, only while it is there,
    // turns the top end down with a high shelf. Leaves the rest of the voice alone.
    class DeEsser
    {
    public:
        float thresholdDb = -30.0f;
        float maxReductionDb = 10.0f;
        float detectorHz = 6500.0f;

        static float clampDetectorHz (float hz, double sampleRate) noexcept
        {
            const float maxHz = (float) std::min (12000.0, (sampleRate > 0.0 ? sampleRate : 48000.0) * 0.45);
            const float minHz = std::min (2500.0f, maxHz);
            return std::clamp (std::isfinite (hz) ? hz : 6500.0f, minHz, maxHz);
        }

        static float shelfFrequencyFor (float detectorFrequency) noexcept
        {
            const float safeHz = std::isfinite (detectorFrequency) ? detectorFrequency : 6500.0f;
            return safeHz * (4500.0f / 6500.0f);
        }

        void prepare (double sampleRate)
        {
            fs = sampleRate > 0.0 ? sampleRate : 48000.0;
            configuredDetectorHz = -1.0f;
            updateDetector();
            attack = coeff (fs, 1.0);
            release = coeff (fs, 60.0);
            reset();
        }

        void reset() noexcept
        {
            detector.reset(); shelf.reset();
            env = 0.0f; reductionDb = 0.0f; appliedDb = -1.0f; appliedShelfHz = -1.0f;
        }

        float process (float* l, float* r, int n) noexcept
        {
            updateDetector();
            const float shelfHz = shelfFrequencyFor (configuredDetectorHz);
            float maxRed = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                const float band = std::abs (detector.process (0.5f * (l[i] + r[i]), 0));
                const float c = band > env ? attack : release;
                env = c * env + (1.0f - c) * band;

                reductionDb = std::clamp (toDb (env) - thresholdDb, 0.0f, maxReductionDb);
                if ((i & 31) == 0 && (std::abs (reductionDb - appliedDb) > 0.1f || shelfHz != appliedShelfHz))
                {
                    shelf.setHighShelf (fs, shelfHz, -reductionDb);
                    appliedDb = reductionDb;
                    appliedShelfHz = shelfHz;
                }

                l[i] = shelf.process (l[i], 0);
                r[i] = shelf.process (r[i], 1);
                maxRed = std::max (maxRed, appliedDb);
            }
            return toGain (-maxRed);
        }

    private:
        void updateDetector() noexcept
        {
            const float nextHz = clampDetectorHz (detectorHz, fs);
            if (nextHz != configuredDetectorHz)
            {
                detector.setBandPass (fs, nextHz, 1.4);
                detector.reset();
                configuredDetectorHz = nextHz;
                env = 0.0f;
                appliedDb = -1.0f;
                appliedShelfHz = -1.0f;
            }
        }

        double fs = 48000.0;
        Biquad detector, shelf;
        float configuredDetectorHz = -1.0f, appliedShelfHz = -1.0f;
        float attack = 0, release = 0, env = 0, reductionDb = 0, appliedDb = -1;
    };
}
