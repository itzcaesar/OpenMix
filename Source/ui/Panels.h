#pragma once

#include <JuceHeader.h>
#include "engine/MixerEngine.h"
#include "ui/Widgets.h"

#include <cstdint>

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

class ProcessorPanel : public PanelBase, private juce::URL::DownloadTaskListener
{
public:
    // stripIndex: the strip these settings belong to, or -1 for a bus.
    ProcessorPanel (MixerEngine&, ProcessingParams&, Processor, int stripIndex = -1);
    ~ProcessorPanel() override;

private:
    void layoutExtra (juce::Rectangle<int>&) override;
    void tick() override;
    void syncFromVisual();
    void progress (juce::URL::DownloadTask*, juce::int64 bytesDownloaded, juce::int64 totalLength) override;
    void finished (juce::URL::DownloadTask*, bool success) override;
    void startDeepFilterModelDownload();
    void cancelDeepFilterModelDownload();
    void finishDeepFilterModelDownload (std::uint64_t generation, bool success, int statusCode, const juce::File& file);

    MixerEngine& engine;
    ProcessingParams& fx;
    Processor kind;
    int stripIndex;
    juce::ComboBox duckSource;
    juce::Label duckLabel;
    juce::ComboBox denoiseModel;
    juce::TextButton getDeepFilterModelButton { "Get model" }, installDeepFilterModelButton { "Install..." };
    std::unique_ptr<juce::FileChooser> deepFilterModelChooser;
    double deepFilterModelProgressValue = 0.0;
    juce::ProgressBar deepFilterModelProgressBar;
    juce::File deepFilterDownloadFile;
    std::unique_ptr<juce::URL::DownloadTask> deepFilterDownloadTask;
    std::atomic<juce::int64> deepFilterDownloadBytes { 0 }, deepFilterDownloadTotal { 0 };
    std::atomic<std::uint64_t> deepFilterDownloadGeneration { 0 };
    bool deepFilterDownloadActive = false, ownsDeepFilterDownloadSlot = false;
    int deepFilterLastProgressPercent = -1;

    // EQ: one key per band selects it; three knobs edit the selected band.
    void selectEqBand (int band);
    void syncEqKnobs();
    int eqBand = omx::kEqLegacyMid;
    std::array<StripKey, omx::kEqBands> eqBandKeys;
    DbSlider eqGainKnob, eqFreqKnob, eqQKnob;
    juce::Label eqGainLabel, eqFreqLabel, eqQLabel;

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
