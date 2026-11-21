#include "app/OscControl.h"

OscControl::OscControl (MixerEngine& e) : engine (e)
{
    addListener (this);
}

OscControl::~OscControl()
{
    stop();
    removeListener (this);
}

bool OscControl::start (int port, juce::String& error)
{
    stop();
    if (! connect (port))
    {
        error = "Couldn't listen on UDP port " + juce::String (port) + ". Another program may be using it.";
        return false;
    }
    running = true;
    juce::Logger::writeToLog ("OSC remote control listening on UDP " + juce::String (port));
    return true;
}

void OscControl::stop()
{
    if (running)
        disconnect();
    running = false;
}

static float argValue (const juce::OSCMessage& m)
{
    if (m.isEmpty())   return 1.0f;
    if (m[0].isFloat32()) return m[0].getFloat32();
    if (m[0].isInt32())   return (float) m[0].getInt32();
    return 1.0f;
}

static int busIndexFromName (const juce::String& name)
{
    for (int b = 0; b < kNumBuses; ++b)
        if (name.equalsIgnoreCase (kBusNames[b]))
            return b;
    return -1;
}

void OscControl::oscMessageReceived (const juce::OSCMessage& m)
{
    juce::StringArray p;
    p.addTokens (m.getAddressPattern().toString(), "/", {});
    p.removeEmptyStrings();
    if (p.isEmpty() || p[0] != "openmix")
        return;

    const float v = argValue (m);
    bool changed = true;

    if (p[1] == "strip" && p.size() >= 4)
    {
        const int s = p[2].getIntValue() - 1;
        if (! juce::isPositiveAndBelow (s, kNumStrips) || ! engine.getStrip (s).active.load())
            return;
        auto& strip = engine.getStrip (s);

        if (p[3] == "fader")            strip.faderDb.store (juce::jlimit (-60.0f, 12.0f, v));
        else if (p[3] == "gain")        strip.gainDb.store (juce::jlimit (-24.0f, 24.0f, v));
        else if (p[3] == "mute")        strip.mute.store (v > 0.5f);
        else if (p[3] == "solo")        strip.solo.store (v > 0.5f);
        else if (p[3] == "send" && p.size() >= 5)
        {
            if (const int b = busIndexFromName (p[4]); b >= 0) strip.busOn[b].store (v > 0.5f);
        }
        else if (p[3] == "sendlevel" && p.size() >= 5)
        {
            if (const int b = busIndexFromName (p[4]); b >= 0) strip.sendDb[b].store (juce::jlimit (-60.0f, 12.0f, v));
        }
        else changed = false;
    }
    else if (p[1] == "bus" && p.size() >= 4)
    {
        const int b = busIndexFromName (p[2]);
        if (b < 0 || ! engine.getBus (b).active.load())
            return;
        auto& bus = engine.getBus (b);

        if (p[3] == "fader")      bus.faderDb.store (juce::jlimit (-60.0f, 12.0f, v));
        else if (p[3] == "mute")  bus.mute.store (v > 0.5f);
        else changed = false;
    }
    else if (p[1] == "soundboard" && p.size() >= 4 && p[3] == "play")
    {
        engine.getSoundboard().trigger (p[2].getIntValue() - 1);
    }
    else if (p[1] == "record")
    {
        if (onRecord) onRecord (v > 0.5f);
    }
    else if (p[1] == "replay" && p[2] == "save")
    {
        if (onSaveReplay) onSaveReplay();
    }
    else
    {
        changed = false;
    }

    if (changed)
        engine.externalChanges.fetch_add (1);
}
