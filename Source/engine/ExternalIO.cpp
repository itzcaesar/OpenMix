#include "engine/ExternalIO.h"

static std::unique_ptr<juce::AudioIODevice> openWindowsDevice (juce::AudioIODeviceType& type,
                                                                const juce::String& outputName,
                                                                const juce::String& inputName,
                                                                double preferredRate, juce::String& error)
{
    std::unique_ptr<juce::AudioIODevice> device (type.createDevice (outputName, inputName));
    if (device == nullptr)
    {
        error = "Couldn't open " + (outputName.isNotEmpty() ? outputName : inputName);
        return {};
    }

    // Shared mode works best at the device's own rate; prefer the main rate, then 48k.
    const auto rates = device->getAvailableSampleRates();
    double rate = preferredRate;
    if (! rates.isEmpty() && ! rates.contains (rate))
        rate = rates.contains (48000.0) ? 48000.0 : rates[0];

    juce::BigInteger ins, outs;
    if (inputName.isNotEmpty())
        ins.setRange (0, juce::jmin (2, device->getInputChannelNames().size()), true);
    if (outputName.isNotEmpty())
        outs.setRange (0, juce::jmin (2, device->getOutputChannelNames().size()), true);

    error = device->open (ins, outs, rate, device->getDefaultBufferSize());
    if (error.isNotEmpty())
        return {};

    return device;
}

//==============================================================================
std::unique_ptr<DeviceInputSource> DeviceInputSource::open (juce::AudioIODeviceType& type, const juce::String& name,
                                                            double preferredRate, juce::String& error)
{
    std::unique_ptr<DeviceInputSource> source (new DeviceInputSource());
    source->device = openWindowsDevice (type, {}, name, preferredRate, error);
    if (source->device == nullptr)
        return {};

    source->device->start (source.get());
    return source;
}

bool DeviceInputSource::isHealthy() const
{
    return device != nullptr && device->isPlaying() && ! errored.load() && watchdog.alive();
}

DeviceInputSource::~DeviceInputSource()
{
    if (device != nullptr)
    {
        device->stop();
        device->close();
    }
}

void DeviceInputSource::audioDeviceAboutToStart (juce::AudioIODevice* d)
{
    rate = d->getCurrentSampleRate();
}

void DeviceInputSource::audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                                          float* const* outputs, int numOutputs, int numSamples,
                                                          const juce::AudioIODeviceCallbackContext&)
{
    juce::ScopedNoDenormals noDenormals;
    watchdog.beat();

    for (int i = 0; i < numOutputs; ++i)
        if (outputs[i] != nullptr)
            juce::FloatVectorOperations::clear (outputs[i], numSamples);

    if (numInputs > 0)
        bridge.write (inputs, juce::jmin (numInputs, 2), numSamples, rate);
}

//==============================================================================
std::unique_ptr<AppLoopbackSource> AppLoopbackSource::open (uint32_t pid, juce::String& error)
{
    std::unique_ptr<AppLoopbackSource> source (new AppLoopbackSource());
    source->pid = pid;

    std::string err;
    if (! source->capture.start (pid, source->bridge, err))
    {
        error = juce::String (err);
        return {};
    }
    return source;
}

//==============================================================================
std::unique_ptr<DeviceOutputSink> DeviceOutputSink::open (juce::AudioIODeviceType& type, const juce::String& name,
                                                          double preferredRate, juce::String& error)
{
    std::unique_ptr<DeviceOutputSink> sink (new DeviceOutputSink());
    sink->device = openWindowsDevice (type, name, {}, preferredRate, error);
    if (sink->device == nullptr)
        return {};

    sink->device->start (sink.get());
    return sink;
}

bool DeviceOutputSink::isHealthy() const
{
    return device != nullptr && device->isPlaying() && ! errored.load() && watchdog.alive();
}

DeviceOutputSink::~DeviceOutputSink()
{
    if (device != nullptr)
    {
        device->stop();
        device->close();
    }
}

void DeviceOutputSink::audioDeviceAboutToStart (juce::AudioIODevice* d)
{
    rate = d->getCurrentSampleRate();
    temp.setSize (2, juce::jmax (4096, d->getCurrentBufferSizeSamples() * 4));
}

void DeviceOutputSink::audioDeviceIOCallbackWithContext (const float* const*, int,
                                                         float* const* outputs, int numOutputs, int numSamples,
                                                         const juce::AudioIODeviceCallbackContext&)
{
    juce::ScopedNoDenormals noDenormals;
    watchdog.beat();

    for (int i = 0; i < numOutputs; ++i)
        if (outputs[i] != nullptr)
            juce::FloatVectorOperations::clear (outputs[i], numSamples);

    if (numOutputs == 0 || numSamples > temp.getNumSamples())
        return;

    auto* l = temp.getWritePointer (0);
    auto* r = temp.getWritePointer (1);
    bridge.read (l, r, numSamples, rate);

    if (numOutputs >= 2 && outputs[0] != nullptr && outputs[1] != nullptr)
    {
        juce::FloatVectorOperations::copy (outputs[0], l, numSamples);
        juce::FloatVectorOperations::copy (outputs[1], r, numSamples);
    }
    else if (outputs[0] != nullptr)   // mono device
    {
        juce::FloatVectorOperations::copyWithMultiply (outputs[0], l, 0.5f, numSamples);
        juce::FloatVectorOperations::addWithMultiply (outputs[0], r, 0.5f, numSamples);
    }
}
