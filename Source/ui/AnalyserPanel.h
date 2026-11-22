#pragma once

#include <JuceHeader.h>
#include "dsp/Loudness.h"

// Analysis state of one bus, fed by BusView from the bus's analysis FIFO (message thread).
struct BusAnalysis
{
    omx::LoudnessMeter loudness;
    std::vector<float> history = std::vector<float> (4096, 0.0f);   // mono, for the spectrum
    int historyPos = 0;
    double sampleRate = 0.0;

    void push (const float* left, const float* right, int numSamples)
    {
        loudness.push (left, right, numSamples);
        for (int i = 0; i < numSamples; ++i)
        {
            history[(size_t) historyPos] = 0.5f * (left[i] + right[i]);
            historyPos = (historyPos + 1) % (int) history.size();
        }
    }
};

// Small live spectrum shown inside a bus (32 log-spaced bands, 30 Hz to 18 kHz). Click, Enter
// or Space opens the full analyser.
class MiniSpectrum : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit MiniSpectrum (std::shared_ptr<BusAnalysis>);

    void update();   // ~10 Hz from the owner's timer
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override  { if (onClick) onClick(); }
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override      { repaint(); }
    void focusLost (FocusChangeType) override        { repaint(); }

    std::function<void()> onClick;

private:
    static constexpr int fftOrder = 10, fftSize = 1 << fftOrder, numBands = 32;

    std::shared_ptr<BusAnalysis> analysis;
    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann };
    std::array<float, fftSize * 2> fftData {};
    std::array<float, numBands> bands {};   // dB, with decay
};

// Pop-over: spectrum of the bus plus momentary / short-term / integrated loudness.
class AnalyserPanel : public juce::Component, private juce::Timer
{
public:
    AnalyserPanel (const juce::String& busName, std::shared_ptr<BusAnalysis>);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    static constexpr int fftOrder = 11, fftSize = 1 << fftOrder;

    juce::String title;
    std::shared_ptr<BusAnalysis> analysis;
    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann };
    std::array<float, fftSize * 2> fftData {};
    std::array<float, fftSize / 2> levels {};   // dB, with decay
    juce::TextButton resetButton { "Reset integrated" };
    juce::Rectangle<int> titleArea, plotArea, numbersArea;
};
