#pragma once

#include <JuceHeader.h>
#include "engine/MixerEngine.h"
#include "engine/VoicePad.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"
#include "ui/Panels.h"
#include "ui/InsertRack.h"

// One input channel strip, in the mockup's order: INPUT (source, gain slider,
// polarity/mono), PROCESSING keys, inserts (VST3 + built-in), voice pad, sends
// (bus keys + FX sends), then the fader bay: fader + meter, mute/solo, name bar.
// There is deliberately no PRESET or FILTER section on the strip: presets and
// solo-safe live in the strip menu, HPF/LPF live in the EQ panel.
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

    // Column width limits; compact mode narrows them (Theme::isCompact()).
    static int minWidth() { return Theme::isCompact() ? 176 : 210; }
    static int maxWidth() { return Theme::isCompact() ? 320 : 400; }

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

    // Strip presets (MixerEngine::stripPresets, saved in the session file).
    // Reached from the strip menu, not from a section on the strip.
    void applyPreset (const juce::String& name);
    void deletePreset (const juce::String& name);
    void showSavePresetDialog();

    juce::String displayName() const;

    MixerEngine& engine;
    ChannelStrip& strip;
    const int index;

    TapeLabel tape { true };
    MenuDotsButton menuButton;
    ChipButton sourceButton;
    DbSlider gainSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    StripKey invertButton { juce::String::fromUTF8 ("\xc3\x98") }, monoButton { "MONO" };
    std::array<StripKey, 6> processKeys;
    juce::String selectedPreset;
    InsertRack inserts;
    VoicePadComponent pad;
    std::array<StripKey, kNumBuses> sendButtons;
    std::array<DbSlider, omx::kNumFxSends> fxSendSliders;
    StripKey muteButton { "Mute" }, soloButton { "Solo" };
    FaderSection faderSection;

    juce::Rectangle<int> inputLegend, processLegend, insertsLegend,
                         padLegend, sendsLegend, fxLegend, nameBarArea,
                         gainHeadArea, gainScaleArea, iconRowArea;
    std::vector<int> dividerYs;   // y of each 1 px section divider, in strip coords
    std::array<juce::Rectangle<int>, omx::kNumFxSends> fxSendLabelAreas {};
    int bayTop = 0;
    bool inLayout = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StripView)
};
