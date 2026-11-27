#pragma once

#include <JuceHeader.h>
#include "engine/ChannelStrip.h"   // kNumBuses
#include "dsp/Metering.h"

// 32 sound pads (the panel shows them in pages of 8). Files are decoded and resampled to the device rate when loaded (message
// thread) or when the sample rate changes (device thread), never on the audio thread.
// Each pad has its own volume, colour and play mode (see Mode).
class Soundboard
{
public:
    static constexpr int numPads = 32;
    static constexpr int padsPerPage = 8;
    static constexpr double maxSeconds = 60.0;

    // What a press does.
    enum Mode
    {
        OneShot = 0,   // play once; press again to stop
        Restart = 1,   // play once; press again to start over
        Loop    = 2,   // play over and over; press again to stop
        Hold    = 3    // play while the pad (or its key) is held
    };
    static constexpr int numModes = 4;

    struct PadInfo
    {
        juce::String name, file;
        double seconds = 0.0;
        float volumeDb = 0.0f;
        int mode = OneShot;
        int colour = -1;   // index into the panel's palette; -1 = by position
    };

    Soundboard();

    // Message thread.
    bool loadPad (int pad, const juce::File&, juce::String& error);
    void clearPad (int pad);
    void renamePad (int pad, const juce::String& name);
    PadInfo getPadInfo (int pad) const;
    void setPadVolume (int pad, float db);
    void setPadMode (int pad, int mode);
    void setPadColour (int pad, int colour);
    void trigger (int pad);                     // a press: what it does depends on the pad's mode
    void release (int pad);                     // the press ended (stops a Hold pad)
    void stopAll();
    bool isPlaying (int pad) const noexcept     { return pads[(size_t) pad].playingFlag.load(); }
    float getProgress (int pad) const noexcept  { return pads[(size_t) pad].progress.load(); }   // 0..1

    // Device thread (before audio starts).
    void prepare (double sampleRate);

    // Audio thread: adds the pads into left/right (which the caller cleared).
    // Returns false if nothing is playing (the caller can skip the mix).
    bool render (float* left, float* right, int numSamples) noexcept;

    std::atomic<float> volumeDb { 0.0f };
    std::atomic<bool>  busOn[kNumBuses] {};
    std::atomic<float> peakL { 0.0f }, peakR { 0.0f };
    std::atomic<float> rmsL { 0.0f }, rmsR { 0.0f };

private:
    struct Pad
    {
        PadInfo info;
        juce::AudioBuffer<float> original;      // as decoded
        double originalRate = 0.0;
        juce::AudioBuffer<float> playable;      // at the device rate
        std::atomic<int> command { 0 };         // 1 = start, 2 = stop (message -> audio)
        std::atomic<bool> playingFlag { false };// audio -> UI
        std::atomic<float> progress { 0.0f };   // audio -> UI
        std::atomic<float> gainLin { 1.0f };    // message -> audio (from info.volumeDb)
        std::atomic<int> mode { OneShot };      // message -> audio
        int pos = 0;                            // audio thread
        bool playing = false;                   // audio thread
        int fadeOut = 0;                        // audio thread: samples left of a stop fade
    };

    void makePlayable (Pad&, double targetRate);

    std::array<Pad, numPads> pads;
    mutable juce::CriticalSection lock;         // guards `playable` swaps vs. the audio thread
    juce::AudioFormatManager formats;
    juce::SmoothedValue<float> gain;
    omx::RmsMeter rmsMeterL, rmsMeterR;
    double rate = 48000.0;
};
