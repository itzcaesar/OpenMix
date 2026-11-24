#pragma once

// RMS metering and loudness normalisation. Plain C++, no JUCE: covered by CoreTests.
//
// RMS: a 300 ms one-pole smoothing of the block mean-square, so the meter
// reads the sustained level, not the loudest sample. Peak meters show
// transients; RMS shows how loud something actually sounds, which is what
// a streamer checks when a mic and music need to sit together.
//
// Loudness normalisation: a slow gain that pulls a bus towards a target
// short-term loudness (streaming platforms normalise to about -14 LUFS).
// Measured loudness is approximated from the RMS as 20*log10(rms) - 0.691
// (the BS.1770 -0.691 offset, without K-weighting: close enough for a gain
// target, and deliberately not a precision loudness meter). The gain is
// clamped to +/-12 dB and smoothed over ~0.5 s, so it never pumps on a
// drum hit and never chases silence: below -60 dBFS it holds its last gain.

#include <algorithm>
#include <cmath>

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

    // Slow gain that pulls a signal towards a target loudness. Audio thread only.
    class LoudnessNormaliser
    {
    public:
        void prepare (double sampleRate) noexcept
        {
            fs = sampleRate > 0.0 ? sampleRate : 48000.0;
            gain = 1.0f;
        }

        void reset() noexcept { gain = 1.0f; }

        // rms: smoothed RMS level (linear) of the bus, as measured this block.
        // targetLufs: e.g. -14 for streaming, -23 for broadcast (EBU R128).
        // Returns the gain to apply this block (linear, clamped to +/-12 dB).
        float process (float rms, float targetLufs, int numSamples) noexcept
        {
            // Below -60 dBFS there is no usable signal: hold the last gain
            // instead of turning the noise floor up.
            if (rms > 1e-3f)
            {
                const double measuredLufs = 20.0 * std::log10 ((double) rms) - 0.691;
                const double wantedDb = std::clamp (targetLufs - measuredLufs, -12.0, 12.0);
                targetGain = (float) std::pow (10.0, wantedDb / 20.0);
            }

            // ~0.5 s smoothing: fast enough to follow a scene change, slow
            // enough not to pump on individual words or drum hits.
            const double blockSeconds = numSamples / fs;
            const double alpha = 1.0 - std::exp (-blockSeconds / 0.5);
            gain = (float) (gain + alpha * (targetGain - gain));
            return gain;
        }

        float currentGain() const noexcept { return gain; }

    private:
        double fs = 48000.0;
        float gain = 1.0f, targetGain = 1.0f;
    };
}
