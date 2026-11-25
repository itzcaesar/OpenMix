#pragma once

#include <JuceHeader.h>
#include "engine/MixerEngine.h"
#include "app/RemoteCommands.h"

// MIDI controller mapping. Off by default.
//
// A mapping links one MIDI message to one remote-control path (see
// RemoteCommands.h): a CC moves a fader/gain/send level, a note (or a CC
// crossing 64) toggles a mute, solo, send or normalisation, plays a soundboard
// pad or recalls a scene. Mappings are learned, not typed: pick a target in
// Settings, press "Learn", then move a knob or press a key on the controller.
// A learned mapping is saved in the app settings straight away.
//
// Stream Deck: the Stream Deck software has no native OpenMix plugin, so
// use its OSC plugin (addresses are the same as /openmix/...), or a
// WebSocket plugin against the RemoteServer API. A MIDI controller that
// Stream Deck users already own works through this class instead.
class MidiControl : private juce::MidiInputCallback
{
public:
    struct Mapping
    {
        bool isNote = false;   // false = CC, true = note
        int channel = 0;       // 1..16, 0 = any
        int number = 0;        // CC number or note number
        juce::String target;   // e.g. "strip/1/fader", "strip/1/mute", "soundboard/3/play"
    };

    explicit MidiControl (MixerEngine&);
    ~MidiControl() override;

    bool start (const juce::String& deviceIdentifier, juce::String& error);   // no-op if already on that device
    void stop();
    bool isRunning() const noexcept { return input != nullptr; }
    juce::String getDeviceName() const;

    // Message thread, every couple of seconds: closes the input when the controller was
    // unplugged and opens it again when it's back.
    void maintain();

    static juce::Array<juce::MidiDeviceInfo> getDevices();

    // Learn mode: the next CC or note received becomes the mapping for
    // `target`, replacing any mapping for that target. Returns false if a
    // learn is already in progress for another target.
    bool startLearn (const juce::String& target);
    void cancelLearn();
    bool isLearning() const noexcept;
    juce::String getLearningTarget() const;

    std::vector<Mapping> getMappings() const;
    void setMappings (std::vector<Mapping>);
    void clearMappings();
    static juce::String mappingsToJson (const std::vector<Mapping>&);
    static std::vector<Mapping> mappingsFromJson (const juce::String& json);

    // Public for direct testing as well as MIDI callback dispatch.
    void applyMapping (const Mapping&, const juce::MidiMessage&);

    std::function<void (bool)> onRecord;
    std::function<void()> onSaveReplay;
    std::function<void (int scene)> onRecallScene;

private:
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;
    bool open();
    void close();
    bool currentToggle (const juce::StringArray& path) const;   // the mixer's current on/off for a toggle target

    MixerEngine& engine;
    std::unique_ptr<juce::MidiInput> input;
    juce::String deviceId;   // the chosen controller, kept while it's unplugged
    std::vector<Mapping> mappings;
    mutable juce::CriticalSection mappingsLock;
    juce::String learningTarget;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
    // CC toggle targets remember whether the control was last above 64, so a knob
    // crossing 64 flips the mute once instead of on every message above 64.
    std::map<juce::String, bool> ccToggleState;
};
