#pragma once

#include <JuceHeader.h>

// Hardware-desk look for every JUCE control, driven by the active Theme palette.
// Buttons become "lamp" buttons (LED square + label) when their "lamp" property is set:
//   button.getProperties().set ("lamp", "accent")   // amber lamp (sends, modes)
//   button.getProperties().set ("lamp", "mute")     // red lamp (mute)
class OpenMixLookAndFeel : public juce::LookAndFeel_V4
{
public:
    OpenMixLookAndFeel();

    // Re-reads Theme::p(); call after Theme::setDark(), then repaint.
    void applyPalette();

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool isHighlighted, bool isDown) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool isHighlighted, bool isDown) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool isHighlighted, bool isDown) override;

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float startAngle, float endAngle, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;

    void drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics&, const juce::Path&, juce::Image&) override;
    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getPopupMenuFont() override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                           juce::Rectangle<int> parentArea) override;

    static void drawLamp (juce::Graphics&, juce::Rectangle<float> area, bool on, juce::Colour lampColour);
};
