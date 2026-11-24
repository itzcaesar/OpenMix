#pragma once

#include <JuceHeader.h>
#include "engine/MixerEngine.h"
#include "engine/VoicePad.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"
#include "ui/Panels.h"
#include "ui/InsertRack.h"

// One input channel strip, top to bottom in signal-flow order:
// INPUT (source, gain slider, polarity/mono), FILTER (HPF/LPF), processing keys,
// PRESET, inserts (VST3 + built-in), voice pad, then the fader bay: bus sends,
// FX sends, fader + meter, mute/solo/solo-safe, per-channel latency, name bar.
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
    void updateFilter();
    juce::Component* popupParent();

    // Strip presets (MixerEngine::stripPresets, saved in the session file)
    void refreshPresets();
    void applySelectedPreset();
    void showSavePresetDialog();

    juce::String displayName() const;

    MixerEngine& engine;
    ChannelStrip& strip;
    const int index;

    TapeLabel tape { true };
    MenuDotsButton menuButton;
    ChipButton sourceButton;
    DbSlider gainSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    StripKey invertButton { juce::String::fromUTF8 ("\xc3\x98") }, monoButton { "MONO" };
    DbSlider hpfKnob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    DbSlider lpfKnob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    StripKey hpfButton { "HPF" }, lpfButton { "LPF" };
    std::array<StripKey, 6> processKeys;
    juce::ComboBox presetCombo;
    juce::TextButton presetSaveButton { "Save" }, presetDeleteButton { "Delete" };
    juce::String selectedPreset;
    InsertRack inserts;
    VoicePadComponent pad;
    std::array<StripKey, kNumBuses> sendButtons;
    std::array<DbSlider, omx::kNumFxSends> fxSendSliders;
    StripKey muteButton { "Mute" }, soloButton { "Solo" }, safeButton { "SAFE" };
    FaderSection faderSection;
    juce::Label latencyLabel;

    juce::Rectangle<int> inputLegend, filterLegend, processLegend, presetLegend, insertsLegend,
                         padLegend, sendsLegend, fxLegend, nameBarArea;
    std::array<juce::Rectangle<int>, omx::kNumFxSends> fxSendLabelAreas {};
    int lastLatency = -1, tickCount = 0, bayTop = 0;
    bool inLayout = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StripView)
};
