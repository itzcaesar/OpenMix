#pragma once

#include <JuceHeader.h>

// The media player: a playlist of audio and video files (their sound) streamed from disk (a background thread reads
// ahead, AudioTransportSource resamples to the device rate). A strip whose source is
// "Media player" plays it, so the strip's EQ, compressor, ducking and sends all apply.
class MediaPlayer : private juce::Timer
{
public:
    enum Repeat { RepeatOff = 0, RepeatAll = 1, RepeatOne = 2 };

    MediaPlayer();
    ~MediaPlayer() override;

    // Message thread: the playlist.
    void addFiles (const juce::Array<juce::File>&);
    void removeAt (int index);
    void clearPlaylist();
    void eject();   // stops and lets go of the current file; the playlist stays
    const juce::Array<juce::File>& getPlaylist() const noexcept { return playlist; }
    int getCurrentIndex() const noexcept { return current; }

    // Message thread: transport. playIndex loads a track and starts it (false + error if the
    // file can't be read). next / previous follow shuffle and repeat.
    bool playIndex (int index, juce::String& error);
    bool loadIndex (int index, juce::String& error);   // like playIndex, but paused
    void play();
    void pause();
    void togglePlay();
    void next();
    void previous();
    bool isPlaying() const noexcept { return transport.isPlaying(); }
    double getPosition() const { return transport.getCurrentPosition(); }
    double getLength() const { return transport.getLengthInSeconds(); }
    void setPosition (double seconds) { transport.setPosition (seconds); }
    juce::String getTrackName() const;

    Repeat repeat = RepeatAll;
    bool shuffle = false;

    // Called on the message thread when the track, playlist or play state changes.
    std::function<void()> onChange;

    // Device thread.
    void prepare (double sampleRate, int maxBlock);
    void release();

    // Audio thread: writes the next block into l / r (silence when stopped).
    void render (float* l, float* r, int numSamples) noexcept;

    // Files it takes: JUCE reads WAV, AIFF, FLAC, Ogg and MP3; Windows Media Foundation the rest.
    static juce::String extensions() { return "wav;mp3;flac;ogg;aif;aiff;m4a;aac;wma;mp4;m4v;mov;mkv;avi;wmv;webm;3gp"; }
    static juce::String wildcard() { return "*." + extensions().replace (";", ";*."); }
    static bool isVideoFile (const juce::File& f) { return f.hasFileExtension ("mp4;m4v;mov;mkv;avi;wmv;webm;3gp"); }

    // A reader for any file in extensions(), or nullptr (the caller owns it).
    std::unique_ptr<juce::AudioFormatReader> createReader (const juce::File&);

private:
    void timerCallback() override;
    int pickNext (int direction) const;
    void changed() { if (onChange) onChange(); }

    juce::AudioFormatManager formats;
    juce::TimeSliceThread readAhead { "OpenMix media read-ahead" };
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    juce::AudioTransportSource transport;
    juce::AudioBuffer<float> scratch;
    juce::Array<juce::File> playlist;
    int current = -1;
    bool wasPlaying = false;
};
