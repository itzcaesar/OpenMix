#include "engine/PluginSandbox.h"
#include "dsp/AudioSafety.h"
#include "dsp/DelayLine.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bit>
#include <cstring>

namespace
{
constexpr int kNumSlots = kPluginSandboxSlotCount;
constexpr int kSandboxRingSize = 3;
constexpr int kSandboxMaxBlock = 8192;
constexpr int kSandboxMaxParameters = 1024;
constexpr int kSandboxMaxChannels = 16;
constexpr LONG kFree = 0, kWriting = 1, kReady = 2, kProcessing = 3, kDone = 4, kReading = 5;

LONG floatBits (float value) noexcept { return std::bit_cast<LONG> (value); }
float bitsFloat (LONG value) noexcept { return std::bit_cast<float> (value); }

LONG atomicLoad (volatile LONG* value) noexcept { return InterlockedCompareExchange (value, 0, 0); }
LONG64 atomicLoad64 (volatile LONG64* value) noexcept { return InterlockedCompareExchange64 (value, 0, 0); }

struct SharedBlock
{
    volatile LONG state = kFree;
    volatile LONG sampleCount = 0;
    volatile LONG64 sequence = 0;
    float inputLeft[kSandboxMaxBlock] {};
    float inputRight[kSandboxMaxBlock] {};
    float outputLeft[kSandboxMaxBlock] {};
    float outputRight[kSandboxMaxBlock] {};
};
}

struct PluginSandbox::SharedState
{
    volatile LONG ready = 0;
    volatile LONG activeSlot = -1;
    volatile LONG faultedMask = 0;
    volatile LONG64 activeSinceMs = 0;
    volatile LONG64 nextSequence = 0;
    volatile LONG bypass[kNumSlots] {};
    volatile LONG mix[kNumSlots] {};
    volatile LONG parameterCount[kNumSlots] {};
    volatile LONG parameters[kNumSlots][kSandboxMaxParameters] {};
    volatile LONG latency[kNumSlots] {};
    SharedBlock blocks[kSandboxRingSize];
};

PluginSandbox::PluginSandbox (std::function<void (int)> fault)
    : juce::Thread ("OpenMix plugin sandbox supervisor"), onPluginFault (std::move (fault))
{
    const auto id = juce::Uuid().toString().removeCharacters ("{}-");
    mappingName = "Local\\OpenMixPluginAudio-" + id;
    eventName = "Local\\OpenMixPluginWake-" + id;
    configFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                     .getChildFile ("OpenMix").getChildFile ("sandbox-" + id + ".xml");
    configFile.getParentDirectory().createDirectory();

    mappingHandle = CreateFileMappingW (INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                        (DWORD) sizeof (SharedState), mappingName.toWideCharPointer());
    if (mappingHandle != nullptr)
        shared = static_cast<SharedState*> (MapViewOfFile (mappingHandle, FILE_MAP_ALL_ACCESS, 0, 0, sizeof (SharedState)));

    eventHandle = CreateEventW (nullptr, FALSE, FALSE, eventName.toWideCharPointer());

    if (shared != nullptr)
    {
        std::memset (shared, 0, sizeof (SharedState));
        InterlockedExchange (&shared->activeSlot, -1);
        InterlockedExchange (&shared->faultedMask, 0);
        for (auto& m : shared->mix)
            InterlockedExchange (&m, floatBits (1.0f));
    }

    if (shared != nullptr && eventHandle != nullptr)
        startThread (juce::Thread::Priority::high);
}

PluginSandbox::~PluginSandbox()
{
    signalThreadShouldExit();
    if (eventHandle != nullptr)
        SetEvent ((HANDLE) eventHandle);
    stopThread (2000);
    if (child.isRunning())
        child.kill();

    if (shared != nullptr)
        UnmapViewOfFile (shared);
    if (mappingHandle != nullptr)
        CloseHandle ((HANDLE) mappingHandle);
    if (eventHandle != nullptr)
        CloseHandle ((HANDLE) eventHandle);
    configFile.deleteFile();
}

void PluginSandbox::configure (const juce::String& xml, int disabledSlotMask)
{
    if (shared == nullptr)
        return;

    if (xml.isEmpty())
    {
        configFile.deleteFile();
    }
    else if (! configFile.replaceWithText (xml))
    {
        juce::Logger::writeToLog ("Plugin sandbox: couldn't write worker configuration");
        return;
    }

    requestedDisabledMask.store (disabledSlotMask & ((1 << kNumSlots) - 1), std::memory_order_release);
    requestedVersion.fetch_add (1, std::memory_order_acq_rel);
}

void PluginSandbox::setSlotControls (int slot, bool bypass, float mix,
                                     const float* parameters, int parameterCount) noexcept
{
    if (shared == nullptr || ! juce::isPositiveAndBelow (slot, kNumSlots))
        return;

    InterlockedExchange (&shared->bypass[slot], bypass ? 1 : 0);
    InterlockedExchange (&shared->mix[slot], floatBits (juce::jlimit (0.0f, 1.0f, mix)));

    const int count = juce::jlimit (0, kSandboxMaxParameters, parameterCount);
    InterlockedExchange (&shared->parameterCount[slot], count);
    for (int p = 0; p < count; ++p)
        InterlockedExchange (&shared->parameters[slot][p], floatBits (parameters[p]));
}

bool PluginSandbox::processBlock (float* left, float* right, int numSamples) noexcept
{
    if (shared == nullptr || eventHandle == nullptr || numSamples <= 0 || numSamples > kSandboxMaxBlock)
        return false;

    const LONG64 sequence = InterlockedIncrement64 (&shared->nextSequence);

    // Submit the original input before reading any result into the caller's buffer. This
    // keeps a completed worker block from accidentally becoming the next block's input.
    auto& input = shared->blocks[(size_t) (sequence % kSandboxRingSize)];
    LONG state = atomicLoad (&input.state);
    if (state == kDone && InterlockedCompareExchange (&input.state, kFree, kDone) == kDone)
        state = kFree;

    if (state == kFree && InterlockedCompareExchange (&input.state, kWriting, kFree) == kFree)
    {
        juce::FloatVectorOperations::copy (input.inputLeft, left, numSamples);
        juce::FloatVectorOperations::copy (input.inputRight, right, numSamples);
        InterlockedExchange (&input.sampleCount, numSamples);
        InterlockedExchange64 (&input.sequence, sequence);
        InterlockedExchange (&input.state, kReady);
        SetEvent ((HANDLE) eventHandle);
    }

    bool haveOutput = false;
    if (sequence >= pipelineBlocks)
    {
        const LONG64 wanted = sequence - pipelineBlocks;
        auto& result = shared->blocks[(size_t) (wanted % kSandboxRingSize)];
        if (InterlockedCompareExchange (&result.state, kReading, kDone) == kDone)
        {
            if (atomicLoad64 (&result.sequence) == wanted && atomicLoad (&result.sampleCount) == numSamples)
            {
                juce::FloatVectorOperations::copy (left, result.outputLeft, numSamples);
                juce::FloatVectorOperations::copy (right, result.outputRight, numSamples);
                haveOutput = true;
            }
            InterlockedExchange (&result.state, kFree);
        }
    }

    return haveOutput;
}

void PluginSandbox::run()
{
    auto resetRing = [this]
    {
        if (shared == nullptr)
            return;
        InterlockedExchange (&shared->ready, 0);
        InterlockedExchange (&shared->activeSlot, -1);
        InterlockedExchange64 (&shared->activeSinceMs, 0);
        InterlockedExchange (&shared->faultedMask, 0);
        for (auto& block : shared->blocks)
            InterlockedExchange (&block.state, kFree);
    };

    auto launch = [this, &resetRing] (int version, int disabledMask)
    {
        resetRing();
        const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
        const bool started = child.start (juce::StringArray { exe.getFullPathName(), workerFlag,
                                                               mappingName, eventName,
                                                               configFile.getFullPathName(),
                                                               juce::String (disabledMask) }, 0);
        launchedVersion = version;
        launchedDisabledMask = disabledMask;
        unavailable = ! started;
        if (! started)
            juce::Logger::writeToLog ("Plugin sandbox: couldn't start worker process");
        return started;
    };

    juce::int64 launchTime = 0;
    while (! threadShouldExit())
    {
        const int version = requestedVersion.load (std::memory_order_acquire);
        const int requestedMask = requestedDisabledMask.load (std::memory_order_acquire);

        if (version > 0 && (version != launchedVersion || requestedMask != launchedDisabledMask))
        {
            if (child.isRunning())
                child.kill();
            unavailable = false;
            if (configFile.existsAsFile())
            {
                if (launch (version, requestedMask))
                    launchTime = (juce::int64) GetTickCount64();
            }
            else
            {
                resetRing();
                launchedVersion = version;
                launchedDisabledMask = requestedMask;
                unavailable = true;
            }
        }

        if (version > 0 && child.isRunning())
        {
            const int faultMask = InterlockedExchange (&shared->faultedMask, 0);
            for (int fault = 0; fault < kNumSlots; ++fault)
            {
                if ((faultMask & (1 << fault)) != 0)
                {
                    launchedDisabledMask |= 1 << fault;
                    requestedDisabledMask.store (launchedDisabledMask, std::memory_order_release);
                    if (onPluginFault)
                        onPluginFault (fault);
                }
            }

            const LONG activeSlot = atomicLoad (&shared->activeSlot);
            const juce::int64 now = (juce::int64) GetTickCount64();
            const juce::int64 activeSince = (juce::int64) atomicLoad64 (&shared->activeSinceMs);
            const bool startupTimedOut = atomicLoad (&shared->ready) == 0 && now - launchTime > 30000;
            const bool processingTimedOut = atomicLoad (&shared->ready) != 0
                                             && activeSlot >= 0 && activeSince > 0 && now - activeSince > 1500;

            if (startupTimedOut || processingTimedOut)
            {
                child.kill();
                if (juce::isPositiveAndBelow ((int) activeSlot, kNumSlots))
                {
                    launchedDisabledMask |= 1 << activeSlot;
                    requestedDisabledMask.store (launchedDisabledMask, std::memory_order_release);
                    if (onPluginFault)
                        onPluginFault ((int) activeSlot);
                    if (launch (version, launchedDisabledMask))
                        launchTime = now;
                }
                else
                {
                    unavailable = true;
                    juce::Logger::writeToLog ("Plugin sandbox: worker startup timed out");
                }
            }
        }
        else if (version > 0 && ! unavailable && launchedVersion == version)
        {
            // The worker exited unexpectedly. activeSlot is written before each plugin call,
            // so the offending plugin can be removed from the restarted chain.
            const LONG activeSlot = atomicLoad (&shared->activeSlot);
            if (juce::isPositiveAndBelow ((int) activeSlot, kNumSlots)
                && (launchedDisabledMask & (1 << activeSlot)) == 0)
            {
                launchedDisabledMask |= 1 << activeSlot;
                requestedDisabledMask.store (launchedDisabledMask, std::memory_order_release);
                if (onPluginFault)
                    onPluginFault ((int) activeSlot);
                if (launch (version, launchedDisabledMask))
                    launchTime = (juce::int64) GetTickCount64();
            }
            else
            {
                unavailable = true;
                juce::Logger::writeToLog ("Plugin sandbox: worker exited without an active plugin slot");
            }
        }

        wait (20);
    }

    if (child.isRunning())
        child.kill();
}

int PluginSandbox::runWorker (const juce::String& mapping, const juce::String& event,
                              const juce::File& config, int disabledMask, juce::Thread& thread)
{
    auto* mappingHandle = OpenFileMappingW (FILE_MAP_ALL_ACCESS, FALSE, mapping.toWideCharPointer());
    auto* shared = mappingHandle != nullptr
        ? static_cast<SharedState*> (MapViewOfFile (mappingHandle, FILE_MAP_ALL_ACCESS, 0, 0, sizeof (SharedState)))
        : nullptr;
    auto* eventHandle = OpenEventW (EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, event.toWideCharPointer());
    if (shared == nullptr || eventHandle == nullptr)
        TerminateProcess (GetCurrentProcess(), 3);

    const auto xml = juce::parseXML (config);
    if (xml == nullptr || ! xml->hasTagName ("PluginSandbox"))
        TerminateProcess (GetCurrentProcess(), 4);

    const double sampleRate = juce::jmax (8000.0, xml->getDoubleAttribute ("sampleRate", 48000.0));
    const int blockSize = juce::jlimit (16, kSandboxMaxBlock, xml->getIntAttribute ("blockSize", 512));
    const int maxLatency = juce::jmax (1, (int) (sampleRate * 0.25));
#if defined (OPENMIX_ENABLE_SANDBOX_TEST_HOOKS)
    const bool crashOnProcess = xml->getBoolAttribute ("testCrashOnProcess", false) && (disabledMask & 1) == 0;
#else
    constexpr bool crashOnProcess = false;
#endif

    juce::AudioPluginFormatManager formats;
    juce::addDefaultFormatsToManager (formats);
    std::array<std::unique_ptr<juce::AudioPluginInstance>, kNumSlots> plugins;
    std::array<std::array<juce::AudioProcessorParameter*, kSandboxMaxParameters>, kNumSlots> params {};
    std::array<int, kNumSlots> paramCounts {};
    std::array<int, kNumSlots> pluginLatencies {};
    std::array<bool, kNumSlots> wasActive {};
    std::array<DelayLine, kNumSlots> bypassDelay, mixDelay;
    for (auto& delay : bypassDelay) delay.prepare (maxLatency);
    for (auto& delay : mixDelay) delay.prepare (maxLatency);

    juce::AudioBuffer<float> scratch (kSandboxMaxChannels, blockSize);
    juce::AudioBuffer<float> dry (2, blockSize), mixDry (2, blockSize), chain (2, blockSize);
    juce::MidiBuffer midi;
    midi.ensureSize (256);
    int localFaultMask = disabledMask;

    juce::WaitableEvent pluginsLoaded;
    const bool loadPosted = juce::MessageManager::callAsync ([&]
    {
        for (auto* slot : xml->getChildWithTagNameIterator ("Slot"))
        {
            const int index = slot->getIntAttribute ("index", -1);
            if (! juce::isPositiveAndBelow (index, kNumSlots) || (localFaultMask & (1 << index)) != 0)
                continue;

            auto* description = slot->getChildByName ("PLUGIN");
            juce::PluginDescription desc;
            if (description == nullptr || ! desc.loadFromXml (*description))
                continue;

            InterlockedExchange (&shared->activeSlot, index);
            InterlockedExchange64 (&shared->activeSinceMs, (LONG64) GetTickCount64());
            juce::String error;
            auto plugin = formats.createPluginInstance (desc, sampleRate, blockSize, error);
            if (plugin == nullptr)
                TerminateProcess (GetCurrentProcess(), 5);

            auto layout = plugin->getBusesLayout();
            if (! layout.inputBuses.isEmpty())  layout.inputBuses.getReference (0) = juce::AudioChannelSet::stereo();
            if (! layout.outputBuses.isEmpty()) layout.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
            plugin->setBusesLayout (layout);
            plugin->setNonRealtime (false);
            plugin->prepareToPlay (sampleRate, blockSize);

            juce::MemoryBlock state;
            if (state.fromBase64Encoding (slot->getStringAttribute ("state")) && state.getSize() > 0)
                plugin->setStateInformation (state.getData(), (int) state.getSize());

            pluginLatencies[(size_t) index] = juce::jmax (0, plugin->getLatencySamples());
            const auto pluginParams = plugin->getParameters();
            paramCounts[(size_t) index] = juce::jmin (kSandboxMaxParameters, pluginParams.size());
            for (int p = 0; p < paramCounts[(size_t) index]; ++p)
                params[(size_t) index][(size_t) p] = pluginParams.getUnchecked (p);
            plugins[(size_t) index] = std::move (plugin);
            InterlockedExchange (&shared->latency[index], pluginLatencies[(size_t) index]);
            InterlockedExchange (&shared->activeSlot, -1);
            InterlockedExchange64 (&shared->activeSinceMs, 0);
        }
        pluginsLoaded.signal();
    });
    if (! loadPosted || ! pluginsLoaded.wait (30000.0))
        TerminateProcess (GetCurrentProcess(), 6);

    InterlockedExchange (&shared->ready, 1);
    while (! thread.threadShouldExit())
    {
        WaitForSingleObject ((HANDLE) eventHandle, 100);
        for (auto& block : shared->blocks)
        {
            if (InterlockedCompareExchange (&block.state, kProcessing, kReady) != kReady)
                continue;

            const LONG64 sequence = atomicLoad64 (&block.sequence);
            const LONG64 newest = atomicLoad64 (&shared->nextSequence);
            const int samples = atomicLoad (&block.sampleCount);
            if (samples <= 0 || samples > blockSize || newest - sequence >= PluginSandbox::pipelineBlocks)
            {
                InterlockedExchange (&block.state, kFree);
                continue;
            }

            if (crashOnProcess)
            {
                InterlockedExchange (&shared->activeSlot, 0);
                InterlockedExchange64 (&shared->activeSinceMs, (LONG64) GetTickCount64());
                TerminateProcess (GetCurrentProcess(), 86);
            }

            chain.copyFrom (0, 0, block.inputLeft, samples);
            chain.copyFrom (1, 0, block.inputRight, samples);
            int chainLatency = 0;

            for (int s = 0; s < kNumSlots; ++s)
            {
                auto* plugin = plugins[(size_t) s].get();
                if (plugin == nullptr || (localFaultMask & (1 << s)) != 0)
                {
                    wasActive[(size_t) s] = false;
                    continue;
                }

                const int pluginLatency = pluginLatencies[(size_t) s];
                const bool slotOn = atomicLoad (&shared->bypass[s]) == 0 && ! plugin->isSuspended();
                const bool crossfade = slotOn != wasActive[(size_t) s];
                const bool parameterCountMatches = atomicLoad (&shared->parameterCount[s]) == paramCounts[(size_t) s];
                if (parameterCountMatches)
                {
                    for (int p = 0; p < paramCounts[(size_t) s]; ++p)
                        if (params[(size_t) s][(size_t) p] != nullptr)
                            params[(size_t) s][(size_t) p]->setValue (bitsFloat (atomicLoad (&shared->parameters[s][p])));
                }

                if (! slotOn && ! crossfade && plugin->getBypassParameter() == nullptr)
                {
                    bypassDelay[(size_t) s].process (chain.getWritePointer (0), chain.getWritePointer (1), samples, pluginLatency);
                    chainLatency += pluginLatency;
                    wasActive[(size_t) s] = false;
                    InterlockedExchange (&shared->latency[s], pluginLatency);
                    continue;
                }

                if (crossfade)
                {
                    dry.copyFrom (0, 0, chain, 0, 0, samples);
                    dry.copyFrom (1, 0, chain, 1, 0, samples);
                }

                const float mix = bitsFloat (atomicLoad (&shared->mix[s]));
                const bool blendDry = mix < 0.999f;
                if (blendDry)
                {
                    mixDry.copyFrom (0, 0, chain, 0, 0, samples);
                    mixDry.copyFrom (1, 0, chain, 1, 0, samples);
                    mixDelay[(size_t) s].process (mixDry.getWritePointer (0), mixDry.getWritePointer (1), samples, pluginLatency);
                }

                const int channels = juce::jmax (2, plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels());
                if (channels > kSandboxMaxChannels)
                {
                    localFaultMask |= 1 << s;
                    InterlockedOr (&shared->faultedMask, 1 << s);
                    wasActive[(size_t) s] = false;
                    continue;
                }

                juce::AudioBuffer<float> view (scratch.getArrayOfWritePointers(), channels, samples);
                view.clear();
                view.copyFrom (0, 0, chain, 0, 0, samples);
                view.copyFrom (1, 0, chain, 1, 0, samples);
                midi.clear();

                InterlockedExchange (&shared->activeSlot, s);
                InterlockedExchange64 (&shared->activeSinceMs, (LONG64) GetTickCount64());
                bool threw = false;
                try
                {
                    if (slotOn || crossfade)
                        plugin->processBlock (view, midi);
                    else
                        plugin->processBlockBypassed (view, midi);
                }
                catch (...)
                {
                    threw = true;
                }
                InterlockedExchange (&shared->activeSlot, -1);
                InterlockedExchange64 (&shared->activeSinceMs, 0);

                const int outputs = plugin->getTotalNumOutputChannels();
                const int rightSource = outputs == 1 ? 0 : 1;
                if (threw || AudioSafety::containsNonFinite (view.getReadPointer (0), samples)
                           || AudioSafety::containsNonFinite (view.getReadPointer (rightSource), samples))
                {
                    localFaultMask |= 1 << s;
                    InterlockedOr (&shared->faultedMask, 1 << s);
                    bypassDelay[(size_t) s].clear();
                    mixDelay[(size_t) s].clear();
                    wasActive[(size_t) s] = false;
                    InterlockedExchange (&shared->latency[s], 0);
                    continue;
                }

                chain.copyFrom (0, 0, view, 0, 0, samples);
                chain.copyFrom (1, 0, view, rightSource, 0, samples);
                chainLatency += pluginLatency;
                InterlockedExchange (&shared->latency[s], pluginLatency);

                if (blendDry && (slotOn || crossfade))
                {
                    juce::FloatVectorOperations::multiply (chain.getWritePointer (0), mix, samples);
                    juce::FloatVectorOperations::multiply (chain.getWritePointer (1), mix, samples);
                    juce::FloatVectorOperations::addWithMultiply (chain.getWritePointer (0), mixDry.getReadPointer (0), 1.0f - mix, samples);
                    juce::FloatVectorOperations::addWithMultiply (chain.getWritePointer (1), mixDry.getReadPointer (1), 1.0f - mix, samples);
                }
                else if (! blendDry)
                {
                    mixDelay[(size_t) s].clear();
                }

                if (crossfade)
                {
                    const float wetStart = slotOn ? 0.0f : 1.0f;
                    const float wetEnd = slotOn ? 1.0f : 0.0f;
                    float* channelsOut[2] { chain.getWritePointer (0), chain.getWritePointer (1) };
                    juce::AudioBuffer<float> out (channelsOut, 2, samples);
                    out.applyGainRamp (0, samples, wetStart, wetEnd);
                    out.addFromWithRamp (0, 0, dry.getReadPointer (0), samples, 1.0f - wetStart, 1.0f - wetEnd);
                    out.addFromWithRamp (1, 0, dry.getReadPointer (1), samples, 1.0f - wetStart, 1.0f - wetEnd);
                    if (! slotOn)
                        bypassDelay[(size_t) s].clear();
                }

                wasActive[(size_t) s] = slotOn;
            }

            juce::FloatVectorOperations::copy (block.outputLeft, chain.getReadPointer (0), samples);
            juce::FloatVectorOperations::copy (block.outputRight, chain.getReadPointer (1), samples);
            InterlockedExchange (&block.state, kDone);
        }
    }

    for (auto& plugin : plugins)
        if (plugin != nullptr)
            plugin->releaseResources();
    UnmapViewOfFile (shared);
    CloseHandle (mappingHandle);
    CloseHandle (eventHandle);
    return 0;
}

PluginSandboxWorkerThread::PluginSandboxWorkerThread (juce::String mapping, juce::String event,
                                                       juce::File config, int disabledMask)
    : juce::Thread ("OpenMix plugin audio worker"), mappingName (std::move (mapping)),
      eventName (std::move (event)), configFile (std::move (config)), disabledSlots (disabledMask)
{
}

void PluginSandboxWorkerThread::run()
{
    PluginSandbox::runWorker (mappingName, eventName, configFile, disabledSlots, *this);
}
