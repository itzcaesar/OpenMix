#pragma once

#include <JuceHeader.h>
#include "engine/MixerEngine.h"
#include "app/MidiControl.h"

// Contents of the Settings window: startup, tray, theme, plugins, remote control, about.
class SettingsPanel : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    SettingsPanel (MixerEngine&, std::function<void()> toggleTheme, std::function<void()> oscChanged = {},
                   std::function<void()> openHotkeys = {}, MidiControl* midi = nullptr,
                   std::function<void()> appearanceChanged = {});
    ~SettingsPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    // One accent-colour swatch: a filled square, with a ring around the selected one.
    class AccentSwatch : public juce::Component
    {
    public:
        AccentSwatch (juce::Colour, std::function<void()> onClick);
        void paint (juce::Graphics&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void setSelected (bool s) { selected = s; repaint(); }

    private:
        juce::Colour colour;
        std::function<void()> onClick;
        bool selected = false;
    };

    void changeListenerCallback (juce::ChangeBroadcaster*) override { updateScanText(); }
    void timerCallback() override;
    void updateScanText();
    void updateMidiText();
    void refreshMidiDevices();
    void updateAccentSwatches();

    MixerEngine& engine;
    MidiControl* midiControl;
    std::function<void()> toggleTheme, oscChanged, openHotkeys, appearanceChanged;
    juce::TextButton hotkeysButton { "Hotkeys..." };

    juce::ToggleButton startWithWindows { "Start OpenMix when Windows starts" };
    juce::ToggleButton startMinimized   { "Start minimized to the tray" };
    juce::ToggleButton closeToTray      { "Closing the window keeps OpenMix running in the tray" };
    juce::TextButton themeButton;
    std::vector<std::unique_ptr<AccentSwatch>> accentSwatches;
    juce::ToggleButton compactMode { "Compact mode: narrower strips and buses, tighter spacing" };
    juce::TextButton scanButton   { "Scan for new plugins" };
    juce::TextButton retryButton  { "Retry skipped plugins" };
    juce::TextButton presetsButton { "Open presets folder" };
    juce::TextButton pluginsButton { "Open plugin folders" };
    juce::ToggleButton pluginProbe { "Probe plugins in a separate process before loading them" };
    juce::ToggleButton checkUpdates { "Check GitHub for new versions at startup" };
    juce::ToggleButton oscEnabled { "Remote control over OSC, UDP port" };
    juce::TextEditor oscPort;
    juce::ToggleButton wsEnabled { "Phone / browser remote and WebSocket API, TCP port" };
    juce::TextEditor wsPort;
    juce::ToggleButton midiEnabled { "MIDI controller" };
    juce::ComboBox midiDeviceBox;
    juce::ComboBox midiLearnTarget;
    juce::TextButton midiLearnButton { "Learn..." };
    juce::TextButton midiClearButton { "Clear mappings" };
    juce::TextButton logsButton { "Open log folder" };

    juce::String scanText, midiText;
    juce::Rectangle<int> startupLegend, themeLegend, pluginsLegend, networkLegend, aboutLegend,
                         accentLabelArea, oscNoteArea, scanTextArea, aboutArea, wsNoteArea, midiTextArea;
};
