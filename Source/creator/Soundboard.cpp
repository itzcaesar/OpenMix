#include "creator/Soundboard.h"

Soundboard::Soundboard()
{
    formats.registerBasicFormats();   // WAV, AIFF, FLAC, Ogg Vorbis, MP3 (JUCE_USE_MP3AUDIOFORMAT)
    busOn[0].store (true);            // A1: you hear it
    busOn[3].store (true);            // B1: your stream/call hears it
    gain.setCurrentAndTargetValue (1.0f);
}

void Soundboard::makePlayable (Pad& pad, double targetRate)
{
    juce::AudioBuffer<float> out;
    if (pad.original.getNumSamples() > 0 && pad.originalRate > 0.0)
    {
        const double ratio = pad.originalRate / targetRate;
        const int outLen = (int) std::ceil (pad.original.getNumSamples() / ratio);
        out.setSize (2, outLen);
        out.clear();

        for (int c = 0; c < 2; ++c)
        {
            const int src = juce::jmin (c, pad.original.getNumChannels() - 1);   // mono files: both sides
            if (std::abs (ratio - 1.0) < 1.0e-9)
            {
                out.copyFrom (c, 0, pad.original, src, 0, juce::jmin (outLen, pad.original.getNumSamples()));
            }
            else
            {
                juce::WindowedSincInterpolator interp;
                interp.process (ratio, pad.original.getReadPointer (src), out.getWritePointer (c), outLen - 8);
            }
        }
    }

    const juce::ScopedLock sl (lock);
    std::swap (pad.playable, out);
    pad.pos = 0;
    pad.playing = false;
    pad.playingFlag = false;
}

bool Soundboard::loadPad (int index, const juce::File& file, juce::String& error)
{
    if (! juce::isPositiveAndBelow (index, numPads))
        return false;

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
    {
        error = file.getFileName() + " isn't an audio file OpenMix can read (WAV, MP3, FLAC, Ogg or AIFF).";
        return false;
    }

    const auto maxSamples = (juce::int64) (reader->sampleRate * maxSeconds);
    if (reader->lengthInSamples > maxSamples)
    {
        error = file.getFileName() + " is longer than " + juce::String ((int) maxSeconds) + " seconds.";
        return false;
    }

    auto& pad = pads[(size_t) index];
    pad.original.setSize ((int) juce::jlimit (1u, 2u, reader->numChannels), (int) reader->lengthInSamples);
    reader->read (&pad.original, 0, (int) reader->lengthInSamples, 0, true, pad.original.getNumChannels() > 1);
    pad.originalRate = reader->sampleRate;
    pad.info.file = file.getFullPathName();
    pad.info.name = file.getFileNameWithoutExtension();
    pad.info.seconds = reader->lengthInSamples / reader->sampleRate;

    makePlayable (pad, rate);
    return true;
}

void Soundboard::clearPad (int index)
{
    if (! juce::isPositiveAndBelow (index, numPads))
        return;

    auto& pad = pads[(size_t) index];
    pad.original.setSize (0, 0);
    pad.originalRate = 0.0;
    pad.info = {};
    makePlayable (pad, rate);
}

void Soundboard::renamePad (int index, const juce::String& name)
{
    if (juce::isPositiveAndBelow (index, numPads))
        pads[(size_t) index].info.name = name;
}

Soundboard::PadInfo Soundboard::getPadInfo (int index) const
{
    return juce::isPositiveAndBelow (index, numPads) ? pads[(size_t) index].info : PadInfo {};
}

void Soundboard::trigger (int index)
{
    if (! juce::isPositiveAndBelow (index, numPads))
        return;

    auto& pad = pads[(size_t) index];
    pad.command.store (pad.playingFlag.load() ? 2 : 1);
}

void Soundboard::stopAll()
{
    for (auto& pad : pads)
        if (pad.playingFlag.load())
            pad.command.store (2);
}

void Soundboard::prepare (double sampleRate)
{
    gain.reset (sampleRate, 0.02);
    if (std::abs (sampleRate - rate) > 0.5)
    {
        rate = sampleRate;
        for (auto& pad : pads)
            makePlayable (pad, rate);
    }
    rate = sampleRate;
}

bool Soundboard::render (float* left, float* right, int numSamples) noexcept
{
    const juce::ScopedTryLock sl (lock);
    if (! sl.isLocked())
        return false;

    bool any = false;
    for (auto& pad : pads)
    {
        if (const int cmd = pad.command.exchange (0); cmd == 1)
        {
            pad.pos = 0;
            pad.playing = pad.playable.getNumSamples() > 0;
        }
        else if (cmd == 2)
        {
            pad.playing = false;
        }

        if (! pad.playing)
        {
            pad.playingFlag.store (false, std::memory_order_relaxed);
            continue;
        }

        const int n = juce::jmin (numSamples, pad.playable.getNumSamples() - pad.pos);
        juce::FloatVectorOperations::add (left, pad.playable.getReadPointer (0, pad.pos), n);
        juce::FloatVectorOperations::add (right, pad.playable.getReadPointer (1, pad.pos), n);
        pad.pos += n;
        if (pad.pos >= pad.playable.getNumSamples())
            pad.playing = false;

        pad.playingFlag.store (pad.playing, std::memory_order_relaxed);
        any = true;
    }

    gain.setTargetValue (juce::Decibels::decibelsToGain (volumeDb.load(), -60.0f));
    if (! any && ! gain.isSmoothing())
        return false;

    for (int i = 0; i < numSamples; ++i)
    {
        const float g = gain.getNextValue();
        left[i] *= g;
        right[i] *= g;
    }

    const auto rangeL = juce::FloatVectorOperations::findMinAndMax (left, numSamples);
    const auto rangeR = juce::FloatVectorOperations::findMinAndMax (right, numSamples);
    const float pl = juce::jmax (-rangeL.getStart(), rangeL.getEnd());
    const float pr = juce::jmax (-rangeR.getStart(), rangeR.getEnd());
    if (pl > peakL.load()) peakL.store (pl);
    if (pr > peakR.load()) peakR.store (pr);
    return any;
}
