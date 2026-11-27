#pragma once

#include <JuceHeader.h>
#include "engine/MixerEngine.h"
#include "creator/MediaDecode.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

// The media player: a bar along the bottom of the console, above the soundboard. Transport
// keys on the left, the video picture (video files only), the track's waveform (click or drag
// to seek), and the playlist on the right. Audio and video files dropped on the bar are added.
class MediaPlayerPanel : public juce::Component,
                         public juce::FileDragAndDropTarget,
                         private juce::ListBoxModel,
                         private juce::ChangeListener,
                         private juce::Timer
{
public:
    explicit MediaPlayerPanel (MixerEngine&);
    ~MediaPlayerPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void refresh();

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;

    std::function<void()> onChanged;   // something worth saving changed

    static int barHeight() { return Theme::isCompact() ? 124 : 136; }

private:
    // ListBoxModel
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void returnKeyPressed (int row) override { playRow (row); }
    void deleteKeyPressed (int row) override;
    void selectedRowsChanged (int row) override { removeButton.setEnabled (row >= 0); }

    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint (waveArea); }
    void timerCallback() override;
    void trackChanged();
    void seekTo (int x);
    void playRow (int row);
    void addFiles();
    bool anyStripPlaysIt() const;

    MixerEngine& engine;
    MediaPlayer& player;

    TapeLabel tape { false };
    juce::TextButton prevButton { "Prev" }, playButton { "Play" }, nextButton { "Next" },
                     repeatButton { "Repeat all" }, shuffleButton { "Shuffle" },
                     addButton { "+ Add" }, removeButton { "Remove" }, clearButton { "Clear" };
    juce::ListBox list { "Playlist", this };
    std::unique_ptr<juce::FileChooser> chooser;

    juce::AudioFormatManager noFormats;   // AudioThumbnail wants one; it reads from setReader instead
    juce::AudioThumbnailCache thumbCache { 4 };
    juce::AudioThumbnail thumb { 512, noFormats, thumbCache };
    VideoFrames video;
    juce::File shownFile;

    juce::Rectangle<int> videoArea, titleArea, timeArea, waveArea, playlistLegend;
    bool seeking = false;
    double lastPosition = -1.0;
};
