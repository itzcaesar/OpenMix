#include "Denoiser.h"

#include <algorithm>
#include <cmath>
#include <cstring>

extern "C"
{
   #include <rnnoise.h>
}

namespace omx
{
    Denoiser::Denoiser() = default;

    Denoiser::~Denoiser()
    {
        for (auto& c : ch)
            if (c.state != nullptr)
                rnnoise_destroy (c.state);
    }

    void Denoiser::prepare (double sampleRate)
    {
        available = std::abs (sampleRate - 48000.0) < 1.0;

        for (auto& c : ch)
            if (c.state == nullptr)
                c.state = rnnoise_create (nullptr);   // built-in model

        reset();
    }

    void Denoiser::reset() noexcept
    {
        for (auto& c : ch)
        {
            std::memset (c.in, 0, sizeof (c.in));
            std::memset (c.dry, 0, sizeof (c.dry));
            std::memset (c.out, 0, sizeof (c.out));
        }
        pos = 0;
        lastVoice = 0.0f;
    }

    float Denoiser::process (float* left, float* right, int numSamples, float amount, bool mono) noexcept
    {
        if (! available || ch[0].state == nullptr || ch[1].state == nullptr)
            return 0.0f;

        amount = std::clamp (amount, 0.0f, 1.0f);
        float* data[2] { left, right };
        const int numCh = mono ? 1 : 2;

        for (int i = 0; i < numSamples; ++i)
        {
            for (int c = 0; c < numCh; ++c)
            {
                auto& s = ch[c];
                const float x = data[c][i];

                // Output comes from the frame finished one frame ago (wet + matching dry).
                data[c][i] = amount * s.out[pos] / 32768.0f + (1.0f - amount) * s.dry[pos];
                s.in[pos] = x * 32768.0f;   // RNNoise expects 16-bit sample scale
            }

            if (mono)
                data[1][i] = data[0][i];

            if (++pos == frameSize)
            {
                pos = 0;
                for (int c = 0; c < numCh; ++c)
                {
                    auto& s = ch[c];
                    for (int k = 0; k < frameSize; ++k)
                        s.dry[k] = s.in[k] / 32768.0f;
                    lastVoice = rnnoise_process_frame (s.state, s.out, s.in);
                }
            }
        }

        return lastVoice;
    }
}
