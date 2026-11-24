#include "app/MidiControl.h"

MidiControl::MidiControl (MixerEngine& e) : engine (e) {}
MidiControl::~MidiControl() { stop(); }

juce::Array<juce::MidiDeviceInfo> MidiControl::getDevices()
{
    return juce::MidiInput::getAvailableDevices();
}

bool MidiControl::start (const juce::String& deviceIdentifier, juce::String& error)
{
    stop();
    input = juce::MidiInput::openDevice (deviceIdentifier, this);
    if (input == nullptr)
    {
        error = "Couldn't open MIDI device " + deviceIdentifier;
        return false;
    }
    input->start();
    juce::Logger::writeToLog ("MIDI control listening on " + input->getName());
    return true;
}

void MidiControl::stop()
{
    if (input != nullptr)
    {
        input->stop();
        input.reset();
    }
    learningTarget.clear();
}

juce::String MidiControl::getDeviceName() const
{
    return input != nullptr ? input->getName() : juce::String();
}

bool MidiControl::startLearn (const juce::String& target)
{
    if (learningTarget.isNotEmpty())
        return false;
    learningTarget = target;
    return true;
}

void MidiControl::cancelLearn()
{
    learningTarget.clear();
}

std::vector<MidiControl::Mapping> MidiControl::getMappings() const
{
    const juce::ScopedLock sl (mappingsLock);
    return mappings;
}

void MidiControl::setMappings (std::vector<Mapping> m)
{
    const juce::ScopedLock sl (mappingsLock);
    mappings = std::move (m);
}

void MidiControl::clearMappings()
{
    const juce::ScopedLock sl (mappingsLock);
    mappings.clear();
    ccToggleState.clear();
}

juce::String MidiControl::mappingsToJson (const std::vector<Mapping>& maps)
{
    juce::Array<juce::var> arr;
    for (auto& m : maps)
    {
        juce::DynamicObject::Ptr o = new juce::DynamicObject();
        o->setProperty ("type", m.isNote ? "note" : "cc");
        o->setProperty ("channel", m.channel);
        o->setProperty ("number", m.number);
        o->setProperty ("target", m.target);
        arr.add (juce::var (o.get()));
    }
    return juce::JSON::toString (juce::var (arr));
}

std::vector<MidiControl::Mapping> MidiControl::mappingsFromJson (const juce::String& json)
{
    std::vector<Mapping> out;
    juce::var parsed;
    if (juce::JSON::parse (json, parsed).failed() || ! parsed.isArray())
        return out;
    for (auto& v : *parsed.getArray())
    {
        if (! v.isObject())
            continue;
        auto* o = v.getDynamicObject();
        Mapping m;
        m.isNote = o->getProperty ("type").toString() == "note";
        m.channel = (int) o->getProperty ("channel");
        m.number = (int) o->getProperty ("number");
        m.target = o->getProperty ("target").toString();
        if (! m.target.isEmpty())
            out.push_back (m);
    }
    return out;
}

void MidiControl::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& message)
{
    // Learn mode captures the first CC or note, on any channel.
    if (learningTarget.isNotEmpty() && (message.isController() || message.isNoteOn()))
    {
        Mapping m;
        m.isNote = message.isNoteOn();
        m.channel = message.getChannel();
        m.number = message.isController() ? message.getControllerNumber() : message.getNoteNumber();
        m.target = learningTarget;
        learningTarget.clear();

        std::vector<Mapping> maps;
        {
            const juce::ScopedLock sl (mappingsLock);
            maps = mappings;
        }
        maps.erase (std::remove_if (maps.begin(), maps.end(),
                                    [&m] (const Mapping& x) { return x.target == m.target; }),
                    maps.end());
        maps.push_back (m);
        setMappings (maps);
        return;
    }

    std::vector<Mapping> maps;
    {
        const juce::ScopedLock sl (mappingsLock);
        maps = mappings;
    }
    for (auto& m : maps)
    {
        if (m.channel != 0 && m.channel != message.getChannel())
            continue;
        if (! m.isNote && message.isController() && message.getControllerNumber() == m.number)
            applyMapping (m, message);
        else if (m.isNote && message.isNoteOn() && message.getNoteNumber() == m.number)
            applyMapping (m, message);
    }
}

void MidiControl::applyMapping (const Mapping& m, const juce::MidiMessage& message)
{
    RemoteCallbacks cb;
    cb.onRecord = onRecord;
    cb.onSaveReplay = onSaveReplay;
    cb.onRecallScene = onRecallScene;

    // Record / scene / replay run on the message thread: a MIDI callback
    // must never do UI work itself.
    auto callOnMessageThread = [] (std::function<void()> fn)
    {
        juce::MessageManager::callAsync ([fn] { fn(); });
    };

    const auto target = m.target;
    if (target == "record" || target.startsWith ("scene/") || target == "replay/save")
    {
        callOnMessageThread ([this, target, cb]
        {
            applyRemoteCommand (engine, target, 1.0f, cb);
        });
        return;
    }

    if (target.endsWith ("/fader") || target.endsWith ("/gain") || target.endsWith ("/sendlevel"))
    {
        // CC 0..127 across the control's range.
        const float lo = target.endsWith ("/gain") ? -24.0f : -60.0f;
        const float hi = target.endsWith ("/gain") ? 24.0f : 12.0f;
        const float value = lo + (hi - lo) * (message.getControllerValue() / 127.0f);
        applyRemoteCommand (engine, target, value, cb);
        return;
    }

    if (target.endsWith ("/mute") || target.endsWith ("/solo") || target.endsWith ("/norm"))
    {
        if (! m.isNote)
        {
            // CC: flip once when the control crosses 64 upwards, not on
            // every message while it sits above 64.
            const juce::ScopedLock sl (mappingsLock);
            const bool high = message.getControllerValue() >= 64;
            bool& last = ccToggleState[target];
            if (high == last)
                return;
            last = high;
            if (! high)
                return;
            bool& on = ccToggleState[target + ":state"];
            on = ! on;
            applyRemoteCommand (engine, target, on ? 1.0f : 0.0f, cb);
            return;
        }
        // Note on toggles: read the current mute/solo from the engine.
        // (Strip/bus mutes are atomics, safe to read from the MIDI thread.)
        float current = 0.0f;
        juce::StringArray p;
        p.addTokens (target, "/", {});
        if (p[0] == "strip" && p.size() >= 3)
        {
            const int s = p[1].getIntValue() - 1;
            if (juce::isPositiveAndBelow (s, kNumStrips))
                current = p[2] == "mute" ? (engine.getStrip (s).mute.load() ? 1.0f : 0.0f)
                                         : (engine.getStrip (s).solo.load() ? 1.0f : 0.0f);
        }
        else if (p[0] == "bus" && p.size() >= 3)
        {
            // Bus mute is p[1] = name, p[2] = "mute".
            for (int b = 0; b < kNumBuses; ++b)
                if (juce::String (kBusNames[b]).equalsIgnoreCase (p[1]))
                    current = engine.getBus (b).mute.load() ? 1.0f : 0.0f;
        }
        applyRemoteCommand (engine, target, current > 0.5f ? 0.0f : 1.0f, cb);
        return;
    }

    if (target.startsWith ("soundboard/"))
    {
        applyRemoteCommand (engine, target + "/play", 1.0f, cb);
        return;
    }

    // Anything else: pass the raw value through (0..1).
    applyRemoteCommand (engine, target, m.isNote ? 1.0f : message.getControllerValue() / 127.0f, cb);
}
