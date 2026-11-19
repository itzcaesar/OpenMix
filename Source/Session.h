#pragma once

#include <JuceHeader.h>

class MixerEngine;

// Saves and restores the whole mixer (device, strips, plugins + their state, buses)
// to %APPDATA%\OpenMix\session.xml.
namespace Session
{
    juce::File getFile();

    std::unique_ptr<juce::XmlElement> load();
    void save (MixerEngine&);

    // Applies everything except the audio device (which MixerEngine::start handles).
    void apply (const juce::XmlElement&, MixerEngine&);
}
