#pragma once

#include <JuceHeader.h>
#include "dsp/DelayLine.h"
#include "engine/PluginSandbox.h"

constexpr int kNumSlots = kPluginSandboxSlotCount;   // insert slots per strip or bus

// A chain of up to kNumSlots VST3 inserts (plus VST2 when enabled via OPENMIX_ENABLE_VST2),
// used by strips and buses. Loaded plugins are always packed into slots 0..n-1 (no gaps),
// so the UI can show them as a simple ordered list. Per slot: bypass (click-free crossfade,
// latency kept), wet/dry mix (dry delayed by the plugin's latency), and a NaN/Inf guard
// that stops a broken plugin.
class PluginChain
{
public:
    PluginChain();
    ~PluginChain();

    void prepare (double sampleRate, int maxBlockSize);
    void release();

    // Message thread
    bool loadPlugin (int slot, const juce::PluginDescription&, juce::AudioPluginFormatManager&, juce::String& error);
    void installPlugin (int slot, std::unique_ptr<juce::AudioPluginInstance>);   // takes ownership, prepares it
    void removePlugin (int slot);                                                 // later plugins move up
    void removeAllPlugins();
    void movePlugin (int from, int to);
    void synchronizeSandbox(); // call after applying opaque plugin state on the message thread
    juce::AudioPluginInstance* getPlugin (int slot) const noexcept { return slots[(size_t) slot].get(); }
    int getNumPlugins() const noexcept;   // occupied insert slots, packed 0..n-1

    // Audio thread: runs the chain in place on L/R and adds the chain's latency (bypassed
    // plugins included) to `latency`. Skips the block if the UI is swapping a plugin.
    void process (float* L, float* R, int numSamples, int& latency) noexcept;

    std::array<std::atomic<bool>, kNumSlots>  slotBypass {};
    std::array<std::atomic<bool>, kNumSlots>  slotFault {};   // plugin output NaN/Inf -> auto-bypassed
    std::array<std::atomic<float>, kNumSlots> slotMix {};     // wet/dry, 0..1 (1 = fully wet)
    std::atomic<int> faultCount { 0 };                        // NaN/Inf events caught in this chain
    std::atomic<int> latencySamples { 0 };                    // last block's chain latency

private:
    void preparePlugin (juce::AudioPluginInstance&);
    void cachePluginParameters (int slot);
    void configureSandbox();

    std::array<std::unique_ptr<juce::AudioPluginInstance>, kNumSlots> slots;
    std::array<std::vector<juce::AudioProcessorParameter*>, kNumSlots> parameterPointers;
    std::array<int, kNumSlots> pluginLatencies {};
    juce::CriticalSection slotLock;

    juce::AudioBuffer<float> dryBuffer; // dry source for the latency-matched fallback
    juce::AudioBuffer<float> mixDryBuffer;
    juce::AudioBuffer<float> sandboxWorkerBuffer;
    DelayLine sandboxDryDelay;          // protects chain alignment while a worker is late/offline
    std::unique_ptr<PluginSandbox> sandbox;
    bool sandboxWasActive = false;

    double sampleRate = 48000.0;
    int blockSize = 512;

};
