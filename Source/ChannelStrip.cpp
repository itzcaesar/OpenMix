#include "ChannelStrip.h"

static float dbToGain (float db) noexcept
{
    return db <= -60.0f ? 0.0f : juce::Decibels::decibelsToGain (db);
}

ChannelStrip::ChannelStrip() = default;

ChannelStrip::~ChannelStrip()
{
    const juce::ScopedLock sl (slotLock);
    for (auto& p : slots)
        if (p != nullptr)
            p->releaseResources();
}

void ChannelStrip::prepare (double newSampleRate, int maxBlockSize)
{
    const juce::ScopedLock sl (slotLock);

    sampleRate = newSampleRate;
    blockSize = maxBlockSize;

    scratch.setSize (kMaxPluginChannels, maxBlockSize);
    midi.ensureSize (256);
    inGain.reset (sampleRate, 0.02);
    outGain.reset (sampleRate, 0.02);
    inGain.setCurrentAndTargetValue (dbToGain (gainDb.load()));
    outGain.setCurrentAndTargetValue (0.0f);
    padProcessor.prepare (sampleRate, maxBlockSize);

    for (auto& p : slots)
        if (p != nullptr)
            preparePlugin (*p);
}

void ChannelStrip::release()
{
    const juce::ScopedLock sl (slotLock);
    for (auto& p : slots)
        if (p != nullptr)
            p->releaseResources();
}

void ChannelStrip::preparePlugin (juce::AudioPluginInstance& plugin)
{
    // Ask for stereo in / stereo out on the main buses; keep the plugin's default if refused.
    auto layout = plugin.getBusesLayout();
    if (! layout.inputBuses.isEmpty())  layout.inputBuses.getReference (0)  = juce::AudioChannelSet::stereo();
    if (! layout.outputBuses.isEmpty()) layout.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
    plugin.setBusesLayout (layout);

    plugin.setNonRealtime (false);
    plugin.prepareToPlay (sampleRate, blockSize);
}

bool ChannelStrip::loadPlugin (int slot, const juce::PluginDescription& desc,
                               juce::AudioPluginFormatManager& formats, juce::String& error)
{
    auto instance = formats.createPluginInstance (desc, sampleRate, blockSize, error);
    if (instance == nullptr)
        return false;

    std::unique_ptr<juce::AudioPluginInstance> old;
    {
        const juce::ScopedLock sl (slotLock);
        preparePlugin (*instance);
        old = std::move (slots[(size_t) slot]);
        slots[(size_t) slot] = std::move (instance);
    }

    if (old != nullptr)
        old->releaseResources();

    return true;
}

void ChannelStrip::removePlugin (int slot)
{
    slotBypass[(size_t) slot].store (false);
    std::unique_ptr<juce::AudioPluginInstance> old;
    {
        const juce::ScopedLock sl (slotLock);
        old = std::move (slots[(size_t) slot]);
    }

    if (old != nullptr)
        old->releaseResources();
}

void ChannelStrip::setExternalSource (std::unique_ptr<ExternalSource> source)
{
    {
        const juce::ScopedLock sl (sourceLock);
        std::swap (external, source);
    }
    // `source` now holds the old one; it stops its device/thread here, outside the lock.
}

bool ChannelStrip::pullExternal (float* l, float* r, int numSamples, double rate) noexcept
{
    const juce::ScopedTryLock sl (sourceLock);
    if (! sl.isLocked() || external == nullptr)
        return false;

    external->bridge.read (l, r, numSamples, rate);
    return true;
}

void ChannelStrip::process (const float* inL, const float* inR, juce::AudioBuffer<float>& out, int numSamples) noexcept
{
    auto* L = out.getWritePointer (0);
    auto* R = out.getWritePointer (1);

    // 1. Input + gain
    inGain.setTargetValue (dbToGain (gainDb.load()));
    if (inL != nullptr)
    {
        if (inR == nullptr)
            inR = inL;

        for (int i = 0; i < numSamples; ++i)
        {
            const float g = inGain.getNextValue();
            const float l = inL[i] * g, r = inR[i] * g;   // read both before writing (in/out may alias)
            L[i] = l;
            R[i] = r;
        }
    }
    else
    {
        inGain.skip (numSamples);
        out.clear (0, numSamples);
    }

    // 2. Plugin slots. If the UI is swapping a plugin right now, bypass for this block.
    {
        const juce::ScopedTryLock sl (slotLock);
        if (sl.isLocked() && numSamples <= scratch.getNumSamples())
        {
            for (size_t s = 0; s < slots.size(); ++s)
            {
                auto& p = slots[s];
                if (p == nullptr || p->isSuspended() || slotBypass[s].load())
                    continue;

                const int numCh = juce::jmax (2, p->getTotalNumInputChannels(), p->getTotalNumOutputChannels());
                if (numCh > kMaxPluginChannels)
                    continue;

                juce::AudioBuffer<float> view (scratch.getArrayOfWritePointers(), numCh, numSamples);
                view.clear();
                view.copyFrom (0, 0, L, numSamples);
                view.copyFrom (1, 0, R, numSamples);

                midi.clear();
                p->processBlock (view, midi);

                const int outs = p->getTotalNumOutputChannels();
                out.copyFrom (0, 0, view, 0, 0, numSamples);
                out.copyFrom (1, 0, view, outs == 1 ? 0 : 1, 0, numSamples);
            }
        }
    }

    // 3. VoicePad
    padProcessor.process (out, numSamples);

    // 4. Fader / mute
    outGain.setTargetValue (mute.load() ? 0.0f : dbToGain (faderDb.load()));
    for (int i = 0; i < numSamples; ++i)
    {
        const float g = outGain.getNextValue();
        L[i] *= g;
        R[i] *= g;
    }

    // 5. Meters (peak hold with decay done by the UI)
    const float pl = out.getMagnitude (0, 0, numSamples);
    const float pr = out.getMagnitude (1, 0, numSamples);
    if (pl > peakL.load()) peakL.store (pl);
    if (pr > peakR.load()) peakR.store (pr);
}
