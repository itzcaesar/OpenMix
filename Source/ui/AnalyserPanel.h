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
