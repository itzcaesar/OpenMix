#pragma once

#include <JuceHeader.h>
#include "dsp/AudioSafety.h"
#include "dsp/DelayLine.h"

constexpr int kNumSlots = 6;   // insert slots per strip or bus

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

    std::array<std::unique_ptr<juce::AudioPluginInstance>, kNumSlots> slots;
    juce::CriticalSection slotLock;

    juce::AudioBuffer<float> scratch;   // wide buffer for plugins with >2 channels
    juce::AudioBuffer<float> dryBuffer; // dry copy for bypass crossfades
    juce::AudioBuffer<float> mixDryBuffer;
    std::array<bool, kNumSlots> wasActive {};          // audio-thread view of each slot, for crossfades
    std::array<DelayLine, kNumSlots> bypassDelay;      // keeps latency for plugins without a bypass parameter
    std::array<DelayLine, kNumSlots> mixDelay;         // dry path delayed by the plugin's latency
    juce::MidiBuffer midi;

    double sampleRate = 48000.0;
    int blockSize = 512;

    static constexpr int kMaxPluginChannels = 16;
};
