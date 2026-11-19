#pragma once

#include <JuceHeader.h>
#include "Biquad.h"

// Shared state between the pad UI (message thread) and the DSP (audio thread).
struct IntelliPadParams
{
    enum Mode { Voice = 0, Pan = 1 };

    std::atomic<float> x { 0.0f };   // -1 (left / Lo)  .. +1 (right / Hi)
    std::atomic<float> y { -1.0f };  // -1 (bottom, neutral) .. +1 (top)
    std::atomic<int>   mode { Voice };
};

// Voice mode: X = tone tilt (Lo <-> Hi), Y = presence + echo.
// Pan mode:   X = constant-power pan,     Y = stereo width (100% .. 200%).
class IntelliPadProcessor
{
public:
    explicit IntelliPadProcessor (IntelliPadParams& p) : params (p) {}

    void prepare (double sampleRate, int maxBlockSize);
    void process (juce::AudioBuffer<float>& stereo, int numSamples) noexcept;

private:
    void updateFilters (float x, float y01) noexcept;

    IntelliPadParams& params;
    double fs = 48000.0;

    juce::SmoothedValue<float> smX, smY;
    float lastX = 99.0f, lastY = 99.0f;
    Biquad lowShelf, highShelf, presence;

    std::vector<float> delayL, delayR;
    int writePos = 0, delaySamplesL = 0, delaySamplesR = 0;
};

class IntelliPadComponent : public juce::Component, public juce::SettableTooltipClient
{
public:
    static constexpr int headerHeight = 32;   // mode switch above the pad

    explicit IntelliPadComponent (IntelliPadParams& p);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }

    void refreshFromParams();

private:
    juce::Rectangle<float> padArea() const;
    void setFromMouse (juce::Point<float>);
    void setMode (int m);

    juce::String readout() const;

    IntelliPadParams& params;
    juce::TextButton voiceButton { "Voice" }, panButton { "Pan" };
    bool dragging = false;
};
