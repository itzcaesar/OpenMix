#pragma once

#include <JuceHeader.h>
#include <functional>

inline constexpr int kPluginSandboxSlotCount = 6;

// Audio is handed to a child process in fixed-size shared-memory blocks. The
// editor/control instance stays in the main process; only processBlock runs in
// the worker. The mixer callback reads parameter values and exchanges blocks,
// but never runs plugin audio processing or waits on the child.
class PluginSandbox final : private juce::Thread
{
public:
    PluginSandbox (std::function<void (int)> onPluginFault);
    ~PluginSandbox() override;

    // Message thread. XML contains one PLUGIN description and state blob per slot.
    void configure (const juce::String& xml, int disabledSlotMask);

    // Audio thread. Control values are copied into shared memory before submit.
    void setSlotControls (int slot, bool bypass, float mix,
                          const float* parameters, int parameterCount) noexcept;
    bool processBlock (float* left, float* right, int numSamples) noexcept;

    static constexpr const char* workerFlag = "--plugin-audio-worker";
    static constexpr int pipelineBlocks = 2;

private:
    friend class PluginSandboxWorkerThread;
    void run() override;
    struct SharedState;

    std::function<void (int)> onPluginFault;
    juce::File configFile;
    juce::String mappingName, eventName;
    void* mappingHandle = nullptr;
    void* eventHandle = nullptr;
    SharedState* shared = nullptr;
    juce::ChildProcess child;
    std::atomic<int> requestedVersion { 0 };
    std::atomic<int> requestedDisabledMask { 0 };
    int launchedVersion = 0;
    int launchedDisabledMask = 0;
    bool unavailable = false;

    static int runWorker (const juce::String& mapping, const juce::String& event,
                          const juce::File& config, int disabledMask, juce::Thread& thread);
};

class PluginSandboxWorkerThread final : public juce::Thread
{
public:
    PluginSandboxWorkerThread (juce::String mapping, juce::String event,
                               juce::File config, int disabledMask);

private:
    void run() override;

    juce::String mappingName, eventName;
    juce::File configFile;
    int disabledSlots = 0;
};
