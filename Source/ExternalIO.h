#pragma once

#include <JuceHeader.h>
#include "AppCapture.h"
#include "ClockBridge.h"

// Audio that arrives on a different clock than the main device: a second Windows
// input device, or one app's audio. The main callback pulls it through `bridge`.
class ExternalSource
{
public:
    virtual ~ExternalSource() = default;
    ClockBridge bridge;
};

// A Windows (WASAPI shared) input device used by one strip. Captures its first two channels.
class DeviceInputSource : public ExternalSource, private juce::AudioIODeviceCallback
{
public:
    static std::unique_ptr<DeviceInputSource> open (juce::AudioIODeviceType&, const juce::String& deviceName,
                                                    double preferredRate, juce::String& error);
    ~DeviceInputSource() override;

private:
    DeviceInputSource() = default;
    void audioDeviceIOCallbackWithContext (const float* const*, int, float* const*, int, int,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override {}

    std::unique_ptr<juce::AudioIODevice> device;
    double rate = 48000.0;
};

// One app's audio, via process loopback.
class AppLoopbackSource : public ExternalSource
{
public:
    static std::unique_ptr<AppLoopbackSource> open (uint32_t pid, juce::String& error);
    ~AppLoopbackSource() override { capture.stop(); }

    uint32_t getPid() const noexcept { return pid; }

private:
    AppLoopbackSource() = default;
    AppCapture capture;
    uint32_t pid = 0;
};

// A Windows (WASAPI shared) output device fed by one bus.
class DeviceOutputSink : private juce::AudioIODeviceCallback
{
public:
    static std::unique_ptr<DeviceOutputSink> open (juce::AudioIODeviceType&, const juce::String& deviceName,
                                                   double preferredRate, juce::String& error);
    ~DeviceOutputSink() override;

    ClockBridge bridge;

private:
    DeviceOutputSink() = default;
    void audioDeviceIOCallbackWithContext (const float* const*, int, float* const*, int, int,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override {}

    std::unique_ptr<juce::AudioIODevice> device;
    juce::AudioBuffer<float> temp;
    double rate = 48000.0;
};
