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

    static constexpr const char* minimizedFlag = "--minimized";

private:
    AppSettings();
    std::unique_ptr<juce::PropertiesFile> props;
};
