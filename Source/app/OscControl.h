#pragma once

#include <JuceHeader.h>
#include "engine/MixerEngine.h"

// OSC remote control (TouchOSC, Stream Deck OSC plugins, scripts). Off by default.
// Strips are numbered from 1 in on-screen order of their slots; buses by name.
//
//   /openmix/strip/<n>/fader <dB>        /openmix/strip/<n>/gain <dB>
//   /openmix/strip/<n>/mute <0|1>        /openmix/strip/<n>/solo <0|1>
//   /openmix/strip/<n>/send/<bus> <0|1>  /openmix/strip/<n>/sendlevel/<bus> <dB>
//   /openmix/bus/<name>/fader <dB>       /openmix/bus/<name>/mute <0|1>
//   /openmix/soundboard/<n>/play         /openmix/record <0|1>     /openmix/replay/save
class OscControl : private juce::OSCReceiver,
                   private juce::OSCReceiver::Listener<juce::OSCReceiver::MessageLoopCallback>
{
public:
    explicit OscControl (MixerEngine&);
    ~OscControl() override;

    bool start (int port, juce::String& error);
    void stop();
    bool isRunning() const noexcept { return running; }
    int getPort() const noexcept { return running ? port : 0; }

    std::function<void (bool)> onRecord;   // UI decides what to record
    std::function<void()> onSaveReplay;
    std::function<void (int scene)> onRecallScene;   // 0-based

private:
    void oscMessageReceived (const juce::OSCMessage&) override;

    MixerEngine& engine;
    bool running = false;
    int port = 0;
};
