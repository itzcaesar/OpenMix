#include "ui/MediaPlayerPanel.h"

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
    tape.setText ("Media player");

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

    addButton.setTooltip ("Add audio or video files (MP3, WAV, FLAC, M4A, MP4, MKV, MOV and more). You can also drop files on the bar.");
    addButton.onClick = [this] { addFiles(); };
    removeButton.setTooltip ("Remove the selected track (or press Delete).");
    removeButton.onClick = [this] { deleteKeyPressed (list.getSelectedRow()); };
    clearButton.setTooltip ("Empty the playlist.");
    clearButton.onClick = [this] { player.clearPlaylist(); refresh(); if (onChanged) onChanged(); };

    list.setRowHeight (22);
    list.setTitle ("Playlist");
    list.setColour (juce::ListBox::backgroundColourId, Theme::p().well);
    list.setColour (juce::ListBox::outlineColourId, Theme::p().line);
    list.setOutlineThickness (1);

    addAndMakeVisible (tape);
    for (auto* c : std::initializer_list<juce::Component*> { &prevButton, &playButton, &nextButton, &repeatButton, &shuffleButton,
                                                             &addButton, &removeButton, &clearButton, &list })
    {
        makeClickable (*c);
        addAndMakeVisible (c);
    }

    thumb.addChangeListener (this);
    refresh();
    startTimerHz (30);
}

MediaPlayerPanel::~MediaPlayerPanel()
{
    thumb.removeChangeListener (this);
}

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
    trackChanged();
    repaint();
}

// A new track: read its waveform, and open its picture if it's a video.
void MediaPlayerPanel::trackChanged()
{
    const int i = player.getCurrentIndex();
    const auto file = juce::isPositiveAndBelow (i, player.getPlaylist().size()) ? player.getPlaylist()[i] : juce::File();
    if (file == shownFile)
        return;
    shownFile = file;

    thumb.clear();
    if (file != juce::File())
        if (auto reader = player.createReader (file))
            thumb.setReader (reader.release(), file.hashCode64());

    video.close();
    if (MediaPlayer::isVideoFile (file))
    {
        video.open (file);
        video.setTime (player.getPosition());
    }
    resized();
    repaint();
}

void MediaPlayerPanel::timerCallback()
{
    const double pos = player.getPosition();
    if (video.hasVideo())
    {
        video.setTime (pos);
        repaint (videoArea);
    }
    if (pos != lastPosition)
    {
        lastPosition = pos;
        repaint (waveArea);
        repaint (timeArea);
    }
    if (playButton.getButtonText() != (player.isPlaying() ? "Pause" : "Play")
        || shownFile != (juce::isPositiveAndBelow (player.getCurrentIndex(), player.getPlaylist().size())
                             ? player.getPlaylist()[player.getCurrentIndex()] : juce::File()))
        refresh();   // the track ended or changed on its own
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

void MediaPlayerPanel::seekTo (int x)
{
    const double length = player.getLength();
    if (length <= 0.0 || waveArea.getWidth() <= 0)
        return;
    player.setPosition (juce::jlimit (0.0, length, (x - waveArea.getX()) * length / waveArea.getWidth()));
    lastPosition = -1.0;
    timerCallback();
}

void MediaPlayerPanel::mouseDown (const juce::MouseEvent& e)
{
    seeking = waveArea.contains (e.getPosition());
    if (seeking)
        seekTo (e.x);
}

void MediaPlayerPanel::mouseDrag (const juce::MouseEvent& e)
{
    if (seeking)
        seekTo (e.x);
}

void MediaPlayerPanel::mouseUp (const juce::MouseEvent&)
{
    seeking = false;
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
    g.setFont (Theme::ui (12.0f, current));
    const auto name = player.getPlaylist()[row].getFileNameWithoutExtension();
    g.drawText ((current ? juce::String::fromUTF8 ("\xe2\x96\xb6  ") : juce::String ("     ")) + name,
                juce::Rectangle<int> (8, 0, width - 16, height), juce::Justification::centredLeft, true);
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
        if (juce::File (f).hasFileExtension (MediaPlayer::extensions()))
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
    Theme::drawFaceplate (g, getLocalBounds().toFloat());
    Theme::drawLegend (g, "Playlist", playlistLegend);

    // Video picture, letterboxed.
    if (! videoArea.isEmpty())
    {
        g.setColour (juce::Colours::black);
        g.fillRect (videoArea);
        const auto img = video.getFrame();
        if (img.isValid())
            g.drawImage (img, videoArea.toFloat(), juce::RectanglePlacement::centred);
    }

    const auto name = player.getTrackName();
    g.setColour (t.text);
    g.setFont (Theme::ui (14.0f, true));
    g.drawFittedText (name.isNotEmpty() ? name : juce::String ("No track"), titleArea, juce::Justification::centredLeft, 1, 0.8f);

    g.setColour (t.textDim);
    g.setFont (Theme::mono (12.0f));
    g.drawText (timeText (player.getPosition()) + " / " + timeText (player.getLength()), timeArea, juce::Justification::centredRight);

    // Waveform: the played part in the accent colour, a line at the play position.
    g.setColour (t.well);
    g.fillRoundedRectangle (waveArea.toFloat(), 4.0f);
    const double length = player.getLength();
    if (thumb.getTotalLength() > 0.0 && length > 0.0)
    {
        const auto wave = waveArea.reduced (2, 4);
        const float zoom = juce::jlimit (1.0f, 8.0f, 0.9f / juce::jmax (0.001f, thumb.getApproximatePeak()));   // quiet files stay readable
        const int headX = waveArea.getX() + (int) std::round (waveArea.getWidth() * juce::jlimit (0.0, 1.0, player.getPosition() / length));
        {
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (waveArea.withRight (headX));
            g.setColour (t.accent);
            thumb.drawChannels (g, wave, 0.0, thumb.getTotalLength(), zoom);
        }
        {
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (waveArea.withLeft (headX));
            g.setColour (t.textFaint);
            thumb.drawChannels (g, wave, 0.0, thumb.getTotalLength(), zoom);
        }
        g.setColour (t.text);
        g.fillRect (headX, waveArea.getY(), 2, waveArea.getHeight());
    }
    else
    {
        g.setColour (t.textDim);
        g.setFont (Theme::ui (12.0f));
        g.drawFittedText (player.getPlaylist().isEmpty() ? juce::String ("Drop audio or video files here, or click + Add.")
                                                         : juce::String ("Double-click a track in the playlist to play it."),
                          waveArea.reduced (Theme::s2), juce::Justification::centred, 2);
    }

    if (! anyStripPlaysIt())
    {
        g.setColour (t.textDim);
        g.setFont (Theme::ui (11.0f));
        g.drawText ("Not on any strip yet: pick Media player in a strip's source menu.",
                    waveArea.withHeight (16).translated (0, waveArea.getHeight() + 2), juce::Justification::centredLeft, true);
    }
}

void MediaPlayerPanel::resized()
{
    auto r = getLocalBounds().reduced (Theme::s3);
    constexpr int rowH = 28;

    // Left: name, transport, repeat and shuffle.
    auto left = r.removeFromLeft (196);
    tape.setBounds (left.removeFromTop (rowH));
    auto modes = left.removeFromBottom (rowH);
    repeatButton.setBounds (modes.removeFromLeft (110));
    modes.removeFromLeft (Theme::s1);
    shuffleButton.setBounds (modes);
    auto transport = left.withSizeKeepingCentre (left.getWidth(), rowH);
    const int w = (transport.getWidth() - 2 * Theme::s1) / 3;
    for (auto* b : { &prevButton, &playButton, &nextButton })
    {
        b->setBounds (transport.removeFromLeft (w));
        transport.removeFromLeft (Theme::s1);
    }
    r.removeFromLeft (Theme::s4);

    // Right: the playlist with its buttons.
    auto right = r.removeFromRight (juce::jlimit (200, 300, r.getWidth() / 3));
    r.removeFromRight (Theme::s4);
    playlistLegend = right.removeFromTop (16);
    auto buttons = right.removeFromBottom (24);
    right.removeFromBottom (Theme::s1);
    list.setBounds (right);
    addButton.setBounds (buttons.removeFromLeft (64));
    buttons.removeFromLeft (Theme::s1);
    removeButton.setBounds (buttons.removeFromLeft (70));
    buttons.removeFromLeft (Theme::s1);
    clearButton.setBounds (buttons.removeFromLeft (56));

    // Video picture (16:9, video files only), then title, time and the waveform.
    videoArea = {};
    if (video.hasVideo())
    {
        videoArea = r.removeFromLeft (r.getHeight() * 16 / 9);
        r.removeFromLeft (Theme::s3);
    }
    auto top = r.removeFromTop (rowH - 4);
    timeArea = top.removeFromRight (100);
    titleArea = top;
    r.removeFromTop (Theme::s1);
    waveArea = r.withTrimmedBottom (18);
}
