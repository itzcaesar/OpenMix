#include "app/Session.h"
#include "app/AppSettings.h"
#include "engine/MixerEngine.h"
#include "ui/Theme.h"

#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <vector>

// Session format
//   version 1: 3 strips, buses saved by position (0 = A1, 1 = A2, 2 = B1)
//   version 2: up to 8 strips / 6 buses with an "active" flag, buses saved by name,
//              sends, processing, colours, stereo pairs, app routing, monitor bus
static constexpr int sessionVersion = 2;

juce::File Session::getFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("OpenMix").getChildFile ("session.xml");
}

static juce::File pendingFile() { return Session::getFile().getSiblingFile ("session-pending.xml"); }

std::unique_ptr<juce::XmlElement> Session::load()
{
    const auto file = getFile();
    if (const auto pending = pendingFile(); pending.existsAsFile())
    {
        if (file.existsAsFile())
            file.copyFileTo (file.getSiblingFile ("session-backup.xml"));
        if (pending.getSize() > 0)
            pending.copyFileTo (file);
        else
            file.deleteFile();   // Reset settings: start as on a first run
        pending.deleteFile();
    }
    else if (const auto startup = AppSettings::get().startupSessionFile(); isSessionFile (startup))
    {
        return juce::parseXML (startup);
    }
    return file.existsAsFile() ? juce::parseXML (file) : nullptr;
}

bool Session::isSessionFile (const juce::File& f)
{
    if (! f.existsAsFile())
        return false;
    const auto xml = juce::parseXML (f);
    return xml != nullptr && xml->hasTagName ("OpenMixSession");
}

bool Session::queueForNextStart (const juce::XmlElement* session)
{
    const auto pending = pendingFile();
    pending.getParentDirectory().createDirectory();
    if (session == nullptr)
        return pending.replaceWithText ({});
    return session->writeTo (pending);
}

static void writePadXml (juce::XmlElement& e, const VoicePadParams& pad)
{
    e.setAttribute ("padX", pad.x.load());
    e.setAttribute ("padY", pad.y.load());
    e.setAttribute ("padMode", pad.mode.load());
    for (int m = 0; m < VoicePadParams::numModes; ++m)
    {
        e.setAttribute ("padModeX" + juce::String (m), pad.modeX[(size_t) m].load());
        e.setAttribute ("padModeY" + juce::String (m), pad.modeY[(size_t) m].load());
    }
}

static void readPadXml (const juce::XmlElement& e, VoicePadParams& pad)
{
    const int m = e.getIntAttribute ("padMode", pad.mode.load());
    pad.mode.store (juce::jlimit (0, VoicePadParams::numModes - 1, m));
    for (int i = 0; i < VoicePadParams::numModes; ++i)
    {
        const float defX = pad.modeX[(size_t) i].load();
        const float defY = pad.modeY[(size_t) i].load();
        pad.setModePosition (i,
                             (float) e.getDoubleAttribute ("padModeX" + juce::String (i), defX),
                             (float) e.getDoubleAttribute ("padModeY" + juce::String (i), defY));
    }
    const float legacyX = (float) e.getDoubleAttribute ("padX", pad.x.load());
    const float legacyY = (float) e.getDoubleAttribute ("padY", pad.y.load());
    pad.setPosition (legacyX, legacyY);
}

static void saveProcessing (juce::XmlElement& x, const ProcessingParams& fx)
{
    auto* e = x.createNewChildElement ("Processing");
    e->setAttribute ("denoiseOn", fx.denoiseOn.load());
    e->setAttribute ("denoiseAmount", fx.denoiseAmount.load());
    e->setAttribute ("denoiseModel", fx.denoiseModel.load());
    e->setAttribute ("gateOn", fx.gateOn.load());
    e->setAttribute ("gateThreshold", fx.gateThresholdDb.load());
    e->setAttribute ("gateRange", fx.gateRangeDb.load());
    e->setAttribute ("gateAttack", fx.gateAttackMs.load());
    e->setAttribute ("gateHold", fx.gateHoldMs.load());
    e->setAttribute ("gateRelease", fx.gateReleaseMs.load());
    e->setAttribute ("hpfOn", fx.hpfOn.load());
    e->setAttribute ("hpfHz", fx.hpfHz.load());
    e->setAttribute ("lpfOn", fx.lpfOn.load());
    e->setAttribute ("lpfHz", fx.lpfHz.load());
    e->setAttribute ("eqOn", fx.eqOn.load());
    for (int b = 0; b < omx::kEqBands; ++b)
    {
        const juce::String n (b + 1);
        e->setAttribute ("eq" + n + "Db", fx.eqDb[(size_t) b].load());
        e->setAttribute ("eq" + n + "Hz", fx.eqHz[(size_t) b].load());
        e->setAttribute ("eq" + n + "Q", fx.eqQ[(size_t) b].load());
    }
    e->setAttribute ("compOn", fx.compOn.load());
    e->setAttribute ("compThreshold", fx.compThresholdDb.load());
    e->setAttribute ("compRatio", fx.compRatio.load());
    e->setAttribute ("compKnee", fx.compKneeDb.load());
    e->setAttribute ("compAttack", fx.compAttackMs.load());
    e->setAttribute ("compRelease", fx.compReleaseMs.load());
    e->setAttribute ("deessOn", fx.deessOn.load());
    e->setAttribute ("deessThreshold", fx.deessThresholdDb.load());
    e->setAttribute ("deessMax", fx.deessMaxDb.load());
    e->setAttribute ("deessFreq", fx.deessFreqHz.load());
    e->setAttribute ("duckOn", fx.duckOn.load());
    e->setAttribute ("duckSource", fx.duckSource.load());
    e->setAttribute ("duckDepth", fx.duckDepthDb.load());
    e->setAttribute ("duckThreshold", fx.duckThresholdDb.load());
}

// EQ bands from a <Processing> element, clamped to each band's range. Files from the
// 3-band EQ (eqLow / eqMid / eqMidHz / eqHigh) land in the bands that sound the same.
static void readEqXml (const juce::XmlElement& e, float* db, float* hz, float* q)
{
    for (int b = 0; b < omx::kEqBands; ++b)
    {
        const juce::String n (b + 1);
        float legacyDb = 0.0f, legacyHz = omx::kEqDefaultHz[(size_t) b];
        if (b == omx::kEqLegacyLow)  legacyDb = (float) e.getDoubleAttribute ("eqLow", 0.0);
        if (b == omx::kEqLegacyMid)  { legacyDb = (float) e.getDoubleAttribute ("eqMid", 0.0); legacyHz = (float) e.getDoubleAttribute ("eqMidHz", legacyHz); }
        if (b == omx::kEqLegacyHigh) legacyDb = (float) e.getDoubleAttribute ("eqHigh", 0.0);

        db[b] = juce::jlimit (-omx::kEqMaxDb, omx::kEqMaxDb, (float) e.getDoubleAttribute ("eq" + n + "Db", legacyDb));
        hz[b] = juce::jlimit (omx::eqMinHz (b), omx::eqMaxHz (b), (float) e.getDoubleAttribute ("eq" + n + "Hz", legacyHz));
        q[b]  = juce::jlimit (omx::kEqMinQ, omx::kEqMaxQ, (float) e.getDoubleAttribute ("eq" + n + "Q", omx::kEqDefaultQ));
    }
}

static void loadProcessing (const juce::XmlElement* e, ProcessingParams& fx)
{
    if (e == nullptr)
        return;

    auto f = [e] (const char* name, double def) { return (float) e->getDoubleAttribute (name, def); };
    fx.denoiseOn.store (e->getBoolAttribute ("denoiseOn"));
    fx.denoiseAmount.store (f ("denoiseAmount", 1.0));
    fx.denoiseModel.store (juce::jlimit (0, 1, e->getIntAttribute ("denoiseModel", 0)));
    fx.gateOn.store (e->getBoolAttribute ("gateOn"));
    fx.gateThresholdDb.store (f ("gateThreshold", -50.0));
    fx.gateRangeDb.store (f ("gateRange", -40.0));
    fx.gateAttackMs.store (juce::jlimit (0.1f, 100.0f, f ("gateAttack", 1.0f)));
    fx.gateHoldMs.store (juce::jlimit (0.0f, 500.0f, f ("gateHold", 80.0f)));
    fx.gateReleaseMs.store (juce::jlimit (10.0f, 1000.0f, f ("gateRelease", 120.0f)));
    fx.hpfOn.store (e->getBoolAttribute ("hpfOn"));
    fx.hpfHz.store (f ("hpfHz", 80.0));
    fx.lpfOn.store (e->getBoolAttribute ("lpfOn"));
    fx.lpfHz.store (f ("lpfHz", 18000.0));
    fx.eqOn.store (e->getBoolAttribute ("eqOn"));
    {
        float db[omx::kEqBands], hz[omx::kEqBands], q[omx::kEqBands];
        readEqXml (*e, db, hz, q);
        for (size_t b = 0; b < (size_t) omx::kEqBands; ++b)
        {
            fx.eqDb[b].store (db[b]); fx.eqHz[b].store (hz[b]); fx.eqQ[b].store (q[b]);
        }
    }
    fx.compOn.store (e->getBoolAttribute ("compOn"));
    fx.compThresholdDb.store (f ("compThreshold", -18.0));
    fx.compRatio.store (f ("compRatio", 3.0));
    fx.compKneeDb.store (juce::jlimit (0.0f, 18.0f, f ("compKnee", 6.0f)));
    fx.compAttackMs.store (juce::jlimit (0.1f, 100.0f, f ("compAttack", 5.0f)));
    fx.compReleaseMs.store (juce::jlimit (10.0f, 1000.0f, f ("compRelease", 120.0f)));
    fx.deessOn.store (e->getBoolAttribute ("deessOn"));
    fx.deessThresholdDb.store (f ("deessThreshold", -30.0));
    fx.deessMaxDb.store (f ("deessMax", 10.0));
    fx.deessFreqHz.store (juce::jlimit (2500.0f, 12000.0f, f ("deessFreq", 6500.0f)));
    fx.duckOn.store (e->getBoolAttribute ("duckOn"));
    fx.duckSource.store (e->getIntAttribute ("duckSource", -1));
    fx.duckDepthDb.store (f ("duckDepth", -12.0));
    fx.duckThresholdDb.store (f ("duckThreshold", -40.0));
}

// Plugin state copies for Session::save (message thread only).
struct CachedPluginState
{
    juce::String plugin;   // which plugin it came from: a new plugin may get a freed one's address
    juce::String base64;
    juce::uint32 takenMs = 0;
};
static std::map<const juce::AudioPluginInstance*, CachedPluginState> stateCache;
static std::set<const juce::AudioPluginInstance*> stateSeen;
static bool reuseStateThisSave = false;

static juce::String pluginState (juce::AudioPluginInstance& plugin)
{
    const auto id = plugin.getPluginDescription().createIdentifierString();
    const auto now = juce::Time::getMillisecondCounter();
    auto& cached = stateCache[&plugin];
    stateSeen.insert (&plugin);

    if (! reuseStateThisSave || cached.plugin != id || cached.takenMs == 0 || now - cached.takenMs > 60000)
    {
        juce::MemoryBlock state;
        plugin.getStateInformation (state);
        cached = { id, state.toBase64Encoding(), now };
    }
    return cached.base64;
}

static void saveChain (juce::XmlElement& x, PluginChain& chain)
{
    for (int slot = 0; slot < kNumSlots; ++slot)
    {
        auto* plugin = chain.getPlugin (slot);
        if (plugin == nullptr)
            continue;

        auto* xp = x.createNewChildElement ("Slot");
        xp->setAttribute ("index", slot);
        xp->setAttribute ("bypass", chain.slotBypass[(size_t) slot].load());
        xp->setAttribute ("mix", chain.slotMix[(size_t) slot].load());
        xp->addChildElement (plugin->getPluginDescription().createXml().release());

        xp->setAttribute ("state", pluginState (*plugin));
    }
}

static void loadChain (const juce::XmlElement& x, PluginChain& chain, MixerEngine& engine)
{
    std::vector<const juce::XmlElement*> saved;
    for (auto* xp : x.getChildWithTagNameIterator ("Slot"))
        saved.push_back (xp);
    std::sort (saved.begin(), saved.end(), [] (const juce::XmlElement* a, const juce::XmlElement* b)
    {
        return a->getIntAttribute ("index", -1) < b->getIntAttribute ("index", -1);
    });

    for (const auto* xp : saved)
    {
        const int slot = xp->getIntAttribute ("index", -1);
        if (! juce::isPositiveAndBelow (slot, kNumSlots))
            continue;

        auto* xd = xp->getChildByName ("PLUGIN");
        if (xd == nullptr)
            continue;

        juce::PluginDescription desc;
        if (! desc.loadFromXml (*xd))
            continue;

        juce::String pluginError;
        if (! chain.loadPlugin (slot, desc, engine.getFormatManager(), pluginError))
        {
            juce::Logger::writeToLog ("Could not restore " + desc.name + ": " + pluginError);
            continue;
        }

        // Slots are packed, so the plugin may sit lower than its saved index.
        const int installed = chain.getNumPlugins() - 1;
        chain.slotBypass[(size_t) installed].store (xp->getBoolAttribute ("bypass", false));
        chain.slotMix[(size_t) installed].store ((float) xp->getDoubleAttribute ("mix", 1.0));

        juce::MemoryBlock state;
        if (state.fromBase64Encoding (xp->getStringAttribute ("state")) && state.getSize() > 0)
            chain.getPlugin (installed)->setStateInformation (state.getData(), (int) state.getSize());
    }

    chain.synchronizeSandbox();
}

//==============================================================================
// Named strip presets (MixerEngine::stripPresets). Stored under <StripPresets> in
// the session file. A preset carries the strip's sound only (see StripLogic.h):
// inserts are stored by plugin identifier, never with state blobs, so a preset
// whose plugin is missing still loads -- the slot is skipped on apply.

static void saveStripPresetXml (juce::XmlElement& parent, const juce::String& name, const omx::StripPreset& p)
{
    auto* e = parent.createNewChildElement ("Preset");
    e->setAttribute ("name", name);
    e->setAttribute ("gain", p.gainDb);
    e->setAttribute ("fader", p.faderDb);
    e->setAttribute ("mono", p.mono);
    e->setAttribute ("invertPolarity", p.invertPolarity);
    e->setAttribute ("soloSafe", p.soloSafe);
    e->setAttribute ("padX", p.padX);
    e->setAttribute ("padY", p.padY);
    e->setAttribute ("padMode", p.padMode);
    for (int m = 0; m < VoicePadParams::numModes; ++m)
    {
        e->setAttribute ("padModeX" + juce::String (m), p.padModeX[(size_t) m]);
        e->setAttribute ("padModeY" + juce::String (m), p.padModeY[(size_t) m]);
    }
    for (int b = 0; b < kNumBuses; ++b)
    {
        const juce::String bus (kBusNames[b]);
        e->setAttribute ("bus" + bus, p.busOn[(size_t) b]);
        e->setAttribute ("send" + bus, p.sendDb[(size_t) b]);
        e->setAttribute ("pre" + bus, p.sendPre[(size_t) b]);
    }

    auto* fx = e->createNewChildElement ("Processing");
    fx->setAttribute ("denoiseOn", p.denoiseOn);
    fx->setAttribute ("denoiseAmount", p.denoiseAmount);
    fx->setAttribute ("denoiseModel", p.denoiseModel);
    fx->setAttribute ("gateOn", p.gateOn);
    fx->setAttribute ("gateThreshold", p.gateThresholdDb);
    fx->setAttribute ("gateRange", p.gateRangeDb);
    fx->setAttribute ("gateAttack", p.gateAttackMs);
    fx->setAttribute ("gateHold", p.gateHoldMs);
    fx->setAttribute ("gateRelease", p.gateReleaseMs);
    fx->setAttribute ("hpfOn", p.hpfOn);
    fx->setAttribute ("hpfHz", p.hpfHz);
    fx->setAttribute ("lpfOn", p.lpfOn);
    fx->setAttribute ("lpfHz", p.lpfHz);
    fx->setAttribute ("eqOn", p.eqOn);
    for (int b = 0; b < omx::kEqBands; ++b)
    {
        const juce::String n (b + 1);
        fx->setAttribute ("eq" + n + "Db", p.eqDb[(size_t) b]);
        fx->setAttribute ("eq" + n + "Hz", p.eqHz[(size_t) b]);
        fx->setAttribute ("eq" + n + "Q", p.eqQ[(size_t) b]);
    }
    fx->setAttribute ("compOn", p.compOn);
    fx->setAttribute ("compThreshold", p.compThresholdDb);
    fx->setAttribute ("compRatio", p.compRatio);
    fx->setAttribute ("compKnee", p.compKneeDb);
    fx->setAttribute ("compAttack", p.compAttackMs);
    fx->setAttribute ("compRelease", p.compReleaseMs);
    fx->setAttribute ("deessOn", p.deessOn);
    fx->setAttribute ("deessThreshold", p.deessThresholdDb);
    fx->setAttribute ("deessMax", p.deessMaxDb);
    fx->setAttribute ("deessFreq", p.deessFreqHz);
    fx->setAttribute ("duckOn", p.duckOn);
    fx->setAttribute ("duckSource", p.duckSource);
    fx->setAttribute ("duckDepth", p.duckDepthDb);
    fx->setAttribute ("duckThreshold", p.duckThresholdDb);

    for (int s = 0; s < p.numSlots; ++s)
    {
        const auto& slot = p.slots[(size_t) s];
        auto* xs = e->createNewChildElement ("Slot");
        xs->setAttribute ("index", s);
        xs->setAttribute ("identifier", slot.identifier);
        xs->setAttribute ("name", slot.name);
        xs->setAttribute ("format", slot.format);
        xs->setAttribute ("bypass", slot.bypass);
        xs->setAttribute ("mix", slot.mix);
    }
}

static bool loadStripPresetXml (const juce::XmlElement& e, juce::String& name, omx::StripPreset& p)
{
    name = e.getStringAttribute ("name");
    if (name.isEmpty())
        return false;

    p = omx::StripPreset();   // unknown fields keep their defaults; old files stay loadable
    auto f = [&e] (const juce::String& attr, double def) { return (float) e.getDoubleAttribute (attr, def); };
    p.gainDb = f ("gain", 0.0);
    p.faderDb = f ("fader", 0.0);
    p.mono = e.getBoolAttribute ("mono");
    p.invertPolarity = e.getBoolAttribute ("invertPolarity");
    p.soloSafe = e.getBoolAttribute ("soloSafe");
    p.padX = f ("padX", 0.0);
    p.padY = f ("padY", -1.0);
    p.padMode = e.getIntAttribute ("padMode", 0);
    for (int m = 0; m < VoicePadParams::numModes; ++m)
    {
        const float defX = (m == p.padMode) ? p.padX : 0.0f;
        const float defY = (m == p.padMode) ? p.padY : -1.0f;
        p.padModeX[(size_t) m] = f ("padModeX" + juce::String (m), defX);
        p.padModeY[(size_t) m] = f ("padModeY" + juce::String (m), defY);
    }
    for (int b = 0; b < kNumBuses; ++b)
    {
        const juce::String bus (kBusNames[b]);
        p.busOn[(size_t) b] = e.getBoolAttribute ("bus" + bus, b == 0);
        p.sendDb[(size_t) b] = f ("send" + bus, 0.0);
        p.sendPre[(size_t) b] = e.getBoolAttribute ("pre" + bus, false);
    }

    if (auto* fx = e.getChildByName ("Processing"))
    {
        p.denoiseOn = fx->getBoolAttribute ("denoiseOn");
        p.denoiseAmount = (float) fx->getDoubleAttribute ("denoiseAmount", 1.0);
        p.denoiseModel = juce::jlimit (0, 1, fx->getIntAttribute ("denoiseModel", 0));
        p.gateOn = fx->getBoolAttribute ("gateOn");
        p.gateThresholdDb = (float) fx->getDoubleAttribute ("gateThreshold", -50.0);
        p.gateRangeDb = (float) fx->getDoubleAttribute ("gateRange", -40.0);
        p.gateAttackMs = juce::jlimit (0.1f, 100.0f, (float) fx->getDoubleAttribute ("gateAttack", 1.0));
        p.gateHoldMs = juce::jlimit (0.0f, 500.0f, (float) fx->getDoubleAttribute ("gateHold", 80.0));
        p.gateReleaseMs = juce::jlimit (10.0f, 1000.0f, (float) fx->getDoubleAttribute ("gateRelease", 120.0));
        p.hpfOn = fx->getBoolAttribute ("hpfOn");
        p.hpfHz = (float) fx->getDoubleAttribute ("hpfHz", 80.0);
        p.lpfOn = fx->getBoolAttribute ("lpfOn");
        p.lpfHz = (float) fx->getDoubleAttribute ("lpfHz", 18000.0);
        p.eqOn = fx->getBoolAttribute ("eqOn");
        readEqXml (*fx, p.eqDb.data(), p.eqHz.data(), p.eqQ.data());
        p.compOn = fx->getBoolAttribute ("compOn");
        p.compThresholdDb = (float) fx->getDoubleAttribute ("compThreshold", -18.0);
        p.compRatio = (float) fx->getDoubleAttribute ("compRatio", 3.0);
        p.compKneeDb = juce::jlimit (0.0f, 18.0f, (float) fx->getDoubleAttribute ("compKnee", 6.0));
        p.compAttackMs = juce::jlimit (0.1f, 100.0f, (float) fx->getDoubleAttribute ("compAttack", 5.0));
        p.compReleaseMs = juce::jlimit (10.0f, 1000.0f, (float) fx->getDoubleAttribute ("compRelease", 120.0));
        p.deessOn = fx->getBoolAttribute ("deessOn");
        p.deessThresholdDb = (float) fx->getDoubleAttribute ("deessThreshold", -30.0);
        p.deessMaxDb = (float) fx->getDoubleAttribute ("deessMax", 10.0);
        p.deessFreqHz = juce::jlimit (2500.0f, 12000.0f, (float) fx->getDoubleAttribute ("deessFreq", 6500.0));
        p.duckOn = fx->getBoolAttribute ("duckOn");
        p.duckSource = fx->getIntAttribute ("duckSource", -1);
        p.duckDepthDb = (float) fx->getDoubleAttribute ("duckDepth", -12.0);
        p.duckThresholdDb = (float) fx->getDoubleAttribute ("duckThreshold", -40.0);
    }

    p.numSlots = 0;
    for (auto* xs : e.getChildWithTagNameIterator ("Slot"))
    {
        const int s = xs->getIntAttribute ("index", -1);
        if (! juce::isPositiveAndBelow (s, kNumSlots) || s >= omx::kPresetNumSlots)
            continue;
        auto& slot = p.slots[(size_t) s];
        slot.identifier = xs->getStringAttribute ("identifier").toStdString();
        slot.name = xs->getStringAttribute ("name").toStdString();
        slot.format = xs->getStringAttribute ("format").toStdString();
        slot.bypass = xs->getBoolAttribute ("bypass", false);
        slot.mix = (float) xs->getDoubleAttribute ("mix", 1.0);
        p.numSlots = std::max (p.numSlots, s + 1);
    }
    return true;
}

//==============================================================================
// Scenes: the mix only (levels, mutes, sends, processing, plugin bypass and wet/dry, the pad,
// bus processing, soundboard level and buses). Sources, devices and plugins are left alone, so
// recalling one is instant; every value is smoothed on the audio thread, so it doesn't click.

static void saveSlots (juce::XmlElement& x, PluginChain& chain)
{
    for (int s = 0; s < kNumSlots; ++s)
    {
        auto* e = x.createNewChildElement ("SlotMix");
        e->setAttribute ("index", s);
        e->setAttribute ("bypass", chain.slotBypass[(size_t) s].load());
        e->setAttribute ("mix", chain.slotMix[(size_t) s].load());
    }
}

static void loadSlots (const juce::XmlElement& x, PluginChain& chain)
{
    for (auto* e : x.getChildWithTagNameIterator ("SlotMix"))
    {
        const int s = e->getIntAttribute ("index", -1);
        if (! juce::isPositiveAndBelow (s, kNumSlots) || chain.getPlugin (s) == nullptr)
            continue;
        chain.slotFault[(size_t) s].store (false);
        chain.slotBypass[(size_t) s].store (e->getBoolAttribute ("bypass"));
        chain.slotMix[(size_t) s].store ((float) e->getDoubleAttribute ("mix", 1.0));
    }
}

// Bus-to-bus feeds, saved on the source bus as feed<A1..B3> and feedDb<A1..B3>.
static void writeFeedsXml (juce::XmlElement& x, const Bus& bus)
{
    for (int d = 0; d < kNumBuses; ++d)
    {
        const juce::String name (kBusNames[d]);
        x.setAttribute ("feed" + name, bus.feedOn[(size_t) d].load());
        x.setAttribute ("feedDb" + name, bus.feedDb[(size_t) d].load());
    }
}

// Runs after all buses are read: every saved feed is turned off first, then the "on" ones go
// back in through setBusFeed, so a hand-edited file can't make a loop. Elements without feed
// attributes (older files and scenes) leave that bus's feeds as they are.
static void readFeedsXml (const std::vector<std::pair<const juce::XmlElement*, int>>& items, MixerEngine& engine)
{
    for (int pass = 0; pass < 2; ++pass)
        for (const auto& [x, b] : items)
            for (int d = 0; d < kNumBuses; ++d)
            {
                const juce::String name (kBusNames[d]);
                if (! x->hasAttribute ("feed" + name))
                    continue;
                if (pass == 0)
                {
                    engine.setBusFeed (b, d, false);
                    engine.getBus (b).feedDb[(size_t) d].store (
                        juce::jlimit (-60.0f, 12.0f, (float) x->getDoubleAttribute ("feedDb" + name, 0.0)));
                }
                else if (x->getBoolAttribute ("feed" + name))
                {
                    engine.setBusFeed (b, d, true);
                }
            }
}

// A strip's own settings: name, colour, levels, voice pad, sends, processing and plugins with
// their state. Not its source or mute (the session saves those itself; copy/paste leaves them).
static void writeStripSettings (juce::XmlElement& xs, ChannelStrip& strip)
{
    xs.setAttribute ("name", strip.name);
    xs.setAttribute ("colour", strip.colour);
    xs.setAttribute ("icon", strip.icon);
    xs.setAttribute ("gain", strip.gainDb.load());
    xs.setAttribute ("fader", strip.faderDb.load());
    xs.setAttribute ("soloSafe", strip.soloSafe.load());
    xs.setAttribute ("mono", strip.mono.load());
    xs.setAttribute ("invertPolarity", strip.invertPolarity.load());
    writePadXml (xs, strip.pad);

    for (int b = 0; b < kNumBuses; ++b)
    {
        const juce::String bus (kBusNames[b]);
        xs.setAttribute ("bus" + bus, strip.busOn[b].load());
        xs.setAttribute ("send" + bus, strip.sendDb[b].load());
        xs.setAttribute ("pre" + bus, strip.sendPre[b].load());
    }

    saveProcessing (xs, strip.fx);
    saveChain (xs, strip.inserts);
}

static void readStripSettings (const juce::XmlElement& xs, ChannelStrip& strip, MixerEngine& engine, bool loadPlugins)
{
    strip.name = xs.getStringAttribute ("name");
    strip.colour = xs.getIntAttribute ("colour", 0);
    strip.icon = juce::jlimit (0, 9, xs.getIntAttribute ("icon", 0));
    strip.gainDb.store ((float) xs.getDoubleAttribute ("gain", 0.0));
    strip.faderDb.store ((float) xs.getDoubleAttribute ("fader", 0.0));
    strip.soloSafe.store (xs.getBoolAttribute ("soloSafe", false));
    strip.mono.store (xs.getBoolAttribute ("mono", false));
    strip.invertPolarity.store (xs.getBoolAttribute ("invertPolarity", false));
    readPadXml (xs, strip.pad);

    for (int b = 0; b < kNumBuses; ++b)
    {
        const juce::String bus (kBusNames[b]);
        strip.busOn[b].store (xs.getBoolAttribute ("bus" + bus, b == 0));
        strip.sendDb[b].store ((float) xs.getDoubleAttribute ("send" + bus, 0.0));
        strip.sendPre[b].store (xs.getBoolAttribute ("pre" + bus, false));
    }

    loadProcessing (xs.getChildByName ("Processing"), strip.fx);
    if (loadPlugins)
        loadChain (xs, strip.inserts, engine);
}

std::unique_ptr<juce::XmlElement> Session::copyStrip (MixerEngine& engine, int s)
{
    if (! juce::isPositiveAndBelow (s, kNumStrips))
        return {};
    reuseStateThisSave = false;   // the plugins' state now, not the autosave's copy
    auto xs = std::make_unique<juce::XmlElement> ("Strip");
    writeStripSettings (*xs, engine.getStrip (s));
    return xs;
}

void Session::pasteStrip (const juce::XmlElement& xs, MixerEngine& engine, int s)
{
    if (! juce::isPositiveAndBelow (s, kNumStrips))
        return;
    auto& strip = engine.getStrip (s);
    strip.inserts.removeAllPlugins();
    readStripSettings (xs, strip, engine, true);

    // Copied from a strip that ducked under this one: a strip can't duck under itself.
    if (strip.fx.duckSource.load() == s)
    {
        strip.fx.duckSource.store (-1);
        strip.fx.duckOn.store (false);
    }
}

std::unique_ptr<juce::XmlElement> Session::captureScene (MixerEngine& engine)
{
    auto root = std::make_unique<juce::XmlElement> ("Mix");

    for (int s = 0; s < kNumStrips; ++s)
    {
        auto& strip = engine.getStrip (s);
        auto* x = root->createNewChildElement ("Strip");
        x->setAttribute ("index", s);
        x->setAttribute ("gain", strip.gainDb.load());
        x->setAttribute ("fader", strip.faderDb.load());
        x->setAttribute ("mute", strip.mute.load());
        x->setAttribute ("solo", strip.solo.load());
        x->setAttribute ("soloSafe", strip.soloSafe.load());
        x->setAttribute ("mono", strip.mono.load());
        x->setAttribute ("invertPolarity", strip.invertPolarity.load());
        writePadXml (*x, strip.pad);
        for (int b = 0; b < kNumBuses; ++b)
        {
            const juce::String bus (kBusNames[b]);
            x->setAttribute ("bus" + bus, strip.busOn[b].load());
            x->setAttribute ("send" + bus, strip.sendDb[b].load());
            x->setAttribute ("pre" + bus, strip.sendPre[b].load());
        }
        saveProcessing (*x, strip.fx);
        saveSlots (*x, strip.inserts);
    }

    for (int b = 0; b < kNumBuses; ++b)
    {
        auto& bus = engine.getBus (b);
        auto* x = root->createNewChildElement ("Bus");
        x->setAttribute ("index", b);
        x->setAttribute ("fader", bus.faderDb.load());
        x->setAttribute ("mute", bus.mute.load());
        x->setAttribute ("normOn", bus.normOn.load());
        x->setAttribute ("normTarget", bus.normTargetLufs.load());
        writeFeedsXml (*x, bus);
        saveProcessing (*x, bus.fx);
        saveSlots (*x, bus.inserts);
    }

    auto& board = engine.getSoundboard();
    auto* xsb = root->createNewChildElement ("Soundboard");
    xsb->setAttribute ("volume", board.volumeDb.load());
    for (int b = 0; b < kNumBuses; ++b)
        xsb->setAttribute (juce::String ("bus") + kBusNames[b], board.busOn[b].load());

    return root;
}

void Session::applyScene (const juce::XmlElement& root, MixerEngine& engine)
{
    for (auto* x : root.getChildWithTagNameIterator ("Strip"))
    {
        const int s = x->getIntAttribute ("index", -1);
        if (! juce::isPositiveAndBelow (s, kNumStrips))
            continue;
        auto& strip = engine.getStrip (s);
        strip.gainDb.store ((float) x->getDoubleAttribute ("gain", strip.gainDb.load()));
        strip.faderDb.store ((float) x->getDoubleAttribute ("fader", strip.faderDb.load()));
        strip.mute.store (x->getBoolAttribute ("mute", strip.mute.load()));
        strip.solo.store (x->getBoolAttribute ("solo", false));
        strip.soloSafe.store (x->getBoolAttribute ("soloSafe", strip.soloSafe.load()));
        strip.mono.store (x->getBoolAttribute ("mono", strip.mono.load()));
        strip.invertPolarity.store (x->getBoolAttribute ("invertPolarity", strip.invertPolarity.load()));
        readPadXml (*x, strip.pad);
        for (int b = 0; b < kNumBuses; ++b)
        {
            const juce::String bus (kBusNames[b]);
            strip.busOn[b].store (x->getBoolAttribute ("bus" + bus, strip.busOn[b].load()));
            strip.sendDb[b].store ((float) x->getDoubleAttribute ("send" + bus, strip.sendDb[b].load()));
            strip.sendPre[b].store (x->getBoolAttribute ("pre" + bus, strip.sendPre[b].load()));
        }
        loadProcessing (x->getChildByName ("Processing"), strip.fx);
        loadSlots (*x, strip.inserts);
    }

    std::vector<std::pair<const juce::XmlElement*, int>> feeds;
    for (auto* x : root.getChildWithTagNameIterator ("Bus"))
    {
        const int b = x->getIntAttribute ("index", -1);
        if (! juce::isPositiveAndBelow (b, kNumBuses))
            continue;
        feeds.emplace_back (x, b);
        auto& bus = engine.getBus (b);
        bus.faderDb.store ((float) x->getDoubleAttribute ("fader", bus.faderDb.load()));
        bus.mute.store (x->getBoolAttribute ("mute", bus.mute.load()));
        bus.normOn.store (x->getBoolAttribute ("normOn", bus.normOn.load()));
        bus.normTargetLufs.store ((float) x->getDoubleAttribute ("normTarget", bus.normTargetLufs.load()));
        loadProcessing (x->getChildByName ("Processing"), bus.fx);
        loadSlots (*x, bus.inserts);
    }
    readFeedsXml (feeds, engine);

    if (auto* xsb = root.getChildByName ("Soundboard"))
    {
        auto& board = engine.getSoundboard();
        board.volumeDb.store ((float) xsb->getDoubleAttribute ("volume", board.volumeDb.load()));
        for (int b = 0; b < kNumBuses; ++b)
            board.busOn[b].store (xsb->getBoolAttribute (juce::String ("bus") + kBusNames[b], board.busOn[b].load()));
    }

    engine.externalChanges.fetch_add (1);   // the UI refreshes what it shows
}

void Session::save (MixerEngine& engine, bool reusePluginState)
{
    reuseStateThisSave = reusePluginState;
    stateSeen.clear();

    juce::XmlElement root ("OpenMixSession");
    root.setAttribute ("version", sessionVersion);
    root.setAttribute ("theme", Theme::isDark() ? "dark" : "light");
    root.setAttribute ("soloPfl", engine.soloPreFader.load());

    if (auto device = engine.getDeviceManager().createStateXml())
        root.addChildElement (device.release());

    for (int s = 0; s < kNumStrips; ++s)
    {
        auto& strip = engine.getStrip (s);
        if (! strip.active.load())
            continue;

        auto* xs = root.createNewChildElement ("Strip");
        xs->setAttribute ("index", s);
        xs->setAttribute ("active", true);
        xs->setAttribute ("input", strip.inputChannel.load());
        xs->setAttribute ("inputRight", strip.inputChannelRight.load());
        xs->setAttribute ("sourceType", strip.sourceType.load());
        xs->setAttribute ("sourceDevice", strip.sourceDeviceName);
        xs->setAttribute ("sourceApp", strip.sourceAppExe);
        xs->setAttribute ("loopbackId", strip.loopbackId);
        xs->setAttribute ("loopbackName", strip.loopbackName);
        xs->setAttribute ("appOutputId", strip.appOutputDeviceId);
        xs->setAttribute ("appOutputName", strip.appOutputDeviceName);
        xs->setAttribute ("mute", strip.mute.load());
        writeStripSettings (*xs, strip);
    }

    for (int b = 0; b < kNumBuses; ++b)
    {
        auto& bus = engine.getBus (b);
        auto* xb = root.createNewChildElement ("Bus");
        xb->setAttribute ("name", kBusNames[b]);
        xb->setAttribute ("customName", bus.customName);   // "" = default name; group stays slot-based
        xb->setAttribute ("borrowed", bus.borrowed);        // a B bus in a free hardware slot
        xb->setAttribute ("active", bus.active.load());
        xb->setAttribute ("monitor", bus.monitor.load());
        xb->setAttribute ("fader", bus.faderDb.load());
        xb->setAttribute ("mute", bus.mute.load());
        xb->setAttribute ("normOn", bus.normOn.load());
        xb->setAttribute ("normTarget", bus.normTargetLufs.load());
        xb->setAttribute ("output", bus.outputPair.load());
        xb->setAttribute ("target", bus.target.load());
        xb->setAttribute ("device", bus.deviceName);
        writeFeedsXml (*xb, bus);
        saveProcessing (*xb, bus.fx);
        saveChain (*xb, bus.inserts);
    }

    {
        auto& board = engine.getSoundboard();
        auto* xsb = root.createNewChildElement ("Soundboard");
        xsb->setAttribute ("volume", board.volumeDb.load());
        for (int b = 0; b < kNumBuses; ++b)
            xsb->setAttribute (juce::String ("bus") + kBusNames[b], board.busOn[b].load());
        for (int p = 0; p < Soundboard::numPads; ++p)
        {
            const auto info = board.getPadInfo (p);
            if (info.file.isEmpty())
                continue;
            auto* xp = xsb->createNewChildElement ("Pad");
            xp->setAttribute ("index", p);
            xp->setAttribute ("file", info.file);
            xp->setAttribute ("name", info.name);
            xp->setAttribute ("volume", info.volumeDb);
            xp->setAttribute ("mode", info.mode);
            xp->setAttribute ("colour", info.colour);
        }
    }

    {
        auto& player = engine.getMediaPlayer();
        auto* xm = root.createNewChildElement ("MediaPlayer");
        xm->setAttribute ("current", player.getCurrentIndex());
        xm->setAttribute ("repeat", (int) player.repeat);
        xm->setAttribute ("shuffle", player.shuffle);
        for (auto& f : player.getPlaylist())
            xm->createNewChildElement ("Track")->setAttribute ("file", f.getFullPathName());
    }

    if (! engine.scenes.empty())
    {
        auto* xs = root.createNewChildElement ("Scenes");
        xs->setAttribute ("current", engine.currentScene);
        for (const auto& scene : engine.scenes)
        {
            auto* e = xs->createNewChildElement ("Scene");
            e->setAttribute ("name", scene.name);
            e->addChildElement (new juce::XmlElement (*scene.state));
        }
    }

    if (! engine.stripPresets.empty())
    {
        auto* xs = root.createNewChildElement ("StripPresets");
        for (const auto& preset : engine.stripPresets)
            saveStripPresetXml (*xs, preset.name, preset.preset);
    }

    // Forget copies of plugins that are gone.
    for (auto it = stateCache.begin(); it != stateCache.end();)
        it = stateSeen.count (it->first) != 0 ? std::next (it) : stateCache.erase (it);

    // Only touch the disk when something actually changed (this runs every few seconds).
    static juce::String lastWritten;
    const auto text = root.toString();
    const auto file = getFile();
    if (text == lastWritten && file.existsAsFile())
        return;

    file.getParentDirectory().createDirectory();
    if (file.replaceWithText (text))
        lastWritten = text;
}

static int busIndexFor (const juce::XmlElement& xb, int version)
{
    if (xb.hasAttribute ("name"))
    {
        for (int b = 0; b < kNumBuses; ++b)
            if (xb.getStringAttribute ("name") == kBusNames[b])
                return b;
        return -1;
    }

    // Version 1 saved buses by position: 0 = A1, 1 = A2, 2 = B1.
    const int old = xb.getIntAttribute ("index", -1);
    if (version < 2)
        return old == 0 ? 0 : old == 1 ? 1 : old == 2 ? 3 : -1;
    return old;
}

void Session::apply (const juce::XmlElement& root, MixerEngine& engine, bool loadPlugins)
{
    const int version = root.getIntAttribute ("version", 1);
    engine.soloPreFader.store (root.getBoolAttribute ("soloPfl", false));

    // Only strips present in the file are active (version 1 files list exactly the 3 defaults).
    for (int s = 0; s < kNumStrips; ++s)
        engine.getStrip (s).active.store (false);

    for (auto* xs : root.getChildWithTagNameIterator ("Strip"))
    {
        const int s = xs->getIntAttribute ("index", -1);
        if (! juce::isPositiveAndBelow (s, kNumStrips))
            continue;

        auto& strip = engine.getStrip (s);
        strip.active.store (xs->getBoolAttribute ("active", true));

        // App routing first, so setStripSourceApp applies it once the app is captured.
        strip.appOutputDeviceId = xs->getStringAttribute ("appOutputId");
        strip.appOutputDeviceName = xs->getStringAttribute ("appOutputName");
        strip.sourceAppExe = xs->getStringAttribute ("sourceApp");

        juce::String error;
        switch ((SourceType) xs->getIntAttribute ("sourceType", (int) SourceType::Main))
        {
            case SourceType::None:   engine.setStripSourceNone (s); break;
            case SourceType::Main:   engine.setStripSourceMain (s, xs->getIntAttribute ("input", -1), xs->getIntAttribute ("inputRight", -1)); break;
            case SourceType::Device: engine.setStripSourceDevice (s, xs->getStringAttribute ("sourceDevice"), error); break;
            case SourceType::App:    engine.setStripSourceApp (s, xs->getStringAttribute ("sourceApp"), error); break;
            case SourceType::Media:  engine.setStripSourceMedia (s); break;
            case SourceType::Loopback:
                if (! engine.setStripSourceLoopback (s, xs->getStringAttribute ("loopbackId"), xs->getStringAttribute ("loopbackName"), error))
                    juce::Logger::writeToLog ("Loopback " + strip.loopbackName + ": " + error);   // maintainSources() retries
                break;
        }

        strip.mute.store (xs->getBoolAttribute ("mute", false));
        readStripSettings (*xs, strip, engine, loadPlugins);
    }

    bool anyMonitor = false;
    std::vector<std::pair<const juce::XmlElement*, int>> feeds;
    for (auto* xb : root.getChildWithTagNameIterator ("Bus"))
    {
        const int b = busIndexFor (*xb, version);
        if (! juce::isPositiveAndBelow (b, kNumBuses))
            continue;
        feeds.emplace_back (xb, b);

        auto& bus = engine.getBus (b);
        bus.active.store (xb->getBoolAttribute ("active", true));
        bus.customName = xb->getStringAttribute ("customName");
        bus.borrowed = bus.active.load() && xb->getBoolAttribute ("borrowed", false) && ! omx::isVirtualBusSlot (b);
        bus.monitor.store (xb->getBoolAttribute ("monitor", false));
        anyMonitor = anyMonitor || bus.monitor.load();
        bus.faderDb.store ((float) xb->getDoubleAttribute ("fader", 0.0));
        bus.mute.store (xb->getBoolAttribute ("mute", false));
        bus.normOn.store (xb->getBoolAttribute ("normOn", false));
        bus.normTargetLufs.store ((float) xb->getDoubleAttribute ("normTarget", -14.0));
        loadProcessing (xb->getChildByName ("Processing"), bus.fx);
        if (loadPlugins)
            loadChain (*xb, bus.inserts, engine);

        juce::String error;
        switch ((BusTarget) xb->getIntAttribute ("target", (int) BusTarget::MainPair))
        {
            case BusTarget::Off:      engine.setBusOff (b); break;
            case BusTarget::MainPair: engine.setBusMainPair (b, xb->getIntAttribute ("output", 0)); break;
            case BusTarget::Device:
                engine.setBusDevice (b, xb->getStringAttribute ("device"), error);   // unplugged: kept, retried
                break;
        }
    }

    readFeedsXml (feeds, engine);

    if (version < 2 || ! anyMonitor)
        engine.setMonitorBus (0);   // A1 by default

    engine.scenes.clear();
    engine.currentScene = -1;
    if (auto* xs = root.getChildByName ("Scenes"))
    {
        for (auto* e : xs->getChildWithTagNameIterator ("Scene"))
            if (auto* state = e->getChildByName ("Mix"))
                engine.scenes.push_back ({ e->getStringAttribute ("name", "Scene"), std::make_unique<juce::XmlElement> (*state) });
        engine.currentScene = juce::jlimit (-1, (int) engine.scenes.size() - 1, xs->getIntAttribute ("current", -1));
    }

    engine.stripPresets.clear();
    if (auto* xs = root.getChildByName ("StripPresets"))
    {
        for (auto* e : xs->getChildWithTagNameIterator ("Preset"))
        {
            juce::String name;
            omx::StripPreset preset;
            if (loadStripPresetXml (*e, name, preset))
                engine.stripPresets.push_back ({ name, std::move (preset) });
        }
    }

    if (auto* xsb = root.getChildByName ("Soundboard"))
    {
        auto& board = engine.getSoundboard();
        board.volumeDb.store ((float) xsb->getDoubleAttribute ("volume", 0.0));
        for (int b = 0; b < kNumBuses; ++b)
            board.busOn[b].store (xsb->getBoolAttribute (juce::String ("bus") + kBusNames[b], b == 0 || b == 3));
        for (auto* xp : xsb->getChildWithTagNameIterator ("Pad"))
        {
            const int p = xp->getIntAttribute ("index", -1);
            const juce::File file (xp->getStringAttribute ("file"));
            if (! juce::isPositiveAndBelow (p, Soundboard::numPads) || ! file.existsAsFile())
                continue;
            juce::String error;
            if (board.loadPad (p, file, error))
            {
                board.renamePad (p, xp->getStringAttribute ("name", file.getFileNameWithoutExtension()));
                board.setPadVolume (p, (float) xp->getDoubleAttribute ("volume", 0.0));
                board.setPadMode (p, xp->getIntAttribute ("mode", Soundboard::OneShot));
                board.setPadColour (p, xp->getIntAttribute ("colour", -1));
            }
            else
                juce::Logger::writeToLog ("Soundboard pad " + juce::String (p + 1) + ": " + error);
        }
    }

    // The media player's playlist comes back paused at the track it was on.
    if (auto* xm = root.getChildByName ("MediaPlayer"))
    {
        auto& player = engine.getMediaPlayer();
        player.clearPlaylist();
        juce::Array<juce::File> files;
        for (auto* xt : xm->getChildWithTagNameIterator ("Track"))
            files.add (juce::File (xt->getStringAttribute ("file")));
        player.addFiles (files);   // files that are gone are left out
        player.repeat = (MediaPlayer::Repeat) juce::jlimit (0, 2, xm->getIntAttribute ("repeat", MediaPlayer::RepeatAll));
        player.shuffle = xm->getBoolAttribute ("shuffle", false);
        juce::String error;
        player.loadIndex (juce::jlimit (0, juce::jmax (0, player.getPlaylist().size() - 1), xm->getIntAttribute ("current", 0)), error);
    }
}
