#pragma once

#include <JuceHeader.h>
#include "../MixerEngine.h"

// Contents of the Settings window: startup, tray, theme, plugins, about.
class SettingsPanel : public juce::Component, private juce::ChangeListener
{
public:
    SettingsPanel (MixerEngine&, std::function<void()> toggleTheme);
    ~SettingsPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { updateScanText(); }
    void updateScanText();

    MixerEngine& engine;
    std::function<void()> toggleTheme;

    juce::ToggleButton startWithWindows { "Start OpenMix when Windows starts" };
    juce::ToggleButton startMinimized   { "Start minimized to the tray" };
    juce::ToggleButton closeToTray      { "Closing the window keeps OpenMix running in the tray" };
    juce::TextButton themeButton;
    juce::TextButton scanButton   { "Scan for new plugins" };
    juce::TextButton retryButton  { "Retry skipped plugins" };
    juce::TextButton presetsButton { "Open presets folder" };
    juce::TextButton pluginsButton { "Open VST3 folder" };

    juce::String scanText;
    juce::Rectangle<int> startupLegend, themeLegend, pluginsLegend, aboutLegend, scanTextArea, aboutArea;
};
