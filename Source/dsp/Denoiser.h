#pragma once

// AI noise removal (RNNoise, BSD) for one strip. RNNoise works on 10 ms frames of 480 samples at
// 48 kHz, so this adds exactly `latency` samples of delay (reported to latency compensation).
// The "amount" blend uses a dry signal delayed by the same frame, so 50% doesn't comb-filter.
// At other sample rates it passes audio through untouched and reports isAvailable() == false.
// Allocation happens in prepare() only.

#include <atomic>
#include <memory>

struct DenoiseState;

namespace omx
{
    class Denoiser
    {
    public:
        static constexpr int frameSize = 480;
        static constexpr int latency = frameSize;

        Denoiser();
        ~Denoiser();

        void prepare (double sampleRate);
        void reset() noexcept;
        bool isAvailable() const noexcept { return available; }

        // In place. mono = the strip's source is mono (L == R): runs one network instead of two.
        // Returns the voice probability of the last finished frame (0..1).
        float process (float* left, float* right, int numSamples, float amount, bool mono) noexcept;

    private:
        struct Channel
        {
            DenoiseState* state = nullptr;
            float in[frameSize] {}, dry[frameSize] {}, out[frameSize] {};
        };

        Channel ch[2];
        int pos = 0;
        bool available = false;
        float lastVoice = 0.0f;
    };
}
