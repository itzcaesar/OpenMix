#include "app/RemoteCommands.h"

static int busIndexFromName (const juce::String& name)
{
    for (int b = 0; b < kNumBuses; ++b)
        if (name.equalsIgnoreCase (kBusNames[b]))
            return b;
    return -1;
}

bool applyRemoteCommand (MixerEngine& engine, const juce::String& path, float v,
                         const RemoteCallbacks& cb)
{
    juce::StringArray p;
    p.addTokens (path, "/", {});
    p.removeEmptyStrings();
    if (p.isEmpty())
        return false;
    if (p[0] == "openmix")
        p.remove (0);

    if (p.isEmpty())
        return false;

    bool changed = true;

    if (p[0] == "strip" && p.size() >= 3)
    {
        const int s = p[1].getIntValue() - 1;
        if (! juce::isPositiveAndBelow (s, kNumStrips) || ! engine.getStrip (s).active.load())
            return false;
        auto& strip = engine.getStrip (s);

        if (p[2] == "fader")            strip.faderDb.store (juce::jlimit (-60.0f, 12.0f, v));
        else if (p[2] == "gain")        strip.gainDb.store (juce::jlimit (-24.0f, 24.0f, v));
        else if (p[2] == "mute")        strip.mute.store (v > 0.5f);
        else if (p[2] == "solo")        strip.solo.store (v > 0.5f);
        else if (p[2] == "send" && p.size() >= 4)
        {
            if (const int b = busIndexFromName (p[3]); b >= 0) strip.busOn[b].store (v > 0.5f);
            else changed = false;
        }
        else if (p[2] == "sendlevel" && p.size() >= 4)
        {
            if (const int b = busIndexFromName (p[3]); b >= 0) strip.sendDb[b].store (juce::jlimit (-60.0f, 12.0f, v));
            else changed = false;
        }
        else changed = false;
    }
    else if (p[0] == "bus" && p.size() >= 3)
    {
        const int b = busIndexFromName (p[1]);
        if (b < 0 || ! engine.getBus (b).active.load())
            return false;
        auto& bus = engine.getBus (b);

        if (p[2] == "fader")      bus.faderDb.store (juce::jlimit (-60.0f, 12.0f, v));
        else if (p[2] == "mute")  bus.mute.store (v > 0.5f);
        else if (p[2] == "norm")  bus.normOn.store (v > 0.5f);
        else changed = false;
    }
    else if (p[0] == "soundboard" && p.size() >= 3 && p[2] == "play")
    {
        const int pad = p[1].getIntValue() - 1;
        if (juce::isPositiveAndBelow (pad, Soundboard::numPads))
            engine.getSoundboard().trigger (pad);
        else
            changed = false;
    }
    else if (p[0] == "record")
    {
        if (cb.onRecord) cb.onRecord (v > 0.5f);
    }
    else if (p[0] == "replay" && p.size() >= 2 && p[1] == "save")
    {
        if (cb.onSaveReplay) cb.onSaveReplay();
    }
    else if (p[0] == "scene" && p.size() >= 2)
    {
        if (cb.onRecallScene) cb.onRecallScene (p[1].getIntValue() - 1);
    }
    else
    {
        changed = false;
    }

    if (changed)
        engine.externalChanges.fetch_add (1);
    return changed;
}

juce::String remoteStateJson (MixerEngine& engine, bool includeMeters)
{
    juce::DynamicObject::Ptr root = new juce::DynamicObject();

    juce::Array<juce::var> strips;
    for (int s = 0; s < kNumStrips; ++s)
    {
        auto& strip = engine.getStrip (s);
        if (! strip.active.load())
            continue;
        juce::DynamicObject::Ptr o = new juce::DynamicObject();
        o->setProperty ("index", s + 1);
        o->setProperty ("name", strip.name.isEmpty() ? juce::String ("Input ") + juce::String (s + 1) : strip.name);
        o->setProperty ("faderDb", (double) strip.faderDb.load());
        o->setProperty ("gainDb", (double) strip.gainDb.load());
        o->setProperty ("mute", strip.mute.load());
        o->setProperty ("solo", strip.solo.load());
        if (includeMeters)
            o->setProperty ("peakDb", (double) juce::Decibels::gainToDecibels (juce::jmax (strip.peakL.load(), strip.peakR.load()), -100.0f));
        strips.add (juce::var (o.get()));
    }
    root->setProperty ("strips", strips);

    juce::Array<juce::var> buses;
    for (int b = 0; b < kNumBuses; ++b)
    {
        auto& bus = engine.getBus (b);
        if (! bus.active.load())
            continue;
        juce::DynamicObject::Ptr o = new juce::DynamicObject();
        o->setProperty ("name", juce::String (kBusNames[b]));
        o->setProperty ("displayName", engine.getBusName (b));
        o->setProperty ("faderDb", (double) bus.faderDb.load());
        o->setProperty ("mute", bus.mute.load());
        if (includeMeters)
            o->setProperty ("peakDb", (double) juce::Decibels::gainToDecibels (juce::jmax (bus.peakL.load(), bus.peakR.load()), -100.0f));
        o->setProperty ("normOn", bus.normOn.load());
        o->setProperty ("normTargetLufs", (double) bus.normTargetLufs.load());
        buses.add (juce::var (o.get()));
    }
    root->setProperty ("buses", buses);

    juce::Array<juce::var> pads;
    juce::Array<juce::var> padNames;
    for (int p = 0; p < Soundboard::numPads; ++p)
    {
        pads.add (engine.getSoundboard().isPlaying (p));
        padNames.add (engine.getSoundboard().getPadInfo (p).name);
    }
    root->setProperty ("soundboardPlaying", pads);
    root->setProperty ("soundboardNames", padNames);

    juce::Array<juce::var> scenes;
    for (auto& scene : engine.scenes)
        scenes.add (scene.name);
    root->setProperty ("scenes", scenes);
    root->setProperty ("recording", engine.getRecorder().isRecording());

    return juce::JSON::toString (juce::var (root.get()));
}
