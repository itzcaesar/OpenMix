#pragma once

#include <JuceHeader.h>
#include "../MixerEngine.h"
#include "Widgets.h"

// One output bus in the master section: name tape, output device, limiter lamp, mute, fader.
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
    static bool isVirtualCable (const juce::String& deviceName);
    bool isVirtual() const { return juce::String (kBusNames[index]).startsWith ("B"); }

    MixerEngine& engine;
    Bus& bus;
    const int index;

    TapeLabel tape { false };
    MenuDotsButton menuButton;
    ChipButton targetButton;
    juce::TextButton muteButton { "Mute" };
    FaderSection faderSection;

    juce::Rectangle<int> kindLegend, outputLegend, limiterArea, monitorArea;
    bool limiting = false;
    int limitHoldFrames = 0;
};
