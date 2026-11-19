#include "OpenMixLookAndFeel.h"
#include "Theme.h"

OpenMixLookAndFeel::OpenMixLookAndFeel()
{
    using namespace Theme;

    setColourScheme ({ background, panel, panel, outline, text, accent, text, accent, text });

    setColour (juce::ResizableWindow::backgroundColourId, background);
    setColour (juce::DocumentWindow::textColourId, text);

    setColour (juce::TextButton::buttonColourId, raised);
    setColour (juce::TextButton::buttonOnColourId, accent);
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, background);

    setColour (juce::Slider::thumbColourId, text);
    setColour (juce::Slider::trackColourId, accent);
    setColour (juce::Slider::backgroundColourId, panelDark);
    setColour (juce::Slider::rotarySliderFillColourId, accent);
    setColour (juce::Slider::rotarySliderOutlineColourId, raised);
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, accent.withAlpha (0.4f));

    setColour (juce::Label::textColourId, text);
    setColour (juce::Label::textWhenEditingColourId, text);
    setColour (juce::Label::backgroundWhenEditingColourId, panelDark);
    setColour (juce::Label::outlineWhenEditingColourId, accent);
    setColour (juce::TextEditor::backgroundColourId, panelDark);
    setColour (juce::TextEditor::highlightColourId, accent.withAlpha (0.4f));
    setColour (juce::TextEditor::focusedOutlineColourId, accent);
    setColour (juce::CaretComponent::caretColourId, accent);

    setColour (juce::PopupMenu::backgroundColourId, panel);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::headerTextColourId, textFaint);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.22f));
    setColour (juce::PopupMenu::highlightedTextColourId, text);

    setColour (juce::ComboBox::backgroundColourId, raised);
    setColour (juce::ComboBox::outlineColourId, outline);
    setColour (juce::ComboBox::arrowColourId, textDim);
    setColour (juce::ListBox::backgroundColourId, panelDark);
    setColour (juce::ScrollBar::thumbColourId, outline);
    setColour (juce::TooltipWindow::backgroundColourId, raised);
    setColour (juce::TooltipWindow::textColourId, text);
    setColour (juce::TooltipWindow::outlineColourId, outline);
    setColour (juce::ToggleButton::tickColourId, accent);
    setColour (juce::ToggleButton::tickDisabledColourId, textFaint);
}

//==============================================================================
juce::Font OpenMixLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return Theme::ui (juce::jlimit (12.0f, 14.0f, (float) buttonHeight * 0.52f), true);
}

void OpenMixLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                               const juce::Colour& backgroundColour,
                                               bool isHighlighted, bool isDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = button.getToggleState();

    auto fill = backgroundColour;
    if (isDown)             fill = fill.darker (0.25f);
    else if (isHighlighted) fill = fill.brighter (on ? 0.12f : 0.18f);
    if (! button.isEnabled()) fill = fill.withMultipliedAlpha (0.5f);

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, Theme::smallRadius);

    g.setColour (on ? fill.brighter (0.25f) : Theme::outline);
    g.drawRoundedRectangle (bounds, Theme::smallRadius, 1.0f);

    if (button.hasKeyboardFocus (true))
    {
        g.setColour (Theme::accent);
        g.drawRoundedRectangle (bounds.reduced (1.0f), Theme::smallRadius, 1.5f);
    }
}

//==============================================================================
void OpenMixLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                           float pos, float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float trackWidth = juce::jmax (3.0f, radius * 0.14f);
    const float arcRadius = radius - trackWidth * 0.5f;

    // Track
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (Theme::raised);
    g.strokePath (track, { trackWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });

    // Value arc. Bipolar sliders (range crosses 0, e.g. gain) fill from the centre.
    const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
    const float zeroPos = bipolar ? (float) slider.valueToProportionOfLength (0.0) : 0.0f;
    const float fromAngle = startAngle + zeroPos * (endAngle - startAngle);
    const float toAngle   = startAngle + pos * (endAngle - startAngle);

    if (std::abs (toAngle - fromAngle) > 0.001f)
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                             juce::jmin (fromAngle, toAngle), juce::jmax (fromAngle, toAngle), true);
        g.setColour (slider.findColour (juce::Slider::rotarySliderFillColourId)
                         .withMultipliedAlpha (slider.isEnabled() ? 1.0f : 0.4f));
        g.strokePath (value, { trackWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });
    }

    // Knob body + pointer
    const float bodyRadius = arcRadius - trackWidth * 1.4f;
    g.setColour (Theme::panelDark);
    g.fillEllipse (juce::Rectangle<float> (bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre (centre));
    g.setColour (Theme::outline);
    g.drawEllipse (juce::Rectangle<float> (bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre (centre), 1.0f);

    const auto tip = centre.getPointOnCircumference (bodyRadius * 0.78f, toAngle);
    const auto base = centre.getPointOnCircumference (bodyRadius * 0.25f, toAngle);
    g.setColour (Theme::text);
    g.drawLine ({ base, tip }, 2.0f);
}

void OpenMixLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                           float sliderPos, float minSliderPos, float maxSliderPos,
                                           juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style != juce::Slider::LinearVertical)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
        return;
    }

    const auto area = juce::Rectangle<int> (x, y, width, height).toFloat();
    const float cx = area.getCentreX();

    // Slot
    const auto slot = juce::Rectangle<float> (cx - 2.0f, area.getY(), 4.0f, area.getHeight());
    g.setColour (Theme::panelDark);
    g.fillRoundedRectangle (slot, 2.0f);

    // Fill from the bottom up to the cap
    g.setColour (slider.findColour (juce::Slider::trackColourId).withAlpha (0.55f));
    g.fillRoundedRectangle (slot.withTop (sliderPos), 2.0f);

    // 0 dB mark
    if (slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0)
    {
        const float zeroY = (float) slider.getPositionOfValue (0.0);
        g.setColour (Theme::textDim);
        g.fillRect (cx - 9.0f, zeroY - 0.5f, 18.0f, 1.0f);
    }

    // Cap
    const float capW = juce::jmin (36.0f, area.getWidth() - 2.0f), capH = 18.0f;
    const auto cap = juce::Rectangle<float> (capW, capH).withCentre ({ cx, sliderPos });
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (cap.translated (0.0f, 2.0f), 4.0f);
    g.setGradientFill (juce::ColourGradient (Theme::raised.brighter (0.35f), cap.getX(), cap.getY(),
                                             Theme::raised, cap.getX(), cap.getBottom(), false));
    g.fillRoundedRectangle (cap, 4.0f);
    g.setColour (Theme::outline.brighter (0.3f));
    g.drawRoundedRectangle (cap, 4.0f, 1.0f);
    g.setColour (slider.isMouseOverOrDragging() ? Theme::accent : Theme::text);
    g.fillRect (cap.getX() + 5.0f, cap.getCentreY() - 1.0f, cap.getWidth() - 10.0f, 2.0f);
}

juce::Label* OpenMixLookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* label = LookAndFeel_V4::createSliderTextBox (slider);
    label->setFont (Theme::mono (12.0f));
    label->setColour (juce::Label::textColourId, Theme::textDim);
    label->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    label->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    return label;
}

juce::Font OpenMixLookAndFeel::getLabelFont (juce::Label& label)
{
    return label.getFont();
}

juce::Font OpenMixLookAndFeel::getPopupMenuFont()
{
    return Theme::ui (14.0f);
}

void OpenMixLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    const auto area = juce::Rectangle<int> (width, height).toFloat();
    g.setColour (Theme::raised);
    g.fillRoundedRectangle (area, 4.0f);
    g.setColour (Theme::outline);
    g.drawRoundedRectangle (area.reduced (0.5f), 4.0f, 1.0f);
    g.setColour (Theme::text);
    g.setFont (Theme::ui (13.0f));
    g.drawFittedText (text, area.toNearestInt().reduced (8, 4), juce::Justification::centredLeft, 6);
}

juce::Rectangle<int> OpenMixLookAndFeel::getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                                           juce::Rectangle<int> parentArea)
{
    juce::AttributedString s;
    s.append (tipText, Theme::ui (13.0f));
    juce::TextLayout layout;
    layout.createLayout (s, 380.0f);
    const int w = (int) std::ceil (layout.getWidth()) + 18;
    const int h = (int) std::ceil (layout.getHeight()) + 10;

    return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                                 screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6,
                                 w, h).constrainedWithin (parentArea);
}
