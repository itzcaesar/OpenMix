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

#include "dsp/EqBands.h"
#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace omx
{
    constexpr int kPresetNumBuses = 6;   // must match kNumBuses (ChannelStrip.h)
    constexpr int kPresetNumSlots = 6;   // must match kNumSlots (PluginChain.h)

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
    // One insert slot in a preset: just enough to find the plugin again.
    // No state blob: the plugin loads with its defaults, and if the plugin is not
    // installed any more the slot is skipped (counted as missing by loadPreset).
    struct StripPluginSlot
    {
        std::string identifier;   // PluginDescription::fileOrIdentifier (e.g. the .vst3 path)
        std::string name;         // display name, for matching / diagnostics only
        std::string format;       // e.g. "VST3"
        bool bypass = false;
        float mix = 1.0f;         // wet/dry, 0..1 (1 = fully wet)
    };

    struct StripPreset
    {
        float gainDb = 0.0f, faderDb = 0.0f;
        bool mono = false, invertPolarity = false, soloSafe = false;

        // Built-in processing (mirrors ProcessingParams; HPF/LPF included).
        bool denoiseOn = false;   float denoiseAmount = 1.0f;
        bool gateOn = false;      float gateThresholdDb = -50.0f, gateRangeDb = -40.0f;
        bool eqOn = false;
        std::array<float, kEqBands> eqDb {}, eqHz = kEqDefaultHz, eqQ { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
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

        // VoicePad (x = pan/tone, y, mode + per-mode saved coordinates).
        float padX = 0.0f, padY = -1.0f;
        int padMode = 0;   // VoicePadParams::Mode
        std::array<float, 4> padModeX { 0.0f, 0.0f, 0.0f, 0.0f };
        std::array<float, 4> padModeY { -1.0f, -1.0f, -1.0f, -1.0f };

        // Insert slots, packed (numSlots entries of `slots` are used).
        std::array<StripPluginSlot, kPresetNumSlots> slots {};
        int numSlots = 0;
    };
}
