#include "engine/VoicePad.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

//==============================================================================
// DSP

void VoicePadProcessor::prepare (double sampleRate, int)
{
    fs = sampleRate;
    smX.reset (sampleRate, 0.05);
    smY.reset (sampleRate, 0.05);
    smX.setCurrentAndTargetValue (params.x.load());
    smY.setCurrentAndTargetValue ((params.y.load() + 1.0f) * 0.5f);

    lowShelf.reset(); highShelf.reset(); presence.reset(); midPeak.reset();
    lastX = lastY = 99.0f;
    lastMode = -1;

    const auto maxDelay = (size_t) (sampleRate * 0.5);
    delayL.assign (maxDelay, 0.0f);
    delayR.assign (maxDelay, 0.0f);
    writePos = 0;
    delaySamplesL = (int) (sampleRate * 0.140);
    delaySamplesR = (int) (sampleRate * 0.165);
}

void VoicePadProcessor::reset() noexcept
{
    lowShelf.reset(); highShelf.reset(); presence.reset(); midPeak.reset();
    std::fill (delayL.begin(), delayL.end(), 0.0f);
    std::fill (delayR.begin(), delayR.end(), 0.0f);
    writePos = 0;
}

void VoicePadProcessor::updateFilters (float x, float y01) noexcept
{
    if (std::abs (x - lastX) < 1.0e-3f && std::abs (y01 - lastY) < 1.0e-3f)
        return;

    lastX = x; lastY = y01;
    lowShelf.setLowShelf  (fs, 250.0,  -x * 6.0);
    highShelf.setHighShelf (fs, 3000.0, x * 6.0);
    presence.setPeak       (fs, 5000.0, 0.9, y01 * 6.0);
}

// Color: bilinear blend of four voice characters (gains in dB for low shelf 200 Hz,
// mid bell 1.5 kHz, presence bell 4 kHz, high shelf 8 kHz).
void VoicePadProcessor::updateColor (float x, float y01) noexcept
{
    if (std::abs (x - lastX) < 1.0e-3f && std::abs (y01 - lastY) < 1.0e-3f)
        return;
    lastX = x; lastY = y01;

    struct Character { float low, mid, pres, high; };
    static constexpr Character warm   { 4.0f,  0.0f, -1.0f, -3.0f };   // bottom left
    static constexpr Character bright { -2.0f, 0.0f,  2.0f,  4.0f };   // bottom right
    static constexpr Character radio  { -12.0f, 5.0f, 0.0f, -12.0f };  // top left
    static constexpr Character close  { 3.0f,  0.0f,  3.0f,  2.0f };   // top right

    const float u = (x + 1.0f) * 0.5f, v = y01;
    const float wBL = (1 - u) * (1 - v), wBR = u * (1 - v), wTL = (1 - u) * v, wTR = u * v;
    auto mix = [&] (float Character::* f) { return wBL * warm.*f + wBR * bright.*f + wTL * radio.*f + wTR * close.*f; };

    lowShelf.setLowShelf   (fs, 200.0,  mix (&Character::low));
    midPeak.setPeak        (fs, 1500.0, 0.8, mix (&Character::mid));
    presence.setPeak       (fs, 4000.0, 1.0, mix (&Character::pres));
    highShelf.setHighShelf (fs, 8000.0, mix (&Character::high));
}

// Space: further away = darker (high shelf down to -8 dB at 4 kHz).
void VoicePadProcessor::updateSpace (float y01) noexcept
{
    if (std::abs (y01 - lastY) < 1.0e-3f)
        return;
    lastY = y01;
    highShelf.setHighShelf (fs, 4000.0, -8.0 * y01);
}

void VoicePadProcessor::process (juce::AudioBuffer<float>& stereo, int numSamples) noexcept
{
    auto* L = stereo.getWritePointer (0);
    auto* R = stereo.getWritePointer (1);

    smX.setTargetValue (params.x.load());
    smY.setTargetValue ((params.y.load() + 1.0f) * 0.5f);

    const int mode = params.mode.load();
    if (mode != lastMode)
    {
        // Each mode uses the filters differently: start clean, recompute coefficients.
        lowShelf.reset(); highShelf.reset(); presence.reset(); midPeak.reset();
        lowShelf = {}; highShelf = {}; presence = {}; midPeak = {};
        std::fill (delayL.begin(), delayL.end(), 0.0f);
        std::fill (delayR.begin(), delayR.end(), 0.0f);
        lastX = lastY = 99.0f;
        lastMode = mode;
    }

    if (mode == VoicePadParams::Color)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float x = smX.getNextValue();
            const float y01 = smY.getNextValue();
            if ((i & 31) == 0)
                updateColor (x, y01);

            L[i] = highShelf.process (presence.process (midPeak.process (lowShelf.process (L[i], 0), 0), 0), 0);
            R[i] = highShelf.process (presence.process (midPeak.process (lowShelf.process (R[i], 1), 1), 1), 1);
        }
        return;
    }

    if (mode == VoicePadParams::Space)
    {
        const auto delaySize = (int) delayL.size();
        if (delaySize == 0)
            return;

        // Short early reflections (17 / 23 ms), like a small room.
        const int reflL = (int) (fs * 0.017), reflR = (int) (fs * 0.023);

        for (int i = 0; i < numSamples; ++i)
        {
            const float x = smX.getNextValue();
            const float d = smY.getNextValue();            // 0 = near, 1 = far
            if ((i & 31) == 0)
                updateSpace (d);

            const float level = juce::Decibels::decibelsToGain (-9.0f * d);
            const float mono = 0.5f * (L[i] + R[i]);
            const float dark = highShelf.process (mono, 0) * level;

            const float reflA = delayL[(size_t) ((writePos - reflL + delaySize) % delaySize)];
            const float reflB = delayR[(size_t) ((writePos - reflR + delaySize) % delaySize)];
            delayL[(size_t) writePos] = dark * 0.5f * d + reflB * 0.25f;
            delayR[(size_t) writePos] = dark * 0.5f * d + reflA * 0.25f;
            writePos = (writePos + 1) % delaySize;

            const float theta = (x + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
            const float gl = std::cos (theta) * juce::MathConstants<float>::sqrt2;
            const float gr = std::sin (theta) * juce::MathConstants<float>::sqrt2;
            L[i] = dark * gl + reflA * 0.7f;
            R[i] = dark * gr + reflB * 0.7f;
        }
        return;
    }

    if (mode == VoicePadParams::Voice)
    {
        const auto delaySize = (int) delayL.size();
        if (delaySize == 0)
            return;

        for (int i = 0; i < numSamples; ++i)
        {
            const float x = smX.getNextValue();
            const float y01 = smY.getNextValue();

            if ((i & 31) == 0)
                updateFilters (x, y01);

            float l = presence.process (highShelf.process (lowShelf.process (L[i], 0), 0), 0);
            float r = presence.process (highShelf.process (lowShelf.process (R[i], 1), 1), 1);

            // Simple echo: send amount grows with Y.
            const float send = y01 * 0.30f;
            const int readL = (writePos - delaySamplesL + delaySize) % delaySize;
            const int readR = (writePos - delaySamplesR + delaySize) % delaySize;
            const float echoL = delayL[(size_t) readL];
            const float echoR = delayR[(size_t) readR];

            delayL[(size_t) writePos] = l * send + echoL * 0.35f;
            delayR[(size_t) writePos] = r * send + echoR * 0.35f;
            writePos = (writePos + 1) % delaySize;

            L[i] = l + echoL;
            R[i] = r + echoR;
        }
    }
    else
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float x = smX.getNextValue();
            const float width = 1.0f + smY.getNextValue();   // 1 .. 2

            const float mid  = 0.5f * (L[i] + R[i]);
            const float side = 0.5f * (L[i] - R[i]) * width;

            const float theta = (x + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
            const float gl = std::cos (theta) * juce::MathConstants<float>::sqrt2;
            const float gr = std::sin (theta) * juce::MathConstants<float>::sqrt2;

            L[i] = (mid + side) * gl;
            R[i] = (mid - side) * gr;
        }
    }
}

//==============================================================================
// UI

VoicePadComponent::VoicePadComponent (VoicePadParams& p, const juce::String& ownerName) : params (p)
{
    // Two-key mode switch; the selected key lights up like a lit hardware key.
    voiceButton.setConnectedEdges (juce::Button::ConnectedOnRight);
    panButton.setConnectedEdges (juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
    colorButton.setConnectedEdges (juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
    spaceButton.setConnectedEdges (juce::Button::ConnectedOnLeft);
    for (auto* b : { &voiceButton, &panButton, &colorButton, &spaceButton })
    {
        makeLamp (*b, "key");
        addAndMakeVisible (b);
    }
    colorButton.setTitle (ownerName + " pad mode: color");
    spaceButton.setTitle (ownerName + " pad mode: space");
    colorButton.setTooltip ("Color: blend between four voice characters. Warm and bright at the bottom, radio and close mic at the top.");
    spaceButton.setTooltip ("Space: left/right places the voice, up moves it further away into a small room.");
    colorButton.onClick = [this] { setMode (VoicePadParams::Color); };
    spaceButton.onClick = [this] { setMode (VoicePadParams::Space); };

    voiceButton.setTitle (ownerName + " pad mode: voice");
    panButton.setTitle (ownerName + " pad mode: pan");
    voiceButton.setTooltip ("Voice: left/right makes the voice darker/brighter, up adds presence and echo.");
    panButton.setTooltip ("Pan: left/right places the sound, up makes it wider.");
    voiceButton.onClick = [this] { setMode (VoicePadParams::Voice); };
    panButton.onClick   = [this] { setMode (VoicePadParams::Pan); };

    setTitle (ownerName + " voice pad");
    setWantsKeyboardFocus (true);
    makeClickable (*this);
    setTooltip ("Drag the puck, or use the arrow keys (Shift for fine steps). Double-click or Home resets it.");
    refreshFromParams();
}

void VoicePadComponent::refreshFromParams()
{
    const int m = params.mode.load();
    voiceButton.setToggleState (m == VoicePadParams::Voice, juce::dontSendNotification);
    panButton.setToggleState   (m == VoicePadParams::Pan,   juce::dontSendNotification);
    colorButton.setToggleState (m == VoicePadParams::Color, juce::dontSendNotification);
    spaceButton.setToggleState (m == VoicePadParams::Space, juce::dontSendNotification);
    setDescription (readout());
    repaint();
}

void VoicePadComponent::setMode (int m)
{
    params.mode.store (m);
    refreshFromParams();
}

void VoicePadComponent::resized()
{
    auto top = getLocalBounds().removeFromTop (headerHeight - 4);
    const int w = top.getWidth() / 4;
    voiceButton.setBounds (top.removeFromLeft (w));
    panButton.setBounds (top.removeFromLeft (w));
    colorButton.setBounds (top.removeFromLeft (w));
    spaceButton.setBounds (top);
}

juce::Rectangle<float> VoicePadComponent::padArea() const
{
    return getLocalBounds().withTrimmedTop (headerHeight).toFloat().reduced (0.5f);
}

juce::String VoicePadComponent::readout() const
{
    const float x = params.x.load();
    const float y01 = (params.y.load() + 1.0f) * 0.5f;

    if (params.mode.load() == VoicePadParams::Voice)
    {
        const float tilt = x * 6.0f;
        return "Tone " + juce::String (tilt > 0.05f ? "+" : "") + juce::String (tilt, 1) + " dB  "
             + "Echo " + juce::String (juce::roundToInt (y01 * 100.0f)) + "%";
    }

    const int pan = juce::roundToInt (x * 100.0f);
    const juce::String panText = pan == 0 ? juce::String ("C") : (pan < 0 ? "L" : "R") + juce::String (std::abs (pan));

    if (params.mode.load() == VoicePadParams::Color)
    {
        const float u = (x + 1.0f) * 0.5f, v = y01;
        const float w[4] { (1 - u) * (1 - v), u * (1 - v), (1 - u) * v, u * v };
        const char* names[4] { "Warm", "Bright", "Radio", "Close" };
        int a = 0;
        for (int i = 1; i < 4; ++i) if (w[i] > w[a]) a = i;
        int b = a == 0 ? 1 : 0;
        for (int i = 0; i < 4; ++i) if (i != a && w[i] > w[b]) b = i;
        return juce::String (names[a]) + " " + juce::String (juce::roundToInt (w[a] * 100)) + "%  "
             + names[b] + " " + juce::String (juce::roundToInt (w[b] * 100)) + "%";
    }

    if (params.mode.load() == VoicePadParams::Space)
        return "Pan " + panText + "  Distance " + juce::String (juce::roundToInt (y01 * 100.0f)) + "%";

    return "Pan " + panText + "  Width " + juce::String (juce::roundToInt (100.0f + y01 * 100.0f)) + "%";
}

void VoicePadComponent::paint (juce::Graphics& g)
{
    const auto& t = Theme::p();
    const auto area = padArea();
    const int padMode = params.mode.load();
    const bool voice = padMode == VoicePadParams::Voice || padMode == VoicePadParams::Color;

    // Recessed rubber pad
    g.setColour (t.well);
    g.fillRoundedRectangle (area, Theme::controlRadius);
    g.setColour (isMouseOverOrDragging() || hasKeyboardFocus (false) ? t.controlEdge : t.line);
    g.drawRoundedRectangle (area, Theme::controlRadius, 1.0f);

    const auto inner = area.reduced (10.0f);

    // Centre axes and edge ticks: position references, not a background grid.
    g.setColour (t.line);
    g.drawVerticalLine ((int) inner.getCentreX(), inner.getY(), inner.getBottom());
    if (! voice)
        g.drawHorizontalLine ((int) inner.getCentreY(), inner.getX(), inner.getRight());
    g.setColour (t.textFaint);
    for (int i = 1; i < 4; ++i)
    {
        const float fx = inner.getX() + inner.getWidth() * (float) i / 4.0f;
        const float fy = inner.getY() + inner.getHeight() * (float) i / 4.0f;
        g.drawVerticalLine ((int) fx, area.getBottom() - 5.0f, area.getBottom() - 1.0f);
        g.drawHorizontalLine ((int) fy, area.getX() + 1.0f, area.getX() + 5.0f);
    }

    // Legends
    g.setFont (Theme::ui (11.5f, true));
    g.setColour (t.textFaint);
    const auto legends = area.reduced (8.0f, 5.0f).toNearestInt();
    if (padMode == VoicePadParams::Color)
    {
        g.drawText ("RADIO", legends, juce::Justification::topLeft);
        g.drawText ("CLOSE", legends, juce::Justification::topRight);
        g.drawText ("WARM", legends.withTrimmedBottom (16), juce::Justification::bottomLeft);
        g.drawText ("BRIGHT", legends.withTrimmedBottom (16), juce::Justification::bottomRight);
    }
    else
    {
        const bool space = padMode == VoicePadParams::Space;
        g.drawText (padMode == VoicePadParams::Voice ? "PRESENCE + ECHO" : space ? "FAR" : "WIDE", legends, juce::Justification::centredTop);
        g.drawText (padMode == VoicePadParams::Voice ? "DARK" : "L", legends.withTrimmedBottom (16), juce::Justification::bottomLeft);
        g.drawText (padMode == VoicePadParams::Voice ? "BRIGHT" : "R", legends.withTrimmedBottom (16), juce::Justification::bottomRight);
    }

    // Readout: what the position does, in numbers
    g.setFont (Theme::mono (11.5f));
    g.setColour (dragging ? t.text : t.textDim);
    g.drawText (readout(), legends, juce::Justification::centredBottom);

    // Puck (raised, so it gets the small shadow)
    const float px = juce::jmap (params.x.load(), -1.0f, 1.0f, inner.getX(), inner.getRight());
    const float py = juce::jmap (params.y.load(), -1.0f, 1.0f, inner.getBottom(), inner.getY());
    const float r = 7.5f;
    g.setColour (juce::Colours::black.withAlpha (t.dark ? 0.45f : 0.2f));
    g.fillEllipse (px - r, py - r + 1.5f, r * 2.0f, r * 2.0f);
    g.setColour (t.accent);
    g.fillEllipse (px - r, py - r, r * 2.0f, r * 2.0f);
    g.setColour (t.dark ? t.accent.darker (0.5f) : t.accentEdge);
    g.drawEllipse (px - r, py - r, r * 2.0f, r * 2.0f, 1.5f);

    if (hasKeyboardFocus (false))
        Theme::drawFocusRing (g, area.expanded (1.0f));
}

void VoicePadComponent::setFromMouse (juce::Point<float> pos)
{
    const auto inner = padArea().reduced (10.0f);
    params.x.store (juce::jlimit (-1.0f, 1.0f, juce::jmap (pos.x, inner.getX(), inner.getRight(), -1.0f, 1.0f)));
    params.y.store (juce::jlimit (-1.0f, 1.0f, juce::jmap (pos.y, inner.getBottom(), inner.getY(), -1.0f, 1.0f)));
    setDescription (readout());
    repaint();
}

void VoicePadComponent::nudge (float dx, float dy)
{
    params.x.store (juce::jlimit (-1.0f, 1.0f, params.x.load() + dx));
    params.y.store (juce::jlimit (-1.0f, 1.0f, params.y.load() + dy));
    setDescription (readout());
    repaint();
}

bool VoicePadComponent::keyPressed (const juce::KeyPress& k)
{
    const float step = k.getModifiers().isShiftDown() ? 0.02f : 0.1f;
    if (k.isKeyCode (juce::KeyPress::leftKey))  { nudge (-step, 0.0f); return true; }
    if (k.isKeyCode (juce::KeyPress::rightKey)) { nudge (step, 0.0f);  return true; }
    if (k.isKeyCode (juce::KeyPress::upKey))    { nudge (0.0f, step);  return true; }
    if (k.isKeyCode (juce::KeyPress::downKey))  { nudge (0.0f, -step); return true; }
    if (k.isKeyCode (juce::KeyPress::homeKey))
    {
        params.x.store (0.0f);
        params.y.store (-1.0f);
        refreshFromParams();
        return true;
    }
    return false;
}

void VoicePadComponent::mouseDown (const juce::MouseEvent& e)
{
    if (padArea().contains (e.position))
    {
        dragging = true;
        setFromMouse (e.position);
    }
}

void VoicePadComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging)
        setFromMouse (e.position);
}

void VoicePadComponent::mouseUp (const juce::MouseEvent&)
{
    dragging = false;
    repaint();
}

void VoicePadComponent::mouseDoubleClick (const juce::MouseEvent&)
{
    params.x.store (0.0f);
    params.y.store (-1.0f);
    refreshFromParams();
}
