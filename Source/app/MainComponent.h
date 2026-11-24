#pragma once

#include <JuceHeader.h>
#include "engine/MixerEngine.h"
#include "ui/StripView.h"
#include "ui/BusView.h"
#include "ui/SoundboardPanel.h"
#include "ui/Theme.h"
#include "app/OscControl.h"
#include "app/RemoteServer.h"
#include "app/MidiControl.h"
#include "app/UpdateChecker.h"
#include "app/GlobalHotkeys.h"

// The console: input strips on the left, the master section (buses) on the right.
class ConsoleContent : public juce::Component
{
public:
    explicit ConsoleContent (MixerEngine&);

    void paint (juce::Graphics&) override;
    void resized() override;

    // Shows active strips/buses only; call after adding or removing one.
    void relayout();
    std::function<void()> onSizeChanged;   // the minimum width changed (MainComponent resizes)

    int minimumWidth() const;
    int minimumHeight() const { return 560 + (soundboard.isVisible() ? SoundboardPanel::barHeight() + Theme::s3 : 0); }

    std::array<std::unique_ptr<StripView>, kNumStrips> strips;
    std::array<std::unique_ptr<BusView>, kNumBuses> buses;
    SoundboardPanel soundboard;

private:
    void showAddBusMenu();

    MixerEngine& engine;
    juce::TextButton addStripButton { "+" }, addBusButton { "+" };
    juce::Rectangle<int> inputsLegend, outputsLegend;
    int dividerX = 0, deskBottom = 0;
};

// The header's status readout (device, rate, latency, CPU, dropouts). It is also the way into
// Audio settings: one control instead of a readout plus a separate button.
class StatusDisplay : public juce::Button
{
public:
    StatusDisplay();
    void setSegments (const juce::StringArray& s) { segments = s; repaint(); }
    void paintButton (juce::Graphics&, bool isHighlighted, bool isDown) override;

private:
    juce::StringArray segments;
};

class MainComponent : public juce::Component,
                      private juce::ChangeListener,
                      private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void showAudioSettings();
    void showSettings();
    void toggleTheme();
    void applyTheme();
    void updateStatus();

    // Creator tools
    void toggleRecording();
    void startRecording();
    void showRecordMenu();
    void saveReplay();
    void applyReplaySettings();
    void setSoundboardShown (bool);
    void applyOsc();
    void applyRemote();   // OSC + WebSocket/phone remote + MIDI, from AppSettings
    void showNote (const juce::String&);
    void updateRecordButton();

    // Scenes and hotkeys
    void showSceneMenu();
    void recallScene (int index);
    void saveScene (int index);          // -1 = as a new scene (asks for a name)
    void renameScene (int index);
    void deleteScene (int index);
    void updateSceneButton();
    void handleHotkey (const GlobalHotkeys::Binding&, bool down);
    void showHotkeys();
    juce::StringArray stripNames() const;
    juce::StringArray sceneNames() const;

    // Declared first so it is destroyed last (after plugin editor windows).
    MixerEngine engine;

    juce::TooltipWindow tooltips { this, 600 };
    StatusDisplay statusDisplay;
    juce::TextButton appSettingsButton { "Settings" };
    juce::TextButton fixButton { "Open audio settings" };
    StripKey recordButton { "Record" };
    MenuDotsButton recordMenuButton;
    juce::TextButton replayButton { "Save replay" };
    StripKey soundboardButton { "Soundboard" };
    ChipButton sceneButton;
    juce::TextButton updateButton;
    GlobalHotkeys hotkeys;
    OscControl osc { engine };
    RemoteServer remoteServer { engine };
    MidiControl midiControl { engine };
    UpdateChecker updater;
    juce::URL updatePage;
    juce::String note;
    juce::uint32 noteUntilMs = 0;
    int lastExternalChanges = 0;
    juce::Viewport consoleView;
    std::unique_ptr<ConsoleContent> console;

    juce::StringArray statusSegments;
    juce::Rectangle<int> headerArea, bannerArea;
    int transportDividerX = 0;
    bool showBanner = false;
    int ticks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
