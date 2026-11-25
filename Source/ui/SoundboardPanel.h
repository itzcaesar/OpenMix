#pragma once

#include <JuceHeader.h>
#include "engine/MixerEngine.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

// The soundboard: a bar along the bottom of the console. Name and Stop all on the left, the
// 8 pads in a row, then volume, meter and the buses it plays on.
class SoundboardPanel : public juce::Component, private juce::Timer
{
public:
    explicit SoundboardPanel (MixerEngine&);
    ~SoundboardPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void refresh();
    std::function<void()> onChanged;   // something worth saving changed

    static int barHeight() { return Theme::isCompact() ? 92 : 104; }
    static constexpr int minWidth = 760;

private:
    class Pad;
    void timerCallback() override;
    void loadPad (int pad);
    void showPadMenu (int pad);
    void renamePad (int pad);

    MixerEngine& engine;
    Soundboard& board;
    TapeLabel tape { false };
    std::vector<std::unique_ptr<Pad>> pads;
    DbSlider volume { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    LevelMeter meter;
    std::array<StripKey, kNumBuses> sendButtons;
    juce::TextButton stopButton { "Stop all" };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> padsLegend, volumeLegend, sendsLegend;
};
