#pragma once

#include <JuceHeader.h>
#include "../MixerEngine.h"
#include "Widgets.h"

// Pop-over panels opened from a strip: one per built-in processor, and one per send.

enum class Processor { Denoise, Gate, Eq, Comp, DeEss, Duck };

juce::String processorShortName (Processor);   // label on the strip key, e.g. "NR"
juce::String processorName (Processor);        // full name, e.g. "Noise removal"
std::atomic<bool>& processorOn (ProcessingParams&, Processor);

// Base: title, On lamp, a grid of knobs, optional meter and help text.
class PanelBase : public juce::Component, private juce::Timer
{
public:
    std::function<void()> onChange;   // tell the strip to refresh its keys

protected:
    PanelBase (const juce::String& title, std::atomic<bool>* onFlag);

    struct KnobSpec
    {
        juce::String label;
        double min, max, step;
        juce::String suffix;
        std::atomic<float>& target;
        double skewMid = 0.0;      // 0 = linear
        double scale = 1.0;        // shown value = stored * scale (e.g. 0..1 shown as 0..100 %)
    };

    void addKnob (const KnobSpec&);
    void setHelp (const juce::String& text) { help = text; repaint(); }
    void setMeter (std::atomic<float>* gainMeter, const juce::String& meterLabel);
    virtual void layoutExtra (juce::Rectangle<int>&) {}   // extra controls above the knobs
    virtual void tick() {}

    void paint (juce::Graphics&) override;
    void resized() override;

    juce::TextButton onButton { "On" };
    juce::String help;

private:
    void timerCallback() override;

    struct Knob
    {
        std::unique_ptr<DbSlider> slider;
        juce::String label;
    };

    juce::String title;
    std::atomic<bool>* onFlag;
    std::vector<Knob> knobs;
    std::atomic<float>* meterSource = nullptr;
    juce::String meterLabel;
    float meterDb = 0.0f;
    juce::Rectangle<int> titleArea, knobArea, meterArea, helpArea;
};

class ProcessorPanel : public PanelBase
{
public:
    ProcessorPanel (MixerEngine&, int stripIndex, Processor);

private:
    void layoutExtra (juce::Rectangle<int>&) override;
    void tick() override;

    MixerEngine& engine;
    ChannelStrip& strip;
    Processor kind;
    juce::ComboBox duckSource;
    juce::Label duckLabel;
};

class SendPanel : public PanelBase
{
public:
    SendPanel (MixerEngine&, int stripIndex, int busIndex);

private:
    void layoutExtra (juce::Rectangle<int>&) override;

    ChannelStrip& strip;
    int bus;
    juce::TextButton preButton { "Pre-fader" };
};
