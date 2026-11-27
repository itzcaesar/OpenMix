#pragma once

#include <JuceHeader.h>
#include "engine/MixerEngine.h"
#include "ui/Widgets.h"

// The media player window: the track, a position bar, transport keys (previous, play/pause,
// next, repeat, shuffle) and the playlist. Audio files dropped on the window are added.
class MediaPlayerPanel : public juce::Component,
                         public juce::FileDragAndDropTarget,
                         private juce::ListBoxModel,
                         private juce::Timer
{
public:
    explicit MediaPlayerPanel (MixerEngine&);
    ~MediaPlayerPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;

    std::function<void()> onChanged;   // something worth saving changed

private:
    // ListBoxModel
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void returnKeyPressed (int row) override { playRow (row); }
    void deleteKeyPressed (int row) override;
    void selectedRowsChanged (int row) override { removeButton.setEnabled (row >= 0); }

    void timerCallback() override;
    void refresh();
    void playRow (int row);
    void addFiles();
    bool anyStripPlaysIt() const;

    MixerEngine& engine;
    MediaPlayer& player;

    juce::TextButton prevButton { "Prev" }, playButton { "Play" }, nextButton { "Next" },
                     repeatButton { "Repeat all" }, shuffleButton { "Shuffle" },
                     addButton { "+ Add files" }, removeButton { "Remove" }, clearButton { "Clear" };
    juce::Slider position { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::ListBox list { "Playlist", this };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> titleArea, timeArea, noteArea;
    bool seeking = false;
};
