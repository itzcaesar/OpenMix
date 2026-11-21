#pragma once

#include <JuceHeader.h>
#include "engine/AppCapture.h"
#include "engine/ClockBridge.h"

// Audio that arrives on a different clock than the main device: a second Windows
// input device, or one app's audio. The main callback pulls it through `bridge`.
class ExternalSource
{
public:
    virtual ~ExternalSource() = default;

    // False once the device/capture has died (unplugged, stopped, stalled, stream invalidated).
    // The engine then drops it and reconnects when possible.
    virtual bool isHealthy() const = 0;

    ClockBridge bridge;
};

// Watchdog for a device callback: healthy while callbacks keep arriving.
struct CallbackWatchdog
{
    void beat() noexcept { lastBeatMs.store (juce::Time::getMillisecondCounter(), std::memory_order_relaxed); }
    bool alive (juce::uint32 timeoutMs = 1500) const noexcept
    {
        const auto last = lastBeatMs.load (std::memory_order_relaxed);
        return juce::Time::getMillisecondCounter() - last < timeoutMs;
    }
    std::atomic<juce::uint32> lastBeatMs { juce::Time::getMillisecondCounter() };
};

// A Windows (WASAPI shared) input device used by one strip. Captures its first two channels.
class DeviceInputSource : public ExternalSource, private juce::AudioIODeviceCallback
{
public:
    static std::unique_ptr<DeviceInputSource> open (juce::AudioIODeviceType&, const juce::String& deviceName,
                                                    double preferredRate, juce::String& error);
    ~DeviceInputSource() override;

    bool isHealthy() const override;

private:
    DeviceInputSource() = default;
    void audioDeviceIOCallbackWithContext (const float* const*, int, float* const*, int, int,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override {}
    void audioDeviceError (const juce::String&) override { errored = true; }

    std::unique_ptr<juce::AudioIODevice> device;
    double rate = 48000.0;
    CallbackWatchdog watchdog;
    std::atomic<bool> errored { false };
};

// One app's audio, via process loopback.
class AppLoopbackSource : public ExternalSource
{
public:
    static std::unique_ptr<AppLoopbackSource> open (uint32_t pid, juce::String& error);
    ~AppLoopbackSource() override { capture.stop(); }

    uint32_t getPid() const noexcept { return pid; }
    bool isHealthy() const override { return ! capture.hasFailed(); }

private:
    AppLoopbackSource() = default;
    AppCapture capture;
    uint32_t pid = 0;
};

// Everything one Windows output device plays (WASAPI device loopback), no virtual cable needed.
class DeviceLoopbackSource : public ExternalSource
{
public:
    static std::unique_ptr<DeviceLoopbackSource> open (const juce::String& endpointId, juce::String& error);
    ~DeviceLoopbackSource() override { capture.stop(); }

    bool isHealthy() const override { return ! capture.hasFailed(); }

private:
    DeviceLoopbackSource() = default;
    AppCapture capture;
};

// A Windows (WASAPI shared) output device fed by one bus.
class DeviceOutputSink : private juce::AudioIODeviceCallback
{
public:
    static std::unique_ptr<DeviceOutputSink> open (juce::AudioIODeviceType&, const juce::String& deviceName,
                                                   double preferredRate, juce::String& error);
    ~DeviceOutputSink() override;

    bool isHealthy() const;
    ClockBridge bridge;

private:
    DeviceOutputSink() = default;
    void audioDeviceIOCallbackWithContext (const float* const*, int, float* const*, int, int,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override {}
    void audioDeviceError (const juce::String&) override { errored = true; }

    std::unique_ptr<juce::AudioIODevice> device;
    CallbackWatchdog watchdog;
    std::atomic<bool> errored { false };
    juce::AudioBuffer<float> temp;
    double rate = 48000.0;
};
