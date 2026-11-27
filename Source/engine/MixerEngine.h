#pragma once

#include <JuceHeader.h>
#include "engine/ChannelStrip.h"
#include "engine/CpuMeter.h"
#include "engine/ExternalIO.h"
#include "engine/PluginScanner.h"
#include "dsp/Metering.h"
#include "creator/Recorder.h"
#include "creator/Soundboard.h"
#include "creator/MediaPlayer.h"

// Where a bus goes.
enum class BusTarget : int
{
    Off      = 0,
    MainPair = 1,   // an output pair of the main device
    Device   = 2    // a separate Windows output device (with drift correction)
};

struct Bus
{
    std::atomic<bool>  active { false };        // shown and processed
    std::atomic<bool>  monitor { false };       // the bus you listen on: gets soloed strips
    std::atomic<float> faderDb { 0.0f };
    std::atomic<bool>  mute { false };
    std::atomic<int>   target { (int) BusTarget::MainPair };
    std::atomic<int>   outputPair { 0 };        // for BusTarget::MainPair (0 = outs 1/2)
    std::atomic<float> peakL { 0.0f }, peakR { 0.0f };
    std::atomic<float> rmsL { 0.0f }, rmsR { 0.0f };   // sustained level, for the RMS meter
    std::atomic<float> limiterGain { 1.0f };   // lowest safety-limiter gain since the UI last read it

    // Loudness normalisation (off by default): a slow gain that pulls this bus
    // towards normTargetLufs (streaming ~-14, broadcast ~-23), clamped to +/-12 dB.
    // normGain is the gain actually applied, for the UI to show.
    std::atomic<bool>  normOn { false };
    std::atomic<float> normTargetLufs { -14.0f };
    std::atomic<float> normGain { 1.0f };

    // Bus-to-bus feeds: feedOn[d] sends this bus, after its fader and limiter, into bus d at
    // feedDb[d]. Set feedOn through MixerEngine::setBusFeed, which refuses loops.
    std::array<std::atomic<bool>, kNumBuses> feedOn {};
    std::array<std::atomic<float>, kNumBuses> feedDb {};

    // Bus processing, in order: EQ -> compressor -> VST3 inserts -> loudness normalisation
    // -> fader -> safety limiter.
    // Only the EQ and compressor fields of `fx` are used on a bus.
    ProcessingParams fx;
    PluginChain inserts;

    // Analysis feed for the UI (loudness, spectrum): the audio thread writes, the UI reads.
    juce::AbstractFifo analysisFifo { 32768 };
    juce::AudioBuffer<float> analysisBuffer { 2, 32768 };

    juce::String deviceName;                    // for BusTarget::Device (message thread only)
    std::unique_ptr<DeviceOutputSink> sink;
    juce::CriticalSection sinkLock;

    // Custom name (message thread only). Empty means the default name
    // (kBusNames[slot]: A1..A3 / B1..B3). The bus's group is its slot, never
    // its name -- see MixerEngine::isVirtualBus / omx::isVirtualBusSlot --
    // so renaming a bus can never move it between the Hardware and Virtual
    // groups. Persisted in the session file by Session::save/apply.
    juce::String customName;
};

class MixerEngine : private juce::AudioIODeviceCallback
{
public:
    MixerEngine();
    ~MixerEngine() override;

    // Opens the main audio device (restoring saved settings if given).
    void start (const juce::XmlElement* savedDeviceState);

    juce::AudioDeviceManager& getDeviceManager() noexcept { return deviceManager; }
    juce::AudioPluginFormatManager& getFormatManager() noexcept { return formatManager; }
    PluginScanner& getScanner() noexcept { return *scanner; }
    Recorder& getRecorder() noexcept { return recorder; }
    ReplayBuffer& getReplay() noexcept { return replay; }
    Soundboard& getSoundboard() noexcept { return soundboard; }
    MediaPlayer& getMediaPlayer() noexcept { return mediaPlayer; }

    // Bumped when something outside the UI (OSC remote control) changes a setting.
    std::atomic<int> externalChanges { 0 };

    // Scenes (message thread only): saved snapshots of the mix, see Session::captureScene.
    struct Scene
    {
        juce::String name;
        std::unique_ptr<juce::XmlElement> state;
    };
    std::vector<Scene> scenes;
    int currentScene = -1;

    // Named strip presets (message thread only): a strip's sound saved under a
    // name, persisted in the session file by Session::save/apply. Applying one to
    // a different strip is supported; see ChannelStrip::loadPreset for what a
    // preset does and does not carry.
    struct NamedStripPreset
    {
        juce::String name;
        omx::StripPreset preset;
    };
    std::vector<NamedStripPreset> stripPresets;

    juce::StringArray getStripPresetNames() const;
    const omx::StripPreset* findStripPreset (const juce::String& name) const;
    bool saveStripPreset (const juce::String& name, int strip);   // false: bad name/strip; overwrites a preset of the same name
    int applyStripPreset (const juce::String& name, int strip);   // returns missing-plugin count, or -1 if not found / bad strip
    int applyStripPreset (int presetIndex, int strip);
    bool deleteStripPreset (const juce::String& name);

    // Solo (message thread / audio thread). getMonitorBus returns the monitor bus
    // index or -1. isStripMutedBySolo is true when the solo action (not the strip's
    // own mute) silences this strip on the monitor bus: something is soloed, this
    // strip isn't soloed, and it isn't solo-safe-and-routed-to-the-monitor-bus.
    int getMonitorBus() const noexcept;
    // Solo listens after the fader (AFL, false) or before it (PFL, true): with PFL a soloed
    // strip is heard at its pre-fader level, so you can check it with its fader down.
    std::atomic<bool> soloPreFader { false };
    bool isSoloActive() const noexcept;
    bool isStripMutedBySolo (int strip) const noexcept;

    // Bus names and groups (message thread). Buses keep the fixed 6-slot
    // capacity: slots 0-2 are the Hardware (A) group, 3-5 the Virtual (B)
    // group. getBusName returns the custom name when one is set, otherwise
    // the default (A1..A3 / B1..B3); setBusName with an empty string clears
    // the custom name back to the default. findNextFreeSlotInGroup returns
    // the first inactive slot in the group, or -1 when the group is full.
    // isVirtualBus is slot-based, never name-based.
    juce::String getBusName (int bus) const;
    void setBusName (int bus, const juce::String& name);
    int findNextFreeSlotInGroup (bool virtualGroup) const noexcept;
    static bool isVirtualBus (int bus) noexcept { return omx::isVirtualBusSlot (bus); }

    // CPU meter (any thread): audio-thread CPU usage as a percentage of
    // real time, smoothed with an exponential moving average (see
    // engine/CpuMeter.h). 100% means the engine used the whole block
    // duration; over 100% means it overran real time. 0 before the first
    // audio block has been measured.
    double getCpuUsagePercent() const noexcept { return cpuUsagePercent.load (std::memory_order_relaxed); }

    ChannelStrip& getStrip (int i) noexcept { return *strips[(size_t) i]; }
    Bus& getBus (int i) noexcept { return buses[(size_t) i]; }

    // Main device: active input channel names and output pair names.
    juce::StringArray getActiveInputNames() const;
    juce::StringArray getActiveOutputPairNames() const;

    // Other Windows devices (WASAPI shared), rescanned on each call.
    juce::StringArray getWindowsInputNames();
    juce::StringArray getWindowsOutputNames();

    // Message thread: routing changes. Return false + error text on failure.
    void setStripSourceNone (int strip);
    void setStripSourceMain (int strip, int channel, int rightChannel = -1);   // rightChannel >= 0: stereo pair
    bool setStripSourceDevice (int strip, const juce::String& deviceName, juce::String& error);
    bool setStripSourceApp (int strip, const juce::String& exeName, juce::String& error);
    bool setStripSourceLoopback (int strip, const juce::String& endpointId, const juce::String& name, juce::String& error);
    void setStripSourceMedia (int strip);    // the built-in media player

    // Output devices OpenMix itself plays into (main device and bus devices). Capturing one of
    // them on a strip that feeds a bus would loop the sound back on itself.
    juce::StringArray getOwnOutputDeviceNames() const;

    // Where a captured app's own output goes, so it isn't heard twice ("" = leave it alone).
    bool setStripAppOutput (int strip, const juce::String& deviceId, const juce::String& deviceName, juce::String& error);
    void undoAppRedirect (int strip);        // puts the app back on the Windows default output
    void undoAllAppRedirects();              // on shutdown

    // Strips and buses on demand (message thread). addStrip returns the index or -1 if full.
    int  addStrip();
    void removeStrip (int strip);            // clears it (plugins, source, settings) and hides it
    void addBus (int bus);
    void removeBus (int bus);
    void setMonitorBus (int bus);            // -1 = none
    int  getNumActiveStrips() const;
    int  getNumActiveBuses() const;

    void setBusOff (int bus);
    void setBusMainPair (int bus, int pair);
    // The bus stays set to this device even when it can't be opened now (shown as
    // unplugged); maintainSources() connects it when the device appears.
    bool setBusDevice (int bus, const juce::String& deviceName, juce::String& error);

    // Bus-to-bus feeds (message thread). setBusFeed returns false when src == dst or the feed
    // would make a loop (dst already reaches src); turning a feed off always works.
    bool busFeedWouldLoop (int src, int dst) const noexcept;
    bool setBusFeed (int src, int dst, bool on);

    // Message thread, call every couple of seconds. Drops sources/outputs that died
    // (device unplugged or stalled, app closed, capture stream invalidated) and reconnects
    // them as soon as the device or app is back.
    void maintainSources();

    double getSampleRate() const noexcept { return currentSampleRate.load(); }

    // Message thread diagnostics: dropouts on extra devices / app captures since start.
    int getLinkDropouts() const;

    // True when the main device takes input and output from two different Windows devices.
    // JUCE doesn't correct clock drift between those, which causes periodic crackles.
    bool mainDeviceHasSplitClocks() const;

private:
    void audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                           float* const* outputs, int numOutputs,
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;

    void processChunk (const float* const* inputs, int numInputs,
                       float* const* outputs, int numOutputs, int offset, int numSamples) noexcept;

    juce::AudioIODeviceType* getWindowsAudioType();

    juce::AudioDeviceManager deviceManager;
    juce::AudioPluginFormatManager formatManager;
    std::unique_ptr<PluginScanner> scanner;   // after formatManager: stops before it goes away
    std::unique_ptr<juce::AudioIODeviceType> windowsType;   // our own WASAPI type for extra devices

    std::array<std::unique_ptr<ChannelStrip>, kNumStrips> strips;
    std::array<Bus, kNumBuses> buses;

    Recorder recorder;
    ReplayBuffer replay;
    Soundboard soundboard;
    MediaPlayer mediaPlayer;
    juce::AudioBuffer<float> mediaBuffer;
    std::array<juce::SmoothedValue<float>, kNumBuses> soundSend;

    juce::AudioBuffer<float> stripBuffer, preBuffer, busBuffer, sourceBuffer, soundBuffer;
    std::array<juce::SmoothedValue<float>, kNumBuses> busGain;
    std::array<AudioSafety::SafetyLimiter, kNumBuses> busLimiter;
    std::array<omx::LoudnessNormaliser, kNumBuses> busNorm;
    std::array<omx::RmsMeter, kNumBuses> busRmsL;
    std::array<omx::RmsMeter, kNumBuses> busRmsR;
    std::array<omx::ParametricEq, kNumBuses> busEq;
    std::array<omx::Compressor, kNumBuses> busComp;
    std::array<juce::SmoothedValue<float>, kNumBuses> busEqMix, busCompMix;   // on/off crossfades
    juce::AudioBuffer<float> busDry;                                           // dry copy while fading
    std::array<std::array<juce::SmoothedValue<float>, kNumBuses>, kNumStrips> sendGain;
    std::array<std::array<juce::SmoothedValue<float>, kNumBuses>, kNumBuses> feedGain;   // [src][dst]

    // CPU meter (see the public getters above).
    omx::CpuMeter cpuMeter;
    std::atomic<float> cpuUsagePercent { 0.0f };

    int maxBlock = 512;
    std::atomic<double> currentSampleRate { 48000.0 };
};
