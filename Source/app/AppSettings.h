#pragma once

#include <JuceHeader.h>

// App-wide preferences (not part of the mixer session), saved in %APPDATA%\OpenMix\settings.xml.
class AppSettings
{
public:
    static AppSettings& get();

    bool startWithWindows() const;
    void setStartWithWindows (bool);          // writes the per-user Run key (no admin rights needed)

    bool startMinimized() const               { return props->getBoolValue ("startMinimized", false); }
    void setStartMinimized (bool b)           { props->setValue ("startMinimized", b); props->saveIfNeeded(); }

    // Closing the window keeps OpenMix (and your audio) running in the tray.
    bool closeToTray() const                  { return props->getBoolValue ("closeToTray", true); }
    void setCloseToTray (bool b)              { props->setValue ("closeToTray", b); props->saveIfNeeded(); }

    // Off by default: it contacts GitHub (api.github.com) once at startup.
    bool checkForUpdates() const              { return props->getBoolValue ("checkForUpdates", false); }
    void setCheckForUpdates (bool b)          { props->setValue ("checkForUpdates", b); props->saveIfNeeded(); }

    // Off by default: while on, anything on the local network can control the mixer over OSC.
    bool oscEnabled() const                   { return props->getBoolValue ("oscEnabled", false); }
    void setOscEnabled (bool b)               { props->setValue ("oscEnabled", b); props->saveIfNeeded(); }
    int oscPort() const                       { return props->getIntValue ("oscPort", 9000); }
    void setOscPort (int p)                   { props->setValue ("oscPort", p); props->saveIfNeeded(); }

    // Phone / browser remote and WebSocket API (RemoteServer). Off by default.
    bool wsEnabled() const                    { return props->getBoolValue ("wsEnabled", false); }
    void setWsEnabled (bool b)                { props->setValue ("wsEnabled", b); props->saveIfNeeded(); }
    int wsPort() const                        { return props->getIntValue ("wsPort", 8080); }
    void setWsPort (int p)                    { props->setValue ("wsPort", p); props->saveIfNeeded(); }

    // MIDI controller mapping (MidiControl). Off by default.
    bool midiEnabled() const                  { return props->getBoolValue ("midiEnabled", false); }
    void setMidiEnabled (bool b)              { props->setValue ("midiEnabled", b); props->saveIfNeeded(); }
    juce::String midiDevice() const           { return props->getValue ("midiDevice"); }
    void setMidiDevice (const juce::String& d) { props->setValue ("midiDevice", d); props->saveIfNeeded(); }
    juce::String midiMappings() const         { return props->getValue ("midiMappings"); }
    void setMidiMappings (const juce::String& j) { props->setValue ("midiMappings", j); props->saveIfNeeded(); }

    // Crash-safe plugin loading: load a plugin in a child process first, and
    // only load it in the mixer if the child survived. On by default.
    bool pluginProbeEnabled() const           { return props->getBoolValue ("pluginProbeEnabled", true); }
    void setPluginProbeEnabled (bool b)       { props->setValue ("pluginProbeEnabled", b); props->saveIfNeeded(); }

    // Compact mode: narrower strips and buses, tighter gaps, shorter plugin rows.
    bool compactMode() const                  { return props->getBoolValue ("compactMode", false); }
    void setCompactMode (bool b)              { props->setValue ("compactMode", b); props->saveIfNeeded(); }

    // Recorder: 0 = every bus, 1 = every strip, 2 = buses and strips (one file each)
    int recordWhat() const                    { return props->getIntValue ("recordWhat", 0); }
    void setRecordWhat (int w)                { props->setValue ("recordWhat", w); props->saveIfNeeded(); }

    // Replay buffer: seconds kept (0 = off) and which bus it listens to
    int replaySeconds() const                 { return props->getIntValue ("replaySeconds", 0); }
    void setReplaySeconds (int s)             { props->setValue ("replaySeconds", s); props->saveIfNeeded(); }
    int replayBus() const                     { return props->getIntValue ("replayBus", 3); }   // B1
    void setReplayBus (int b)                 { props->setValue ("replayBus", b); props->saveIfNeeded(); }

    // Media player bar in the console
bool mediaShown() const                   { return props->getBoolValue ("mediaShown", false); }
void setMediaShown (bool b)               { props->setValue ("mediaShown", b); }

// Soundboard column in the console
    bool soundboardShown() const              { return props->getBoolValue ("soundboardShown", true); }
    void setSoundboardShown (bool b)          { props->setValue ("soundboardShown", b); props->saveIfNeeded(); }

    // Global hotkeys, as XML (GlobalHotkeys reads and writes it)
    juce::String hotkeysXml() const           { return props->getValue ("hotkeys"); }
    void setHotkeysXml (const juce::String& x) { props->setValue ("hotkeys", x); props->saveIfNeeded(); }

    static juce::File getLogFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("OpenMix");
    }

    static constexpr const char* minimizedFlag = "--minimized";

private:
    AppSettings();
    std::unique_ptr<juce::PropertiesFile> props;
};
