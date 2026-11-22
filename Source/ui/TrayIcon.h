#pragma once

#include <JuceHeader.h>

// Notification-area icon. Left double-click shows the window; any click opens a small menu.
class TrayIcon : public juce::SystemTrayIconComponent
{
public:
    TrayIcon();

    std::function<void()> onShow, onQuit;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    // One-time notice the first time the window hides into the tray.
    void tellStillRunning();

    // The app icon at any pixel size (also drawn next to the name in the header).
    static juce::Image makeIcon (int size = 32);

private:
    bool told = false;
};
