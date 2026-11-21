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

    // Recorder: 0 = every bus, 1 = every strip, 2 = buses and strips (one file each)
    int recordWhat() const                    { return props->getIntValue ("recordWhat", 0); }
    void setRecordWhat (int w)                { props->setValue ("recordWhat", w); props->saveIfNeeded(); }

    // Replay buffer: seconds kept (0 = off) and which bus it listens to
    int replaySeconds() const                 { return props->getIntValue ("replaySeconds", 0); }
    void setReplaySeconds (int s)             { props->setValue ("replaySeconds", s); props->saveIfNeeded(); }
    int replayBus() const                     { return props->getIntValue ("replayBus", 3); }   // B1
    void setReplayBus (int b)                 { props->setValue ("replayBus", b); props->saveIfNeeded(); }

    static juce::File getLogFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("OpenMix");
    }

    static constexpr const char* minimizedFlag = "--minimized";

private:
    AppSettings();
    std::unique_ptr<juce::PropertiesFile> props;
};
