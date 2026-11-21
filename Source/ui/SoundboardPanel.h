#pragma once

#include <JuceHeader.h>
#include "engine/MixerEngine.h"
#include "ui/Widgets.h"

// The soundboard column in the console: 8 pads, a volume fader and bus sends.
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

    static constexpr int columnWidth = 196;

private:
    class Pad;
    void timerCallback() override;
    void loadPad (int pad);
    void showPadMenu (int pad);
    void renamePad (int pad);

    MixerEngine& engine;
    Soundboard& board;
    std::vector<std::unique_ptr<Pad>> pads;
    DbSlider volume { juce::Slider::LinearHorizontal, juce::Slider::TextBoxBelow };
    std::array<StripKey, kNumBuses> sendButtons;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> titleArea, padsLegend, volumeLegend, sendsLegend;
};
