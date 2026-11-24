#pragma once

#include <JuceHeader.h>
#include "engine/MixerEngine.h"

// One remote-control language, shared by OSC, the WebSocket/HTTP remote and
// MIDI mapping, so a fader moved from a phone, a Stream Deck or a MIDI
// controller does exactly what the same OSC address does.
//
// Paths (a leading "/" is optional):
//   strip/<n>/fader <dB -60..12>   strip/<n>/gain <dB -24..24>
//   strip/<n>/mute <0|1>           strip/<n>/solo <0|1>
//   strip/<n>/send/<bus> <0|1>     strip/<n>/sendlevel/<bus> <dB -60..12>
//   bus/<name>/fader <dB>          bus/<name>/mute <0|1>
//   soundboard/<n>/play            record <0|1>
//   replay/save                    scene/<n>   (1-based, recalls a scene)
//
// Returns true if the command changed something in the mixer.
struct RemoteCallbacks
{
    std::function<void (bool)> onRecord;         // UI decides what to record
    std::function<void()> onSaveReplay;
    std::function<void (int scene)> onRecallScene;   // 0-based
};

bool applyRemoteCommand (MixerEngine&, const juce::String& path, float value,
                         const RemoteCallbacks& callbacks = {});

// The mixer's state as JSON, for the phone remote and the WebSocket API.
// Strips and buses carry their fader, mute and (for strips) solo; the
// soundboard carries which pads are playing; scenes are listed by name.
juce::String remoteStateJson (MixerEngine&);
