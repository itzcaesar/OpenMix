#pragma once

#include <JuceHeader.h>

class MixerEngine;

// Saves and restores the whole mixer (device, strips, plugins + their state, buses)
// to %APPDATA%\OpenMix\session.xml.
namespace Session
{
    juce::File getFile();

    std::unique_ptr<juce::XmlElement> load();
    // reusePluginState: take each plugin's state from a copy up to a minute old instead of
    // asking the plugin (some take 100 ms+ and return megabytes). For the periodic autosave.
    void save (MixerEngine&, bool reusePluginState = false);

    // Applies everything except the audio device (which MixerEngine::start handles).
    // loadPlugins = false skips plugin slots (safe mode after a crash while loading them).
    void apply (const juce::XmlElement&, MixerEngine&, bool loadPlugins = true);

    // Scenes: a snapshot of the mix only (not sources, devices or plugins), for quick recall.
    std::unique_ptr<juce::XmlElement> captureScene (MixerEngine&);
    void applyScene (const juce::XmlElement&, MixerEngine&);
}
