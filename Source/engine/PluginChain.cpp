#include "engine/PluginChain.h"

PluginChain::PluginChain()
{
    for (auto& m : slotMix)
        m.store (1.0f);
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
    const juce::ScopedLock sl (slotLock);

    sampleRate = newSampleRate;
    blockSize = maxBlockSize;

    scratch.setSize (kMaxPluginChannels, maxBlockSize);
    dryBuffer.setSize (2, maxBlockSize);
    mixDryBuffer.setSize (2, maxBlockSize);
    midi.ensureSize (256);
    wasActive.fill (false);

    // Up to 250 ms of latency per plugin.
    for (auto& d : bypassDelay)
        d.prepare ((int) (sampleRate * 0.25));
    for (auto& d : mixDelay)
        d.prepare ((int) (sampleRate * 0.25));

    for (auto& p : slots)
        if (p != nullptr)
            preparePlugin (*p);
    for (auto& fx : builtIns)
        if (fx != nullptr)
            prepareBuiltIn (*fx);
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

void PluginChain::prepareBuiltIn (omx::BuiltInFx& fx)
{
    fx.prepareToPlay (sampleRate, blockSize);
}

//==============================================================================
// Built-in effects (message thread)

std::unique_ptr<omx::BuiltInFx> PluginChain::makeBuiltInFx (omx::FxType type)
{
    switch (type)
    {
        case omx::FxType::Reverb: return std::make_unique<omx::BuiltInReverb>();
        case omx::FxType::Delay:  return std::make_unique<omx::BuiltInDelay>();
        case omx::FxType::Chorus: return std::make_unique<omx::BuiltInChorus>();
    }
    return nullptr;
}

juce::String PluginChain::fxTypeDisplayName (omx::FxType type)
{
    switch (type)
    {
        case omx::FxType::Reverb: return "Reverb";
        case omx::FxType::Delay:  return "Delay";
        case omx::FxType::Chorus: return "Chorus";
    }
    return {};
}

bool PluginChain::fxTypeFromName (const juce::String& name, omx::FxType& type)
{
    if (name.equalsIgnoreCase ("Reverb")) { type = omx::FxType::Reverb; return true; }
    if (name.equalsIgnoreCase ("Delay"))  { type = omx::FxType::Delay;  return true; }
    if (name.equalsIgnoreCase ("Chorus")) { type = omx::FxType::Chorus; return true; }
    return false;
}

juce::String PluginChain::getInsertDisplayName (int slot) const
{
    if (! juce::isPositiveAndBelow (slot, kNumSlots))
        return {};

    if (auto* p = slots[(size_t) slot].get())
        return p->getName();
    if (auto* fx = builtIns[(size_t) slot].get())
        return fxTypeDisplayName (fx->getType()) + " (built-in)";
    return {};
}

void PluginChain::installBuiltIn (int slot, std::unique_ptr<omx::BuiltInFx> fx)
{
    if (fx == nullptr || ! juce::isPositiveAndBelow (slot, kNumSlots))
        return;

    // Keep the chain packed: a new effect past the end goes right after the last one.
    slot = juce::jmin (slot, getNumPlugins());

    std::unique_ptr<juce::AudioPluginInstance> oldPlugin;
    {
        const juce::ScopedLock sl (slotLock);
        prepareBuiltIn (*fx);
        oldPlugin = std::move (slots[(size_t) slot]);   // one occupant per slot: a VST here goes away
        builtIns[(size_t) slot] = std::move (fx);
        wasActive[(size_t) slot] = false;   // fade the new effect in
        bypassDelay[(size_t) slot].clear();
        mixDelay[(size_t) slot].clear();
        slotBypass[(size_t) slot].store (false);
        slotMix[(size_t) slot].store (1.0f);
        slotFault[(size_t) slot].store (false);
    }

    if (oldPlugin != nullptr)
        oldPlugin->releaseResources();
}

bool PluginChain::getBuiltInParams (int slot, std::array<float, 6>& params, bool& tempoSync) const
{
    params.fill (0.0f);
    tempoSync = false;
    if (! juce::isPositiveAndBelow (slot, kNumSlots))
        return false;

    if (auto* reverb = dynamic_cast<omx::BuiltInReverb*> (builtIns[(size_t) slot].get()))
    {
        params[0] = reverb->getRoomSize();
        params[1] = reverb->getDamping();
        params[2] = reverb->getMix();
        params[3] = reverb->getPreDelayMs();
        return true;
    }
    if (auto* delay = dynamic_cast<omx::BuiltInDelay*> (builtIns[(size_t) slot].get()))
    {
        params[0] = delay->getTimeMs();
        params[1] = delay->getFeedback();
        params[2] = delay->getMix();
        params[3] = delay->getFeedbackLowpassHz();
        params[4] = delay->getNoteDivision();
        params[5] = delay->getTempoBpm();
        tempoSync = delay->isTempoSynced();
        return true;
    }
    if (auto* chorus = dynamic_cast<omx::BuiltInChorus*> (builtIns[(size_t) slot].get()))
    {
        params[0] = chorus->getRateHz();
        params[1] = chorus->getDepthMs();
        params[2] = chorus->getMix();
        params[3] = chorus->getFeedback();
        return true;
    }
    return false;
}

bool PluginChain::setBuiltInParams (int slot, const std::array<float, 6>& params, bool tempoSync)
{
    if (! juce::isPositiveAndBelow (slot, kNumSlots))
        return false;

    if (auto* reverb = dynamic_cast<omx::BuiltInReverb*> (builtIns[(size_t) slot].get()))
    {
        reverb->setRoomSize (params[0]);
        reverb->setDamping (params[1]);
        reverb->setMix (params[2]);
        reverb->setPreDelayMs (params[3]);
        return true;
    }
    if (auto* delay = dynamic_cast<omx::BuiltInDelay*> (builtIns[(size_t) slot].get()))
    {
        // Order matters: setTempoBpm / setNoteDivision re-enable tempo sync and
        // recompute the time from each other, so set the BPM first, then the
        // division; a non-synced delay gets its direct time instead.
        if (tempoSync)
        {
            delay->setTempoBpm (params[5]);
            delay->setNoteDivision (params[4]);
        }
        else
        {
            delay->setTimeMs (params[0]);
        }
        delay->setFeedback (params[1]);
        delay->setMix (params[2]);
        delay->setFeedbackLowpassHz (params[3]);
        return true;
    }
    if (auto* chorus = dynamic_cast<omx::BuiltInChorus*> (builtIns[(size_t) slot].get()))
    {
        chorus->setRateHz (params[0]);
        chorus->setDepthMs (params[1]);
        chorus->setMix (params[2]);
        chorus->setFeedback (params[3]);
        return true;
    }
    return false;
}

void PluginChain::setDelayTempoBpm (float bpm)
{
    const juce::ScopedLock sl (slotLock);
    for (auto& fx : builtIns)
        if (auto* delay = dynamic_cast<omx::BuiltInDelay*> (fx.get()))
            if (delay->isTempoSynced())
                delay->setTempoBpm (bpm);
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
    // Occupied insert slots, packed 0..n-1 (VST plugins and built-in effects).
    int n = 0;
    while (n < kNumSlots && (slots[(size_t) n] != nullptr || builtIns[(size_t) n] != nullptr))
        ++n;
    return n;
}

int PluginChain::getNumVstPlugins() const noexcept
{
    int n = 0;
    for (auto& p : slots)
        n += p != nullptr ? 1 : 0;
    return n;
}

void PluginChain::installPlugin (int slot, std::unique_ptr<juce::AudioPluginInstance> instance)
{
    if (instance == nullptr || ! juce::isPositiveAndBelow (slot, kNumSlots))
        return;

    // Keep the chain packed: a new plugin past the end goes right after the last one.
    slot = juce::jmin (slot, getNumPlugins());

    std::unique_ptr<juce::AudioPluginInstance> old;
    {
        const juce::ScopedLock sl (slotLock);
        preparePlugin (*instance);
        old = std::move (slots[(size_t) slot]);
        slots[(size_t) slot] = std::move (instance);
        builtIns[(size_t) slot].reset();   // one occupant per slot: a built-in here goes away
        wasActive[(size_t) slot] = false;   // fade the new plugin in
        bypassDelay[(size_t) slot].clear();
        mixDelay[(size_t) slot].clear();
        slotBypass[(size_t) slot].store (false);
        slotMix[(size_t) slot].store (1.0f);
        slotFault[(size_t) slot].store (false);
    }

    if (old != nullptr)
        old->releaseResources();
}

void PluginChain::removePlugin (int slot)
{
    if (! juce::isPositiveAndBelow (slot, kNumSlots))
        return;

    std::unique_ptr<juce::AudioPluginInstance> old;
    {
        const juce::ScopedLock sl (slotLock);
        old = std::move (slots[(size_t) slot]);
        builtIns[(size_t) slot].reset();

        // Move later inserts up so the chain stays packed.
        for (int i = slot; i < kNumSlots - 1; ++i)
        {
            std::swap (slots[(size_t) i], slots[(size_t) i + 1]);
            std::swap (builtIns[(size_t) i], builtIns[(size_t) i + 1]);
            slotBypass[(size_t) i].store (slotBypass[(size_t) i + 1].load());
            slotFault[(size_t) i].store (slotFault[(size_t) i + 1].load());
            slotMix[(size_t) i].store (slotMix[(size_t) i + 1].load());
            std::swap (wasActive[(size_t) i], wasActive[(size_t) i + 1]);
            std::swap (bypassDelay[(size_t) i], bypassDelay[(size_t) i + 1]);
            std::swap (mixDelay[(size_t) i], mixDelay[(size_t) i + 1]);
        }
        slotBypass[kNumSlots - 1].store (false);
        slotMix[kNumSlots - 1].store (1.0f);
        slotFault[kNumSlots - 1].store (false);
        wasActive[kNumSlots - 1] = false;
    }

    if (old != nullptr)
        old->releaseResources();
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

    const juce::ScopedLock sl (slotLock);
    const int step = to > from ? 1 : -1;
    for (int i = from; i != to; i += step)
    {
        const auto a = (size_t) i, b = (size_t) (i + step);
        std::swap (slots[a], slots[b]);
        std::swap (builtIns[a], builtIns[b]);
        const bool bypassA = slotBypass[a].load(), faultA = slotFault[a].load();
        slotBypass[a].store (slotBypass[b].load()); slotBypass[b].store (bypassA);
        slotFault[a].store (slotFault[b].load());   slotFault[b].store (faultA);
        const float mixA = slotMix[a].load();
        slotMix[a].store (slotMix[b].load());       slotMix[b].store (mixA);
        std::swap (wasActive[a], wasActive[b]);
        std::swap (bypassDelay[a], bypassDelay[b]);
        std::swap (mixDelay[a], mixDelay[b]);
    }
}

//==============================================================================
// Audio

void PluginChain::process (float* L, float* R, int numSamples, int& latency) noexcept
{
    // If the UI is swapping a plugin right now, skip the inserts for this block.
    const juce::ScopedTryLock sl (slotLock);
    if (! sl.isLocked() || numSamples > scratch.getNumSamples())
        return;

    int chainLatency = 0;

    for (size_t s = 0; s < slots.size(); ++s)
    {
        auto* vst = slots[s].get();
        auto* fx = builtIns[s].get();
        if (vst == nullptr && fx == nullptr)
        {
            wasActive[s] = false;
            continue;
        }

        // Built-ins are always stereo in / stereo out with zero latency.
        const int numCh = vst != nullptr
            ? juce::jmax (2, vst->getTotalNumInputChannels(), vst->getTotalNumOutputChannels()) : 2;
        if (numCh > kMaxPluginChannels || slotFault[s].load())
        {
            wasActive[s] = false;
            continue;   // unusable or faulted: out of the chain (and out of the latency sum)
        }

        const int pluginLatency = vst != nullptr ? juce::jmax (0, vst->getLatencySamples()) : 0;
        chainLatency += pluginLatency;

        const bool slotOn = ! slotBypass[s].load() && (vst == nullptr || ! vst->isSuspended());
        const bool crossfade = slotOn != wasActive[s];

        // Steady bypass: keep the plugin's latency so timing never jumps.
        // Built-ins have no bypass parameter of their own, so a steady
        // bypass runs through the (zero-latency) delay, i.e. passes through.
        if (! slotOn && ! crossfade && (vst == nullptr || vst->getBypassParameter() == nullptr))
        {
            bypassDelay[s].process (L, R, numSamples, pluginLatency);
            continue;
        }

        if (crossfade)
        {
            dryBuffer.copyFrom (0, 0, L, numSamples);
            dryBuffer.copyFrom (1, 0, R, numSamples);
        }

        // Wet/dry: keep the dry input, delayed by the plugin's latency so the blend lines up.
        const float mix = slotMix[s].load();
        const bool blendDry = mix < 0.999f;
        if (blendDry)
        {
            mixDryBuffer.copyFrom (0, 0, L, numSamples);
            mixDryBuffer.copyFrom (1, 0, R, numSamples);
            mixDelay[s].process (mixDryBuffer.getWritePointer (0), mixDryBuffer.getWritePointer (1), numSamples, pluginLatency);
        }

        juce::AudioBuffer<float> view (scratch.getArrayOfWritePointers(), numCh, numSamples);
        view.clear();
        view.copyFrom (0, 0, L, numSamples);
        view.copyFrom (1, 0, R, numSamples);

        midi.clear();
        if (slotOn || crossfade)
        {
            if (fx != nullptr)
                fx->processBlock (view);
            else
                vst->processBlock (view, midi);
        }
        else if (vst != nullptr)
            vst->processBlockBypassed (view, midi);   // the plugin's own latency-correct bypass
        // (a bypassed built-in in a crossfade leaves the dry copy in `view`,
        // which the crossfade below then blends back in)

        const int outs = vst != nullptr ? vst->getTotalNumOutputChannels() : 2;
        const int rightSrc = outs == 1 ? 0 : 1;

        // A plugin that outputs NaN/Inf would poison everything after it permanently.
        // Drop its output, reset it and bypass it.
        if (AudioSafety::containsNonFinite (view.getReadPointer (0), numSamples)
            || AudioSafety::containsNonFinite (view.getReadPointer (rightSrc), numSamples))
        {
            slotFault[s].store (true);
            slotBypass[s].store (true);
            faultCount.fetch_add (1);
            chainLatency -= pluginLatency;
            if (fx != nullptr)
                fx->reset();
            else
                vst->reset();
            bypassDelay[s].clear();
            mixDelay[s].clear();
            wasActive[s] = false;
            continue;   // keep the dry signal
        }

        juce::FloatVectorOperations::copy (L, view.getReadPointer (0), numSamples);
        juce::FloatVectorOperations::copy (R, view.getReadPointer (rightSrc), numSamples);

        if (blendDry && (slotOn || crossfade))
        {
            juce::FloatVectorOperations::multiply (L, mix, numSamples);
            juce::FloatVectorOperations::multiply (R, mix, numSamples);
            juce::FloatVectorOperations::addWithMultiply (L, mixDryBuffer.getReadPointer (0), 1.0f - mix, numSamples);
            juce::FloatVectorOperations::addWithMultiply (R, mixDryBuffer.getReadPointer (1), 1.0f - mix, numSamples);
        }
        else if (! blendDry)
        {
            mixDelay[s].clear();
        }

        // Bypass / enable / newly loaded: crossfade over this block instead of clicking.
        if (crossfade)
        {
            const float wetStart = slotOn ? 0.0f : 1.0f;
            const float wetEnd   = slotOn ? 1.0f : 0.0f;
            float* chans[2] { L, R };
            juce::AudioBuffer<float> out (chans, 2, numSamples);
            out.applyGainRamp (0, numSamples, wetStart, wetEnd);
            out.addFromWithRamp (0, 0, dryBuffer.getReadPointer (0), numSamples, 1.0f - wetStart, 1.0f - wetEnd);
            out.addFromWithRamp (1, 0, dryBuffer.getReadPointer (1), numSamples, 1.0f - wetStart, 1.0f - wetEnd);
            if (! slotOn)
                bypassDelay[s].clear();
        }

        wasActive[s] = slotOn;
    }

    latencySamples.store (chainLatency, std::memory_order_relaxed);
    latency += chainLatency;
}
