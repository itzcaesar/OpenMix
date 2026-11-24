#pragma once

// JUCE-free strip logic, so it can be unit-tested by tests/CoreTests.cpp (plain C++,
// no JUCE) and shared by the engine: solo-safe audibility, latency composition,
// and the plain-data strip preset (ChannelStrip converts to/from its atomics).
//
// A strip preset is the strip's *sound*: gain, fader, mono/polarity, solo-safe,
// all built-in processing (incl. HPF/LPF), bus sends, the VoicePad, and the insert
// slots (plugin identifiers + bypass + wet/dry). It deliberately does NOT carry
// the strip's name, colour, source/routing, mute, solo or active state: those are
// identity and mix state, not sound, and applying a preset to another strip must
// never re-route it, rename it or mute it. Plugin *state* blobs are not stored
// either (see StripPluginSlot): a preset re-loads each plugin by identifier and
// a missing plugin is skipped, never fatal.

#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace omx
{
    constexpr int kPresetNumBuses = 6;   // must match kNumBuses (ChannelStrip.h)
    constexpr int kPresetNumSlots = 6;   // must match kNumSlots (PluginChain.h)

    // FX sends: one send per built-in effect type, indexed 0..2.
    // The order matches omx::FxType (Reverb, Delay, Chorus) in dsp/BuiltInFx.h.
    constexpr int kFxReverb = 0, kFxDelay = 1, kFxChorus = 2;
    constexpr int kNumFxSends = 3;

    // Bus groups are slot-based, never name-based (see isVirtualBusSlot):
    // slots 0-2 are the Hardware (A) group, slots 3-5 the Virtual (B) group.
    // A bus keeps its group when it is renamed, so a custom name can never
    // move a bus between groups.
    constexpr int kHardwareBusFirst = 0, kVirtualBusFirst = 3;
    inline bool isVirtualBusSlot (int slot) noexcept { return slot >= kVirtualBusFirst; }
    inline int firstSlotInGroup (bool virtualGroup) noexcept
    {
        return virtualGroup ? kVirtualBusFirst : kHardwareBusFirst;
    }

    //==========================================================================
    // Solo-safe: while anything is soloed, the monitor bus plays only soloed strips
    // -- except solo-safe ones, which are never muted by the solo action. A solo-safe
    // strip keeps its normal monitor-bus routing: it is heard only if it is routed
    // to the monitor bus (busOn), at its normal send level (a soloed strip is heard
    // at unity instead). Other buses are never affected by solo at all.
    inline bool stripAudibleOnMonitorBus (bool soloed, bool soloSafe, bool busOn) noexcept
    {
        return soloed || (soloSafe && busOn);
    }

    //==========================================================================
    // Latency: a strip's total is its denoiser latency (only while noise removal is
    // on) plus its insert chain's latency. Negative inputs are treated as 0.
    inline int stripTotalLatencySamples (int denoiseLatencySamples, int pluginLatencySamples) noexcept
    {
        return std::max (0, denoiseLatencySamples) + std::max (0, pluginLatencySamples);
    }

    //==========================================================================
    // One insert slot in a preset: just enough to find the plugin again.
    // No state blob: the plugin loads with its defaults, and if the plugin is not
    // installed any more the slot is skipped (counted as missing by loadPreset).
    // A built-in effect slot instead carries its type + parameters (fxParams);
    // built-ins are always available, so they are never counted as missing.
    // fxParams layout per type:
    //   Reverb: [roomSize, damping, mix, preDelayMs]
    //   Delay:  [timeMs, feedback, mix, feedbackLowpassHz, noteDivision, tempoBpm]
    //   Chorus: [rateHz, depthMs, mix, feedback]
    struct StripPluginSlot
    {
        std::string identifier;   // PluginDescription::fileOrIdentifier (e.g. the .vst3 path)
        std::string name;         // display name, for matching / diagnostics only
        std::string format;       // e.g. "VST3"
        bool bypass = false;
        float mix = 1.0f;         // wet/dry, 0..1 (1 = fully wet)

        bool isBuiltIn = false;
        std::string fxType;       // "Reverb" / "Delay" / "Chorus" (built-in slots only)
        bool fxTempoSync = false; // Delay only: follow the tap-tempo BPM (see fxParams[5])
        std::array<float, 6> fxParams {};
    };

    struct StripPreset
    {
        float gainDb = 0.0f, faderDb = 0.0f;
        bool mono = false, invertPolarity = false, soloSafe = false;

        // Built-in processing (mirrors ProcessingParams; HPF/LPF included).
        bool denoiseOn = false;   float denoiseAmount = 1.0f;
        bool gateOn = false;      float gateThresholdDb = -50.0f, gateRangeDb = -40.0f;
        bool eqOn = false;        float eqLowDb = 0.0f, eqMidDb = 0.0f, eqMidHz = 1000.0f, eqHighDb = 0.0f;
        bool compOn = false;      float compThresholdDb = -18.0f, compRatio = 3.0f;
        bool deessOn = false;     float deessThresholdDb = -30.0f, deessMaxDb = 10.0f;
        bool duckOn = false;      int duckSource = -1;   // strip index, -1 = none
        float duckDepthDb = -12.0f, duckThresholdDb = -40.0f;
        bool hpfOn = false;       float hpfHz = 80.0f;    // 20..1000 Hz
        bool lpfOn = false;       float lpfHz = 18000.0f; // 1k..20k Hz

        // Bus sends (busOn / level / pre-fader), indexed as kBusNames.
        std::array<bool,  kPresetNumBuses> busOn  { true, false, false, false, false, false };
        std::array<float, kPresetNumBuses> sendDb {};
        std::array<bool,  kPresetNumBuses> sendPre {};

        // VoicePad (x = pan/tone, y, mode). Pan lives here, on the pad, as before.
        float padX = 0.0f, padY = -1.0f;
        int padMode = 0;   // VoicePadParams::Mode

        // Insert slots, packed (numSlots entries of `slots` are used).
        // A slot holds either a VST plugin (isBuiltIn false) or a built-in
        // effect (isBuiltIn true, see StripPluginSlot).
        std::array<StripPluginSlot, kPresetNumSlots> slots {};
        int numSlots = 0;

        // FX sends (post-fader, linear gain 0..1, 0 = no send), indexed by
        // omx::kFxReverb / kFxDelay / kFxChorus. They feed the engine's
        // global FX units; see MixerEngine for the return routing.
        std::array<float, kNumFxSends> fxSend {};
    };
}
