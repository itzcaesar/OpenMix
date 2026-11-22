#pragma once

#include <JuceHeader.h>
#include "engine/ChannelStrip.h"
#include "engine/ExternalIO.h"
#include "engine/PluginScanner.h"
#include "creator/Recorder.h"
#include "creator/Soundboard.h"

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
    std::atomic<float> limiterGain { 1.0f };   // lowest safety-limiter gain since the UI last read it

    // Bus processing, in order: EQ -> compressor -> VST3 inserts -> fader -> safety limiter.
    // Only the EQ and compressor fields of `fx` are used on a bus.
    ProcessingParams fx;
    PluginChain inserts;

    // Analysis feed for the UI (loudness, spectrum): the audio thread writes, the UI reads.
    juce::AbstractFifo analysisFifo { 32768 };
    juce::AudioBuffer<float> analysisBuffer { 2, 32768 };

    juce::String deviceName;                    // for BusTarget::Device (message thread only)
    std::unique_ptr<DeviceOutputSink> sink;
    juce::CriticalSection sinkLock;
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
    bool setBusDevice (int bus, const juce::String& deviceName, juce::String& error);

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
    std::array<juce::SmoothedValue<float>, kNumBuses> soundSend;

    juce::AudioBuffer<float> stripBuffer, preBuffer, busBuffer, sourceBuffer, soundBuffer;
    std::array<juce::SmoothedValue<float>, kNumBuses> busGain;
    std::array<AudioSafety::SafetyLimiter, kNumBuses> busLimiter;
    std::array<omx::Eq3, kNumBuses> busEq;
    std::array<omx::Compressor, kNumBuses> busComp;
    std::array<std::array<juce::SmoothedValue<float>, kNumBuses>, kNumStrips> sendGain;
    int maxBlock = 512;
    std::atomic<double> currentSampleRate { 48000.0 };
};
