#pragma once

#include <JuceHeader.h>
#include "engine/ChannelStrip.h"   // kNumBuses

// Eight sound pads. Files are decoded and resampled to the device rate when loaded (message
// thread) or when the sample rate changes (device thread), never on the audio thread.
// A pad plays once from the start; pressing it again while playing stops it.
class Soundboard
{
public:
    static constexpr int numPads = 8;
    static constexpr double maxSeconds = 60.0;

    struct PadInfo
    {
        juce::String name, file;
        double seconds = 0.0;
    };

    Soundboard();

    // Message thread.
    bool loadPad (int pad, const juce::File&, juce::String& error);
    void clearPad (int pad);
    void renamePad (int pad, const juce::String& name);
    PadInfo getPadInfo (int pad) const;
    void trigger (int pad);                     // play, or stop if it's playing
    void stopAll();
    bool isPlaying (int pad) const noexcept     { return pads[(size_t) pad].playingFlag.load(); }

    // Device thread (before audio starts).
    void prepare (double sampleRate);

    // Audio thread: adds the pads into left/right (which the caller cleared).
    // Returns false if nothing is playing (the caller can skip the mix).
    bool render (float* left, float* right, int numSamples) noexcept;

    std::atomic<float> volumeDb { 0.0f };
    std::atomic<bool>  busOn[kNumBuses] {};
    std::atomic<float> peakL { 0.0f }, peakR { 0.0f };

private:
    struct Pad
    {
        PadInfo info;
        juce::AudioBuffer<float> original;      // as decoded
        double originalRate = 0.0;
        juce::AudioBuffer<float> playable;      // at the device rate
        std::atomic<int> command { 0 };         // 1 = start, 2 = stop (message -> audio)
        std::atomic<bool> playingFlag { false };// audio -> UI
        int pos = 0;                            // audio thread
        bool playing = false;                   // audio thread
    };

    void makePlayable (Pad&, double targetRate);

    std::array<Pad, numPads> pads;
    mutable juce::CriticalSection lock;         // guards `playable` swaps vs. the audio thread
    juce::AudioFormatManager formats;
    juce::SmoothedValue<float> gain;
    double rate = 48000.0;
};
