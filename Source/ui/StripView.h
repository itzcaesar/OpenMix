#pragma once

#include <JuceHeader.h>
#include "engine/MixerEngine.h"
#include "engine/VoicePad.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"
#include "ui/Panels.h"
#include "ui/InsertRack.h"

// One input channel strip, a narrow console column (top to bottom): INPUT (source, gain /
// HPF / LPF knobs, polarity and mono), PROCESSING keys, INSERTS (VST3), SENDS (a key and
// a level knob per bus), the voice pad button, then the fader bay shared by every column:
// fader + meter, M / S / SAFE, latency, and the coloured name plate at the bottom.
// Presets live in the strip menu.
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
    std::function<void()> onOpenMediaPlayer; // the source was set to the media player

    // Where the fader bay starts (from the top of the strip); the console sets the same value
    // on every column so faders line up.
    void setBayTop (int y) { bayTop = y; }

    // Column width limits; compact mode narrows them (Theme::isCompact()).
    static int minWidth() { return Theme::isCompact() ? 96 : 106; }
    static int maxWidth() { return Theme::isCompact() ? 118 : 132; }

private:
    // Source + strip menu
    void showSourceMenu();
    void updateSourceButton();
    void showStripMenu();
    void confirmRemove();

    // Processing + sends + voice pad
    void openProcessorPanel (int index);
    void openSendPanel (int bus);
    void openVoicePad();
    void updateKeys();
    juce::Component* popupParent();

    // Strip presets (MixerEngine::stripPresets, saved in the session file).
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
    DbSlider gainKnob, hpfKnob, lpfKnob;
    StripKey invertButton { juce::String::fromUTF8 ("\xc3\x98") }, monoButton { "MONO" };
    std::array<StripKey, 6> processKeys;
    juce::String selectedPreset;
    InsertRack inserts;
    std::array<StripKey, kNumBuses> sendButtons;
    std::array<DbSlider, kNumBuses> sendKnobs;
    juce::TextButton padButton { "Voice pad" };
    StripKey muteButton { "M" }, soloButton { "S" }, safeButton { "SAFE" };
    FaderSection faderSection;

    juce::Rectangle<int> inputLegend, processLegend, insertsLegend, sendsLegend,
                         gainLabel, hpfLabel, lpfLabel, latencyArea;
    std::vector<int> dividerYs;   // y of each 1 px section divider, in strip coords
    juce::String latencyText;
    int bayTop = 0;
    bool inLayout = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StripView)
};
