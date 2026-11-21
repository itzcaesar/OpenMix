#pragma once

#include <JuceHeader.h>
#include "dsp/Biquad.h"

// Shared state between the pad UI (message thread) and the DSP (audio thread).
struct VoicePadParams
{
    enum Mode { Voice = 0, Pan = 1, Color = 2, Space = 3 };
    static constexpr int numModes = 4;

    std::atomic<float> x { 0.0f };   // -1 (left / Lo)  .. +1 (right / Hi)
    std::atomic<float> y { -1.0f };  // -1 (bottom, neutral) .. +1 (top)
    std::atomic<int>   mode { Voice };
};

// Voice mode: X = tone tilt (Lo <-> Hi), Y = presence + echo.
// Pan mode:   X = constant-power pan,     Y = stereo width (100% .. 200%).
// Color mode: the four corners are voice characters (warm, bright, radio, close mic);
//             the puck blends between them.
// Space mode: X = pan, Y = distance (quieter, darker, more short room reflections).
class VoicePadProcessor
{
public:
    explicit VoicePadProcessor (VoicePadParams& p) : params (p) {}

    void prepare (double sampleRate, int maxBlockSize);
    void reset() noexcept;   // clears filter + echo state (audio thread safe, no allocation)
    void process (juce::AudioBuffer<float>& stereo, int numSamples) noexcept;

private:
    void updateFilters (float x, float y01) noexcept;
    void updateColor (float x, float y01) noexcept;
    void updateSpace (float y01) noexcept;

    VoicePadParams& params;
    double fs = 48000.0;

    juce::SmoothedValue<float> smX, smY;
    float lastX = 99.0f, lastY = 99.0f;
    Biquad lowShelf, highShelf, presence, midPeak;
    int lastMode = -1;

    std::vector<float> delayL, delayR;
    int writePos = 0, delaySamplesL = 0, delaySamplesR = 0;
};

class VoicePadComponent : public juce::Component, public juce::SettableTooltipClient
{
public:
    static constexpr int headerHeight = 30;   // mode switch above the pad

    VoicePadComponent (VoicePadParams& p, const juce::String& ownerName);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override   { repaint(); }

    void refreshFromParams();

private:
    juce::Rectangle<float> padArea() const;
    void setFromMouse (juce::Point<float>);
    void nudge (float dx, float dy);
    void setMode (int m);
    juce::String readout() const;

    VoicePadParams& params;
    juce::TextButton voiceButton { "Voice" }, panButton { "Pan" }, colorButton { "Color" }, spaceButton { "Space" };
    bool dragging = false;
};
