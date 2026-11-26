#pragma once

#include <JuceHeader.h>
#include "engine/MixerEngine.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"
#include "ui/AnalyserPanel.h"
#include "ui/InsertRack.h"
#include "ui/Panels.h"

// One output bus, a narrow console column like the strips: output device, processing
// (EQ, compressor), inserts, analyser, limiter gain reduction, loudness (short-term LUFS +
// normalisation), then the shared fader bay: fader + meter, M / MON, latency and the
// coloured name plate. MON is lit on the monitor bus (where soloed strips are heard).
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

    void setBayTop (int y) { bayTop = y; }   // same fader-bay line as every other column

    // Column width limits; compact mode narrows them (Theme::isCompact()).
    // A bus gets about 3/4 of a strip's width (see ConsoleContent::resized).
    static int minWidth() { return Theme::isCompact() ? 100 : 110; }
    static int maxWidth() { return Theme::isCompact() ? 122 : 136; }

private:
    void showTargetMenu();
    void showBusMenu();
    void renameBus();
    void openProcessorPanel (Processor);
    void updateKeys();
    void refreshNormGainLabel();
    static bool isVirtualCable (const juce::String& deviceName);
    // Slot-based, never name-based: slots 0-2 are Hardware, 3-5 Virtual.
    // A custom bus name must never move a bus between groups.
    bool isVirtual() const { return index >= 3; }
    // The bus's own name: the custom name when one is set, else A1..A3 / B1..B3.
    juce::String busName() const { return engine.getBusName (index); }

    MixerEngine& engine;
    Bus& bus;
    const int index;

    TapeLabel tape { false };
    MenuDotsButton menuButton;
    ChipButton targetButton;
    StripKey muteButton { "M" }, monButton { "MON" };
    StripKey eqKey { "EQ" }, compKey { "Comp" };
    InsertRack inserts;
    bool inLayout = false;
    int bayTop = 0;
    FaderSection faderSection;

    juce::ComboBox normCombo;   // loudness normalisation: Off / -23 / -16 / -14 LUFS
    juce::Label normGainLabel;  // gain the normaliser is currently applying

    juce::Rectangle<int> outputLegend, limiterLegend, grHeadArea, grTrackArea,
                         limiterSectionArea, loudnessLegend, loudnessValueArea,
                         processLegend, insertsLegend, latencyArea;
    juce::String latencyText;
    std::vector<int> dividerYs;   // y of each 1 px section divider, in strip coords
    std::shared_ptr<BusAnalysis> analysis = std::make_shared<BusAnalysis>();
    MiniSpectrum spectrum { analysis };
    juce::Rectangle<int> spectrumLegend;
    juce::AudioBuffer<float> drainBuffer { 2, 32768 };
    int analysisTicks = 0;
    void drainAnalysis();
    void openAnalyser();
    bool limiting = false;
    int limitHoldFrames = 0;
    float limiterReductionDb = 0.0f;   // smoothed gain reduction of the safety limiter
};
