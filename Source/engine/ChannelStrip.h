#pragma once

#include <JuceHeader.h>
#include "engine/VoicePad.h"
#include "engine/ExternalIO.h"
#include "engine/PluginChain.h"
#include "engine/StripLogic.h"
#include "dsp/AudioSafety.h"
#include "dsp/DelayLine.h"
#include "dsp/Dynamics.h"
#include "dsp/Denoiser.h"
#include "dsp/Metering.h"

// Capacity. Strips and buses are allocated up front (real-time safe) and switched on/off by the
// user; only active ones are processed and shown.
constexpr int kNumStrips = 8;
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
    App    = 3,   // one application's audio (process loopback)
    Loopback = 4  // everything a Windows output device plays (device loopback)
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

    // HPF/LPF, before the 3-band EQ (see ChannelStrip::process). Cutoffs are clamped
    // by the setters to their useful ranges; the DSP clamps them again to Nyquist.
    std::atomic<bool>  hpfOn { false };
    std::atomic<float> hpfHz { 80.0f };       // 20..1000 Hz
    std::atomic<bool>  lpfOn { false };
    std::atomic<float> lpfHz { 18000.0f };    // 1k..20k Hz

    // Ducking: this strip gets quieter while another strip (e.g. the mic) has signal.
    std::atomic<bool>  duckOn { false };
    std::atomic<int>   duckSource { -1 };           // strip index
    std::atomic<float> duckDepthDb { -12.0f };
    std::atomic<float> duckThresholdDb { -40.0f };

    // Meters for the UI: lowest gain since the UI last read it (1 = nothing happening).
    std::atomic<float> gateGain { 1.0f }, compGain { 1.0f }, deessGain { 1.0f }, duckGain { 1.0f };
    std::atomic<float> voiceProbability { 0.0f };
    std::atomic<bool>  denoiseUnavailable { false }; // RNNoise couldn't start
    std::atomic<int>   denoiseLatencyMs { 20 };      // for the panel text
};

// One input strip. Signal flow (top to bottom, same as the UI):
// source -> gain -> polarity / mono -> DC blocker -> noise removal -> gate -> HPF/LPF
// -> EQ -> compressor -> de-esser -> VST3 inserts -> VoicePad -> ducking
// -> latency alignment -> [pre-fader tap] -> mute -> fader -> bus sends.
class ChannelStrip
{
public:
    ChannelStrip();
    ~ChannelStrip();

    void prepare (double sampleRate, int maxBlockSize);
    void release();

    // HPF/LPF (message thread). Cutoffs are clamped to 20..1000 Hz (HPF) and
    // 1k..20k Hz (LPF). Toggling is click-free: the filters keep running and the
    // wet/dry blend is smoothed over ~20 ms (see process()).
    void setHpfEnabled (bool enabled) noexcept { fx.hpfOn.store (enabled); }
    bool isHpfEnabled() const noexcept         { return fx.hpfOn.load(); }
    void setHpfCutoff (float hz) noexcept      { fx.hpfHz.store (juce::jlimit (20.0f, 1000.0f, hz)); }
    float getHpfCutoff() const noexcept        { return fx.hpfHz.load(); }
    void setLpfEnabled (bool enabled) noexcept { fx.lpfOn.store (enabled); }
    bool isLpfEnabled() const noexcept         { return fx.lpfOn.load(); }
    void setLpfCutoff (float hz) noexcept      { fx.lpfHz.store (juce::jlimit (1000.0f, 20000.0f, hz)); }
    float getLpfCutoff() const noexcept        { return fx.lpfHz.load(); }

    // Solo-safe (message thread): while anything is soloed, this strip is never
    // muted by the solo action on the monitor bus (see omx::stripAudibleOnMonitorBus).
    void setSoloSafe (bool safe) noexcept { soloSafe.store (safe); }
    bool isSoloSafe() const noexcept      { return soloSafe.load(); }

    // Latency readout (any thread). getLatencySamples() is the last block's total,
    // the value latency alignment uses. getTotalLatencySamples/Ms() recompute it
    // live from the current state: denoiser latency (only while noise removal is
    // on) + insert-chain latency. getLatencyMs() is the same total in milliseconds.
    int getLatencySamples() const noexcept { return latencySamples.load (std::memory_order_relaxed); }
    int getPluginLatencySamples() const noexcept { return inserts.latencySamples.load (std::memory_order_relaxed); }
    int getDenoiserLatencySamples() const noexcept
    {
        return (fx.denoiseOn.load() && denoiser.isAvailable()) ? denoiser.getLatency() : 0;
    }
    int getTotalLatencySamples() const noexcept
    {
        return omx::stripTotalLatencySamples (getDenoiserLatencySamples(), getPluginLatencySamples());
    }
    double getLatencyMs() const noexcept       { return getTotalLatencyMs(); }
    double getTotalLatencyMs() const noexcept
    {
        return sampleRate > 0.0 ? 1000.0 * (double) getTotalLatencySamples() / sampleRate : 0.0;
    }

    // Strip preset (message thread): the strip's sound, see omx::StripPreset.
    // savePreset() snapshots the current settings; loadPreset() applies a preset
    // (to this strip or any other) and returns how many insert plugins could not
    // be found and were skipped (0 = all loaded; plugins load with defaults, no
    // state blobs are stored). Never touches name, colour, source, mute, solo or
    // active state. Safe to call with a preset whose plugins are missing.
    omx::StripPreset savePreset() const;
    // isBlocked (optional): plugin files known to crash (the scanner's skip list) are
    // skipped and counted as missing instead of being loaded into the mixer.
    int loadPreset (const omx::StripPreset&, juce::AudioPluginFormatManager& formats,
                    const std::function<bool (const juce::String& file)>& isBlocked = {});

    // Audio thread. Writes the post-fader signal into `out` and the pre-fader (post-mute) signal
    // into `preOut` (both 2 channels). inL/inR may be the same pointer (mono source) or nullptr.
    void process (const float* inL, const float* inR, juce::AudioBuffer<float>& out,
                  juce::AudioBuffer<float>& preOut, int numSamples) noexcept;

    // Audio thread: pulls from the external source, if any. Returns false if there's none.
    bool pullExternal (float* l, float* r, int numSamples, double sampleRate) noexcept;

    // Message thread: replaces the external source (nullptr removes it).
    void setExternalSource (std::unique_ptr<ExternalSource>);
    ExternalSource* getExternalSource() const noexcept { return external.get(); }

    // Back to a fresh strip (used when the user removes it).
    void resetToDefaults();

    //==========================================================================
    std::atomic<bool> active { false };      // shown and processed

    PluginChain inserts;                     // VST3 inserts (slots, bypass, wet/dry)
    std::atomic<int> faultCount { 0 };       // NaN/Inf events caught after the inserts

    // Message thread only
    juce::String name;                  // user-editable label
    int colour = 0;                     // tape colour index (Theme::tapeColours)
    juce::String sourceDeviceName;      // for SourceType::Device
    juce::String sourceAppExe;          // for SourceType::App, e.g. "Spotify.exe"
    juce::String loopbackId, loopbackName;   // for SourceType::Loopback (endpoint ID, friendly name)
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
    std::atomic<bool>  soloSafe { false };        // never muted by someone else's solo
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
    std::atomic<float> rmsL { 0.0f }, rmsR { 0.0f };   // sustained level, for the RMS meter
    std::atomic<float> sidechainLevel { 0.0f };   // pre-fader peak of the last block, for ducking

private:
    static void storeMin (std::atomic<float>& meter, float value) noexcept
    {
        if (value < meter.load (std::memory_order_relaxed))
            meter.store (value, std::memory_order_relaxed);
    }

    std::unique_ptr<ExternalSource> external;
    juce::CriticalSection sourceLock;

    VoicePadProcessor padProcessor { pad };
    AudioSafety::DcBlocker dcBlocker;
    omx::Denoiser denoiser;
    omx::RmsMeter rmsMeterL, rmsMeterR;
    omx::Gate gate;
    // HPF/LPF (before the EQ). The filters always run, so their state stays warm;
    // the smoothed wet/dry mixes make enabling/disabling click-free.
    Biquad hpf, lpf;
    juce::SmoothedValue<float> hpfMix, lpfMix;
    float lastHpfHz = -1.0f, lastLpfHz = -1.0f;   // -1 forces a coefficient update
    omx::Eq3 eq;
    omx::Compressor comp;
    omx::DeEsser deesser;
    bool denoiseWasOn = false;

    DelayLine alignLine;                            // strip-level latency alignment
    juce::SmoothedValue<float> inGain, muteGain, faderGain, duckGain;

    double sampleRate = 48000.0;
};
