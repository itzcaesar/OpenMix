#pragma once

#include <JuceHeader.h>
#include "engine/MixerEngine.h"
#include "ui/StripView.h"
#include "ui/BusView.h"
#include "ui/SoundboardPanel.h"
#include "app/OscControl.h"
#include "app/UpdateChecker.h"

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
    static constexpr int minimumHeight = 560;

    std::array<std::unique_ptr<StripView>, kNumStrips> strips;
    std::array<std::unique_ptr<BusView>, kNumBuses> buses;

private:
    void showAddBusMenu();

    MixerEngine& engine;
    juce::TextButton addStripButton { "+" }, addBusButton { "+" };
    juce::Rectangle<int> inputsLegend, outputsLegend;
    int dividerX = 0;
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
    void showSoundboard();
    void applyOsc();
    void showNote (const juce::String&);
    void updateRecordButton();

    // Declared first so it is destroyed last (after plugin editor windows).
    MixerEngine engine;

    juce::TooltipWindow tooltips { this, 600 };
    juce::TextButton settingsButton { "Audio settings" };
    juce::TextButton appSettingsButton { "Settings" };
    juce::TextButton themeButton;
    juce::TextButton fixButton { "Open audio settings" };
    StripKey recordButton { "Record" };
    MenuDotsButton recordMenuButton;
    juce::TextButton replayButton { "Save replay" };
    juce::TextButton soundboardButton { "Soundboard" };
    juce::TextButton updateButton;
    std::unique_ptr<SoundboardWindow> soundboardWindow;
    OscControl osc { engine };
    UpdateChecker updater;
    juce::URL updatePage;
    juce::String note;
    juce::uint32 noteUntilMs = 0;
    int lastExternalChanges = 0;
    juce::Viewport consoleView;
    std::unique_ptr<ConsoleContent> console;

    juce::StringArray statusSegments;
    juce::Rectangle<int> headerArea, statusArea, bannerArea;
    bool showBanner = false;
    int ticks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
