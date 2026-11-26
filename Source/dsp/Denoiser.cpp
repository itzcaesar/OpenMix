#include "dsp/Denoiser.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <limits>
#include <mutex>
#include <string>

#ifndef OPENMIX_DEEPFILTERNET_ENABLED
 #define OPENMIX_DEEPFILTERNET_ENABLED 0
#endif

#if defined(_WIN32)
 #define NOMINMAX
 #include <windows.h>
#endif

extern "C"
{
   #include <rnnoise.h>
}

namespace omx
{
    static constexpr uint64_t ringMask = 8192 - 1;
    static constexpr double outStartOffset = 24.0;   // read the output this far behind the first frame

    namespace
    {
        constexpr wchar_t deepFilterModelName[] = L"DeepFilterNet3_onnx.tar.gz";
        constexpr wchar_t deepFilterLibraryName[] = L"df.dll";
        std::atomic<bool> deepFilterFilesAvailable { false };

        std::filesystem::path getDeepFilterModelPath()
        {
           #if defined(_WIN32) && OPENMIX_DEEPFILTERNET_ENABLED
            wchar_t appData[MAX_PATH] {};
            const DWORD length = GetEnvironmentVariableW (L"APPDATA", appData, MAX_PATH);
            if (length > 0 && length < MAX_PATH)
                return std::filesystem::path (appData) / L"OpenMix" / L"Models" / deepFilterModelName;
           #endif
            return {};
        }

        std::filesystem::path getDeepFilterLibraryPath()
        {
           #if defined(_WIN32) && OPENMIX_DEEPFILTERNET_ENABLED
            wchar_t executable[MAX_PATH] {};
            const DWORD length = GetModuleFileNameW (nullptr, executable, MAX_PATH);
            if (length > 0 && length < MAX_PATH)
                return std::filesystem::path (executable).parent_path() / deepFilterLibraryName;
           #endif
            return {};
        }

        std::string toUtf8 (const std::wstring& text)
        {
           #if defined(_WIN32) && OPENMIX_DEEPFILTERNET_ENABLED
            if (text.empty()) return {};
            const int length = WideCharToMultiByte (CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                                                     (int) text.size(), nullptr, 0, nullptr, nullptr);
            if (length <= 0) return {};
            std::string utf8 ((size_t) length, '\0');
            if (WideCharToMultiByte (CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), (int) text.size(),
                                     utf8.data(), length, nullptr, nullptr) != length)
                return {};
            return utf8;
           #else
            (void) text;
            return {};
           #endif
        }

       #if defined(_WIN32) && OPENMIX_DEEPFILTERNET_ENABLED
        struct DeepFilterApi
        {
            using Create = void* (__cdecl*) (const char*, uint32_t, float);
            using Process = float (__cdecl*) (void*, float*, float*);
            using FrameLength = size_t (__cdecl*) (void*);
            using Latency = size_t (__cdecl*) (void*);
            using Reset = bool (__cdecl*) (void*);
            using Free = void (__cdecl*) (void*);

            HMODULE library = nullptr;
            Create create = nullptr;
            Process process = nullptr;
            FrameLength frameLength = nullptr;
            Latency latency = nullptr;
            Reset reset = nullptr;
            Free free = nullptr;

            bool load (const std::filesystem::path& path)
            {
                library = LoadLibraryW (path.c_str());
                if (library == nullptr)
                    return false;

                create = reinterpret_cast<Create> (GetProcAddress (library, "df_create_channels"));
                process = reinterpret_cast<Process> (GetProcAddress (library, "df_process_frame"));
                frameLength = reinterpret_cast<FrameLength> (GetProcAddress (library, "df_get_frame_length"));
                latency = reinterpret_cast<Latency> (GetProcAddress (library, "df_get_latency_samples"));
                reset = reinterpret_cast<Reset> (GetProcAddress (library, "df_reset"));
                free = reinterpret_cast<Free> (GetProcAddress (library, "df_free"));
                if (create != nullptr && process != nullptr && frameLength != nullptr
                    && latency != nullptr && reset != nullptr && free != nullptr)
                    return true;

                FreeLibrary (library);
                library = nullptr;
                return false;
            }

            ~DeepFilterApi()
            {
                if (library != nullptr)
                    FreeLibrary (library);
            }
        };
       #endif
    }

    Denoiser::Denoiser()
    {
        refreshDeepFilterNetAvailability();
        deepFilterThread = std::thread ([this] { deepFilterWorkerLoop(); });
    }

    Denoiser::~Denoiser()
    {
        deepFilterStop.store (true, std::memory_order_release);
        deepFilterWake.notify_one();
        if (deepFilterThread.joinable())
            deepFilterThread.join();

        for (auto* s : states)
            if (s != nullptr)
                rnnoise_destroy (s);
    }

    bool Denoiser::isAvailable (Model model) const noexcept
    {
        if (model == Model::RNNoise)
            return isAvailable();
        return deepFilterFilesAvailable.load (std::memory_order_acquire)
            && ! deepFilterFailed.load (std::memory_order_acquire);
    }

    bool Denoiser::isDeepFilterNetLoading() const noexcept
    {
        return isAvailable (Model::DeepFilterNet) && ! isDeepFilterNetReady();
    }

    void Denoiser::refreshDeepFilterNetAvailability() noexcept
    {
       #if defined(_WIN32) && OPENMIX_DEEPFILTERNET_ENABLED
        std::error_code ec;
        const bool modelExists = std::filesystem::is_regular_file (getDeepFilterModelPath(), ec);
        ec.clear();
        const bool libraryExists = std::filesystem::is_regular_file (getDeepFilterLibraryPath(), ec);
        deepFilterFilesAvailable.store (modelExists && libraryExists, std::memory_order_release);
       #else
        deepFilterFilesAvailable.store (false, std::memory_order_release);
       #endif
    }

    int Denoiser::getLatency (Model model) const noexcept
    {
        if (model == Model::RNNoise)
            return latency;

        const int algorithmLatency = deepFilterModelLatency48k.load (std::memory_order_relaxed);
        const int deepLatency48k = deepFilterWorkerLeadFrames * frameSize + algorithmLatency;
        return resampling ? (int) std::lround ((outStartOffset + deepLatency48k) * upStep) + 9
                          : deepLatency48k;
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
        deepAudioSequence = 0;
        deepAudioGeneration = deepGeneration.fetch_add (1, std::memory_order_acq_rel) + 1;
        deepFilterVoice.store (0.0f, std::memory_order_relaxed);

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

    void Denoiser::runFrame (int numCh, float amount, Model model) noexcept
    {
        if (model == Model::DeepFilterNet)
        {
            enqueueDeepFilterFrame (numCh);
            loadDeepFilterOutputFrame (amount);
            return;
        }

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

    void Denoiser::enqueueDeepFilterFrame (int numCh) noexcept
    {
        const uint64_t sequence = deepAudioSequence++;
        DeepFrame frame;
        frame.sequence = sequence;
        frame.generation = deepAudioGeneration;
        frame.channels = numCh;

        for (int k = 0; k < frameSize; ++k)
        {
            frame.samples[0][k] = frameIn[0][k] / 32768.0f;
            frame.samples[1][k] = numCh == 1 ? frame.samples[0][k] : frameIn[1][k] / 32768.0f;
        }
        deepDryHistory[sequence % deepDryHistorySize] = frame;

        const auto write = deepInputWrite.load (std::memory_order_relaxed);
        const auto read = deepInputRead.load (std::memory_order_acquire);
        if (write - read < deepFrameQueueSize)
        {
            deepInputQueue[write % deepFrameQueueSize] = frame;
            deepInputWrite.store (write + 1, std::memory_order_release);
            deepFilterWake.notify_one();
        }
    }

    void Denoiser::loadDeepFilterOutputFrame (float amount) noexcept
    {
        const uint64_t sequence = deepAudioSequence;
        if (sequence < deepFilterWorkerLeadFrames)
        {
            std::memset (blend, 0, sizeof (blend));
            return;
        }

        const uint64_t target = sequence - deepFilterWorkerLeadFrames;
        const uint64_t generation = deepAudioGeneration;
        const auto latencySamples = deepFilterModelLatency48k.load (std::memory_order_relaxed);
        const uint64_t dryDelayFrames = (uint64_t) ((latencySamples + frameSize - 1) / frameSize);
        const DeepFrame* dry = nullptr;
        if (target >= dryDelayFrames)
        {
            const auto drySequence = target - dryDelayFrames;
            const auto& candidate = deepDryHistory[drySequence % deepDryHistorySize];
            if (candidate.sequence == drySequence && candidate.generation == generation)
                dry = &candidate;
        }

        DeepFrame wetFrame;
        bool hasWet = false;
        auto read = deepOutputRead.load (std::memory_order_relaxed);
        const auto write = deepOutputWrite.load (std::memory_order_acquire);
        while (read < write)
        {
            const auto& candidate = deepOutputQueue[read % deepFrameQueueSize];
            if (candidate.generation == generation && candidate.sequence >= target)
            {
                if (candidate.sequence == target)
                {
                    wetFrame = candidate;
                    hasWet = true;
                }
                break;
            }
            ++read;
        }
        if (hasWet)
            ++read;
        deepOutputRead.store (read, std::memory_order_release);

        if (dry == nullptr)
        {
            std::memset (blend, 0, sizeof (blend));
            return;
        }

        for (int c = 0; c < 2; ++c)
        {
            const int sourceChannel = c < dry->channels ? c : 0;
            const int wetChannel = hasWet && c < wetFrame.channels ? c : 0;
            for (int k = 0; k < frameSize; ++k)
            {
                const float drySample = dry->samples[sourceChannel][k];
                const float wetSample = hasWet ? wetFrame.samples[wetChannel][k] : drySample;
                blend[c][k] = amount * wetSample + (1.0f - amount) * drySample;
            }
        }

        if (hasWet)
        {
            deepFilterVoice.store (wetFrame.voice, std::memory_order_relaxed);
            lastVoice = wetFrame.voice;
        }
        else
        {
            lastVoice = deepFilterVoice.load (std::memory_order_relaxed);
        }
    }

    void Denoiser::deepFilterWorkerLoop() noexcept
    {
       #if defined(_WIN32) && OPENMIX_DEEPFILTERNET_ENABLED
        DeepFilterApi api;
        void* state = nullptr;
        int modelChannels = 0;
        uint64_t modelGeneration = std::numeric_limits<uint64_t>::max();
        uint64_t expectedSequence = 0;

        auto destroyState = [&]
        {
            if (state != nullptr && api.free != nullptr)
                api.free (state);
            state = nullptr;
            modelChannels = 0;
            deepFilterReady.store (false, std::memory_order_release);
        };

        while (! deepFilterStop.load (std::memory_order_acquire))
        {
            const auto read = deepInputRead.load (std::memory_order_relaxed);
            const auto write = deepInputWrite.load (std::memory_order_acquire);
            if (read >= write)
            {
                std::unique_lock<std::mutex> lock (deepFilterWaitMutex);
                deepFilterWake.wait_for (lock, std::chrono::milliseconds (50), [this]
                {
                    return deepFilterStop.load (std::memory_order_acquire)
                        || deepInputRead.load (std::memory_order_relaxed)
                             < deepInputWrite.load (std::memory_order_acquire);
                });
                continue;
            }

            DeepFrame frame = deepInputQueue[read % deepFrameQueueSize];
            deepInputRead.store (read + 1, std::memory_order_release);

            if (api.library == nullptr)
            {
                if (! api.load (getDeepFilterLibraryPath()))
                {
                    deepFilterFailed.store (true, std::memory_order_release);
                    break;
                }
            }

            if (frame.generation != modelGeneration || frame.channels != modelChannels)
            {
                if (state != nullptr && frame.channels != modelChannels)
                    destroyState();

                if (state == nullptr)
                {
                    const auto modelPath = toUtf8 (getDeepFilterModelPath().wstring());
                    if (modelPath.empty())
                    {
                        deepFilterFailed.store (true, std::memory_order_release);
                        break;
                    }
                    state = api.create (modelPath.c_str(), (uint32_t) frame.channels, 100.0f);
                    if (state == nullptr || api.frameLength (state) != frameSize)
                    {
                        destroyState();
                        deepFilterFailed.store (true, std::memory_order_release);
                        break;
                    }
                    modelChannels = frame.channels;
                    deepFilterModelLatency48k.store ((int) api.latency (state), std::memory_order_release);
                    deepFilterReady.store (true, std::memory_order_release);
                }
                else if (! api.reset (state))
                {
                    deepFilterFailed.store (true, std::memory_order_release);
                    break;
                }

                modelGeneration = frame.generation;
                expectedSequence = frame.sequence;
            }

            if (frame.sequence != expectedSequence && ! api.reset (state))
            {
                deepFilterFailed.store (true, std::memory_order_release);
                break;
            }

            DeepFrame output = frame;
            const float localSnr = api.process (state, frame.samples[0], output.samples[0]);
            bool outputIsFinite = std::isfinite (localSnr);
            for (int c = 0; c < frame.channels && outputIsFinite; ++c)
                for (int k = 0; k < frameSize; ++k)
                    if (! std::isfinite (output.samples[c][k]))
                    {
                        outputIsFinite = false;
                        break;
                    }
            if (! outputIsFinite)
            {
                deepFilterFailed.store (true, std::memory_order_release);
                break;
            }

            // DeepFilterNet reports local SNR (dB), not RNNoise voice probability. Map it
            // smoothly to the shared activity display without changing the audio processing.
            output.voice = 1.0f / (1.0f + std::exp (-0.2f * (localSnr - 2.0f)));
            const auto outputWrite = deepOutputWrite.load (std::memory_order_relaxed);
            const auto outputRead = deepOutputRead.load (std::memory_order_acquire);
            if (outputWrite - outputRead < deepFrameQueueSize)
            {
                deepOutputQueue[outputWrite % deepFrameQueueSize] = output;
                deepOutputWrite.store (outputWrite + 1, std::memory_order_release);
            }

            expectedSequence = frame.sequence + 1;
        }

        destroyState();
       #else
        deepFilterFailed.store (true, std::memory_order_release);
       #endif
    }

    float Denoiser::process (float* left, float* right, int numSamples, float amount, bool mono) noexcept
    {
        return process (left, right, numSamples, amount, mono, Model::RNNoise);
    }

    float Denoiser::process (float* left, float* right, int numSamples, float amount, bool mono, Model model) noexcept
    {
        if (! isAvailable (model))
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
                    runFrame (numCh, amount, model);
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
                    runFrame (numCh, amount, model);
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
