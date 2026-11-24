#pragma once

#include <JuceHeader.h>
#include "dsp/AudioSafety.h"
#include "dsp/BuiltInFx.h"
#include "dsp/DelayLine.h"

constexpr int kNumSlots = 6;   // insert slots per strip or bus

// A chain of up to kNumSlots inserts (VST3, plus VST2 when the build
// enables it with OPENMIX_ENABLE_VST2), used by strips and buses.
// Each slot holds either one VST plugin or one built-in effect
// (omx::BuiltInFx: Reverb / Delay / Chorus) -- never both, and never more
// than one occupant. Loaded inserts are always packed into slots 0..n-1
// (no gaps), so the UI can show them as a simple ordered list. Per slot:
// bypass (click-free crossfade, latency kept), wet/dry mix
// (dry delayed by the plugin's latency; built-ins have zero latency, so
// the dry path is un-delayed for them), and a NaN/Inf guard that stops a
// broken plugin.
//
// Built-in inserts behave like VST inserts in the chain (same slots,
// bypass, wet/dry, packing, session/preset persistence); the difference
// is that their parameters live on the effect object itself (see
// getBuiltInParams / setBuiltInParams) and they are always available, so
// they can never be "missing" on load. A built-in's own wet/dry mix
// parameter and the chain's slotMix both apply -- set the chain mix to 1
// (the default) and drive the effect's own mix unless a second blend is
// really wanted.
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
    void removePlugin (int slot);                                                 // later slots move up (either kind)
    void removeAllPlugins();
    void movePlugin (int from, int to);
    juce::AudioPluginInstance* getPlugin (int slot) const noexcept { return slots[(size_t) slot].get(); }
    int getNumPlugins() const noexcept;   // occupied insert slots (VST + built-in), packed 0..n-1
    int getNumVstPlugins() const noexcept;
    int getNumInserts() const noexcept { return getNumPlugins(); }   // same count, clearer name

    // Built-in effects (message thread). installBuiltIn takes ownership and
    // prepares the effect; makeBuiltInFx is the factory the UI / Session use
    // to create one. Installing one kind in a slot replaces the other kind.
    static std::unique_ptr<omx::BuiltInFx> makeBuiltInFx (omx::FxType type);
    static juce::String fxTypeDisplayName (omx::FxType type);
    static bool fxTypeFromName (const juce::String& name, omx::FxType& type);
    void installBuiltIn (int slot, std::unique_ptr<omx::BuiltInFx> fx);
    omx::BuiltInFx* getBuiltIn (int slot) const noexcept { return builtIns[(size_t) slot].get(); }
    bool isBuiltInSlot (int slot) const noexcept { return getBuiltIn (slot) != nullptr; }
    juce::String getInsertDisplayName (int slot) const;   // VST name, or e.g. "Reverb (built-in)"

    // Built-in parameters (message thread). The layout is the one documented
    // on omx::StripPluginSlot::fxParams in engine/StripLogic.h:
    //   Reverb: [roomSize, damping, mix, preDelayMs]
    //   Delay:  [timeMs, feedback, mix, feedbackLowpassHz, noteDivision, tempoBpm]
    //   Chorus: [rateHz, depthMs, mix, feedback]
    // tempoSync is only meaningful for Delay (it follows the tap-tempo BPM).
    // Returns false when the slot holds no built-in effect.
    bool getBuiltInParams (int slot, std::array<float, 6>& params, bool& tempoSync) const;
    bool setBuiltInParams (int slot, const std::array<float, 6>& params, bool tempoSync);

    // Message thread: push the current tap-tempo BPM into every tempo-synced
    // BuiltInDelay in this chain (others are untouched). Called by
    // MixerEngine::applyBpmToDelays() on every tap / BPM change.
    void setDelayTempoBpm (float bpm);

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
    void prepareBuiltIn (omx::BuiltInFx&);

    std::array<std::unique_ptr<juce::AudioPluginInstance>, kNumSlots> slots;
    std::array<std::unique_ptr<omx::BuiltInFx>, kNumSlots> builtIns;
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
