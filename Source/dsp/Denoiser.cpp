#include "dsp/Denoiser.h"

#include <algorithm>
#include <cmath>
#include <cstring>

extern "C"
{
   #include <rnnoise.h>
}

namespace omx
{
    static constexpr uint64_t ringMask = 8192 - 1;
    static constexpr double outStartOffset = 24.0;   // read the output this far behind the first frame

    Denoiser::Denoiser() = default;

    Denoiser::~Denoiser()
    {
        for (auto* s : states)
            if (s != nullptr)
                rnnoise_destroy (s);
    }

    void Denoiser::prepare (double sampleRate)
    {
        for (auto& s : states)
            if (s == nullptr)
                s = rnnoise_create (nullptr);   // built-in model

        rate = sampleRate;
        resampling = std::abs (sampleRate - 48000.0) >= 1.0;

        if (resampling)
        {
            upStep = sampleRate / 48000.0;     // device samples per 48 kHz sample
            downStep = 48000.0 / sampleRate;   // 48 kHz samples per device sample
            upSinc.design (0.45 * std::min (1.0, 48000.0 / sampleRate));
            downSinc.design (0.45 * std::min (1.0, sampleRate / 48000.0));
            for (int c = 0; c < 2; ++c)
            {
                inRing[c].assign (ringSize, 0.0f);
                outRing[c].assign (ringSize, 0.0f);
            }

            // In the 48 kHz domain the delay is the same 960 as the plain path (frame fill plus
            // overlap-add), plus outStartOffset for the output reader; the two sinc stages add about
            // 9 device samples of look-ahead. In device samples:
            latency = (int) std::lround ((outStartOffset + latency48k) * upStep) + 9;
        }
        else
        {
            latency = latency48k;
        }

        reset();
    }

    void Denoiser::reset() noexcept
    {
        std::memset (frameIn, 0, sizeof (frameIn));
        std::memset (wet, 0, sizeof (wet));
        std::memset (dry1, 0, sizeof (dry1));
        std::memset (dry2, 0, sizeof (dry2));
        std::memset (blend, 0, sizeof (blend));
        pos = 0;
        lastVoice = 0.0f;

        for (int c = 0; c < 2; ++c)
        {
            std::fill (inRing[c].begin(), inRing[c].end(), 0.0f);
            std::fill (outRing[c].begin(), outRing[c].end(), 0.0f);
        }
        inWritten = outWritten = 0;
        inReadPos = 0.0;
        outReadPos = -outStartOffset;
        outStarted = false;
    }

    void Denoiser::runFrame (int numCh, float amount) noexcept
    {
        for (int c = 0; c < numCh; ++c)
        {
            // wet now holds frame t-1 (RNNoise's overlap-add); dry2 is frame t-1 too.
            std::memcpy (dry2[c], dry1[c], sizeof (dry2[c]));
            for (int k = 0; k < frameSize; ++k)
                dry1[c][k] = frameIn[c][k] / 32768.0f;

            lastVoice = rnnoise_process_frame (states[c], wet[c], frameIn[c]);

            for (int k = 0; k < frameSize; ++k)
                blend[c][k] = amount * wet[c][k] / 32768.0f + (1.0f - amount) * dry2[c][k];
        }
    }

    float Denoiser::process (float* left, float* right, int numSamples, float amount, bool mono) noexcept
    {
        if (! isAvailable())
            return 0.0f;

        amount = std::clamp (amount, 0.0f, 1.0f);
        float* data[2] { left, right };
        const int numCh = mono ? 1 : 2;

        if (! resampling)
        {
            // 48 kHz: exact, no resampling. Output comes from the frame finished last time.
            for (int i = 0; i < numSamples; ++i)
            {
                for (int c = 0; c < numCh; ++c)
                {
                    const float x = data[c][i];
                    data[c][i] = blend[c][pos];
                    frameIn[c][pos] = x * 32768.0f;   // RNNoise expects 16-bit sample scale
                }
                if (mono)
                    data[1][i] = data[0][i];

                if (++pos == frameSize)
                {
                    pos = 0;
                    runFrame (numCh, amount);
                }
            }
            return lastVoice;
        }

        // Other rates: device -> 48 kHz -> RNNoise -> 48 kHz blend -> device.
        for (int i = 0; i < numSamples; ++i)
        {
            for (int c = 0; c < numCh; ++c)
                inRing[c][(uint64_t) inWritten & ringMask] = data[c][i];
            ++inWritten;

            // Produce 48 kHz samples while the sinc has enough look-ahead.
            while (inReadPos + 9.0 < (double) inWritten)
            {
                const auto idx = (int64_t) std::floor (inReadPos);
                const float t = (float) (inReadPos - (double) idx);
                for (int c = 0; c < numCh; ++c)
                    frameIn[c][pos] = upSinc.at (inRing[c].data(), ringMask, idx, t) * 32768.0f;
                inReadPos += upStep;

                if (++pos == frameSize)
                {
                    pos = 0;
                    runFrame (numCh, amount);
                    for (int c = 0; c < numCh; ++c)
                        for (int k = 0; k < frameSize; ++k)
                            outRing[c][(uint64_t) (outWritten + k) & ringMask] = blend[c][k];
                    outWritten += frameSize;
                    outStarted = true;
                }
            }

            // Back to the device rate.
            for (int c = 0; c < numCh; ++c)
            {
                float y = 0.0f;
                if (outStarted && outReadPos >= 0.0 && outReadPos + 9.0 < (double) outWritten)
                {
                    const auto idx = (int64_t) std::floor (outReadPos);
                    y = downSinc.at (outRing[c].data(), ringMask, idx, (float) (outReadPos - (double) idx));
                }
                data[c][i] = y;
            }
            if (outStarted)
                outReadPos += downStep;
            if (mono)
                data[1][i] = data[0][i];
        }
        return lastVoice;
    }
}
