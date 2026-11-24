#pragma once

#include <JuceHeader.h>
#include "engine/MixerEngine.h"
#include "ui/Widgets.h"

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
    // stripIndex: the strip these settings belong to, or -1 for a bus.
    ProcessorPanel (MixerEngine&, ProcessingParams&, Processor, int stripIndex = -1);

private:
    void layoutExtra (juce::Rectangle<int>&) override;
    void tick() override;

    MixerEngine& engine;
    ProcessingParams& fx;
    Processor kind;
    int stripIndex;
    juce::ComboBox duckSource;
    juce::Label duckLabel;
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

// Editor for a built-in insert (Reverb / Delay / Chorus) in a PluginChain slot.
// Unlike PanelBase its parameters live on the effect object, not in atomics, so
// it reads them with PluginChain::getBuiltInParams and writes them back with
// setBuiltInParams. A delay's tempo-sync switch and note division live here;
// when tempo sync is on, the time knob is disabled and the delay follows the
// tap-tempo BPM (see MixerEngine::applyBpmToDelays).
class BuiltInFxPanel : public juce::Component
{
public:
    BuiltInFxPanel (MixerEngine&, PluginChain&, int slot);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct KnobDef
    {
        juce::String label;
        double min, max, step, skewMid = 0.0;
        juce::String suffix;
        int paramIndex;
        double scale = 1.0;   // shown value = stored * scale
    };

    void push();   // write the sliders back into the chain
    void rebuildFromChain();

    MixerEngine& engine;
    PluginChain& chain;
    int slot;
    omx::FxType type;
    std::array<float, 6> params {};
    bool tempoSync = false;

    std::vector<KnobDef> defs;
    std::vector<std::unique_ptr<DbSlider>> sliders;
    juce::TextButton tempoButton { "Tempo sync" };
    juce::ComboBox noteCombo;
    juce::String title, help;
    juce::Rectangle<int> titleArea, helpArea;
};
