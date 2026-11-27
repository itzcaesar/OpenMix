#pragma once

#include <JuceHeader.h>

// Windows Media Foundation decoding for what JUCE's own readers don't open: MP4, M4A, AAC,
// WMA, MOV, MKV, AVI, WMV and the audio of any other container Windows can play.

// An AudioFormatReader for the file's first audio stream (32-bit float), or nullptr.
std::unique_ptr<juce::AudioFormatReader> createMediaFoundationReader (const juce::File&);

// Decodes the frame at a given time of a video file on its own thread, so the media bar can
// show it. setTime() from the message thread (the player's position); getFrame() returns the
// latest decoded frame.
class VideoFrames : private juce::Thread
{
public:
    VideoFrames();
    ~VideoFrames() override;

    bool open (const juce::File&);   // false when the file has no video stream
    void close();
    bool hasVideo() const noexcept { return reader != nullptr; }
    void setTime (double seconds) noexcept { target.store (seconds); notify(); }
    juce::Image getFrame() const;

private:
    void run() override;

    struct Reader;
    std::unique_ptr<Reader> reader;
    std::atomic<double> target { 0.0 };
    mutable juce::CriticalSection frameLock;
    juce::Image frame;
};
