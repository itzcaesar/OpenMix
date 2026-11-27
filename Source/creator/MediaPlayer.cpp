#include "creator/MediaPlayer.h"

MediaPlayer::MediaPlayer()
{
    formats.registerBasicFormats();   // WAV, AIFF, FLAC, Ogg Vorbis, MP3
    readAhead.startThread (juce::Thread::Priority::normal);
    startTimerHz (5);
}

MediaPlayer::~MediaPlayer()
{
    stopTimer();
    transport.setSource (nullptr);
    readerSource.reset();
    readAhead.stopThread (2000);
}

void MediaPlayer::addFiles (const juce::Array<juce::File>& files)
{
    for (auto& f : files)
        if (f.existsAsFile() && f.hasFileExtension ("wav;mp3;flac;ogg;aif;aiff"))
            playlist.add (f);
    changed();
}

void MediaPlayer::removeAt (int index)
{
    if (! juce::isPositiveAndBelow (index, playlist.size()))
        return;

    playlist.remove (index);
    if (index == current)
    {
        transport.stop();
        transport.setSource (nullptr);
        readerSource.reset();
        current = -1;
    }
    else if (index < current)
    {
        --current;
    }
    changed();
}

void MediaPlayer::clearPlaylist()
{
    transport.stop();
    transport.setSource (nullptr);
    readerSource.reset();
    playlist.clear();
    current = -1;
    changed();
}

bool MediaPlayer::loadIndex (int index, juce::String& error)
{
    if (! juce::isPositiveAndBelow (index, playlist.size()))
        return false;

    const auto file = playlist[index];
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
    {
        error = file.getFileName() + " couldn't be opened. It may have been moved, or isn't WAV, MP3, FLAC, Ogg or AIFF.";
        return false;
    }

    const double fileRate = reader->sampleRate;
    auto source = std::make_unique<juce::AudioFormatReaderSource> (reader.release(), true);
    transport.stop();
    transport.setSource (source.get(), 48000, &readAhead, fileRate, 2);   // ~1 s read ahead
    readerSource = std::move (source);
    current = index;
    wasPlaying = false;
    changed();
    return true;
}

bool MediaPlayer::playIndex (int index, juce::String& error)
{
    if (! loadIndex (index, error))
        return false;
    play();
    return true;
}

void MediaPlayer::play()
{
    if (readerSource == nullptr)
    {
        juce::String error;
        if (playlist.isEmpty() || ! loadIndex (juce::jmax (0, current), error))
            return;
    }
    if (transport.hasStreamFinished())
        transport.setPosition (0.0);
    transport.start();
    wasPlaying = true;
    changed();
}

void MediaPlayer::pause()
{
    transport.stop();
    wasPlaying = false;
    changed();
}

void MediaPlayer::togglePlay()
{
    if (isPlaying()) pause(); else play();
}

int MediaPlayer::pickNext (int direction) const
{
    const int n = playlist.size();
    if (n == 0)
        return -1;
    if (shuffle && n > 1)
    {
        int pick = current;
        while (pick == current)
            pick = juce::Random::getSystemRandom().nextInt (n);
        return pick;
    }
    const int next = current + direction;
    if (next >= 0 && next < n)
        return next;
    return repeat == RepeatAll ? (next + n) % n : -1;
}

void MediaPlayer::next()
{
    juce::String error;
    const bool keepPlaying = isPlaying();
    // Skip over tracks that can't be opened (moved or deleted), at most once round the list.
    for (int tries = 0, i = pickNext (1); i >= 0 && tries < playlist.size(); ++tries)
    {
        if (keepPlaying ? playIndex (i, error) : loadIndex (i, error))
            return;
        current = i;
        i = pickNext (1);
    }
}

void MediaPlayer::previous()
{
    // Like a CD player: past the first few seconds, go back to the start of this track.
    if (getPosition() > 3.0 || playlist.size() < 2)
    {
        transport.setPosition (0.0);
        return;
    }
    juce::String error;
    const int i = pickNext (-1);
    if (i >= 0)
        isPlaying() ? playIndex (i, error) : loadIndex (i, error);
}

juce::String MediaPlayer::getTrackName() const
{
    return juce::isPositiveAndBelow (current, playlist.size()) ? playlist[current].getFileNameWithoutExtension() : juce::String();
}

void MediaPlayer::timerCallback()
{
    // End of a track: repeat it, go on to the next, or stop at the end of the list.
    if (wasPlaying && transport.hasStreamFinished())
    {
        if (repeat == RepeatOne)
        {
            transport.setPosition (0.0);
            transport.start();
            return;
        }
        juce::String error;
        const int i = pickNext (1);
        if (i < 0 || ! playIndex (i, error))
        {
            transport.stop();
            transport.setPosition (0.0);
            wasPlaying = false;
            changed();
        }
    }
}

void MediaPlayer::prepare (double sampleRate, int maxBlock)
{
    scratch.setSize (2, maxBlock);
    transport.prepareToPlay (maxBlock, sampleRate);
}

void MediaPlayer::release()
{
    transport.releaseResources();
}

void MediaPlayer::render (float* l, float* r, int numSamples) noexcept
{
    if (numSamples > scratch.getNumSamples())
    {
        juce::FloatVectorOperations::clear (l, numSamples);
        juce::FloatVectorOperations::clear (r, numSamples);
        return;
    }
    juce::AudioSourceChannelInfo info (&scratch, 0, numSamples);
    transport.getNextAudioBlock (info);
    juce::FloatVectorOperations::copy (l, scratch.getReadPointer (0), numSamples);
    juce::FloatVectorOperations::copy (r, scratch.getReadPointer (1), numSamples);
}
