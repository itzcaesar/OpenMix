#pragma once

#include <JuceHeader.h>
#include "engine/MixerEngine.h"
#include "ui/Widgets.h"

// Pop-over panels opened from a strip or bus: one per built-in processor,
// plus wet/dry insert mix and bus send panels.

enum class Processor { Denoise, Gate, Eq, Comp, DeEss, Duck };

juce::String processorShortName (Processor);   // label on the strip key, e.g. "NR"
juce::String processorName (Processor);        // full name, e.g. "Noise removal"
std::atomic<bool>& processorOn (ProcessingParams&, Processor);

// Base: title, On lamp, optional interactive visual + overview card, optional
// extra controls, rotary knobs, optional meter, and help text.
class PanelBase : public juce::Component, private juce::Timer
{
public:
    std::function<void()> onChange;   // tell the strip/bus to refresh its keys

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
    void setVisual (std::unique_ptr<juce::Component> v, int height);
    juce::Component* getVisual() const noexcept { return visual.get(); }
    void repaintVisual();
    void syncKnobsFromTargets();
    float getSmoothedReductionDb() const noexcept { return meterDb; }

    virtual void layoutExtra (juce::Rectangle<int>&) {}   // extra controls below visual / above knobs
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
        std::atomic<float>* target = nullptr;
        double scale = 1.0;
    };

    juce::String title;
    std::atomic<bool>* onFlag;
    std::unique_ptr<juce::Component> visual;
    int visualHeight = 0;
    std::vector<Knob> knobs;
    std::atomic<float>* meterSource = nullptr;
    juce::String meterLabel;
    float meterDb = 0.0f;
    juce::Rectangle<int> titleArea, helpArea, meterArea;
};

class ProcessorPanel : public PanelBase
{
public:
    // stripIndex: the strip these settings belong to, or -1 for a bus.
    ProcessorPanel (MixerEngine&, ProcessingParams&, Processor, int stripIndex = -1);

private:
    void layoutExtra (juce::Rectangle<int>&) override;
    void tick() override;
    void syncFromVisual();

    MixerEngine& engine;
    ProcessingParams& fx;
    Processor kind;
    int stripIndex;
    juce::ComboBox duckSource;
    juce::Label duckLabel;

    // High-pass / low-pass filters, shown in the EQ panel on strips only
    // (there is deliberately no FILTER section on the strip itself).
    juce::TextButton hpfToggle { "HPF" }, lpfToggle { "LPF" };
    DbSlider hpfCutoffSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    DbSlider lpfCutoffSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
};

class SlotMixPanel : public PanelBase
{
public:
    SlotMixPanel (const juce::String& pluginName, std::atomic<float>& mix);
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
