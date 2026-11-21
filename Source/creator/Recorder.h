#pragma once

#include <JuceHeader.h>

// Recording to WAV (24-bit) without touching the disk from the audio thread: samples go into
// JUCE ThreadedWriter FIFOs that a background thread drains. One file per recorded bus/strip,
// in Documents\OpenMix Recordings\<date time>\.
class Recorder
{
public:
    static constexpr int maxTargets = 16;

    struct Target
    {
        juce::String name;   // file name, e.g. "A1" or "Mic"
        bool isBus = true;
        int index = 0;
    };

    Recorder();
    ~Recorder();

    // Message thread. Returns false + error if nothing could be opened.
    bool start (const std::vector<Target>&, double sampleRate, juce::String& error);
    void stop();
    bool isRecording() const noexcept { return recording.load(); }
    double getElapsedSeconds() const;
    juce::File getLastFolder() const { return lastFolder; }
    static juce::File getRecordingsFolder();

    // Audio thread (real-time safe: lock-free FIFO writes, skipped while start/stop swaps).
    void writeBus (int bus, const float* left, const float* right, int numSamples) noexcept;
    void writeStrip (int strip, const float* left, const float* right, int numSamples) noexcept;

private:
    void write (bool isBus, int index, const float* left, const float* right, int numSamples) noexcept;

    struct Slot
    {
        bool isBus = true;
        int index = -1;
        std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> writer;
    };

    juce::TimeSliceThread diskThread { "OpenMix recorder" };
    juce::CriticalSection lock;
    std::vector<Slot> slots;
    std::atomic<bool> recording { false };
    juce::uint32 startedAtMs = 0;
    juce::File lastFolder;
};

// Keeps the last N seconds of one bus in memory; saves it as a WAV on request.
class ReplayBuffer
{
public:
    // Message thread: (re)allocates. seconds <= 0 turns it off.
    void configure (double sampleRate, double seconds);
    bool isEnabled() const noexcept { return enabled.load(); }
    double getSeconds() const noexcept { return seconds; }

    // Audio thread.
    void push (const float* left, const float* right, int numSamples) noexcept;

    // Message thread: copies the buffer and writes it on a background thread. Calls onDone on
    // the message thread with the file (or an empty file and an error).
    void save (std::function<void (juce::File, juce::String)> onDone);

    std::atomic<int> sourceBus { 0 };

private:
    juce::CriticalSection lock;
    juce::AudioBuffer<float> ring;
    int writePos = 0, filled = 0;
    double rate = 48000.0, seconds = 0.0;
    std::atomic<bool> enabled { false };
};
