#pragma once

#include <JuceHeader.h>
#include "../MixerEngine.h"
#include "../VoicePad.h"
#include "Widgets.h"
#include "Panels.h"

// A plugin's own editor in its own window.
class PluginWindow : public juce::DocumentWindow
{
public:
    PluginWindow (juce::AudioPluginInstance&, std::function<void()> onCloseFn);
    ~PluginWindow() override;
    void closeButtonPressed() override;

private:
    std::function<void()> onClose;
};

// One input channel strip, top to bottom in signal-flow order:
// name tape, source + gain, inserts, processing, voice pad, sends, mute/solo, fader + meter.
class StripView : public juce::Component
{
public:
    StripView (MixerEngine&, int index);
    ~StripView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void refreshDevice();
    void refreshFromStrip();
    void tick();

    std::function<void()> onLayoutChanged;   // the strip was removed, or buses changed

    static constexpr int minWidth = 214, maxWidth = 300;

private:
    class InsertList;

    // Source + strip menu
    void showSourceMenu();
    void updateSourceButton();
    void showStripMenu();
    void confirmRemove();

    // Processing + sends
    void openProcessorPanel (int index);
    void openSendPanel (int bus);
    void updateKeys();
    juce::Component* popupParent();

    // Inserts
    void updateSlotRows();
    void slotMain (int slot);
    void showSlotMenu (int slot);
    void showPluginMenu (int slot);
    void toggleBypass (int slot);
    void removeSlot (int slot);
    void moveSlot (int from, int to);
    void loadPluginAsync (int slot, const juce::PluginDescription&, std::function<void (juce::AudioPluginInstance&)> then = {});
    void savePreset (int slot);
    void loadPreset (int slot);
    void openEditor (juce::AudioPluginInstance*);
    void closeEditor (juce::AudioPluginInstance*);
    int visibleRowCount() const;

    juce::String displayName() const;

    MixerEngine& engine;
    ChannelStrip& strip;
    const int index;

    TapeLabel tape { true };
    MenuDotsButton menuButton;
    ChipButton sourceButton;
    DbSlider gainKnob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxRight };
    juce::Viewport insertsView;
    std::unique_ptr<InsertList> insertList;
    std::array<StripKey, 6> processKeys;
    VoicePadComponent pad;
    std::array<StripKey, kNumBuses> sendButtons;
    StripKey muteButton { "Mute" }, soloButton { "Solo" };
    FaderSection faderSection;

    juce::Rectangle<int> gainLegend, insertsLegend, processLegend, padLegend, sendsLegend;
    int loadingSlot = -1;
    juce::String loadingName;
    int lastFaultCount = 0, lastLatency = -1, tickCount = 0;
    int dragFrom = -1, dropIndex = -1;
    bool inLayout = false;

    std::map<juce::AudioPluginInstance*, std::unique_ptr<PluginWindow>> editorWindows;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StripView)
};
