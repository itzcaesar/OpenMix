#pragma once

#include <JuceHeader.h>
#include "app/MixHistory.h"
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
// The bus row is split into two visual groups -- HARDWARE (slots 0-2) and VIRTUAL
// (slots 3-5) -- with a divider between them when both groups have active buses.
// Grouping is slot-based, never name-based, so a renamed bus stays in its group.
class ConsoleContent : public juce::Component
{
public:
    explicit ConsoleContent (MixerEngine&);

    void paint (juce::Graphics&) override;
    void resized() override;

    // Shows active strips/buses only; call after adding or removing one.
    void relayout();

    // The Mixer menu's "Add strip" / "Add bus" (same as the + buttons).
    void addStrip()  { addStripButton.triggerClick(); }
    void addBus()    { showAddBusMenu(); }
    std::function<void()> onSizeChanged;   // the minimum width changed (MainComponent resizes)

    int minimumWidth() const;
    // Tall enough for the strip's top sections (input, filter, processing, preset) plus
    // the pad and at least one insert row above the fader bay; shorter windows scroll.
    // The desk is tall: the strip's sections (input, processing, inserts, voice
    // pad, sends) plus the fader bay need ~950 px of console height (900 compact)
    // before anything squeezes. Shorter windows scroll instead of crushing the
    // strips -- the inserts are the only part that shrinks, and they scroll.
    int minimumHeight() const { return (Theme::isCompact() ? 640 : 685) + (soundboard.isVisible() ? SoundboardPanel::barHeight() + Theme::s2 : 0); }

    std::array<std::unique_ptr<StripView>, kNumStrips> strips;
    std::array<std::unique_ptr<BusView>, kNumBuses> buses;
    SoundboardPanel soundboard;

private:
    void showAddBusMenu();
    void askBusName (bool virtualGroup);   // group first, then a custom name, then the next free slot
    static int groupGap() { return Theme::isCompact() ? 10 : 12; }   // gap + divider between the bus groups

    MixerEngine& engine;
    juce::TextButton addStripButton { "+" }, addBusButton { "+" };
    juce::Rectangle<int> inputsLegend, outputsLegend, hardwareLegend, virtualLegend;
    int dividerX = 0, groupDividerX = 0, groupDividerTop = 0, groupDividerBottom = 0, deskBottom = 0;
};

// The bottom status bar: a LIVE indicator, the engine's CPU and background work on the left,
// and a lamp per remote-control service that is switched on (OSC, phone remote, MIDI) on
// the right, lit while it is actually listening.
class StatusBar : public juce::Component
{
public:
    struct Service
    {
        juce::String name, detail;
        bool running = false;
    };

    StatusBar() = default;
    void setSegments (const juce::StringArray& s) { if (s != segments) { segments = s; repaint(); } }
    void setServices (std::vector<Service> s);
    void setLive (bool live) { if (live != isLive) { isLive = live; repaint(); } }
    void setReservedRight (int w) { reservedRight = w; }   // the device readout sits there
    void paint (juce::Graphics&) override;

private:
    juce::StringArray segments;
    std::vector<Service> services;
    int reservedRight = 0;
    bool isLive = false;
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

// Window buttons in the header: minimise, maximise / restore, close. Drawn by
// OpenMixLookAndFeel::drawWindowButton, the same as every dialog's title-bar buttons.
class WindowDot : public juce::Button
{
public:
    enum Kind { minimise, maximise, close };
    explicit WindowDot (Kind k);
    void paintButton (juce::Graphics&, bool isHighlighted, bool isDown) override;
    void setMaximised (bool m) { if (m != maximised) { maximised = m; repaint(); } }   // restore glyph

private:
    Kind kind;
    bool maximised = false;
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
    bool keyStateChanged (bool isKeyDown) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void showAudioSettings();
    void showSettings();
    void toggleTheme();
    void applyTheme();
    void updateStatus();
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
    void showRouting();
    void showMediaPlayer();
    void undoMix (bool redo);
    juce::String captureMixText();
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    juce::DocumentWindow* window() const { return dynamic_cast<juce::DocumentWindow*> (getTopLevelComponent()); }
    void toggleMaximised();
    void showMixerMenu();
    void showViewMenu();
    void showSetupMenu();
    void stepScene (int delta);
    juce::StringArray stripNames() const;
    juce::StringArray sceneNames() const;

    // Declared first so it is destroyed last (after plugin editor windows).
    MixerEngine engine;

    juce::TooltipWindow tooltips { this, 600 };
    StatusDisplay statusDisplay;
    StatusBar statusBar;
    juce::TextButton fixButton { "Open audio settings" };
    StripKey recordButton { "Record" };
    MenuDotsButton recordMenuButton;
    juce::TextButton replayButton { "Save replay" };
    StripKey soundboardButton { "Soundboard" };
    ChipButton sceneButton;
    juce::TextButton scenePrevButton { "<" }, sceneNextButton { ">" }, sceneStoreButton { "Store" }, sceneNewButton { "New" };
    juce::TextButton mixerMenuButton { "Mixer" }, viewMenuButton { "View" }, setupMenuButton { "Setup" };
    juce::TextButton updateButton;
    WindowDot minimiseDot { WindowDot::minimise }, maximiseDot { WindowDot::maximise }, closeDot { WindowDot::close };
    juce::ComponentDragger windowDragger;
    bool draggingWindow = false;
    juce::Rectangle<int> restoreBounds;   // set while maximised: the size to go back to
    GlobalHotkeys hotkeys;
    OscControl osc { engine };
    RemoteServer remoteServer { engine };
    MidiControl midiControl { engine };
    UpdateChecker updater;
    juce::URL updatePage;
    juce::String note;
    juce::uint32 noteUntilMs = 0;
    int lastExternalChanges = 0;
    MixHistory history;
    juce::Component::SafePointer<juce::DialogWindow> mediaWindow;   // one media player window at a time
    juce::Viewport consoleView;
    std::unique_ptr<ConsoleContent> console;

    juce::StringArray statusSegments;
    juce::Rectangle<int> headerArea, bannerArea;
    int transportDividerX = 0;
    bool showBanner = false;
    int ticks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
