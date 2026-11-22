#pragma once

#include <JuceHeader.h>
#include "engine/MixerEngine.h"
#include "engine/VoicePad.h"
#include "ui/Widgets.h"
#include "ui/Panels.h"
#include "ui/InsertRack.h"

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

    // Where the fader bay starts (from the top of the strip); the console sets the same value
    // on every column so faders line up.
    void setBayTop (int y) { bayTop = y; }

    static constexpr int minWidth = 210, maxWidth = 400;

private:
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

    juce::String displayName() const;

    MixerEngine& engine;
    ChannelStrip& strip;
    const int index;

    TapeLabel tape { true };
    MenuDotsButton menuButton;
    ChipButton sourceButton;
    DbSlider gainKnob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxRight };
    InsertRack inserts;
    std::array<StripKey, 6> processKeys;
    VoicePadComponent pad;
    std::array<StripKey, kNumBuses> sendButtons;
    StripKey muteButton { "Mute" }, soloButton { "Solo" };
    FaderSection faderSection;

    juce::Rectangle<int> gainLegend, insertsLegend, processLegend, padLegend, sendsLegend;
    int lastLatency = -1, tickCount = 0, bayTop = 0;
    bool inLayout = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StripView)
};
