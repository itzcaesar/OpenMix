#pragma once

// AI noise removal for one strip: RNNoise runs directly, while DeepFilterNet uses a
// bounded frame queue and worker so model loading and inference stay off the audio callback.
//
// RNNoise works on 10 ms frames of 480 samples at 48 kHz and overlap-adds with the previous
// frame, so its output lags its input by two frames (960 samples, 20 ms). The "amount" blend
// uses a dry signal delayed by exactly the same, so partial amounts don't comb-filter.
//
// At other sample rates the audio is resampled to 48 kHz and back (windowed sinc), and the
// blend happens in the 48 kHz domain so wet and dry stay aligned. getLatency() reports the
// selected model's total delay for latency compensation. Model storage stays off the callback.

#include <atomic>
#include <array>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "dsp/SincTable.h"

struct DenoiseState;

namespace omx
{
    class Denoiser
    {
    public:
        enum class Model : int { RNNoise = 0, DeepFilterNet = 1 };

        static constexpr int frameSize = 480;
        static constexpr int latency48k = 2 * frameSize;   // RNNoise frame + its overlap-add frame
        static constexpr int deepFilterWorkerLeadFrames = 8;

        Denoiser();
        ~Denoiser();

        void prepare (double sampleRate);
        void reset() noexcept;
        bool isAvailable() const noexcept { return states[0] != nullptr && states[1] != nullptr; }
        bool isAvailable (Model model) const noexcept;
        bool isDeepFilterNetReady() const noexcept { return deepFilterReady.load (std::memory_order_acquire); }
        bool isDeepFilterNetLoading() const noexcept;
        static void refreshDeepFilterNetAvailability() noexcept;
        bool isResampling() const noexcept { return resampling; }

        // Delay this adds at the current sample rate, in samples (exact at 48 kHz, within one
        // sample otherwise).
        int getLatency() const noexcept { return getLatency (Model::RNNoise); }
        int getLatency (Model model) const noexcept;

        // In place. mono = the strip's source is mono (L == R): runs one network instead of two.
        // Returns the voice probability of the last finished frame (0..1).
        float process (float* left, float* right, int numSamples, float amount, bool mono) noexcept;
        float process (float* left, float* right, int numSamples, float amount, bool mono, Model model) noexcept;

    private:
        struct DeepFrame
        {
            uint64_t sequence = 0, generation = 0;
            int channels = 1;
            float voice = 0.0f;
            float samples[2][frameSize] {};
        };

        static constexpr uint64_t deepFrameQueueSize = 32;
        static constexpr uint64_t deepDryHistorySize = 32;

        void runFrame (int numCh, float amount, Model model) noexcept;   // frame48 -> blend48
        void enqueueDeepFilterFrame (int numCh) noexcept;
        void loadDeepFilterOutputFrame (float amount) noexcept;
        void deepFilterWorkerLoop() noexcept;

        DenoiseState* states[2] {};

        // 48 kHz frame state, per channel
        float frameIn[2][frameSize] {};   // scaled to 16-bit range, as RNNoise expects
        float wet[2][frameSize] {};
        float dry1[2][frameSize] {}, dry2[2][frameSize] {};   // dry delayed by one and two frames
        float blend[2][frameSize] {};     // output frame (48 kHz)
        int pos = 0;
        float lastVoice = 0.0f;

        // DeepFilterNet C-API inference runs away from the audio callback. Bounded SPSC
        // frame queues keep the callback allocation-free and let the worker absorb model jitter.
        std::array<DeepFrame, deepFrameQueueSize> deepInputQueue {};
        std::array<DeepFrame, deepFrameQueueSize> deepOutputQueue {};
        std::array<DeepFrame, deepDryHistorySize> deepDryHistory {};
        std::atomic<uint64_t> deepInputWrite { 0 }, deepInputRead { 0 };
        std::atomic<uint64_t> deepOutputWrite { 0 }, deepOutputRead { 0 };
        std::atomic<uint64_t> deepGeneration { 0 };
        std::atomic<bool> deepFilterReady { false }, deepFilterFailed { false };
        std::atomic<bool> deepFilterStop { false };
        std::atomic<float> deepFilterVoice { 0.0f };
        std::atomic<int> deepFilterModelLatency48k { frameSize };
        uint64_t deepAudioSequence = 0, deepAudioGeneration = 0;
        std::thread deepFilterThread;
        std::mutex deepFilterWaitMutex;
        std::condition_variable deepFilterWake;

        // Resampling path (sample rates other than 48 kHz)
        bool resampling = false;
        double rate = 48000.0, upStep = 1.0, downStep = 1.0;
        SincTable upSinc, downSinc;
        static constexpr int ringSize = 8192;
        std::vector<float> inRing[2], outRing[2];
        int64_t inWritten = 0, outWritten = 0;
        double inReadPos = 0.0, outReadPos = 0.0;
        bool outStarted = false;
        int latency = latency48k;
    };
}
