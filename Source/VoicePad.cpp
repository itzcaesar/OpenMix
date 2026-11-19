#include "VoicePad.h"
#include "Theme.h"

//==============================================================================
// DSP

void VoicePadProcessor::prepare (double sampleRate, int)
{
    fs = sampleRate;
    smX.reset (sampleRate, 0.05);
    smY.reset (sampleRate, 0.05);
    smX.setCurrentAndTargetValue (params.x.load());
    smY.setCurrentAndTargetValue ((params.y.load() + 1.0f) * 0.5f);

    lowShelf.reset(); highShelf.reset(); presence.reset();
    lastX = lastY = 99.0f;

    const auto maxDelay = (size_t) (sampleRate * 0.5);
    delayL.assign (maxDelay, 0.0f);
    delayR.assign (maxDelay, 0.0f);
    writePos = 0;
    delaySamplesL = (int) (sampleRate * 0.140);
    delaySamplesR = (int) (sampleRate * 0.165);
}

void VoicePadProcessor::reset() noexcept
{
    lowShelf.reset(); highShelf.reset(); presence.reset();
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

void VoicePadProcessor::process (juce::AudioBuffer<float>& stereo, int numSamples) noexcept
{
    auto* L = stereo.getWritePointer (0);
    auto* R = stereo.getWritePointer (1);

    smX.setTargetValue (params.x.load());
    smY.setTargetValue ((params.y.load() + 1.0f) * 0.5f);

    if (params.mode.load() == VoicePadParams::Voice)
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

VoicePadComponent::VoicePadComponent (VoicePadParams& p) : params (p)
{
    // Two-part segmented switch
    voiceButton.setConnectedEdges (juce::Button::ConnectedOnRight);
    panButton.setConnectedEdges (juce::Button::ConnectedOnLeft);

    for (auto* b : { &voiceButton, &panButton })
    {
        b->setClickingTogglesState (false);
        b->setColour (juce::TextButton::buttonColourId, Theme::panelDark);
        b->setColour (juce::TextButton::buttonOnColourId, Theme::raised.brighter (0.15f));
        b->setColour (juce::TextButton::textColourOffId, Theme::textFaint);
        b->setColour (juce::TextButton::textColourOnId, Theme::text);
        addAndMakeVisible (b);
    }

    voiceButton.setTooltip ("Voice: left/right = darker/brighter tone, up = presence + echo");
    panButton.setTooltip ("Pan: left/right = stereo position, up = wider stereo");
    voiceButton.onClick = [this] { setMode (VoicePadParams::Voice); };
    panButton.onClick   = [this] { setMode (VoicePadParams::Pan); };
    setTooltip ("Drag the dot. Double-click to reset.");
    refreshFromParams();
}

void VoicePadComponent::refreshFromParams()
{
    const int m = params.mode.load();
    voiceButton.setToggleState (m == VoicePadParams::Voice, juce::dontSendNotification);
    panButton.setToggleState   (m == VoicePadParams::Pan,   juce::dontSendNotification);
    repaint();
}

void VoicePadComponent::setMode (int m)
{
    params.mode.store (m);
    refreshFromParams();
}

void VoicePadComponent::resized()
{
    auto top = getLocalBounds().removeFromTop (headerHeight - 6);
    voiceButton.setBounds (top.removeFromLeft (top.getWidth() / 2));
    panButton.setBounds (top);
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
        return "Tone " + juce::String (tilt > 0.05f ? "+" : "") + juce::String (tilt, 1) + " dB   "
             + "Echo " + juce::String (juce::roundToInt (y01 * 100.0f)) + "%";
    }

    const int pan = juce::roundToInt (x * 100.0f);
    const juce::String panText = pan == 0 ? juce::String ("C") : (pan < 0 ? "L" : "R") + juce::String (std::abs (pan));
    return "Pan " + panText + "   Width " + juce::String (juce::roundToInt (100.0f + y01 * 100.0f)) + "%";
}

void VoicePadComponent::paint (juce::Graphics& g)
{
    const auto area = padArea();
    const bool voice = params.mode.load() == VoicePadParams::Voice;
    const auto accent = Theme::accent;

    // Well
    g.setGradientFill (juce::ColourGradient (Theme::panelDark.brighter (0.06f), area.getCentreX(), area.getY(),
                                             Theme::panelDark, area.getCentreX(), area.getBottom(), false));
    g.fillRoundedRectangle (area, Theme::radius);

    // 4x4 grid + centre axes
    const auto inner = area.reduced (10.0f);
    g.setColour (Theme::grid);
    for (int i = 1; i < 4; ++i)
    {
        const float fx = inner.getX() + inner.getWidth() * (float) i / 4.0f;
        const float fy = inner.getY() + inner.getHeight() * (float) i / 4.0f;
        g.drawVerticalLine ((int) fx, inner.getY(), inner.getBottom());
        g.drawHorizontalLine ((int) fy, inner.getX(), inner.getRight());
    }
    g.setColour (Theme::outline);
    g.drawVerticalLine ((int) inner.getCentreX(), inner.getY(), inner.getBottom());
    if (! voice)
        g.drawHorizontalLine ((int) inner.getCentreY(), inner.getX(), inner.getRight());

    g.setColour (isMouseOverOrDragging() ? Theme::outline.brighter (0.2f) : Theme::outline);
    g.drawRoundedRectangle (area.reduced (0.5f), Theme::radius, 1.0f);

    // Axis labels
    g.setFont (Theme::ui (12.0f));
    g.setColour (Theme::textFaint);
    const auto labels = area.reduced (10.0f, 6.0f).toNearestInt();
    g.drawText (voice ? "PRESENCE + ECHO" : "WIDE", labels, juce::Justification::centredTop);
    g.drawText (voice ? "DARK" : "L", labels.withTrimmedBottom (18), juce::Justification::bottomLeft);
    g.drawText (voice ? "BRIGHT" : "R", labels.withTrimmedBottom (18), juce::Justification::bottomRight);

    // Readout
    g.setFont (Theme::mono (12.0f));
    g.setColour (dragging ? Theme::text : Theme::textDim);
    g.drawText (readout(), labels, juce::Justification::centredBottom);

    // Handle position
    const float px = juce::jmap (params.x.load(), -1.0f, 1.0f, inner.getX(), inner.getRight());
    const float py = juce::jmap (params.y.load(), -1.0f, 1.0f, inner.getBottom(), inner.getY());

    // Guide lines to the edges
    g.setColour (accent.withAlpha (dragging ? 0.45f : 0.22f));
    g.drawVerticalLine ((int) px, inner.getY(), inner.getBottom());
    g.drawHorizontalLine ((int) py, inner.getX(), inner.getRight());

    // Glow + dot
    const float r = dragging ? 9.0f : 8.0f;
    for (int i = 3; i >= 1; --i)
    {
        g.setColour (accent.withAlpha (0.07f * (float) (4 - i)));
        const float gr = r + (float) i * 4.0f;
        g.fillEllipse (px - gr, py - gr, gr * 2.0f, gr * 2.0f);
    }
    g.setColour (accent);
    g.fillEllipse (px - r, py - r, r * 2.0f, r * 2.0f);
    g.setColour (Theme::text);
    g.fillEllipse (px - 3.0f, py - 3.0f, 6.0f, 6.0f);
}

void VoicePadComponent::setFromMouse (juce::Point<float> pos)
{
    const auto inner = padArea().reduced (10.0f);
    params.x.store (juce::jlimit (-1.0f, 1.0f, juce::jmap (pos.x, inner.getX(), inner.getRight(), -1.0f, 1.0f)));
    params.y.store (juce::jlimit (-1.0f, 1.0f, juce::jmap (pos.y, inner.getBottom(), inner.getY(), -1.0f, 1.0f)));
    repaint();
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
    repaint();
}
