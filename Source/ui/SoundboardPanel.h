#pragma once

#include <JuceHeader.h>
#include "engine/MixerEngine.h"
#include "ui/Widgets.h"

// The soundboard: 8 pads, a volume fader and bus sends. Shown in its own window.
class SoundboardPanel : public juce::Component, private juce::Timer
{
public:
    explicit SoundboardPanel (MixerEngine&);

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void refresh();
    std::function<void()> onChanged;   // something worth saving changed

private:
    class Pad;
    void timerCallback() override;
    void loadPad (int pad);
    void showPadMenu (int pad);
    void renamePad (int pad);

    MixerEngine& engine;
    Soundboard& board;
    std::vector<std::unique_ptr<Pad>> pads;
    DbSlider volume { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    std::array<StripKey, kNumBuses> sendButtons;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> padsLegend, volumeLegend, sendsLegend;
};

class SoundboardWindow : public juce::DocumentWindow
{
public:
    explicit SoundboardWindow (MixerEngine&);
    void closeButtonPressed() override { setVisible (false); }
    SoundboardPanel& getPanel() { return *panel; }

private:
    SoundboardPanel* panel;
};
