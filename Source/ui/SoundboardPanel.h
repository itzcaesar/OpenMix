#pragma once

#include <JuceHeader.h>
#include "engine/MixerEngine.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

// The soundboard: a bar along the bottom of the console. Name, page keys (A-D, 8 pads each)
// and Stop all on the left, the current page's 8 pads in a row, then volume, meter and the
// buses it plays on. Audio files dropped on a pad load into it.
class SoundboardPanel : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit SoundboardPanel (MixerEngine&);
    ~SoundboardPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void refresh();

    // Keys 1-8 play the current page's pads (also called by the main window). releaseKeys()
    // ends Hold pads whose key was let go; call it from keyStateChanged.
    bool pressKey (const juce::KeyPress&);
    void releaseKeys();

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int x, int y) override;
    std::function<void()> onChanged;   // something worth saving changed

    static int barHeight() { return Theme::isCompact() ? 104 : 112; }   // room for name, page keys and Stop all
    static constexpr int minWidth = 720;

private:
    class Pad;
    void timerCallback() override;
    void loadPad (int pad);
    void showPadMenu (int pad);
    void renamePad (int pad);
    void showPadVolume (int pad);
    void setPage (int page);
    int padAt (int slot) const noexcept { return page * Soundboard::padsPerPage + slot; }

    MixerEngine& engine;
    Soundboard& board;
    TapeLabel tape { false };
    std::vector<std::unique_ptr<Pad>> pads;
    DbSlider volume { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    LevelMeter meter;
    std::array<StripKey, kNumBuses> sendButtons;
    juce::TextButton stopButton { "Stop all" };
    std::array<StripKey, Soundboard::numPads / Soundboard::padsPerPage> pageButtons;
    int page = 0;
    std::array<int, Soundboard::padsPerPage> keyHeld {};    // pad index + 1 while key 1-8 is down (ignores key repeat)
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> padsLegend, volumeLegend, sendsLegend;
};
