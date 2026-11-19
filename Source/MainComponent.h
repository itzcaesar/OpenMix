#pragma once

#include <JuceHeader.h>
#include "MixerEngine.h"
#include "VoicePad.h"

//==============================================================================
// Stereo peak meter: segmented gradient, 1.5 s peak hold, latching clip LED (click to reset).
class LevelMeter : public juce::Component, public juce::SettableTooltipClient
{
public:
    LevelMeter (std::atomic<float>& l, std::atomic<float>& r) : peakL (l), peakR (r) {}
    void tick();
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { clipped = false; repaint(); }

    static float dbToProportion (float db) noexcept;

private:
    struct Channel { float level = 0.0f, hold = 0.0f; int holdFrames = 0; };
    void update (Channel&, float peak);
    void drawChannel (juce::Graphics&, juce::Rectangle<float>, const Channel&);

    std::atomic<float>& peakL;
    std::atomic<float>& peakR;
    Channel left, right;
    bool clipped = false;
};

//==============================================================================
// Fader + dB scale + meter, the bottom half of every strip and bus.
class FaderSection : public juce::Component
{
public:
    FaderSection (std::atomic<float>& peakL, std::atomic<float>& peakR);
    void resized() override;
    void paint (juce::Graphics&) override;
    void tick() { meter.tick(); }

    juce::Slider fader { juce::Slider::LinearVertical, juce::Slider::TextBoxAbove };
    juce::Colour accent = juce::Colour (0xff38bdf8);

private:
    LevelMeter meter;
};

//==============================================================================
// Rounded "chip" button: small caption, value text, chevron. Used for source/output pickers.
class ChipButton : public juce::Button
{
public:
    ChipButton() : juce::Button ({}) {}
    void setContent (const juce::String& caption, const juce::String& text, juce::Colour captionColour, bool problem);
    void paintButton (juce::Graphics&, bool isHighlighted, bool isDown) override;

private:
    juce::String caption, text;
    juce::Colour captionColour;
    bool problem = false;
};

//==============================================================================
// One insert slot: [power] Plugin name [...]
class PluginSlotRow : public juce::Component, public juce::SettableTooltipClient
{
public:
    void setState (const juce::String& pluginName, bool loaded, bool bypassed, int slotNumber);
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    std::function<void()> onMain, onBypass, onMenu;

private:
    enum class Zone { None, Power, Main, Menu };
    Zone zoneAt (juce::Point<int>) const;

    juce::String name;
    bool loaded = false, bypassed = false;
    int number = 1;
    Zone hover = Zone::None;
};

//==============================================================================
class PluginWindow : public juce::DocumentWindow
{
public:
    PluginWindow (juce::AudioPluginInstance&, std::function<void()> onCloseFn);
    ~PluginWindow() override;
    void closeButtonPressed() override;

private:
    std::function<void()> onClose;
};

//==============================================================================
class StripComponent : public juce::Component
{
public:
    StripComponent (MixerEngine&, int index);
    ~StripComponent() override;

    void resized() override;
    void paint (juce::Graphics&) override;
    void refreshDevice();
    void refreshFromStrip();
    void tick() { faderSection.tick(); }

private:
    void showSourceMenu();
    void updateSourceButton();
    void slotClicked (int slot);
    void showSlotMenu (int slot);
    void showPluginMenu (int slot);
    void loadPluginFile (int slot, const juce::String& fileOrId);
    void openEditor (int slot);
    void closeEditor (int slot);
    void updateSlotRows();
    juce::String defaultName() const { return "Input " + juce::String (index + 1); }

    MixerEngine& engine;
    ChannelStrip& strip;
    int index;
    juce::Colour colour;

    juce::Label nameLabel;
    ChipButton sourceButton;
    juce::Slider gainKnob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    std::array<PluginSlotRow, kNumSlots> slotRows;
    VoicePadComponent pad;
    FaderSection faderSection;
    juce::TextButton muteButton { "MUTE" };
    std::array<juce::TextButton, kNumBuses> busButtons;

    juce::Rectangle<int> insertsCaptionArea, padCaptionArea, routeCaptionArea;
    juce::String sourceKind;

    std::array<std::unique_ptr<PluginWindow>, kNumSlots> editorWindows;
};

//==============================================================================
class BusComponent : public juce::Component
{
public:
    BusComponent (MixerEngine&, int index);
    void resized() override;
    void paint (juce::Graphics&) override;
    void refreshDevice();
    void refreshFromBus();
    void tick() { faderSection.tick(); }

private:
    void showTargetMenu();
    bool isVirtual() const { return juce::String (kBusNames[index]).startsWith ("B"); }

    MixerEngine& engine;
    Bus& bus;
    int index;
    ChipButton targetButton;
    FaderSection faderSection;
    juce::TextButton muteButton { "MUTE" };
};

//==============================================================================
class MainComponent : public juce::Component,
                      private juce::ChangeListener,
                      private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct StatusChip { juce::String text; juce::Colour colour; float bar = -1.0f; };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void showAudioSettings();
    void updateStatus();

    // Declared first so it is destroyed last (after plugin editor windows).
    MixerEngine engine;

    juce::TooltipWindow tooltips { this, 500 };
    juce::TextButton settingsButton { "Audio Settings" };
    juce::TextButton fixButton { "Open Audio Settings" };
    std::array<std::unique_ptr<StripComponent>, kNumStrips> strips;
    std::array<std::unique_ptr<BusComponent>, kNumBuses> buses;

    juce::Array<StatusChip> chips;
    juce::Rectangle<int> headerArea, chipsArea, bannerArea, inputsLabelArea, outputsLabelArea;
    bool showBanner = false;
    int ticks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
