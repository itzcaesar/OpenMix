#pragma once

#include <JuceHeader.h>
#include "engine/MixerEngine.h"
#include "ui/Widgets.h"
#include "ui/AnalyserPanel.h"
#include "ui/InsertRack.h"
#include "ui/Panels.h"

// One output bus in the master section: name tape, output device, limiter/monitor/loudness,
// processing (EQ, compressor), inserts, mute, fader.
class BusView : public juce::Component
{
public:
    BusView (MixerEngine&, int index);

    void paint (juce::Graphics&) override;
    void resized() override;

    void refreshDevice();
    void refreshFromBus();
    void tick();

    std::function<void()> onLayoutChanged;   // removed, or the monitor bus changed

    static constexpr int minWidth = 150, maxWidth = 176;

private:
    void showTargetMenu();
    void showBusMenu();
    void openProcessorPanel (Processor);
    void updateKeys();
    static bool isVirtualCable (const juce::String& deviceName);
    bool isVirtual() const { return juce::String (kBusNames[index]).startsWith ("B"); }

    MixerEngine& engine;
    Bus& bus;
    const int index;

    TapeLabel tape { false };
    MenuDotsButton menuButton;
    ChipButton targetButton;
    juce::TextButton muteButton { "Mute" };
    StripKey eqKey { "EQ" }, compKey { "Comp" };
    InsertRack inserts;
    bool inLayout = false;
    FaderSection faderSection;

    juce::Rectangle<int> kindLegend, outputLegend, limiterArea, monitorArea, loudnessArea, processLegend, insertsLegend;
    std::shared_ptr<BusAnalysis> analysis = std::make_shared<BusAnalysis>();
    juce::AudioBuffer<float> drainBuffer { 2, 32768 };
    int analysisTicks = 0;
    void drainAnalysis();
    void openAnalyser();
    bool limiting = false;
    int limitHoldFrames = 0;
};
