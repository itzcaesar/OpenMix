#pragma once

#include <JuceHeader.h>
#include "VoicePad.h"
#include "ExternalIO.h"
#include "AudioSafety.h"
#include "DelayLine.h"
#include "dsp/Dynamics.h"
#include "dsp/Denoiser.h"

// Capacity. Strips and buses are allocated up front (real-time safe) and switched on/off by the
// user; only active ones are processed and shown.
constexpr int kNumStrips = 8;
constexpr int kNumSlots  = 6;                     // insert slots per strip
constexpr int kNumBuses  = 6;                     // A1-A3 hardware, B1-B3 virtual
inline const char* const kBusNames[kNumBuses] { "A1", "A2", "A3", "B1", "B2", "B3" };
constexpr int kDefaultStrips = 3;
inline bool isDefaultBus (int b) { return b == 0 || b == 1 || b == 3; }   // A1, A2, B1

// Where a strip's audio comes from.
enum class SourceType : int
{
    None   = 0,
    Main   = 1,   // a channel (or stereo pair) of the main (ASIO/WASAPI) device
    Device = 2,   // a second Windows input device
    App    = 3    // one application's audio (process loopback)
};

// Settings of the built-in processors. Written by the UI, read by the audio thread.
struct ProcessingParams
{
    std::atomic<bool>  denoiseOn { false };
    std::atomic<float> denoiseAmount { 1.0f };      // 0..1

    std::atomic<bool>  gateOn { false };
    std::atomic<float> gateThresholdDb { -50.0f };
    std::atomic<float> gateRangeDb { -40.0f };

    std::atomic<bool>  eqOn { false };
    std::atomic<float> eqLowDb { 0.0f }, eqMidDb { 0.0f }, eqMidHz { 1000.0f }, eqHighDb { 0.0f };

    std::atomic<bool>  compOn { false };
    std::atomic<float> compThresholdDb { -18.0f };
    std::atomic<float> compRatio { 3.0f };

    std::atomic<bool>  deessOn { false };
    std::atomic<float> deessThresholdDb { -30.0f };
    std::atomic<float> deessMaxDb { 10.0f };

    // Ducking: this strip gets quieter while another strip (e.g. the mic) has signal.
    std::atomic<bool>  duckOn { false };
    std::atomic<int>   duckSource { -1 };           // strip index
    std::atomic<float> duckDepthDb { -12.0f };
    std::atomic<float> duckThresholdDb { -40.0f };

    // Meters for the UI: lowest gain since the UI last read it (1 = nothing happening).
    std::atomic<float> gateGain { 1.0f }, compGain { 1.0f }, deessGain { 1.0f }, duckGain { 1.0f };
    std::atomic<float> voiceProbability { 0.0f };
    std::atomic<bool>  denoiseUnavailable { false }; // sample rate isn't 48 kHz
};

// One input strip. Signal flow (top to bottom, same as the UI):
// source -> gain -> polarity / mono -> DC blocker -> noise removal -> gate -> EQ -> compressor
// -> de-esser -> VST3 inserts -> VoicePad -> ducking -> latency alignment -> [pre-fader tap]
// -> mute -> fader -> bus sends.
class ChannelStrip
{
public:
    ChannelStrip();
    ~ChannelStrip();

    void prepare (double sampleRate, int maxBlockSize);
    void release();

    // Audio thread. Writes the post-fader signal into `out` and the pre-fader (post-mute) signal
    // into `preOut` (both 2 channels). inL/inR may be the same pointer (mono source) or nullptr.
    void process (const float* inL, const float* inR, juce::AudioBuffer<float>& out,
                  juce::AudioBuffer<float>& preOut, int numSamples) noexcept;

    // Audio thread: pulls from the external source, if any. Returns false if there's none.
    bool pullExternal (float* l, float* r, int numSamples, double sampleRate) noexcept;

    // Message thread: replaces the external source (nullptr removes it).
    void setExternalSource (std::unique_ptr<ExternalSource>);
    ExternalSource* getExternalSource() const noexcept { return external.get(); }

    // Message thread: plugin slot management. Loaded plugins are always packed into slots
    // 0..n-1 (no gaps), so the UI can show them as a simple ordered list.
    bool loadPlugin (int slot, const juce::PluginDescription&, juce::AudioPluginFormatManager&, juce::String& error);
    void installPlugin (int slot, std::unique_ptr<juce::AudioPluginInstance>);   // takes ownership, prepares it
    void removePlugin (int slot);                                                 // later slots move up
    void removeAllPlugins();
    void movePlugin (int from, int to);                                           // reorder the chain
    juce::AudioPluginInstance* getPlugin (int slot) const noexcept { return slots[(size_t) slot].get(); }
    int getNumPlugins() const noexcept;

    // Back to a fresh strip (used when the user removes it).
    void resetToDefaults();

    //==========================================================================
    std::atomic<bool> active { false };      // shown and processed

    std::array<std::atomic<bool>, kNumSlots> slotBypass {};
    std::array<std::atomic<bool>, kNumSlots> slotFault {};   // plugin output NaN/Inf -> auto-bypassed
    std::atomic<int> faultCount { 0 };                       // NaN/Inf events caught on this strip

    // Message thread only
    juce::String name;                  // user-editable label
    int colour = 0;                     // tape colour index (Theme::tapeColours)
    juce::String sourceDeviceName;      // for SourceType::Device
    juce::String sourceAppExe;          // for SourceType::App, e.g. "Spotify.exe"
    juce::String appOutputDeviceId;     // where the captured app's own output goes ("" = leave it)
    juce::String appOutputDeviceName;
    uint32_t redirectedPid = 0;         // the app process OpenMix rerouted (to undo it later)

    std::atomic<int> sourceType { (int) SourceType::Main };

    // Latency compensation. The strip reports its latency (plugins incl. bypassed ones, plus
    // noise removal); the engine delays every strip to match the slowest.
    std::atomic<int> latencySamples { 0 };
    std::atomic<int> alignDelay { 0 };

    // Parameters (written by UI, read by audio thread)
    std::atomic<int>   inputChannel { -1 };       // main device input, -1 = none
    std::atomic<int>   inputChannelRight { -1 };  // second channel for a stereo pair, -1 = mono
    std::atomic<float> gainDb   { 0.0f };
    std::atomic<float> faderDb  { 0.0f };
    std::atomic<bool>  mute     { false };
    std::atomic<bool>  solo     { false };        // listen on the monitor bus
    std::atomic<bool>  mono     { false };
    std::atomic<bool>  invertPolarity { false };

    std::atomic<bool>  busOn[kNumBuses] {};
    std::atomic<float> sendDb[kNumBuses] {};      // send level, 0 dB = unity
    std::atomic<bool>  sendPre[kNumBuses] {};     // pre-fader send (independent of the fader)

    VoicePadParams   pad;
    ProcessingParams fx;

    // Set by the engine each block (audio thread): ducking target gain for this strip.
    std::atomic<float> duckTarget { 1.0f };

    // Metering (written by audio thread)
    std::atomic<float> peakL { 0.0f }, peakR { 0.0f };
    std::atomic<float> sidechainLevel { 0.0f };   // pre-fader peak of the last block, for ducking

private:
    void preparePlugin (juce::AudioPluginInstance&);
    void runInserts (float* L, float* R, juce::AudioBuffer<float>& out, int numSamples, int& chainLatency) noexcept;
    static void storeMin (std::atomic<float>& meter, float value) noexcept
    {
        if (value < meter.load (std::memory_order_relaxed))
            meter.store (value, std::memory_order_relaxed);
    }

    std::array<std::unique_ptr<juce::AudioPluginInstance>, kNumSlots> slots;
    juce::CriticalSection slotLock;

    std::unique_ptr<ExternalSource> external;
    juce::CriticalSection sourceLock;

    VoicePadProcessor padProcessor { pad };
    AudioSafety::DcBlocker dcBlocker;
    omx::Denoiser denoiser;
    omx::Gate gate;
    omx::Eq3 eq;
    omx::Compressor comp;
    omx::DeEsser deesser;
    bool denoiseWasOn = false;

    juce::AudioBuffer<float> scratch;   // wide buffer for plugins with >2 channels
    juce::AudioBuffer<float> dryBuffer; // dry copy for bypass crossfades
    std::array<bool, kNumSlots> wasActive {};   // audio-thread view of each slot, for crossfades
    std::array<DelayLine, kNumSlots> bypassDelay;   // keeps latency for bypassed plugins without a bypass parameter
    DelayLine alignLine;                            // strip-level latency alignment
    juce::MidiBuffer midi;
    juce::SmoothedValue<float> inGain, muteGain, faderGain, duckGain;

    double sampleRate = 48000.0;
    int blockSize = 512;

    static constexpr int kMaxPluginChannels = 16;
};
