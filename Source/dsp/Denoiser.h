#pragma once

// AI noise removal (RNNoise, BSD) for one strip.
//
// RNNoise works on 10 ms frames of 480 samples at 48 kHz and overlap-adds with the previous
// frame, so its output lags its input by two frames (960 samples, 20 ms). The "amount" blend
// uses a dry signal delayed by exactly the same, so partial amounts don't comb-filter.
//
// At other sample rates the audio is resampled to 48 kHz and back around RNNoise (windowed
// sinc), and the blend happens in the 48 kHz domain so wet and dry stay aligned. getLatency()
// reports the total delay for latency compensation. Allocation happens in prepare() only.

#include <atomic>
#include <memory>
#include <vector>

#include "dsp/SincTable.h"

struct DenoiseState;

namespace omx
{
    class Denoiser
    {
    public:
        static constexpr int frameSize = 480;
        static constexpr int latency48k = 2 * frameSize;   // RNNoise frame + its overlap-add frame

        Denoiser();
        ~Denoiser();

        void prepare (double sampleRate);
        void reset() noexcept;
        bool isAvailable() const noexcept { return states[0] != nullptr && states[1] != nullptr; }
        bool isResampling() const noexcept { return resampling; }

        // Delay this adds at the current sample rate, in samples (exact at 48 kHz, within one
        // sample otherwise).
        int getLatency() const noexcept { return latency; }

        // In place. mono = the strip's source is mono (L == R): runs one network instead of two.
        // Returns the voice probability of the last finished frame (0..1).
        float process (float* left, float* right, int numSamples, float amount, bool mono) noexcept;

    private:
        void runFrame (int numCh, float amount) noexcept;   // frame48 -> blend48 (both channels)

        DenoiseState* states[2] {};

        // 48 kHz frame state, per channel
        float frameIn[2][frameSize] {};   // scaled to 16-bit range, as RNNoise expects
        float wet[2][frameSize] {};
        float dry1[2][frameSize] {}, dry2[2][frameSize] {};   // dry delayed by one and two frames
        float blend[2][frameSize] {};     // output frame (48 kHz)
        int pos = 0;
        float lastVoice = 0.0f;

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
