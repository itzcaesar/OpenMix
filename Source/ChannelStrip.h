#pragma once

#include <JuceHeader.h>
#include "VoicePad.h"
#include "ExternalIO.h"

constexpr int kNumStrips = 3;
constexpr int kNumSlots  = 3;
constexpr int kNumBuses  = 3;                     // A1, A2, B1
inline const char* const kBusNames[kNumBuses] { "A1", "A2", "B1" };

// Where a strip's audio comes from.
enum class SourceType : int
{
    None   = 0,
    Main   = 1,   // a channel of the main (ASIO/WASAPI) device
    Device = 2,   // a second Windows input device
    App    = 3    // one application's audio (process loopback)
};

// One input strip: source -> gain -> VST3 slots -> VoicePad -> fader -> bus sends.
class ChannelStrip
{
public:
    ChannelStrip();
    ~ChannelStrip();

    void prepare (double sampleRate, int maxBlockSize);
    void release();

    // Audio thread. Writes the processed stereo signal into `out` (2 channels).
    // inL/inR may be the same pointer (mono source) or nullptr (silence).
    void process (const float* inL, const float* inR, juce::AudioBuffer<float>& out, int numSamples) noexcept;

    // Audio thread: pulls from the external source, if any. Returns false if there's none.
    bool pullExternal (float* l, float* r, int numSamples, double sampleRate) noexcept;

    // Message thread: replaces the external source (nullptr removes it).
    void setExternalSource (std::unique_ptr<ExternalSource>);
    ExternalSource* getExternalSource() const noexcept { return external.get(); }

    std::array<std::atomic<bool>, kNumSlots> slotBypass {};
    juce::String name;               // user-editable label (message thread only)

    // Source description (message thread only)
    juce::String sourceDeviceName;   // for SourceType::Device
    juce::String sourceAppExe;       // for SourceType::App, e.g. "Spotify.exe"
    std::atomic<int> sourceType { (int) SourceType::Main };

    // Message thread: plugin slot management.
    bool loadPlugin (int slot, const juce::PluginDescription&, juce::AudioPluginFormatManager&, juce::String& error);
    void removePlugin (int slot);
    juce::AudioPluginInstance* getPlugin (int slot) const noexcept { return slots[(size_t) slot].get(); }

    // Parameters (written by UI, read by audio thread)
    std::atomic<int>   inputChannel { -1 };   // index into active device inputs, -1 = none
    std::atomic<float> gainDb   { 0.0f };
    std::atomic<float> faderDb  { 0.0f };
    std::atomic<bool>  mute     { false };
    std::atomic<bool>  busOn[kNumBuses] { { true }, { false }, { false } };
    VoicePadParams   pad;

    // Metering (written by audio thread)
    std::atomic<float> peakL { 0.0f }, peakR { 0.0f };

private:
    void preparePlugin (juce::AudioPluginInstance&);

    std::array<std::unique_ptr<juce::AudioPluginInstance>, kNumSlots> slots;
    juce::CriticalSection slotLock;

    std::unique_ptr<ExternalSource> external;
    juce::CriticalSection sourceLock;

    VoicePadProcessor padProcessor { pad };
    juce::AudioBuffer<float> scratch;   // wide buffer for plugins with >2 channels
    juce::MidiBuffer midi;
    juce::SmoothedValue<float> inGain, outGain;

    double sampleRate = 48000.0;
    int blockSize = 512;

    static constexpr int kMaxPluginChannels = 16;
};
