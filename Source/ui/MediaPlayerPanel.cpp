#include "ui/MediaPlayerPanel.h"
#include "ui/Theme.h"

static juce::String timeText (double seconds)
{
    const int s = juce::jmax (0, (int) seconds);
    return juce::String (s / 60) + ":" + juce::String (s % 60).paddedLeft ('0', 2);
}

static const char* const repeatNames[] { "Repeat off", "Repeat all", "Repeat one" };

MediaPlayerPanel::MediaPlayerPanel (MixerEngine& e) : engine (e), player (e.getMediaPlayer())
{
    setTitle ("Media player");
    setWantsKeyboardFocus (true);

    prevButton.setTooltip ("Previous track (back to the start after the first 3 seconds).");
    prevButton.onClick = [this] { player.previous(); refresh(); };
    playButton.onClick = [this] { player.togglePlay(); refresh(); };
    nextButton.setTooltip ("Next track.");
    nextButton.onClick = [this] { player.next(); refresh(); };

    makeLamp (repeatButton, "key");
    repeatButton.setTooltip ("Click to cycle: repeat the whole list, repeat this track, or stop at the end.");
    repeatButton.onClick = [this]
    {
        player.repeat = (MediaPlayer::Repeat) (((int) player.repeat + 1) % 3);
        refresh();
        if (onChanged) onChanged();
    };
    makeLamp (shuffleButton, "key");
    shuffleButton.setClickingTogglesState (true);
    shuffleButton.setTooltip ("Play the list in random order.");
    shuffleButton.onClick = [this]
    {
        player.shuffle = shuffleButton.getToggleState();
        if (onChanged) onChanged();
    };

    addButton.setTooltip ("Add WAV, MP3, FLAC, Ogg or AIFF files. You can also drop files on this window.");
    addButton.onClick = [this] { addFiles(); };
    removeButton.setTooltip ("Remove the selected track (or press Delete).");
    removeButton.onClick = [this] { deleteKeyPressed (list.getSelectedRow()); };
    clearButton.setTooltip ("Empty the playlist.");
    clearButton.onClick = [this] { player.clearPlaylist(); refresh(); if (onChanged) onChanged(); };

    position.setTitle ("Position");
    position.setRange (0.0, 1.0);
    position.onDragStart = [this] { seeking = true; };
    position.onDragEnd = [this] { player.setPosition (position.getValue()); seeking = false; };
    position.onValueChange = [this] { if (! position.isMouseButtonDown()) player.setPosition (position.getValue()); };

    list.setRowHeight (28);
    list.setTitle ("Playlist");
    list.setColour (juce::ListBox::backgroundColourId, Theme::p().well);
    list.setColour (juce::ListBox::outlineColourId, Theme::p().line);
    list.setOutlineThickness (1);

    for (auto* c : std::initializer_list<juce::Component*> { &prevButton, &playButton, &nextButton, &repeatButton, &shuffleButton,
                                                             &addButton, &removeButton, &clearButton, &position, &list })
    {
        makeClickable (*c);
        addAndMakeVisible (c);
    }

    refresh();
    startTimerHz (10);
}

MediaPlayerPanel::~MediaPlayerPanel() = default;

bool MediaPlayerPanel::anyStripPlaysIt() const
{
    for (int s = 0; s < kNumStrips; ++s)
        if (engine.getStrip (s).active.load() && engine.getStrip (s).sourceType.load() == (int) SourceType::Media)
            return true;
    return false;
}

void MediaPlayerPanel::refresh()
{
    playButton.setButtonText (player.isPlaying() ? "Pause" : "Play");
    playButton.setTooltip (player.isPlaying() ? "Pause (Space)." : "Play (Space).");
    repeatButton.setButtonText (repeatNames[(int) player.repeat]);
    repeatButton.setToggleState (player.repeat != MediaPlayer::RepeatOff, juce::dontSendNotification);
    shuffleButton.setToggleState (player.shuffle, juce::dontSendNotification);
    const bool hasTracks = ! player.getPlaylist().isEmpty();
    for (auto* b : { &prevButton, &playButton, &nextButton, &clearButton })
        b->setEnabled (hasTracks);
    removeButton.setEnabled (list.getSelectedRow() >= 0);
    list.updateContent();
    list.repaint();
    timerCallback();
    repaint();
}

void MediaPlayerPanel::timerCallback()
{
    const double length = player.getLength();
    if (! seeking)
    {
        position.setRange (0.0, juce::jmax (0.1, length), 0.0);
        position.setValue (player.getPosition(), juce::dontSendNotification);
    }
    position.setEnabled (length > 0.0);
    if (playButton.getButtonText() != (player.isPlaying() ? "Pause" : "Play"))
        refresh();   // the track ended or changed on its own
    repaint (timeArea);
    repaint (titleArea);
}

bool MediaPlayerPanel::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::spaceKey)
    {
        player.togglePlay();
        refresh();
        return true;
    }
    return false;
}

int MediaPlayerPanel::getNumRows()
{
    return player.getPlaylist().size();
}

void MediaPlayerPanel::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    const auto& t = Theme::p();
    if (! juce::isPositiveAndBelow (row, player.getPlaylist().size()))
        return;
    const bool current = row == player.getCurrentIndex();
    if (selected)
        g.fillAll (t.selBg);
    g.setColour (current ? t.text : t.textDim);
    g.setFont (Theme::ui (13.0f, current));
    const auto name = player.getPlaylist()[row].getFileNameWithoutExtension();
    g.drawText ((current ? juce::String::fromUTF8 ("\xe2\x96\xb6  ") : juce::String ("     ")) + name,
                juce::Rectangle<int> (10, 0, width - 20, height), juce::Justification::centredLeft, true);
}

void MediaPlayerPanel::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    playRow (row);
}

void MediaPlayerPanel::playRow (int row)
{
    juce::String error;
    if (juce::isPositiveAndBelow (row, player.getPlaylist().size()) && ! player.playIndex (row, error))
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Couldn't play the track", error);
    refresh();
    if (onChanged) onChanged();
}

void MediaPlayerPanel::deleteKeyPressed (int row)
{
    if (row < 0)
        return;
    player.removeAt (row);
    refresh();
    if (onChanged) onChanged();
}

void MediaPlayerPanel::addFiles()
{
    chooser = std::make_unique<juce::FileChooser> ("Add tracks to the media player",
                                                   juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                   MediaPlayer::wildcard());
    juce::Component::SafePointer<MediaPlayerPanel> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectMultipleItems,
        [safe] (const juce::FileChooser& fc)
        {
            if (safe == nullptr || fc.getResults().isEmpty())
                return;
            safe->player.addFiles (fc.getResults());
            safe->refresh();
            if (safe->onChanged) safe->onChanged();
        });
}

bool MediaPlayerPanel::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;mp3;flac;ogg;aif;aiff"))
            return true;
    return false;
}

void MediaPlayerPanel::filesDropped (const juce::StringArray& files, int, int)
{
    juce::Array<juce::File> dropped;
    for (auto& f : files)
        dropped.add (juce::File (f));
    player.addFiles (dropped);
    refresh();
    if (onChanged) onChanged();
}

void MediaPlayerPanel::paint (juce::Graphics& g)
{
    const auto& t = Theme::p();
    g.fillAll (t.window);

    const auto name = player.getTrackName();
    g.setColour (t.text);
    g.setFont (Theme::ui (16.0f, true));
    g.drawFittedText (name.isNotEmpty() ? name : juce::String ("No track"), titleArea, juce::Justification::centredLeft, 1, 0.8f);

    g.setColour (t.textDim);
    g.setFont (Theme::mono (12.0f));
    g.drawText (timeText (player.getPosition()) + " / " + timeText (player.getLength()), timeArea, juce::Justification::centredRight);

    g.setFont (Theme::ui (12.0f));
    g.drawFittedText (anyStripPlaysIt()
                          ? juce::String ("Plays on every strip whose source is Media player. Drop audio files here to add them.")
                          : juce::String ("No strip plays this yet: open a strip's source menu and pick Media player."),
                      noteArea, juce::Justification::centredLeft, 2);
}

void MediaPlayerPanel::resized()
{
    auto r = getLocalBounds().reduced (Theme::s4);
    constexpr int rowH = 30;

    auto top = r.removeFromTop (rowH);
    timeArea = top.removeFromRight (110);
    titleArea = top;
    r.removeFromTop (Theme::s2);
    position.setBounds (r.removeFromTop (24));
    r.removeFromTop (Theme::s2);

    auto transportRow = r.removeFromTop (rowH);
    for (auto* b : { &prevButton, &playButton, &nextButton })
    {
        b->setBounds (transportRow.removeFromLeft (72));
        transportRow.removeFromLeft (Theme::s1);
    }
    shuffleButton.setBounds (transportRow.removeFromRight (80));
    transportRow.removeFromRight (Theme::s1);
    repeatButton.setBounds (transportRow.removeFromRight (100));
    r.removeFromTop (Theme::s3);

    noteArea = r.removeFromBottom (34);
    auto listButtons = r.removeFromBottom (rowH);
    r.removeFromBottom (Theme::s2);
    addButton.setBounds (listButtons.removeFromLeft (100));
    listButtons.removeFromLeft (Theme::s1);
    removeButton.setBounds (listButtons.removeFromLeft (80));
    listButtons.removeFromLeft (Theme::s1);
    clearButton.setBounds (listButtons.removeFromLeft (70));
    list.setBounds (r);
}
