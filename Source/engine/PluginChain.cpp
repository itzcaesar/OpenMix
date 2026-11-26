#include "engine/PluginChain.h"

PluginChain::PluginChain()
{
    for (auto& m : slotMix)
        m.store (1.0f);

#if ! defined (OPENMIX_DISABLE_PLUGINCHAIN_SANDBOX)
    sandbox = std::make_unique<PluginSandbox> ([this] (int slot)
    {
        if (! juce::isPositiveAndBelow (slot, kNumSlots))
            return;
        if (! slotFault[(size_t) slot].exchange (true, std::memory_order_acq_rel))
            faultCount.fetch_add (1, std::memory_order_relaxed);
    });
#endif
}

PluginChain::~PluginChain()
{
    const juce::ScopedLock sl (slotLock);
    for (auto& p : slots)
        if (p != nullptr)
            p->releaseResources();
}

void PluginChain::prepare (double newSampleRate, int maxBlockSize)
{
    {
        const juce::ScopedLock sl (slotLock);

        sampleRate = newSampleRate;
        blockSize = maxBlockSize;

        dryBuffer.setSize (2, maxBlockSize);
        mixDryBuffer.setSize (2, maxBlockSize);
        sandboxWorkerBuffer.setSize (2, maxBlockSize);
        sandboxWasActive = false;
        sandboxDryDelay.prepare ((int) (sampleRate * 1.5) + 2 * blockSize);

        for (int i = 0; i < kNumSlots; ++i)
        {
            if (slots[(size_t) i] != nullptr)
            {
                preparePlugin (*slots[(size_t) i]);
                pluginLatencies[(size_t) i] = juce::jmax (0, slots[(size_t) i]->getLatencySamples());
                cachePluginParameters (i);
            }
            else
            {
                pluginLatencies[(size_t) i] = 0;
                parameterPointers[(size_t) i].clear();
            }
        }
    }

    configureSandbox();
}

void PluginChain::release()
{
    const juce::ScopedLock sl (slotLock);
    for (auto& p : slots)
        if (p != nullptr)
            p->releaseResources();
}

void PluginChain::preparePlugin (juce::AudioPluginInstance& plugin)
{
    // Ask for stereo in / stereo out on the main buses; keep the plugin's default if refused.
    auto layout = plugin.getBusesLayout();
    if (! layout.inputBuses.isEmpty())  layout.inputBuses.getReference (0)  = juce::AudioChannelSet::stereo();
    if (! layout.outputBuses.isEmpty()) layout.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
    plugin.setBusesLayout (layout);

    plugin.setNonRealtime (false);
    plugin.prepareToPlay (sampleRate, blockSize);
}

void PluginChain::cachePluginParameters (int slot)
{
    auto& cached = parameterPointers[(size_t) slot];
    cached.clear();
    if (auto* plugin = slots[(size_t) slot].get())
    {
        const auto params = plugin->getParameters();
        const int count = juce::jmin (1024, params.size());
        cached.reserve ((size_t) count);
        for (int i = 0; i < count; ++i)
            cached.push_back (params.getUnchecked (i));
    }
}

void PluginChain::configureSandbox()
{
    if (sandbox == nullptr)
        return;

    const juce::ScopedLock sl (slotLock);
    juce::XmlElement root ("PluginSandbox");
    root.setAttribute ("sampleRate", sampleRate);
    root.setAttribute ("blockSize", blockSize);
    int disabledMask = 0;
    int count = 0;

    for (int i = 0; i < kNumSlots; ++i)
    {
        auto* plugin = slots[(size_t) i].get();
        if (plugin == nullptr)
            continue;

        ++count;
        if (slotFault[(size_t) i].load (std::memory_order_acquire))
            disabledMask |= 1 << i;

        auto* slot = root.createNewChildElement ("Slot");
        slot->setAttribute ("index", i);
        slot->setAttribute ("bypass", slotBypass[(size_t) i].load());
        slot->setAttribute ("mix", slotMix[(size_t) i].load());
        slot->addChildElement (plugin->getPluginDescription().createXml().release());
        juce::MemoryBlock state;
        plugin->getStateInformation (state);
        slot->setAttribute ("state", state.toBase64Encoding());
    }

    if (count == 0)
        sandbox->configure ({}, 0);
    else
        sandbox->configure (root.toString(), disabledMask);
}

void PluginChain::synchronizeSandbox()
{
    configureSandbox();
}

//==============================================================================
// Slots (message thread)

bool PluginChain::loadPlugin (int slot, const juce::PluginDescription& desc,
                              juce::AudioPluginFormatManager& formats, juce::String& error)
{
    auto instance = formats.createPluginInstance (desc, sampleRate, blockSize, error);
    if (instance == nullptr)
        return false;

    installPlugin (slot, std::move (instance));
    return true;
}

int PluginChain::getNumPlugins() const noexcept
{
    int n = 0;
    while (n < kNumSlots && slots[(size_t) n] != nullptr)
        ++n;
    return n;
}

void PluginChain::installPlugin (int slot, std::unique_ptr<juce::AudioPluginInstance> instance)
{
    if (instance == nullptr || ! juce::isPositiveAndBelow (slot, kNumSlots))
        return;

    // Keep the chain packed: a new plugin past the end goes right after the last one.
    slot = juce::jmin (slot, getNumPlugins());

    // Prepare before locking: a slow prepareToPlay must not make the audio thread skip the chain.
    preparePlugin (*instance);
    const int newLatency = juce::jmax (0, instance->getLatencySamples());

    std::unique_ptr<juce::AudioPluginInstance> old;
    {
        const juce::ScopedLock sl (slotLock);
        old = std::move (slots[(size_t) slot]);
        slots[(size_t) slot] = std::move (instance);
        pluginLatencies[(size_t) slot] = newLatency;
        cachePluginParameters (slot);
        slotBypass[(size_t) slot].store (false);
        slotMix[(size_t) slot].store (1.0f);
        slotFault[(size_t) slot].store (false);
    }

    if (old != nullptr)
        old->releaseResources();

    configureSandbox();
}

void PluginChain::removePlugin (int slot)
{
    if (! juce::isPositiveAndBelow (slot, kNumSlots))
        return;

    std::unique_ptr<juce::AudioPluginInstance> old;
    {
        const juce::ScopedLock sl (slotLock);
        old = std::move (slots[(size_t) slot]);

        // Move later plugins up so the chain stays packed.
        for (int i = slot; i < kNumSlots - 1; ++i)
        {
            std::swap (slots[(size_t) i], slots[(size_t) i + 1]);
            std::swap (parameterPointers[(size_t) i], parameterPointers[(size_t) i + 1]);
            std::swap (pluginLatencies[(size_t) i], pluginLatencies[(size_t) i + 1]);
            slotBypass[(size_t) i].store (slotBypass[(size_t) i + 1].load());
            slotFault[(size_t) i].store (slotFault[(size_t) i + 1].load());
            slotMix[(size_t) i].store (slotMix[(size_t) i + 1].load());
        }
        slotBypass[kNumSlots - 1].store (false);
        slotMix[kNumSlots - 1].store (1.0f);
        slotFault[kNumSlots - 1].store (false);
        parameterPointers[kNumSlots - 1].clear();
        pluginLatencies[kNumSlots - 1] = 0;
    }

    if (old != nullptr)
        old->releaseResources();

    configureSandbox();
}

void PluginChain::removeAllPlugins()
{
    while (getNumPlugins() > 0)
        removePlugin (getNumPlugins() - 1);
}

void PluginChain::movePlugin (int from, int to)
{
    const int n = getNumPlugins();
    if (! juce::isPositiveAndBelow (from, n) || ! juce::isPositiveAndBelow (to, n) || from == to)
        return;

    {
        const juce::ScopedLock sl (slotLock);
        const int step = to > from ? 1 : -1;
        for (int i = from; i != to; i += step)
        {
            const auto a = (size_t) i, b = (size_t) (i + step);
            std::swap (slots[a], slots[b]);
            std::swap (parameterPointers[a], parameterPointers[b]);
            std::swap (pluginLatencies[a], pluginLatencies[b]);
            const bool bypassA = slotBypass[a].load(), faultA = slotFault[a].load();
            slotBypass[a].store (slotBypass[b].load()); slotBypass[b].store (bypassA);
            slotFault[a].store (slotFault[b].load());   slotFault[b].store (faultA);
            const float mixA = slotMix[a].load();
            slotMix[a].store (slotMix[b].load());       slotMix[b].store (mixA);
        }
    }

    configureSandbox();
}

//==============================================================================
// Audio

void PluginChain::process (float* L, float* R, int numSamples, int& latency) noexcept
{
    // The editor/control instances stay in this process, but no plugin processing call is
    // made on the mixer callback. If the UI is changing slots, keep the last timing value.
    const juce::ScopedTryLock sl (slotLock);
    if (! sl.isLocked() || numSamples > dryBuffer.getNumSamples())
    {
        latency += latencySamples.load (std::memory_order_relaxed);
        return;
    }

    if (sandbox == nullptr)
    {
        latency += latencySamples.load (std::memory_order_relaxed);
        return;
    }

    int chainLatency = 0;
    bool hasPlugins = false;
    for (int s = 0; s < kNumSlots; ++s)
    {
        auto* plugin = slots[(size_t) s].get();
        if (plugin != nullptr)
        {
            hasPlugins = true;
            chainLatency += slotFault[(size_t) s].load (std::memory_order_acquire)
                                ? 0 : pluginLatencies[(size_t) s];
            auto& params = parameterPointers[(size_t) s];
            std::array<float, 1024> values {};
            const int count = juce::jmin ((int) params.size(), (int) values.size());
            for (int p = 0; p < count; ++p)
            {
                const float value = params[(size_t) p] != nullptr ? params[(size_t) p]->getValue() : 0.0f;
                values[(size_t) p] = std::isfinite (value) ? juce::jlimit (0.0f, 1.0f, value) : 0.0f;
            }
            sandbox->setSlotControls (s, slotBypass[(size_t) s].load(), slotMix[(size_t) s].load(), values.data(), count);
        }
        else
        {
            sandbox->setSlotControls (s, false, 1.0f, nullptr, 0);
        }
    }

    if (! hasPlugins)
    {
        sandboxWasActive = false;
        latencySamples.store (0, std::memory_order_relaxed);
        return;
    }

    // Keep the dry fallback warm even while the worker is healthy so changes between
    // worker and fallback audio can be faded over one block without a click.
    dryBuffer.copyFrom (0, 0, L, numSamples);
    dryBuffer.copyFrom (1, 0, R, numSamples);
    mixDryBuffer.copyFrom (0, 0, dryBuffer, 0, 0, numSamples);
    mixDryBuffer.copyFrom (1, 0, dryBuffer, 1, 0, numSamples);

    const int pipelineSamples = PluginSandbox::pipelineBlocks * numSamples;
    const int totalLatency = pipelineSamples + chainLatency;
    sandboxDryDelay.process (mixDryBuffer.getWritePointer (0), mixDryBuffer.getWritePointer (1), numSamples, totalLatency);

    const bool workerOutput = sandbox->processBlock (L, R, numSamples);
    if (workerOutput)
    {
        sandboxWorkerBuffer.copyFrom (0, 0, L, numSamples);
        sandboxWorkerBuffer.copyFrom (1, 0, R, numSamples);
        if (! sandboxWasActive)
        {
            juce::FloatVectorOperations::copy (L, mixDryBuffer.getReadPointer (0), numSamples);
            juce::FloatVectorOperations::copy (R, mixDryBuffer.getReadPointer (1), numSamples);
            float* chans[2] { L, R };
            juce::AudioBuffer<float> output (chans, 2, numSamples);
            output.applyGainRamp (0, numSamples, 1.0f, 0.0f);
            output.applyGainRamp (1, numSamples, 1.0f, 0.0f);
            output.addFromWithRamp (0, 0, sandboxWorkerBuffer.getReadPointer (0), numSamples, 0.0f, 1.0f);
            output.addFromWithRamp (1, 0, sandboxWorkerBuffer.getReadPointer (1), numSamples, 0.0f, 1.0f);
        }
    }
    else if (sandboxWasActive)
    {
        float* chans[2] { L, R };
        juce::AudioBuffer<float> output (chans, 2, numSamples);
        output.clear();
        output.addFromWithRamp (0, 0, sandboxWorkerBuffer.getReadPointer (0), numSamples, 1.0f, 0.0f);
        output.addFromWithRamp (1, 0, sandboxWorkerBuffer.getReadPointer (1), numSamples, 1.0f, 0.0f);
        output.addFromWithRamp (0, 0, mixDryBuffer.getReadPointer (0), numSamples, 0.0f, 1.0f);
        output.addFromWithRamp (1, 0, mixDryBuffer.getReadPointer (1), numSamples, 0.0f, 1.0f);
    }
    else
    {
        juce::FloatVectorOperations::copy (L, mixDryBuffer.getReadPointer (0), numSamples);
        juce::FloatVectorOperations::copy (R, mixDryBuffer.getReadPointer (1), numSamples);
    }

    sandboxWasActive = workerOutput;
    latencySamples.store (totalLatency, std::memory_order_relaxed);
    latency += totalLatency;
}
