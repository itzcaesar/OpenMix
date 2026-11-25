#include "ui/OpenMixLookAndFeel.h"
#include "ui/Theme.h"

OpenMixLookAndFeel::OpenMixLookAndFeel()
{
    applyPalette();
}

void OpenMixLookAndFeel::applyPalette()
{
    const auto& t = Theme::p();

    setColourScheme ({ t.window, t.panel, t.panel, t.controlEdge, t.text, t.text, t.text, t.raised, t.text });

    setColour (juce::ResizableWindow::backgroundColourId, t.window);
    setColour (juce::DocumentWindow::textColourId, t.text);

    setColour (juce::TextButton::buttonColourId, t.raised);
    setColour (juce::TextButton::buttonOnColourId, t.raised);
    setColour (juce::TextButton::textColourOffId, t.text);
    setColour (juce::TextButton::textColourOnId, t.text);

    setColour (juce::Slider::thumbColourId, t.raised);
    setColour (juce::Slider::trackColourId, t.textDim);
    setColour (juce::Slider::backgroundColourId, t.well);
    setColour (juce::Slider::rotarySliderFillColourId, t.text);
    setColour (juce::Slider::rotarySliderOutlineColourId, t.controlEdge);
    setColour (juce::Slider::textBoxTextColourId, t.textDim);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, t.text.withAlpha (0.25f));

    setColour (juce::Label::textColourId, t.text);
    setColour (juce::Label::textWhenEditingColourId, t.text);
    setColour (juce::Label::backgroundWhenEditingColourId, t.selBg);
    setColour (juce::Label::outlineWhenEditingColourId, t.focus);
    setColour (juce::TextEditor::backgroundColourId, t.selBg);
    setColour (juce::TextEditor::textColourId, t.text);
    setColour (juce::TextEditor::outlineColourId, t.controlEdge);
    setColour (juce::TextEditor::focusedOutlineColourId, t.focus);
    setColour (juce::TextEditor::highlightColourId, t.text.withAlpha (0.25f));
    setColour (juce::TextEditor::highlightedTextColourId, t.text);
    setColour (juce::CaretComponent::caretColourId, t.text);

    setColour (juce::PopupMenu::backgroundColourId, t.raised);
    setColour (juce::PopupMenu::textColourId, t.text);
    setColour (juce::PopupMenu::headerTextColourId, t.textFaint);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, t.text.withAlpha (t.dark ? 0.14f : 0.10f));
    setColour (juce::PopupMenu::highlightedTextColourId, t.text);

    setColour (juce::ComboBox::backgroundColourId, t.selBg);
    setColour (juce::ComboBox::textColourId, t.text);
    setColour (juce::ComboBox::outlineColourId, t.controlEdge);
    setColour (juce::ComboBox::arrowColourId, t.textDim);
    setColour (juce::ComboBox::focusedOutlineColourId, t.focus);
    setColour (juce::ListBox::backgroundColourId, t.selBg);
    setColour (juce::ListBox::textColourId, t.text);
    setColour (juce::ListBox::outlineColourId, t.controlEdge);
    setColour (juce::ScrollBar::thumbColourId, t.controlEdge);
    setColour (juce::TooltipWindow::backgroundColourId, t.raised);
    setColour (juce::TooltipWindow::textColourId, t.text);
    setColour (juce::TooltipWindow::outlineColourId, t.controlEdge);
    setColour (juce::ToggleButton::textColourId, t.text);
    setColour (juce::ToggleButton::tickColourId, t.text);
    setColour (juce::ToggleButton::tickDisabledColourId, t.textFaint);
    setColour (juce::AlertWindow::backgroundColourId, t.panel);
    setColour (juce::AlertWindow::textColourId, t.text);
    setColour (juce::AlertWindow::outlineColourId, t.controlEdge);
    setColour (juce::BubbleComponent::backgroundColourId, t.selBg);
    setColour (juce::BubbleComponent::outlineColourId, t.controlEdge);
    setColour (juce::GroupComponent::outlineColourId, t.line);
    setColour (juce::GroupComponent::textColourId, t.textDim);
}

//==============================================================================
// Custom window chrome (graphite title bar + crisp vector window buttons)

class OpenMixWindowButton : public juce::Button
{
public:
    OpenMixWindowButton (const juce::String& name, int type) : juce::Button (name), buttonType (type) {}

    void paintButton (juce::Graphics& g, bool isHighlighted, bool isDown) override
    {
        const auto& t = Theme::p();
        const auto r = getLocalBounds().toFloat();
        const bool isClose = (buttonType == juce::DocumentWindow::closeButton);

        if (isDown)
            g.fillAll (isClose ? t.mute.darker (0.2f) : t.raised.darker (0.12f));
        else if (isHighlighted)
            g.fillAll (isClose ? t.mute : (t.dark ? t.raised : t.well));

        const auto fg = (isClose && (isHighlighted || isDown)) ? t.onMute
                      : (isHighlighted || isDown) ? t.text : t.textDim;
        g.setColour (fg);

        const float s = juce::jmin (r.getWidth(), r.getHeight()) * 0.30f;
        const float cx = r.getCentreX(), cy = r.getCentreY();
        const float stroke = 1.35f;

        if (buttonType == juce::DocumentWindow::closeButton)
        {
            g.drawLine (cx - s, cy - s, cx + s, cy + s, stroke);
            g.drawLine (cx + s, cy - s, cx - s, cy + s, stroke);
        }
        else if (buttonType == juce::DocumentWindow::minimiseButton)
        {
            g.drawLine (cx - s, cy + 1.0f, cx + s, cy + 1.0f, stroke);
        }
        else
        {
            if (getToggleState())
            {
                const float d = s * 0.72f;
                g.drawRect (cx - d - 1.5f, cy - d + 1.5f, d * 2.0f, d * 2.0f, stroke);
                g.drawLine (cx - d + 1.5f, cy - d - 1.5f, cx + d + 1.5f, cy - d - 1.5f, stroke);
                g.drawLine (cx + d + 1.5f, cy - d - 1.5f, cx + d + 1.5f, cy + d - 1.5f, stroke);
            }
            else
            {
                g.drawRect (cx - s, cy - s, s * 2.0f, s * 2.0f, stroke);
            }
        }
    }

private:
    int buttonType;
};

juce::Button* OpenMixLookAndFeel::createDocumentWindowButton (int buttonType)
{
    if (buttonType == juce::DocumentWindow::closeButton)    return new OpenMixWindowButton ("Close", buttonType);
    if (buttonType == juce::DocumentWindow::minimiseButton) return new OpenMixWindowButton ("Minimise", buttonType);
    if (buttonType == juce::DocumentWindow::maximiseButton) return new OpenMixWindowButton ("Maximise", buttonType);
    return nullptr;
}

void OpenMixLookAndFeel::positionDocumentWindowButtons (juce::DocumentWindow&,
                                                        int titleBarX, int titleBarY, int titleBarW, int titleBarH,
                                                        juce::Button* minimiseButton, juce::Button* maximiseButton,
                                                        juce::Button* closeButton, bool onLeft)
{
    const int btnW = juce::jmax (34, (int) (titleBarH * 1.35f));
    int x = onLeft ? titleBarX : titleBarX + titleBarW - btnW;
    if (closeButton != nullptr)
    {
        closeButton->setBounds (x, titleBarY, btnW, titleBarH);
        x += onLeft ? btnW : -btnW;
    }
    if (onLeft)
        std::swap (minimiseButton, maximiseButton);
    if (maximiseButton != nullptr)
    {
        maximiseButton->setBounds (x, titleBarY, btnW, titleBarH);
        x += onLeft ? btnW : -btnW;
    }
    if (minimiseButton != nullptr)
        minimiseButton->setBounds (x, titleBarY, btnW, titleBarH);
}

void OpenMixLookAndFeel::drawDocumentWindowTitleBar (juce::DocumentWindow& window, juce::Graphics& g,
                                                     int w, int h, int titleSpaceX, int titleSpaceW,
                                                     const juce::Image* icon, bool drawTitleTextOnLeft)
{
    if (w * h == 0)
        return;

    const auto& t = Theme::p();
    const auto top = t.dark ? juce::Colour (0xff303035) : juce::Colour (0xffe6e6e8);
    g.setGradientFill (juce::ColourGradient::vertical (top, 0.0f, t.panel, (float) h));
    g.fillRect (0, 0, w, h);
    g.setColour (t.line);
    g.fillRect (0, h - 1, w, 1);

    const auto font = Theme::ui (juce::jlimit (12.0f, 13.5f, (float) h * 0.46f), true);
    g.setFont (font);

    int iconW = 0;
    const int iconSize = juce::jlimit (14, 18, h - 10);
    if (icon != nullptr && icon->isValid())
        iconW = iconSize + 8;

    const int textW = juce::jmin (titleSpaceW, juce::GlyphArrangement::getStringWidthInt (font, window.getName()) + iconW);
    int textX = drawTitleTextOnLeft ? juce::jmax (10, titleSpaceX)
                                    : juce::jmax (titleSpaceX, (w - textW) / 2);
    if (textX + textW > titleSpaceX + titleSpaceW)
        textX = titleSpaceX + titleSpaceW - textW;

    const bool active = window.isActiveWindow();
    if (icon != nullptr && icon->isValid())
    {
        g.setOpacity (active ? 1.0f : 0.65f);
        g.drawImageWithin (*icon, textX, (h - iconSize) / 2, iconSize, iconSize,
                           juce::RectanglePlacement::centred, false);
        g.setOpacity (1.0f);
        textX += iconW;
    }

    g.setColour (active ? t.text : t.textDim);
    g.drawText (window.getName(), textX, 0, juce::jmax (0, titleSpaceX + titleSpaceW - textX), h,
                juce::Justification::centredLeft, true);
}

//==============================================================================
// Buttons

void OpenMixLookAndFeel::drawLamp (juce::Graphics& g, juce::Rectangle<float> area, bool on, juce::Colour lampColour)
{
    const auto& t = Theme::p();
    if (on)
    {
        g.setColour (lampColour);
        g.fillRoundedRectangle (area, 1.5f);
    }
    else
    {
        g.setColour (t.dark ? t.well : t.panel);
        g.fillRoundedRectangle (area, 1.5f);
    }
    // The outline is what makes the lamp's shape readable on the light panel (amber is 1.9:1 there).
    g.setColour (on && t.dark ? lampColour.darker (0.4f) : t.controlEdge);
    g.drawRoundedRectangle (area.reduced (0.5f), 1.5f, 1.0f);
}

juce::Font OpenMixLookAndFeel::getTextButtonFont (juce::TextButton& button, int buttonHeight)
{
    if (button.getProperties()["micro"])
        return Theme::mono (10.5f);
    return Theme::ui (juce::jlimit (12.0f, 13.5f, (float) buttonHeight * 0.5f), true);
}

void OpenMixLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                               bool isHighlighted, bool isDown)
{
    const auto& t = Theme::p();
    const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    const auto lamp = button.getProperties()["lamp"].toString();
    const bool on = button.getToggleState();

    juce::Colour fill = t.raised;
    juce::Colour edge = t.controlEdge;

    // Mode buttons (segmented switch) light the whole key in amber when selected.
    if (lamp == "key" && on)
    {
        fill = t.accent;
        edge = t.accentEdge;
    }

    if (isDown)               fill = fill.darker (0.12f);
    else if (isHighlighted)   fill = t.dark ? fill.brighter (0.10f) : fill.darker (0.04f);
    if (! button.isEnabled()) fill = fill.withMultipliedAlpha (0.5f);

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, Theme::controlRadius);
    g.setColour (edge);
    g.drawRoundedRectangle (bounds, Theme::controlRadius, 1.0f);

    if (lamp == "accent" || lamp == "mute")
    {
        const auto lampArea = juce::Rectangle<float> (8.0f, 8.0f)
                                  .withCentre ({ bounds.getX() + 11.0f, bounds.getCentreY() });
        drawLamp (g, lampArea, on, lamp == "mute" ? t.mute : t.accent);
    }

    if (button.hasKeyboardFocus (false))
        Theme::drawFocusRing (g, bounds.expanded (0.5f));
}

void OpenMixLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    const auto& t = Theme::p();
    const auto lamp = button.getProperties()["lamp"].toString();
    const bool on = button.getToggleState();

    auto area = button.getLocalBounds();
    if (lamp == "accent" || lamp == "mute")
        area.removeFromLeft (16);   // room for the lamp

    g.setFont (getTextButtonFont (button, button.getHeight()));
    g.setColour (lamp == "key" && on ? t.onAccent
                 : (lamp.isNotEmpty() && ! on) ? t.textDim : t.text);
    if (! button.isEnabled())
        g.setColour (t.textFaint);

    g.drawFittedText (button.getButtonText(), area.reduced (4, 0), juce::Justification::centred, 1);
}

void OpenMixLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool isHighlighted, bool isDown)
{
    const auto& t = Theme::p();
    const auto box = juce::Rectangle<float> (16.0f, 16.0f).withCentre ({ 10.0f, (float) button.getHeight() * 0.5f });

    g.setColour (isHighlighted ? t.raised.brighter (t.dark ? 0.1f : 0.0f) : t.raised);
    g.fillRoundedRectangle (box, Theme::controlRadius);
    g.setColour (t.controlEdge);
    g.drawRoundedRectangle (box.reduced (0.5f), Theme::controlRadius, 1.0f);

    if (button.getToggleState())
    {
        juce::Path tick;
        tick.startNewSubPath (box.getX() + 3.5f, box.getCentreY());
        tick.lineTo (box.getX() + 6.8f, box.getBottom() - 4.0f);
        tick.lineTo (box.getRight() - 3.5f, box.getY() + 4.0f);
        g.setColour (t.text);
        g.strokePath (tick, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    g.setColour (button.isEnabled() ? t.text : t.textFaint);
    g.setFont (Theme::ui (13.5f));
    g.drawFittedText (button.getButtonText(), button.getLocalBounds().withTrimmedLeft (26), juce::Justification::centredLeft, 2);

    if (button.hasKeyboardFocus (false))
        Theme::drawFocusRing (g, box.expanded (3.0f));

    juce::ignoreUnused (isDown);
}

void OpenMixLookAndFeel::drawTickBox (juce::Graphics& g, juce::Component&, float x, float y, float w, float h,
                                      bool ticked, bool isEnabled, bool shouldDrawButtonAsHighlighted, bool)
{
    const auto& t = Theme::p();
    const auto box = juce::Rectangle<float> (x, y, w, h).withSizeKeepingCentre (juce::jmin (w, 16.0f), juce::jmin (h, 16.0f));

    auto fill = t.raised;
    if (shouldDrawButtonAsHighlighted)
        fill = t.dark ? fill.brighter (0.10f) : fill.darker (0.04f);
    if (! isEnabled)
        fill = fill.withMultipliedAlpha (0.5f);

    g.setColour (fill);
    g.fillRoundedRectangle (box, Theme::controlRadius);
    g.setColour (ticked ? t.accentEdge : t.controlEdge);
    g.drawRoundedRectangle (box.reduced (0.5f), Theme::controlRadius, 1.0f);

    if (ticked)
    {
        juce::Path tick;
        tick.startNewSubPath (box.getX() + 3.5f, box.getCentreY());
        tick.lineTo (box.getX() + 6.8f, box.getBottom() - 4.0f);
        tick.lineTo (box.getRight() - 3.5f, box.getY() + 4.0f);
        g.setColour (t.accent);
        g.strokePath (tick, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

void OpenMixLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool isButtonDown,
                                       int, int, int, int, juce::ComboBox& box)
{
    const auto& t = Theme::p();
    const auto r = juce::Rectangle<int> (width, height).toFloat().reduced (0.5f);

    auto fill = t.selBg;
    if (isButtonDown)              fill = fill.darker (0.10f);
    else if (box.isMouseOver())    fill = t.dark ? fill.brighter (0.08f) : fill.darker (0.03f);
    g.setColour (fill);
    g.fillRoundedRectangle (r, Theme::controlRadius);
    g.setColour (box.hasKeyboardFocus (false) ? t.focus : t.controlEdge);
    g.drawRoundedRectangle (r, Theme::controlRadius, box.hasKeyboardFocus (false) ? 1.8f : 1.0f);

    const auto chev = r.withLeft (r.getRight() - 22.0f).withSizeKeepingCentre (8.0f, 4.5f);
    juce::Path p;
    p.startNewSubPath (chev.getX(), chev.getY());
    p.lineTo (chev.getCentreX(), chev.getBottom());
    p.lineTo (chev.getRight(), chev.getY());
    g.setColour (box.isEnabled() ? t.textDim : t.textFaint);
    g.strokePath (p, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Font OpenMixLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return Theme::ui (13.0f);
}

void OpenMixLookAndFeel::drawAlertBox (juce::Graphics& g, juce::AlertWindow& alert,
                                       const juce::Rectangle<int>& textArea, juce::TextLayout& textLayout)
{
    const auto& t = Theme::p();
    const auto bounds = alert.getLocalBounds().toFloat();
    g.setColour (t.panel);
    g.fillRoundedRectangle (bounds, Theme::panelRadius);
    g.setColour (t.controlEdge);
    g.drawRoundedRectangle (bounds.reduced (0.5f), Theme::panelRadius, 1.0f);

    if (alert.getAlertType() != juce::MessageBoxIconType::NoIcon)
    {
        const auto barCol = alert.getAlertType() == juce::MessageBoxIconType::WarningIcon ? t.mute : t.accent;
        g.setColour (barCol);
        g.fillRect (bounds.getX() + 1.0f, bounds.getY() + 8.0f, 3.0f, bounds.getHeight() - 16.0f);
    }

    g.setColour (t.text);
    textLayout.draw (g, textArea.toFloat());
}

int OpenMixLookAndFeel::getAlertWindowButtonHeight()  { return 30; }
juce::Font OpenMixLookAndFeel::getAlertWindowTitleFont()   { return Theme::ui (15.0f, true); }
juce::Font OpenMixLookAndFeel::getAlertWindowMessageFont() { return Theme::ui (13.0f); }
juce::Font OpenMixLookAndFeel::getAlertWindowFont()        { return Theme::ui (13.0f); }

int OpenMixLookAndFeel::getDefaultScrollbarWidth() { return 8; }

void OpenMixLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height,
                                        bool isScrollbarVertical, int thumbStartPosition, int thumbSize,
                                        bool isMouseOver, bool isMouseDown)
{
    const auto& t = Theme::p();
    const auto track = juce::Rectangle<int> (x, y, width, height).toFloat();
    g.setColour (t.well.withAlpha (0.45f));
    g.fillRoundedRectangle (track.reduced (1.0f), 3.0f);

    const auto thumb = isScrollbarVertical
        ? juce::Rectangle<int> (x, thumbStartPosition, width, thumbSize).toFloat().reduced (1.5f, 1.0f)
        : juce::Rectangle<int> (thumbStartPosition, y, thumbSize, height).toFloat().reduced (1.0f, 1.5f);

    if (thumb.isEmpty())
        return;

    g.setColour (isMouseDown ? t.accent : isMouseOver ? t.textDim : t.controlEdge);
    g.fillRoundedRectangle (thumb, 3.0f);
}

void OpenMixLookAndFeel::drawBubble (juce::Graphics& g, juce::BubbleComponent&,
                                     const juce::Point<float>&, const juce::Rectangle<float>& body)
{
    const auto& t = Theme::p();
    g.setColour (t.selBg);
    g.fillRoundedRectangle (body, Theme::controlRadius);
    g.setColour (t.controlEdge);
    g.drawRoundedRectangle (body.reduced (0.5f), Theme::controlRadius, 1.0f);
}

void OpenMixLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    const auto& t = Theme::p();
    g.fillAll (t.raised);
    g.setColour (t.controlEdge);
    g.drawRect (0, 0, width, height, 1);
}

void OpenMixLookAndFeel::drawPopupMenuSectionHeader (juce::Graphics& g, const juce::Rectangle<int>& area,
                                                     const juce::String& sectionName)
{
    const auto& t = Theme::p();
    g.setFont (Theme::ui (10.5f, true).withExtraKerningFactor (0.08f));
    g.setColour (t.textFaint);
    g.drawText (sectionName.toUpperCase(), area.reduced (10, 0), juce::Justification::centredLeft, true);
    g.setColour (t.line);
    g.fillRect (area.getX() + 8, area.getBottom() - 1, area.getWidth() - 16, 1);
}

void OpenMixLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area,
                                            bool isSeparator, bool isActive, bool isHighlighted, bool isTicked,
                                            bool hasSubMenu, const juce::String& text, const juce::String& shortcutKeyText,
                                            const juce::Drawable* icon, const juce::Colour* textColour)
{
    const auto& t = Theme::p();
    if (isSeparator)
    {
        g.setColour (t.line);
        g.fillRect (area.getX() + 8, area.getCentreY(), area.getWidth() - 16, 1);
        return;
    }

    if (isHighlighted && isActive)
    {
        g.setColour (t.accent);
        g.fillRoundedRectangle (area.reduced (4, 1).toFloat(), Theme::controlRadius);
    }

    const auto fg = (isHighlighted && isActive) ? t.onAccent
                  : (textColour != nullptr ? *textColour : t.text).withMultipliedAlpha (isActive ? 1.0f : 0.45f);

    auto r = area.reduced (10, 0);
    auto iconArea = r.removeFromLeft (18).toFloat();
    if (icon != nullptr)
    {
        icon->drawWithin (g, iconArea, juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize, 1.0f);
    }
    else if (isTicked)
    {
        juce::Path tick;
        const float cx = iconArea.getCentreX(), cy = iconArea.getCentreY();
        tick.startNewSubPath (cx - 4.0f, cy);
        tick.lineTo (cx - 1.0f, cy + 3.0f);
        tick.lineTo (cx + 4.5f, cy - 3.5f);
        g.setColour (isHighlighted && isActive ? t.onAccent : t.accent);
        g.strokePath (tick, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    if (hasSubMenu)
    {
        auto chev = r.removeFromRight (10).toFloat().withSizeKeepingCentre (5.0f, 8.0f);
        juce::Path p;
        p.startNewSubPath (chev.getX(), chev.getY());
        p.lineTo (chev.getRight(), chev.getCentreY());
        p.lineTo (chev.getX(), chev.getBottom());
        g.setColour (fg);
        g.strokePath (p, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    if (shortcutKeyText.isNotEmpty())
    {
        g.setFont (Theme::mono (11.0f));
        g.setColour (fg.withAlpha (0.7f));
        g.drawText (shortcutKeyText, r.removeFromRight (juce::GlyphArrangement::getStringWidthInt (Theme::mono (11.0f), shortcutKeyText) + 8),
                    juce::Justification::centredRight, false);
    }

    g.setFont (Theme::ui (13.5f));
    g.setColour (fg);
    g.drawFittedText (text, r, juce::Justification::centredLeft, 1);
}

void OpenMixLookAndFeel::getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardMenuItemHeight,
                                                    int& idealWidth, int& idealHeight)
{
    if (isSeparator)
    {
        idealWidth = 60;
        idealHeight = 8;
        return;
    }

    const auto font = getPopupMenuFont();
    idealHeight = standardMenuItemHeight > 0 ? juce::jmax (24, standardMenuItemHeight) : 26;
    idealWidth = juce::GlyphArrangement::getStringWidthInt (font, text) + 52;
}

//==============================================================================
// Knob: raised cap with printed tick scale, pointer line.

void OpenMixLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                           float pos, float startAngle, float endAngle, juce::Slider& slider)
{
    const auto& t = Theme::p();
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (2.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();

    // Printed scale: 11 ticks, the centre one longer (0 dB on a bipolar control)
    const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
    for (int i = 0; i <= 10; ++i)
    {
        const float a = startAngle + (endAngle - startAngle) * (float) i / 10.0f;
        const bool major = i == 0 || i == 10 || (bipolar && i == 5);
        const auto outer = centre.getPointOnCircumference (radius, a);
        const auto inner = centre.getPointOnCircumference (radius - (major ? 5.0f : 3.0f), a);
        g.setColour (major ? t.textDim : t.textFaint);
        g.drawLine ({ inner, outer }, 1.0f);
    }

    // Cap (raised: soft shadow + top-lit body)
    const float capR = radius - 7.0f;
    const auto cap = juce::Rectangle<float> (capR * 2.0f, capR * 2.0f).withCentre (centre);
    g.setColour (juce::Colours::black.withAlpha (t.dark ? 0.45f : 0.18f));
    g.fillEllipse (cap.translated (0.0f, 1.5f));
    const auto top = t.dark ? juce::Colour (0xff4a4a4a) : juce::Colour (0xffffffff);
    const auto bottom = t.dark ? juce::Colour (0xff2a2a2a) : juce::Colour (0xffdedbd4);
    g.setGradientFill (juce::ColourGradient (top, cap.getCentreX(), cap.getY(), bottom, cap.getCentreX(), cap.getBottom(), false));
    g.fillEllipse (cap);
    g.setColour (t.controlEdge);
    g.drawEllipse (cap.reduced (0.5f), 1.0f);

    // Pointer
    const float angle = startAngle + pos * (endAngle - startAngle);
    g.setColour (t.text);
    g.drawLine ({ centre.getPointOnCircumference (capR * 0.3f, angle), centre.getPointOnCircumference (capR * 0.85f, angle) }, 2.0f);

    if (slider.hasKeyboardFocus (false))
    {
        g.setColour (t.focus);
        g.drawEllipse (cap.expanded (3.0f), 2.0f);
    }
}

//==============================================================================
// Fader: recessed slot, raised cap with a grip line.

void OpenMixLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                           float sliderPos, float minSliderPos, float maxSliderPos,
                                           juce::Slider::SliderStyle style, juce::Slider& slider)
{
    const auto& t = Theme::p();
    const auto area = juce::Rectangle<int> (x, y, width, height).toFloat();

    if (style == juce::Slider::LinearHorizontal)
    {
        const float cy = area.getCentreY();
        const auto track = juce::Rectangle<float> (area.getX(), cy - 3.0f, area.getWidth(), 6.0f);
        g.setColour (t.well);
        g.fillRoundedRectangle (track, 2.5f);
        g.setColour (t.line);
        g.drawRoundedRectangle (track.reduced (0.5f), 2.5f, 1.0f);

        const float capW = 12.0f, capH = juce::jmin (18.0f, area.getHeight() - 2.0f);
        const float cx = juce::jlimit (track.getX() + capW * 0.5f, track.getRight() - capW * 0.5f, sliderPos);
        const auto cap = juce::Rectangle<float> (capW, capH).withCentre ({ cx, cy });
        g.setColour (juce::Colours::black.withAlpha (t.dark ? 0.45f : 0.18f));
        g.fillRoundedRectangle (cap.translated (0.0f, 1.5f), Theme::controlRadius);
        const auto top = t.dark ? t.raised.brighter (0.16f) : juce::Colours::white;
        const auto bottom = t.dark ? t.raised.darker (0.06f) : juce::Colour (0xffdcdce0);
        g.setGradientFill (juce::ColourGradient (top, cap.getX(), cap.getY(), bottom, cap.getX(), cap.getBottom(), false));
        g.fillRoundedRectangle (cap, Theme::controlRadius);
        g.setColour (t.controlEdge);
        g.drawRoundedRectangle (cap.reduced (0.5f), Theme::controlRadius, 1.0f);

        if (slider.hasKeyboardFocus (false))
            Theme::drawFocusRing (g, cap.expanded (2.5f), Theme::controlRadius + 1.5f);
        return;
    }

    if (style != juce::Slider::LinearVertical)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
        return;
    }

    const float cx = area.getCentreX();

    // Recessed fader well + subtle ticks + centre groove (mockup style)
    g.setColour (t.well);
    g.fillRoundedRectangle (area.reduced (1.0f, 0.0f), Theme::controlRadius);
    g.setColour (t.line);
    g.drawRoundedRectangle (area.reduced (1.5f, 0.5f), Theme::controlRadius, 1.0f);

    g.setColour (t.line.withAlpha (0.55f));
    for (float ty = area.getY() + 10.0f; ty < area.getBottom() - 8.0f; ty += 16.0f)
        g.fillRect (area.getX() + 5.0f, ty, area.getWidth() - 10.0f, 1.0f);

    const auto slot = juce::Rectangle<float> (cx - 2.0f, area.getY() + 6.0f, 4.0f, juce::jmax (4.0f, area.getHeight() - 12.0f));
    g.setColour (t.meterWell.withAlpha (t.dark ? 0.75f : 0.35f));
    g.fillRoundedRectangle (slot, 2.0f);

    // Sculpted cap with grip grooves + bright centre index line
    const float capW = juce::jmin (36.0f, area.getWidth() - 6.0f), capH = 22.0f;
    const auto cap = juce::Rectangle<float> (capW, capH).withCentre ({ cx, sliderPos });
    g.setColour (juce::Colours::black.withAlpha (t.dark ? 0.55f : 0.22f));
    g.fillRoundedRectangle (cap.translated (0.0f, 2.0f), Theme::controlRadius);

    const auto top = t.dark ? t.raised.brighter (0.18f) : juce::Colours::white;
    const auto bottom = t.dark ? t.raised.darker (0.08f) : juce::Colour (0xffd9d9de);
    g.setGradientFill (juce::ColourGradient (top, cap.getX(), cap.getY(), bottom, cap.getX(), cap.getBottom(), false));
    g.fillRoundedRectangle (cap, Theme::controlRadius);
    g.setColour (t.controlEdge);
    g.drawRoundedRectangle (cap.reduced (0.5f), Theme::controlRadius, 1.0f);

    g.setColour (t.controlEdge.withAlpha (0.55f));
    g.fillRect (cap.getX() + 5.0f, cap.getY() + 5.0f, cap.getWidth() - 10.0f, 1.0f);
    g.fillRect (cap.getX() + 5.0f, cap.getBottom() - 6.0f, cap.getWidth() - 10.0f, 1.0f);
    g.setColour (t.text);
    g.fillRect (cap.getX() + 4.0f, cap.getCentreY() - 1.0f, cap.getWidth() - 8.0f, 2.0f);

    if (slider.hasKeyboardFocus (false))
        Theme::drawFocusRing (g, cap.expanded (3.0f), Theme::controlRadius + 2.0f);
}

juce::Label* OpenMixLookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* label = LookAndFeel_V4::createSliderTextBox (slider);
    label->setFont (Theme::mono (12.0f));
    label->setColour (juce::Label::textColourId, Theme::p().text);
    label->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    label->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    return label;
}

void OpenMixLookAndFeel::drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics& g, const juce::Path& path, juce::Image&)
{
    // Popover panels: a raised plate with a real edge (it sits above the desk, so it gets the shadow).
    const auto& t = Theme::p();
    juce::DropShadow (juce::Colours::black.withAlpha (t.dark ? 0.5f : 0.25f), 12, { 0, 3 }).drawForPath (g, path);
    g.setColour (t.panel);
    g.fillPath (path);
    g.setColour (t.controlEdge);
    g.strokePath (path, juce::PathStrokeType (1.0f));
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
    const auto& t = Theme::p();
    const auto area = juce::Rectangle<int> (width, height).toFloat();
    g.setColour (t.raised);
    g.fillRoundedRectangle (area, Theme::controlRadius);
    g.setColour (t.controlEdge);
    g.drawRoundedRectangle (area.reduced (0.5f), Theme::controlRadius, 1.0f);
    g.setColour (t.text);
    g.setFont (Theme::ui (13.0f));
    g.drawFittedText (text, area.toNearestInt().reduced (8, 4), juce::Justification::centredLeft, 6);
}

juce::Rectangle<int> OpenMixLookAndFeel::getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                                           juce::Rectangle<int> parentArea)
{
    juce::AttributedString s;
    s.append (tipText, Theme::ui (13.0f));
    juce::TextLayout layout;
    layout.createLayout (s, 360.0f);
    const int w = (int) std::ceil (layout.getWidth()) + 18;
    const int h = (int) std::ceil (layout.getHeight()) + 10;

    return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                                 screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6,
                                 w, h).constrainedWithin (parentArea);
}
