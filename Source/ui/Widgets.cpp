#include "Widgets.h"
#include "../Theme.h"
#include "../OpenMixLookAndFeel.h"

void makeLamp (juce::TextButton& b, const char* kind)
{
    b.getProperties().set ("lamp", kind);
    b.setClickingTogglesState (std::strcmp (kind, "key") != 0);
    makeClickable (b);
}

void makeClickable (juce::Component& c)
{
    c.setMouseCursor (juce::MouseCursor::PointingHandCursor);

    // Focus rings are for keyboard users: a mouse click shouldn't leave one behind.
    c.setMouseClickGrabsKeyboardFocus (false);
}

//==============================================================================
// LevelMeter

LevelMeter::LevelMeter (std::atomic<float>& l, std::atomic<float>& r) : peakL (l), peakR (r)
{
    setWantsKeyboardFocus (true);
    makeClickable (*this);
    setTitle ("Level meter");
    setTooltip ("Peak meter. The top lamp latches red when the signal clips; click it or press Enter to reset.");
}

static float meterProportion (float db) noexcept
{
    return std::pow (juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f), 1.6f);   // -60 .. +6 dB
}

void LevelMeter::update (Channel& c, float peak)
{
    c.level = juce::jmax (peak, c.level * 0.82f);
    if (peak >= c.hold)
    {
        c.hold = peak;
        c.holdFrames = 45;
    }
    else if (--c.holdFrames <= 0)
    {
        c.hold *= 0.9f;
    }

    if (peak >= 1.0f)
        clipped = true;
}

void LevelMeter::tick()
{
    update (left, peakL.exchange (0.0f));
    update (right, peakR.exchange (0.0f));
    repaint();
}

bool LevelMeter::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::returnKey || k == juce::KeyPress::spaceKey)
    {
        resetClip();
        return true;
    }
    return false;
}

void LevelMeter::drawLadder (juce::Graphics& g, juce::Rectangle<float> r, const Channel& c)
{
    const auto& t = Theme::p();
    const float segH = 3.0f, gap = 1.0f;
    const int numSegs = juce::jmax (1, (int) ((r.getHeight() + gap) / (segH + gap)));
    const float litProp = meterProportion (juce::Decibels::gainToDecibels (c.level, -60.0f));
    const float holdProp = meterProportion (juce::Decibels::gainToDecibels (c.hold, -60.0f));
    const int holdSeg = c.hold > 0.001f ? (int) (holdProp * (float) numSegs) : -1;

    for (int i = 0; i < numSegs; ++i)
    {
        const float prop = (float) (i + 1) / (float) numSegs;
        const float db = -60.0f + 66.0f * std::pow (prop, 1.0f / 1.6f);
        const auto colour = db > -0.5f ? t.meterHigh : db > -9.0f ? t.meterMid : t.meterLow;
        const bool lit = prop <= litProp || i == holdSeg;

        const float y = r.getBottom() - (float) (i + 1) * (segH + gap) + gap;
        g.setColour (lit ? colour : colour.withAlpha (0.12f));
        g.fillRect (r.getX(), y, r.getWidth(), segH);
    }
}

void LevelMeter::paint (juce::Graphics& g)
{
    const auto& t = Theme::p();
    auto area = getLocalBounds().toFloat();

    g.setColour (t.meterWell);
    g.fillRoundedRectangle (area, 2.0f);
    area.reduce (2.0f, 2.0f);

    // Clip lamp
    auto lamp = area.removeFromTop (6.0f);
    g.setColour (clipped ? t.meterHigh : t.meterHigh.withAlpha (0.15f));
    g.fillRect (lamp);
    area.removeFromTop (3.0f);

    const float w = (area.getWidth() - 2.0f) / 2.0f;
    drawLadder (g, area.removeFromLeft (w), left);
    area.removeFromLeft (2.0f);
    drawLadder (g, area, right);

    if (hasKeyboardFocus (false))
        Theme::drawFocusRing (g, getLocalBounds().toFloat(), 2.0f);
}

//==============================================================================
// DbSlider

bool DbSlider::keyPressed (const juce::KeyPress& k)
{
    const bool fine = k.getModifiers().isShiftDown();
    double delta = 0.0;

    if (k.isKeyCode (juce::KeyPress::upKey) || k.isKeyCode (juce::KeyPress::rightKey))         delta = fine ? keyStep * 0.1 : keyStep;
    else if (k.isKeyCode (juce::KeyPress::downKey) || k.isKeyCode (juce::KeyPress::leftKey))   delta = fine ? -keyStep * 0.1 : -keyStep;
    else if (k.isKeyCode (juce::KeyPress::pageUpKey))                                          delta = pageStep;
    else if (k.isKeyCode (juce::KeyPress::pageDownKey))                                        delta = -pageStep;
    else if (k.isKeyCode (juce::KeyPress::homeKey))
    {
        setValue (homeValue, juce::sendNotificationSync);
        return true;
    }
    else
        return juce::Slider::keyPressed (k);

    setValue (getValue() + delta, juce::sendNotificationSync);
    return true;
}

//==============================================================================
// FaderSection

FaderSection::FaderSection (std::atomic<float>& peakL, std::atomic<float>& peakR, const juce::String& ownerName)
    : meter (peakL, peakR)
{
    fader.setRange (-60.0, 12.0, 0.1);
    fader.setSkewFactorFromMidPoint (-12.0);
    fader.setDoubleClickReturnValue (true, 0.0);
    fader.setTextBoxStyle (juce::Slider::TextBoxAbove, false, 64, 20);
    fader.textFromValueFunction = [] (double v) { return v <= -60.0 ? juce::String ("-inf") : (v > 0 ? "+" : "") + juce::String (v, 1); };
    fader.valueFromTextFunction = [] (const juce::String& s) { return s.trim().startsWithIgnoreCase ("-inf") ? -60.0 : s.getDoubleValue(); };
    fader.setTitle (ownerName + " fader");
    fader.setTooltip ("Level in dB. Drag, scroll or use the arrow keys (Shift for fine steps, Page Up/Down for 6 dB). "
                      "Double-click or press Home for 0 dB. Click the number to type a value.");
    fader.setWantsKeyboardFocus (true);
    makeClickable (fader);
    addAndMakeVisible (fader);

    meter.setTitle (ownerName + " meter");
    addAndMakeVisible (meter);
}

void FaderSection::resized()
{
    auto r = getLocalBounds();
    const int scaleW = 30, meterW = 16, gap = 6;
    auto content = r.withSizeKeepingCentre (juce::jmin (r.getWidth(), scaleW + 46 + gap + meterW), r.getHeight());

    content.removeFromLeft (scaleW);
    auto meterArea = content.removeFromRight (meterW);
    content.removeFromRight (gap);
    fader.setBounds (content);

    meterArea.removeFromTop (fader.getTextBoxHeight() + 4);
    meter.setBounds (meterArea);
}

void FaderSection::paint (juce::Graphics& g)
{
    const auto& t = Theme::p();
    const int scaleRight = fader.getX() - 3;
    const int zeroY = fader.getY() + (int) fader.getPositionOfValue (0.0);
    int lastLabelY = -100;

    g.setFont (Theme::mono (11.0f));
    for (double db : { 0.0, 12.0, 6.0, -6.0, -12.0, -24.0, -40.0, -60.0 })
    {
        const int y = fader.getY() + (int) fader.getPositionOfValue (db);
        g.setColour (db == 0.0 ? t.textDim : t.textFaint);
        g.fillRect (scaleRight - (db == 0.0 ? 8 : 5), y, db == 0.0 ? 8 : 5, 1);

        if (db != 0.0 && (std::abs (y - lastLabelY) < 14 || std::abs (y - zeroY) < 14))
            continue;
        if (db != 0.0)
            lastLabelY = y;

        const auto label = db <= -60.0 ? juce::String ("-inf") : (db > 0 ? "+" : "") + juce::String ((int) db);
        g.drawText (label, scaleRight - 38, y - 7, 29, 14, juce::Justification::centredRight);
    }
}

//==============================================================================
// TapeLabel

TapeLabel::TapeLabel (bool canEdit) : editable (canEdit)
{
    label.setFont (Theme::ui (14.5f, true));
    label.setJustificationType (juce::Justification::centredLeft);
    label.setInterceptsMouseClicks (false, false);
    label.onTextChange = [this] { if (onRename) onRename (label.getText().trim()); };
    addAndMakeVisible (label);

    if (editable)
    {
        label.setEditable (false, true, false);
        setWantsKeyboardFocus (true);
        makeClickable (*this);
        setTooltip ("Channel name. Double-click, or press Enter, to rename.");
    }
    applyColours();
}

void TapeLabel::applyColours()
{
    const auto& t = Theme::p();
    label.setColour (juce::Label::textColourId, t.tapeText);
    label.setColour (juce::Label::textWhenEditingColourId, t.tapeText);
    label.setColour (juce::Label::backgroundWhenEditingColourId, t.tape);
    label.setColour (juce::Label::outlineWhenEditingColourId, t.tapeText);
    label.setColour (juce::TextEditor::highlightColourId, t.tapeText.withAlpha (0.2f));
    label.setColour (juce::TextEditor::highlightedTextColourId, t.tapeText);
    label.setColour (juce::CaretComponent::caretColourId, t.tapeText);
}

void TapeLabel::setText (const juce::String& text)
{
    label.setText (text, juce::dontSendNotification);
    setTitle (text);
}

void TapeLabel::resized()
{
    const int badgeW = badges_.isEmpty() ? 0 : juce::GlyphArrangement::getStringWidthInt (Theme::ui (11.0f, true), badges_) + 10;
    label.setBounds (getLocalBounds().reduced (6, 2).withTrimmedRight (badgeW));
}

void TapeLabel::paint (juce::Graphics& g)
{
    const auto& t = Theme::p();
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    const auto tape = Theme::tapeColour (colourIndex_);
    g.setColour (tape);
    g.fillRoundedRectangle (r, 2.0f);
    label.setColour (juce::Label::backgroundWhenEditingColourId, tape);
    g.setColour (colourIndex_ > 0 && t.dark ? tape : t.tapeEdge);
    g.drawRoundedRectangle (r, 2.0f, 1.0f);

    // Status badges written on the tape (mono, polarity): real state, in marker text.
    if (badges_.isNotEmpty())
    {
        g.setColour (t.tapeText);
        g.setFont (Theme::ui (11.0f, true));
        g.drawText (badges_, getLocalBounds().reduced (8, 0), juce::Justification::centredRight);
    }

    if (hasKeyboardFocus (false))
        Theme::drawFocusRing (g, getLocalBounds().toFloat().expanded (0.0f), 3.0f);
}

bool TapeLabel::keyPressed (const juce::KeyPress& k)
{
    if (editable && (k == juce::KeyPress::returnKey || k == juce::KeyPress::F2Key))
    {
        label.showEditor();
        return true;
    }
    return false;
}

void TapeLabel::mouseDoubleClick (const juce::MouseEvent&)
{
    if (editable)
        label.showEditor();
}

//==============================================================================
// ChipButton

void ChipButton::setContent (const juce::String& newCaption, const juce::String& newText, bool isProblem)
{
    caption = newCaption;
    text = newText;
    problem = isProblem;
    setTooltip (caption + ": " + text + (problem ? ". Click to choose another." : ". Click to change."));
    setDescription (caption + ": " + text);
    repaint();
}

void ChipButton::paintButton (juce::Graphics& g, bool isHighlighted, bool isDown)
{
    const auto& t = Theme::p();
    auto r = getLocalBounds().toFloat().reduced (0.5f);

    auto fill = t.raised;
    if (isDown)             fill = fill.darker (0.12f);
    else if (isHighlighted) fill = t.dark ? fill.brighter (0.10f) : fill.darker (0.04f);
    g.setColour (fill);
    g.fillRoundedRectangle (r, Theme::controlRadius);
    g.setColour (problem ? t.mute : t.controlEdge);
    g.drawRoundedRectangle (r, Theme::controlRadius, problem ? 1.5f : 1.0f);

    auto content = getLocalBounds().reduced (8, 0);

    // Chevron (it opens a menu)
    auto chev = content.removeFromRight (10).toFloat().withSizeKeepingCentre (8.0f, 5.0f);
    juce::Path p;
    p.startNewSubPath (chev.getX(), chev.getY());
    p.lineTo (chev.getCentreX(), chev.getBottom());
    p.lineTo (chev.getRight(), chev.getY());
    g.setColour (t.textDim);
    g.strokePath (p, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    content.removeFromRight (6);

    if (caption.isNotEmpty())
    {
        const auto capFont = Theme::ui (11.0f, true);
        const int cw = juce::GlyphArrangement::getStringWidthInt (capFont, caption) + 2;
        g.setColour (t.textFaint);
        g.setFont (capFont);
        g.drawText (caption, content.removeFromLeft (cw), juce::Justification::centredLeft);
        content.removeFromLeft (6);
    }

    g.setColour (t.text);
    g.setFont (Theme::ui (13.0f));
    g.drawText (text, content, juce::Justification::centredLeft, true);

    if (hasKeyboardFocus (false))
        Theme::drawFocusRing (g, getLocalBounds().toFloat());
}

//==============================================================================
// StripKey

void StripKey::mouseUp (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() && onSecondary != nullptr)
    {
        setState (juce::Button::buttonNormal);
        onSecondary();
        return;
    }
    juce::TextButton::mouseUp (e);
}

bool StripKey::keyPressed (const juce::KeyPress& k)
{
    if (onSecondary != nullptr
        && (k.getKeyCode() == 'M' || k.getKeyCode() == 'm'
            || (k.isKeyCode (juce::KeyPress::returnKey) && k.getModifiers().isShiftDown())))
    {
        onSecondary();
        return true;
    }

    if (onSpace != nullptr && k.isKeyCode (juce::KeyPress::spaceKey))
    {
        onSpace();
        return true;
    }

    return juce::TextButton::keyPressed (k);
}

//==============================================================================
// MenuDotsButton

void MenuDotsButton::paintButton (juce::Graphics& g, bool isHighlighted, bool isDown)
{
    const auto& t = Theme::p();
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    auto fill = t.raised;
    if (isDown)             fill = fill.darker (0.12f);
    else if (isHighlighted) fill = t.dark ? fill.brighter (0.10f) : fill.darker (0.04f);
    g.setColour (fill);
    g.fillRoundedRectangle (r, Theme::controlRadius);
    g.setColour (t.controlEdge);
    g.drawRoundedRectangle (r, Theme::controlRadius, 1.0f);

    g.setColour (t.textDim);
    for (int i = -1; i <= 1; ++i)
        g.fillEllipse (r.getCentreX() + (float) i * 5.0f - 1.5f, r.getCentreY() - 1.5f, 3.0f, 3.0f);

    if (hasKeyboardFocus (false))
        Theme::drawFocusRing (g, getLocalBounds().toFloat());
}

//==============================================================================
// PluginSlotRow

PluginSlotRow::PluginSlotRow()
{
    setWantsKeyboardFocus (true);
    makeClickable (*this);
}

void PluginSlotRow::setState (State newState, const juce::String& newName, bool isBypassed, bool isFaulted,
                              int latency, double sampleRate)
{
    state = newState;
    name = newName;
    bypassed = isBypassed;
    faulted = isFaulted;
    latencyText = latency > 0 && sampleRate > 0 ? juce::String (1000.0 * latency / sampleRate, 1) + " ms" : juce::String();

    switch (state)
    {
        case State::Empty:
            setTitle ("Add plugin");
            setTooltip ("Add a VST3 plugin to this strip (Enter).");
            break;
        case State::Loading:
            setTitle ("Loading " + name);
            setTooltip ("Loading " + name + "...");
            break;
        case State::Loaded:
            setTitle (name + (faulted ? ", stopped" : bypassed ? ", bypassed" : ", on"));
            setTooltip (faulted ? name + " produced invalid audio and was stopped. Click the lamp (or press Space) to try it again."
                                : name + ". Click to open its window (Enter). Lamp: bypass (Space). Drag to reorder (Alt+Up/Down). "
                                  "Dots: more options (M). Delete removes it.");
            break;
    }
    repaint();
}

juce::Rectangle<float> PluginSlotRow::powerArea() const
{
    return juce::Rectangle<float> (9.0f, 9.0f).withCentre ({ 13.0f, (float) getHeight() * 0.5f });
}

PluginSlotRow::Zone PluginSlotRow::zoneAt (juce::Point<int> p) const
{
    if (state != State::Loaded)    return state == State::Empty ? Zone::Main : Zone::None;
    if (p.x < 26)                  return Zone::Power;
    if (p.x > getWidth() - 26)     return Zone::Menu;
    return Zone::Main;
}

void PluginSlotRow::mouseMove (const juce::MouseEvent& e)
{
    const auto z = zoneAt (e.getPosition());
    if (z != hover) { hover = z; repaint(); }
}

void PluginSlotRow::mouseExit (const juce::MouseEvent&)
{
    hover = Zone::None;
    repaint();
}

void PluginSlotRow::mouseDown (const juce::MouseEvent&)
{
    dragging = false;
}

void PluginSlotRow::mouseDrag (const juce::MouseEvent& e)
{
    if (state != State::Loaded || zoneAt (e.getMouseDownPosition()) != Zone::Main)
        return;

    if (! dragging && e.getDistanceFromDragStart() > 6)
        dragging = true;

    if (dragging && onDragTo)
        onDragTo (e.getScreenY());
}

void PluginSlotRow::mouseUp (const juce::MouseEvent& e)
{
    if (dragging)
    {
        dragging = false;
        if (onDragEnd) onDragEnd();
        return;
    }

    if (! getLocalBounds().contains (e.getPosition()))
        return;

    if (e.mods.isPopupMenu() && state == State::Loaded) { if (onMenu) onMenu(); return; }

    switch (zoneAt (e.getPosition()))
    {
        case Zone::Power: if (onBypass) onBypass(); break;
        case Zone::Menu:  if (onMenu) onMenu(); break;
        case Zone::Main:  if (onMain) onMain(); break;
        case Zone::None:  break;
    }
}

bool PluginSlotRow::keyPressed (const juce::KeyPress& k)
{
    if (state == State::Loading)
        return false;

    if (k == juce::KeyPress::returnKey)                          { if (onMain) onMain(); return true; }
    if (state != State::Loaded)                                  return false;

    if (k == juce::KeyPress::spaceKey)                           { if (onBypass) onBypass(); return true; }
    if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) { if (onRemove) onRemove(); return true; }
    if (k.getKeyCode() == 'M' || k.getKeyCode() == 'm' || k.getKeyCode() == juce::KeyPress::F10Key)
    {
        if (onMenu) onMenu();
        return true;
    }
    if (k.getModifiers().isAltDown() && k.isKeyCode (juce::KeyPress::upKey))   { if (onMove) onMove (-1); return true; }
    if (k.getModifiers().isAltDown() && k.isKeyCode (juce::KeyPress::downKey)) { if (onMove) onMove (1); return true; }
    return false;
}

void PluginSlotRow::paint (juce::Graphics& g)
{
    const auto& t = Theme::p();
    auto r = getLocalBounds().toFloat().reduced (0.5f);

    if (state == State::Empty)
    {
        // Empty slot: dashed outline, "+ Add plugin"
        const bool hot = hover != Zone::None;
        if (hot)
        {
            g.setColour (t.raised);
            g.fillRoundedRectangle (r, Theme::controlRadius);
        }
        juce::Path outline, dashed;
        outline.addRoundedRectangle (r, Theme::controlRadius);
        const float dashes[] { 4.0f, 3.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dashes, 2);
        g.setColour (t.controlEdge);
        g.fillPath (dashed);

        g.setColour (hot ? t.text : t.textDim);
        g.setFont (Theme::ui (13.0f));
        g.drawText ("+  Add plugin", r, juce::Justification::centred);
    }
    else
    {
        auto fill = t.raised;
        if (hover == Zone::Main || dragging) fill = t.dark ? fill.brighter (0.08f) : fill.darker (0.04f);
        g.setColour (fill);
        g.fillRoundedRectangle (r, Theme::controlRadius);
        g.setColour (faulted ? t.mute : t.controlEdge);
        g.drawRoundedRectangle (r, Theme::controlRadius, faulted ? 1.5f : 1.0f);

        auto nameArea = r.withTrimmedLeft (26.0f).withTrimmedRight (state == State::Loaded ? 26.0f : 6.0f);

        if (state == State::Loading)
        {
            g.setColour (t.textDim);
            g.setFont (Theme::ui (13.0f).italicised());
            g.drawText ("Loading " + name + "...", nameArea, juce::Justification::centredLeft, true);
        }
        else
        {
            // Lamp: lit = plugin on, dark = bypassed, red = stopped after invalid audio.
            OpenMixLookAndFeel::drawLamp (g, powerArea(), ! bypassed || faulted, faulted ? t.mute : t.accent);
            if (hover == Zone::Power)
            {
                g.setColour (t.text);
                g.drawRoundedRectangle (powerArea().expanded (2.5f), 2.0f, 1.0f);
            }

            if (latencyText.isNotEmpty() && ! faulted)
            {
                g.setFont (Theme::mono (11.0f));
                g.setColour (t.textFaint);
                g.drawText (latencyText, nameArea.removeFromRight (52.0f), juce::Justification::centredRight);
            }

            g.setColour (faulted || bypassed ? t.textDim : t.text);
            g.setFont (Theme::ui (13.0f));
            g.drawText (faulted ? name + " (stopped)" : name, nameArea, juce::Justification::centredLeft, true);

            // Menu dots
            const float mx = r.getRight() - 13.0f;
            g.setColour (hover == Zone::Menu ? t.text : t.textDim);
            for (int i = -1; i <= 1; ++i)
                g.fillEllipse (mx - 1.5f, r.getCentreY() + (float) i * 4.5f - 1.5f, 3.0f, 3.0f);
        }
    }

    if (hasKeyboardFocus (false))
        Theme::drawFocusRing (g, getLocalBounds().toFloat());
}
