#include "app/MidiControl.h"
#include "app/AppSettings.h"

static int busIndexFromName (const juce::String& name)
{
    for (int b = 0; b < kNumBuses; ++b)
        if (name.equalsIgnoreCase (kBusNames[b]))
            return b;
    return -1;
}

MidiControl::MidiControl (MixerEngine& e) : engine (e) {}
MidiControl::~MidiControl()
{
    alive->store (false);
    stop();
}

juce::Array<juce::MidiDeviceInfo> MidiControl::getDevices()
{
    return juce::MidiInput::getAvailableDevices();
}

bool MidiControl::start (const juce::String& deviceIdentifier, juce::String& error)
{
    if (input != nullptr && deviceIdentifier == deviceId)
        return true;   // already listening there

    stop();
    deviceId = deviceIdentifier;
    if (! open())
    {
        error = "Couldn't open MIDI device " + deviceIdentifier;
        return false;
    }
    return true;
}

bool MidiControl::open()
{
    input = juce::MidiInput::openDevice (deviceId, this);
    if (input == nullptr)
        return false;
    input->start();
    juce::Logger::writeToLog ("MIDI control listening on " + input->getName());
    return true;
}

void MidiControl::close()
{
    if (input != nullptr)
    {
        input->stop();
        input.reset();
    }
}

void MidiControl::stop()
{
    close();
    deviceId.clear();
    const juce::ScopedLock sl (mappingsLock);
    learningTarget.clear();
}

void MidiControl::maintain()
{
    // Windows doesn't tell an open MIDI input that its device was unplugged: compare with
    // the device list instead. Gone: close it. Back: open it again.
    if (deviceId.isEmpty())
        return;

    bool present = false;
    for (const auto& d : getDevices())
        present = present || d.identifier == deviceId;

    if (! present && input != nullptr)
    {
        close();
        juce::Logger::writeToLog ("MIDI controller disconnected");
    }
    else if (present && input == nullptr)
    {
        open();
    }
}

juce::String MidiControl::getDeviceName() const
{
    return input != nullptr ? input->getName() : juce::String();
}

bool MidiControl::startLearn (const juce::String& target)
{
    const juce::ScopedLock sl (mappingsLock);
    if (learningTarget.isNotEmpty())
        return false;
    learningTarget = target;
    return true;
}

void MidiControl::cancelLearn()
{
    const juce::ScopedLock sl (mappingsLock);
    learningTarget.clear();
}

bool MidiControl::isLearning() const noexcept
{
    const juce::ScopedLock sl (mappingsLock);
    return learningTarget.isNotEmpty();
}

juce::String MidiControl::getLearningTarget() const
{
    const juce::ScopedLock sl (mappingsLock);
    return learningTarget;
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
    if (message.isController() || message.isNoteOn())
    {
        juce::String learnedFor;
        {
            const juce::ScopedLock sl (mappingsLock);
            if (learningTarget.isNotEmpty())
            {
                learnedFor = learningTarget;
                learningTarget.clear();

                Mapping m;
                m.isNote = message.isNoteOn();
                m.channel = message.getChannel();
                m.number = message.isController() ? message.getControllerNumber() : message.getNoteNumber();
                m.target = learnedFor;

                mappings.erase (std::remove_if (mappings.begin(), mappings.end(),
                                                [&m] (const Mapping& x) { return x.target == m.target; }),
                                mappings.end());
                mappings.push_back (m);
            }
        }

        if (learnedFor.isNotEmpty())
        {
            // Saved right away, whether or not the Settings window is still open.
            auto a = alive;
            juce::MessageManager::callAsync ([this, a]
            {
                if (a->load())
                    AppSettings::get().setMidiMappings (mappingsToJson (getMappings()));
            });
            return;
        }
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

bool MidiControl::currentToggle (const juce::StringArray& p) const
{
    // Strip and bus atomics are safe to read from the MIDI thread.
    if (p[0] == "strip" && p.size() >= 3)
    {
        const int s = p[1].getIntValue() - 1;
        if (! juce::isPositiveAndBelow (s, kNumStrips))
            return false;
        auto& strip = engine.getStrip (s);
        if (p[2] == "mute") return strip.mute.load();
        if (p[2] == "solo") return strip.solo.load();
        if (p[2] == "send")
        {
            const int b = busIndexFromName (p[3]);
            return b >= 0 && strip.busOn[b].load();
        }
    }
    else if (p[0] == "bus" && p.size() >= 3)
    {
        const int b = busIndexFromName (p[1]);
        if (b < 0)
            return false;
        return p[2] == "norm" ? engine.getBus (b).normOn.load() : engine.getBus (b).mute.load();
    }
    return false;
}

void MidiControl::applyMapping (const Mapping& m, const juce::MidiMessage& message)
{
    RemoteCallbacks cb;
    cb.onRecord = onRecord;
    cb.onSaveReplay = onSaveReplay;
    cb.onRecallScene = onRecallScene;

    // Record / scene / replay run on the message thread: a MIDI callback
    // must never do UI work itself.
    auto a = alive;
    auto callOnMessageThread = [a] (std::function<void()> fn)
    {
        juce::MessageManager::callAsync ([a, fn] { if (a->load()) fn(); });
    };

    const auto target = m.target;
    if (target == "record")
    {
        // Toggle recording on each trigger (CC >= 64 or note on) so a single button can start and stop.
        if (! m.isNote && message.getControllerValue() < 64)
            return;
        callOnMessageThread ([this, target, cb]
        {
            const float next = engine.getRecorder().isRecording() ? 0.0f : 1.0f;
            applyRemoteCommand (engine, target, next, cb);
        });
        return;
    }
    if (target.startsWith ("scene/") || target == "replay/save")
    {
        callOnMessageThread ([this, target, cb]
        {
            applyRemoteCommand (engine, target, 1.0f, cb);
        });
        return;
    }

    juce::StringArray p;
    p.addTokens (target, "/", {});
    p.removeEmptyStrings();

    // Continuous controls: CC 0..127 across the control's range. Paths: strip/<n>/fader,
    // strip/<n>/gain, bus/<name>/fader, strip/<n>/sendlevel/<bus>.
    const bool isLevel = (p.size() == 3 && (p[2] == "fader" || p[2] == "gain"))
                      || (p.size() == 4 && p[2] == "sendlevel");
    if (isLevel)
    {
        const bool gain = p[2] == "gain";
        const float lo = gain ? -24.0f : -60.0f, hi = gain ? 24.0f : 12.0f;
        const float value = lo + (hi - lo) * (message.getControllerValue() / 127.0f);
        applyRemoteCommand (engine, target, value, cb);
        return;
    }

    // On/off controls: strip/<n>/mute, strip/<n>/solo, strip/<n>/send/<bus>, bus/<name>/mute,
    // bus/<name>/norm. Each press flips what the mixer has now, so a mute made in the app (or
    // anywhere else) is never out of step with the controller.
    const bool isToggle = (p.size() == 3 && (p[2] == "mute" || p[2] == "solo" || p[2] == "norm"))
                       || (p.size() == 4 && p[2] == "send");
    if (isToggle)
    {
        if (! m.isNote)
        {
            // CC: act once when the control crosses 64 upwards, not on every message
            // while it sits above 64.
            const juce::ScopedLock sl (mappingsLock);
            const bool high = message.getControllerValue() >= 64;
            bool& last = ccToggleState[target];
            if (high == last)
                return;
            last = high;
            if (! high)
                return;
        }
        applyRemoteCommand (engine, target, currentToggle (p) ? 0.0f : 1.0f, cb);
        return;
    }

    if (target.startsWith ("soundboard/"))
    {
        const auto cmdPath = target.endsWith ("/play") ? target : target + "/play";
        applyRemoteCommand (engine, cmdPath, 1.0f, cb);
        return;
    }

    // Anything else: pass the raw value through (0..1).
    applyRemoteCommand (engine, target, m.isNote ? 1.0f : message.getControllerValue() / 127.0f, cb);
}
