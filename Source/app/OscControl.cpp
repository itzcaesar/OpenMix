#include "app/OscControl.h"
#include "app/RemoteCommands.h"

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

void OscControl::oscMessageReceived (const juce::OSCMessage& m)
{
    RemoteCallbacks cb;
    cb.onRecord = onRecord;
    cb.onSaveReplay = onSaveReplay;
    cb.onRecallScene = onRecallScene;
    applyRemoteCommand (engine, m.getAddressPattern().toString(), argValue (m), cb);
}
