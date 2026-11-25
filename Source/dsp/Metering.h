#pragma once

// RMS metering and loudness normalisation. Plain C++, no JUCE: covered by CoreTests.
//
// RMS: a 300 ms one-pole smoothing of the block mean-square, so the meter
// reads the sustained level, not the loudest sample. Peak meters show
// transients; RMS shows how loud something actually sounds, which is what
// a streamer checks when a mic and music need to sit together.
//
// Loudness normalisation: a slow gain that pulls a bus towards a target loudness
// (streaming platforms normalise to about -14 LUFS). The bus is measured the way
// BS.1770 does it: K-weighted (a high shelf and a high-pass), both channels summed,
// over a ~3 s window, so the target matches the LUFS readout and the analyser. It
// measures its own input (before the bus fader), so the fader still trims the result.
// The gain is clamped to +/-12 dB and moves with a ~0.5 s time constant, so it never
// pumps on a drum hit; below -60 LUFS it holds its last gain instead of turning the
// noise floor up. Switched off, it glides back to unity rather than jumping.

#include <algorithm>
#include <cmath>
#include "dsp/Loudness.h"   // KWeighting

namespace omx
{
    inline float blockRms (const float* x, int numSamples) noexcept
    {
        if (x == nullptr || numSamples <= 0)
            return 0.0f;
        double sum = 0.0;
        for (int i = 0; i < numSamples; ++i)
            sum += (double) x[i] * (double) x[i];
        return (float) std::sqrt (sum / numSamples);
    }

    // Smoothed RMS level for one channel. Audio thread only.
    class RmsMeter
    {
    public:
        void prepare (double sampleRate) noexcept
        {
            fs = sampleRate > 0.0 ? sampleRate : 48000.0;
            levelSq = 0.0;
        }

        // Returns the smoothed RMS level (linear, 0..1) after this block.
        float push (const float* x, int numSamples) noexcept
        {
            const double blockMs = 1000.0 * numSamples / fs;
            const double alpha = 1.0 - std::exp (-blockMs / 300.0);   // ~300 ms time constant
            const float rms = blockRms (x, numSamples);
            levelSq = levelSq + alpha * ((double) rms * rms - levelSq);
            return (float) std::sqrt (std::max (0.0, levelSq));
        }

        float level() const noexcept { return (float) std::sqrt (std::max (0.0, levelSq)); }
        void reset() noexcept { levelSq = 0.0; }

    private:
        double fs = 48000.0;
        double levelSq = 0.0;
    };

    // Slow gain that pulls a stereo signal towards a target loudness. Audio thread only.
    class LoudnessNormaliser
    {
    public:
        void prepare (double sampleRate) noexcept
        {
            fs = sampleRate > 0.0 ? sampleRate : 48000.0;
            kWeighting.prepare (fs);   // the same BS.1770 filters as the LUFS meter
            reset();
        }

        void reset() noexcept
        {
            gain = 1.0f;
            targetGain = 1.0f;
            meanSquare = 0.0;
            kWeighting.reset();
        }

        // Measures this block (left/right, before the gain is applied) and returns the gain
        // for the end of the block; the caller ramps from currentGain() (read before this
        // call) to it. active = false glides the gain back to unity but keeps measuring, so
        // switching it back on starts from a warm reading.
        // targetLufs: e.g. -14 for streaming, -23 for broadcast (EBU R128).
        float process (const float* left, const float* right, int numSamples, float targetLufs, bool active) noexcept
        {
            if (numSamples <= 0)
                return gain;

            double sum = 0.0;
            for (int i = 0; i < numSamples; ++i)
            {
                const double l = kWeighting.process (left[i], 0);
                const double r = kWeighting.process (right[i], 1);
                sum += l * l + r * r;   // channels are summed, as in BS.1770
            }

            // Only blocks with real signal (above -60 LUFS) count, like BS.1770's absolute
            // gate: pauses and silence neither lower the reading nor push the gain up.
            const double blockSeconds = numSamples / fs;
            const double blockMeanSquare = sum / numSamples;
            if (toLufs (blockMeanSquare) > -60.0)
                meanSquare += (1.0 - std::exp (-blockSeconds / 3.0)) * (blockMeanSquare - meanSquare);   // ~short-term window

            if (! active)
                targetGain = 1.0f;
            else if (const double lufs = measuredLufs(); lufs > -60.0)
                targetGain = (float) std::pow (10.0, std::clamp (targetLufs - lufs, -12.0, 12.0) / 20.0);
            // else: no signal measured yet, hold the last target

            gain = (float) (gain + (1.0 - std::exp (-blockSeconds / 0.5)) * (targetGain - gain));
            return gain;
        }

        float currentGain() const noexcept { return gain; }
        double measuredLufs() const noexcept { return toLufs (meanSquare); }

    private:
        static double toLufs (double ms) noexcept { return ms > 0.0 ? -0.691 + 10.0 * std::log10 (ms) : -100.0; }

        double fs = 48000.0;
        KWeighting kWeighting;
        double meanSquare = 0.0;
        float gain = 1.0f, targetGain = 1.0f;
    };
}
