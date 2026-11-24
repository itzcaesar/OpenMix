#pragma once

#include <JuceHeader.h>
#include "engine/MixerEngine.h"

// Contents of the Settings window: startup, tray, theme, plugins, about.
class SettingsPanel : public juce::Component, private juce::ChangeListener
{
public:
    SettingsPanel (MixerEngine&, std::function<void()> toggleTheme, std::function<void()> oscChanged = {},
                   std::function<void()> openHotkeys = {});
    ~SettingsPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { updateScanText(); }
    void updateScanText();

    MixerEngine& engine;
    std::function<void()> toggleTheme, oscChanged, openHotkeys;
    juce::TextButton hotkeysButton { "Hotkeys..." };

    juce::ToggleButton startWithWindows { "Start OpenMix when Windows starts" };
    juce::ToggleButton startMinimized   { "Start minimized to the tray" };
    juce::ToggleButton closeToTray      { "Closing the window keeps OpenMix running in the tray" };
    juce::TextButton themeButton;
    juce::TextButton scanButton   { "Scan for new plugins" };
    juce::TextButton retryButton  { "Retry skipped plugins" };
    juce::TextButton presetsButton { "Open presets folder" };
    juce::TextButton pluginsButton { "Open plugin folders" };
    juce::ToggleButton checkUpdates { "Check GitHub for new versions at startup" };
    juce::ToggleButton oscEnabled { "Remote control over OSC, UDP port" };
    juce::TextEditor oscPort;
    juce::TextButton logsButton { "Open log folder" };

    juce::String scanText;
    juce::Rectangle<int> startupLegend, themeLegend, pluginsLegend, networkLegend, aboutLegend, oscNoteArea, scanTextArea, aboutArea;
};
