#include "app/Session.h"
#include "engine/MixerEngine.h"
#include "ui/Theme.h"

#include <algorithm>
#include <array>
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

std::unique_ptr<juce::XmlElement> Session::load()
{
    const auto file = getFile();
    return file.existsAsFile() ? juce::parseXML (file) : nullptr;
}

static void saveProcessing (juce::XmlElement& x, const ProcessingParams& fx)
{
    auto* e = x.createNewChildElement ("Processing");
    e->setAttribute ("denoiseOn", fx.denoiseOn.load());
    e->setAttribute ("denoiseAmount", fx.denoiseAmount.load());
    e->setAttribute ("gateOn", fx.gateOn.load());
    e->setAttribute ("gateThreshold", fx.gateThresholdDb.load());
    e->setAttribute ("gateRange", fx.gateRangeDb.load());
    e->setAttribute ("hpfOn", fx.hpfOn.load());
    e->setAttribute ("hpfHz", fx.hpfHz.load());
    e->setAttribute ("lpfOn", fx.lpfOn.load());
    e->setAttribute ("lpfHz", fx.lpfHz.load());
    e->setAttribute ("eqOn", fx.eqOn.load());
    e->setAttribute ("eqLow", fx.eqLowDb.load());
    e->setAttribute ("eqMid", fx.eqMidDb.load());
    e->setAttribute ("eqMidHz", fx.eqMidHz.load());
    e->setAttribute ("eqHigh", fx.eqHighDb.load());
    e->setAttribute ("compOn", fx.compOn.load());
    e->setAttribute ("compThreshold", fx.compThresholdDb.load());
    e->setAttribute ("compRatio", fx.compRatio.load());
    e->setAttribute ("deessOn", fx.deessOn.load());
    e->setAttribute ("deessThreshold", fx.deessThresholdDb.load());
    e->setAttribute ("deessMax", fx.deessMaxDb.load());
    e->setAttribute ("duckOn", fx.duckOn.load());
    e->setAttribute ("duckSource", fx.duckSource.load());
    e->setAttribute ("duckDepth", fx.duckDepthDb.load());
    e->setAttribute ("duckThreshold", fx.duckThresholdDb.load());
}

static void loadProcessing (const juce::XmlElement* e, ProcessingParams& fx)
{
    if (e == nullptr)
        return;

    auto f = [e] (const char* name, double def) { return (float) e->getDoubleAttribute (name, def); };
    fx.denoiseOn.store (e->getBoolAttribute ("denoiseOn"));
    fx.denoiseAmount.store (f ("denoiseAmount", 1.0));
    fx.gateOn.store (e->getBoolAttribute ("gateOn"));
    fx.gateThresholdDb.store (f ("gateThreshold", -50.0));
    fx.gateRangeDb.store (f ("gateRange", -40.0));
    fx.hpfOn.store (e->getBoolAttribute ("hpfOn"));
    fx.hpfHz.store (f ("hpfHz", 80.0));
    fx.lpfOn.store (e->getBoolAttribute ("lpfOn"));
    fx.lpfHz.store (f ("lpfHz", 18000.0));
    fx.eqOn.store (e->getBoolAttribute ("eqOn"));
    fx.eqLowDb.store (f ("eqLow", 0.0));
    fx.eqMidDb.store (f ("eqMid", 0.0));
    fx.eqMidHz.store (f ("eqMidHz", 1000.0));
    fx.eqHighDb.store (f ("eqHigh", 0.0));
    fx.compOn.store (e->getBoolAttribute ("compOn"));
    fx.compThresholdDb.store (f ("compThreshold", -18.0));
    fx.compRatio.store (f ("compRatio", 3.0));
    fx.deessOn.store (e->getBoolAttribute ("deessOn"));
    fx.deessThresholdDb.store (f ("deessThreshold", -30.0));
    fx.deessMaxDb.store (f ("deessMax", 10.0));
    fx.duckOn.store (e->getBoolAttribute ("duckOn"));
    fx.duckSource.store (e->getIntAttribute ("duckSource", -1));
    fx.duckDepthDb.store (f ("duckDepth", -12.0));
    fx.duckThresholdDb.store (f ("duckThreshold", -40.0));
}

static void saveChain (juce::XmlElement& x, PluginChain& chain)
{
    for (int slot = 0; slot < kNumSlots; ++slot)
    {
        // A built-in insert is stored as a <BuiltInSlot> with its type and
        // parameters (no state blob -- the parameters are the whole state).
        // A VST insert keeps the existing <Slot> format (description + blob).
        if (auto* fx = chain.getBuiltIn (slot))
        {
            auto* xf = x.createNewChildElement ("BuiltInSlot");
            xf->setAttribute ("index", slot);
            xf->setAttribute ("type", PluginChain::fxTypeDisplayName (fx->getType()));
            xf->setAttribute ("bypass", chain.slotBypass[(size_t) slot].load());
            xf->setAttribute ("mix", chain.slotMix[(size_t) slot].load());

            std::array<float, 6> params {};
            bool tempoSync = false;
            chain.getBuiltInParams (slot, params, tempoSync);
            xf->setAttribute ("tempoSync", tempoSync);
            for (int i = 0; i < 6; ++i)
                xf->setAttribute ("param" + juce::String (i), params[(size_t) i]);
            continue;
        }

        auto* plugin = chain.getPlugin (slot);
        if (plugin == nullptr)
            continue;

        auto* xp = x.createNewChildElement ("Slot");
        xp->setAttribute ("index", slot);
        xp->setAttribute ("bypass", chain.slotBypass[(size_t) slot].load());
        xp->setAttribute ("mix", chain.slotMix[(size_t) slot].load());
        xp->addChildElement (plugin->getPluginDescription().createXml().release());

        juce::MemoryBlock state;
        plugin->getStateInformation (state);
        xp->setAttribute ("state", state.toBase64Encoding());
    }
}

static void loadChain (const juce::XmlElement& x, PluginChain& chain, MixerEngine& engine)
{
    // Load in saved slot order (VST and built-in slots interleaved), so the
    // packed chain comes back in the same order it was saved in.
    struct SavedSlot { int index; const juce::XmlElement* element; bool builtIn; };
    std::vector<SavedSlot> saved;
    for (auto* xp : x.getChildWithTagNameIterator ("Slot"))
        saved.push_back ({ xp->getIntAttribute ("index", -1), xp, false });
    for (auto* xf : x.getChildWithTagNameIterator ("BuiltInSlot"))
        saved.push_back ({ xf->getIntAttribute ("index", -1), xf, true });
    std::sort (saved.begin(), saved.end(),
               [] (const SavedSlot& a, const SavedSlot& b) { return a.index < b.index; });

    for (const auto& entry : saved)
    {
        const int slot = entry.index;
        const juce::XmlElement* xp = entry.element;
        if (! juce::isPositiveAndBelow (slot, kNumSlots))
            continue;

        if (entry.builtIn)
        {
            omx::FxType type;
            if (! PluginChain::fxTypeFromName (xp->getStringAttribute ("type"), type))
                continue;

            chain.installBuiltIn (slot, PluginChain::makeBuiltInFx (type));

            // Slots are packed, so the effect may sit lower than its saved index.
            const int installed = chain.getNumInserts() - 1;
            std::array<float, 6> params {};
            for (int i = 0; i < 6; ++i)
                params[(size_t) i] = (float) xp->getDoubleAttribute ("param" + juce::String (i), 0.0);
            chain.setBuiltInParams (installed, params, xp->getBoolAttribute ("tempoSync", false));
            chain.slotBypass[(size_t) installed].store (xp->getBoolAttribute ("bypass", false));
            chain.slotMix[(size_t) installed].store ((float) xp->getDoubleAttribute ("mix", 1.0));
            continue;
        }

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
    fx->setAttribute ("gateOn", p.gateOn);
    fx->setAttribute ("gateThreshold", p.gateThresholdDb);
    fx->setAttribute ("gateRange", p.gateRangeDb);
    fx->setAttribute ("hpfOn", p.hpfOn);
    fx->setAttribute ("hpfHz", p.hpfHz);
    fx->setAttribute ("lpfOn", p.lpfOn);
    fx->setAttribute ("lpfHz", p.lpfHz);
    fx->setAttribute ("eqOn", p.eqOn);
    fx->setAttribute ("eqLow", p.eqLowDb);
    fx->setAttribute ("eqMid", p.eqMidDb);
    fx->setAttribute ("eqMidHz", p.eqMidHz);
    fx->setAttribute ("eqHigh", p.eqHighDb);
    fx->setAttribute ("compOn", p.compOn);
    fx->setAttribute ("compThreshold", p.compThresholdDb);
    fx->setAttribute ("compRatio", p.compRatio);
    fx->setAttribute ("deessOn", p.deessOn);
    fx->setAttribute ("deessThreshold", p.deessThresholdDb);
    fx->setAttribute ("deessMax", p.deessMaxDb);
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
        // Built-in slots carry their type + parameters instead of a plugin
        // identifier; they are always available, so they never go missing.
        xs->setAttribute ("builtIn", slot.isBuiltIn);
        xs->setAttribute ("fxType", slot.fxType);
        xs->setAttribute ("tempoSync", slot.fxTempoSync);
        for (int i = 0; i < 6; ++i)
            xs->setAttribute ("param" + juce::String (i), slot.fxParams[(size_t) i]);
    }

    e->setAttribute ("fxSendReverb", p.fxSend[omx::kFxReverb]);
    e->setAttribute ("fxSendDelay", p.fxSend[omx::kFxDelay]);
    e->setAttribute ("fxSendChorus", p.fxSend[omx::kFxChorus]);
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
        p.gateOn = fx->getBoolAttribute ("gateOn");
        p.gateThresholdDb = (float) fx->getDoubleAttribute ("gateThreshold", -50.0);
        p.gateRangeDb = (float) fx->getDoubleAttribute ("gateRange", -40.0);
        p.hpfOn = fx->getBoolAttribute ("hpfOn");
        p.hpfHz = (float) fx->getDoubleAttribute ("hpfHz", 80.0);
        p.lpfOn = fx->getBoolAttribute ("lpfOn");
        p.lpfHz = (float) fx->getDoubleAttribute ("lpfHz", 18000.0);
        p.eqOn = fx->getBoolAttribute ("eqOn");
        p.eqLowDb = (float) fx->getDoubleAttribute ("eqLow", 0.0);
        p.eqMidDb = (float) fx->getDoubleAttribute ("eqMid", 0.0);
        p.eqMidHz = (float) fx->getDoubleAttribute ("eqMidHz", 1000.0);
        p.eqHighDb = (float) fx->getDoubleAttribute ("eqHigh", 0.0);
        p.compOn = fx->getBoolAttribute ("compOn");
        p.compThresholdDb = (float) fx->getDoubleAttribute ("compThreshold", -18.0);
        p.compRatio = (float) fx->getDoubleAttribute ("compRatio", 3.0);
        p.deessOn = fx->getBoolAttribute ("deessOn");
        p.deessThresholdDb = (float) fx->getDoubleAttribute ("deessThreshold", -30.0);
        p.deessMaxDb = (float) fx->getDoubleAttribute ("deessMax", 10.0);
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
        slot.isBuiltIn = xs->getBoolAttribute ("builtIn", false);
        slot.fxType = xs->getStringAttribute ("fxType").toStdString();
        slot.fxTempoSync = xs->getBoolAttribute ("tempoSync", false);
        for (int i = 0; i < 6; ++i)
            slot.fxParams[(size_t) i] = (float) xs->getDoubleAttribute ("param" + juce::String (i), 0.0);
        p.numSlots = std::max (p.numSlots, s + 1);
    }
    p.fxSend[omx::kFxReverb] = (float) e.getDoubleAttribute ("fxSendReverb", 0.0);
    p.fxSend[omx::kFxDelay] = (float) e.getDoubleAttribute ("fxSendDelay", 0.0);
    p.fxSend[omx::kFxChorus] = (float) e.getDoubleAttribute ("fxSendChorus", 0.0);
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
        x->setAttribute ("padX", strip.pad.x.load());
        x->setAttribute ("padY", strip.pad.y.load());
        x->setAttribute ("padMode", strip.pad.mode.load());
        for (int b = 0; b < kNumBuses; ++b)
        {
            const juce::String bus (kBusNames[b]);
            x->setAttribute ("bus" + bus, strip.busOn[b].load());
            x->setAttribute ("send" + bus, strip.sendDb[b].load());
            x->setAttribute ("pre" + bus, strip.sendPre[b].load());
        }
        x->setAttribute ("fxSendReverb", strip.getFxSendLevel (omx::kFxReverb));
        x->setAttribute ("fxSendDelay", strip.getFxSendLevel (omx::kFxDelay));
        x->setAttribute ("fxSendChorus", strip.getFxSendLevel (omx::kFxChorus));
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
        strip.pad.x.store ((float) x->getDoubleAttribute ("padX", strip.pad.x.load()));
        strip.pad.y.store ((float) x->getDoubleAttribute ("padY", strip.pad.y.load()));
        strip.pad.mode.store (x->getIntAttribute ("padMode", strip.pad.mode.load()));
        for (int b = 0; b < kNumBuses; ++b)
        {
            const juce::String bus (kBusNames[b]);
            strip.busOn[b].store (x->getBoolAttribute ("bus" + bus, strip.busOn[b].load()));
            strip.sendDb[b].store ((float) x->getDoubleAttribute ("send" + bus, strip.sendDb[b].load()));
            strip.sendPre[b].store (x->getBoolAttribute ("pre" + bus, strip.sendPre[b].load()));
        }
        strip.setFxSendLevel (omx::kFxReverb, (float) x->getDoubleAttribute ("fxSendReverb", strip.getFxSendLevel (omx::kFxReverb)));
        strip.setFxSendLevel (omx::kFxDelay, (float) x->getDoubleAttribute ("fxSendDelay", strip.getFxSendLevel (omx::kFxDelay)));
        strip.setFxSendLevel (omx::kFxChorus, (float) x->getDoubleAttribute ("fxSendChorus", strip.getFxSendLevel (omx::kFxChorus)));
        loadProcessing (x->getChildByName ("Processing"), strip.fx);
        loadSlots (*x, strip.inserts);
    }

    for (auto* x : root.getChildWithTagNameIterator ("Bus"))
    {
        const int b = x->getIntAttribute ("index", -1);
        if (! juce::isPositiveAndBelow (b, kNumBuses))
            continue;
        auto& bus = engine.getBus (b);
        bus.faderDb.store ((float) x->getDoubleAttribute ("fader", bus.faderDb.load()));
        bus.mute.store (x->getBoolAttribute ("mute", bus.mute.load()));
        bus.normOn.store (x->getBoolAttribute ("normOn", bus.normOn.load()));
        bus.normTargetLufs.store ((float) x->getDoubleAttribute ("normTarget", bus.normTargetLufs.load()));
        loadProcessing (x->getChildByName ("Processing"), bus.fx);
        loadSlots (*x, bus.inserts);
    }

    if (auto* xsb = root.getChildByName ("Soundboard"))
    {
        auto& board = engine.getSoundboard();
        board.volumeDb.store ((float) xsb->getDoubleAttribute ("volume", board.volumeDb.load()));
        for (int b = 0; b < kNumBuses; ++b)
            board.busOn[b].store (xsb->getBoolAttribute (juce::String ("bus") + kBusNames[b], board.busOn[b].load()));
    }

    engine.externalChanges.fetch_add (1);   // the UI refreshes what it shows
}

void Session::save (MixerEngine& engine)
{
    juce::XmlElement root ("OpenMixSession");
    root.setAttribute ("version", sessionVersion);
    root.setAttribute ("theme", Theme::isDark() ? "dark" : "light");
    root.setAttribute ("bpm", engine.getBpm());   // tap tempo, 40..240 BPM

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
        xs->setAttribute ("name", strip.name);
        xs->setAttribute ("colour", strip.colour);
        xs->setAttribute ("input", strip.inputChannel.load());
        xs->setAttribute ("inputRight", strip.inputChannelRight.load());
        xs->setAttribute ("sourceType", strip.sourceType.load());
        xs->setAttribute ("sourceDevice", strip.sourceDeviceName);
        xs->setAttribute ("sourceApp", strip.sourceAppExe);
        xs->setAttribute ("loopbackId", strip.loopbackId);
        xs->setAttribute ("loopbackName", strip.loopbackName);
        xs->setAttribute ("appOutputId", strip.appOutputDeviceId);
        xs->setAttribute ("appOutputName", strip.appOutputDeviceName);
        xs->setAttribute ("gain", strip.gainDb.load());
        xs->setAttribute ("fader", strip.faderDb.load());
        xs->setAttribute ("mute", strip.mute.load());
        xs->setAttribute ("soloSafe", strip.soloSafe.load());
        xs->setAttribute ("mono", strip.mono.load());
        xs->setAttribute ("invertPolarity", strip.invertPolarity.load());
        xs->setAttribute ("padX", strip.pad.x.load());
        xs->setAttribute ("padY", strip.pad.y.load());
        xs->setAttribute ("padMode", strip.pad.mode.load());

        for (int b = 0; b < kNumBuses; ++b)
        {
            const juce::String bus (kBusNames[b]);
            xs->setAttribute ("bus" + bus, strip.busOn[b].load());
            xs->setAttribute ("send" + bus, strip.sendDb[b].load());
            xs->setAttribute ("pre" + bus, strip.sendPre[b].load());
        }
        xs->setAttribute ("fxSendReverb", strip.getFxSendLevel (omx::kFxReverb));
        xs->setAttribute ("fxSendDelay", strip.getFxSendLevel (omx::kFxDelay));
        xs->setAttribute ("fxSendChorus", strip.getFxSendLevel (omx::kFxChorus));

        saveProcessing (*xs, strip.fx);
        saveChain (*xs, strip.inserts);
    }

    for (int b = 0; b < kNumBuses; ++b)
    {
        auto& bus = engine.getBus (b);
        auto* xb = root.createNewChildElement ("Bus");
        xb->setAttribute ("name", kBusNames[b]);
        xb->setAttribute ("customName", bus.customName);   // "" = default name; group stays slot-based
        xb->setAttribute ("active", bus.active.load());
        xb->setAttribute ("monitor", bus.monitor.load());
        xb->setAttribute ("fader", bus.faderDb.load());
        xb->setAttribute ("mute", bus.mute.load());
        xb->setAttribute ("normOn", bus.normOn.load());
        xb->setAttribute ("normTarget", bus.normTargetLufs.load());
        xb->setAttribute ("output", bus.outputPair.load());
        xb->setAttribute ("target", bus.target.load());
        xb->setAttribute ("device", bus.deviceName);
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
        }
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
            case SourceType::Loopback:
                if (! engine.setStripSourceLoopback (s, xs->getStringAttribute ("loopbackId"), xs->getStringAttribute ("loopbackName"), error))
                    juce::Logger::writeToLog ("Loopback " + strip.loopbackName + ": " + error);   // maintainSources() retries
                break;
        }

        strip.name = xs->getStringAttribute ("name");
        strip.colour = xs->getIntAttribute ("colour", 0);
        strip.gainDb.store ((float) xs->getDoubleAttribute ("gain", 0.0));
        strip.faderDb.store ((float) xs->getDoubleAttribute ("fader", 0.0));
        strip.mute.store (xs->getBoolAttribute ("mute", false));
        strip.soloSafe.store (xs->getBoolAttribute ("soloSafe", false));
        strip.mono.store (xs->getBoolAttribute ("mono", false));
        strip.invertPolarity.store (xs->getBoolAttribute ("invertPolarity", false));
        strip.pad.x.store ((float) xs->getDoubleAttribute ("padX", 0.0));
        strip.pad.y.store ((float) xs->getDoubleAttribute ("padY", -1.0));
        strip.pad.mode.store (xs->getIntAttribute ("padMode", 0));

        for (int b = 0; b < kNumBuses; ++b)
        {
            const juce::String bus (kBusNames[b]);
            strip.busOn[b].store (xs->getBoolAttribute ("bus" + bus, b == 0));
            strip.sendDb[b].store ((float) xs->getDoubleAttribute ("send" + bus, 0.0));
            strip.sendPre[b].store (xs->getBoolAttribute ("pre" + bus, false));
        }
        strip.setFxSendLevel (omx::kFxReverb, (float) xs->getDoubleAttribute ("fxSendReverb", 0.0));
        strip.setFxSendLevel (omx::kFxDelay, (float) xs->getDoubleAttribute ("fxSendDelay", 0.0));
        strip.setFxSendLevel (omx::kFxChorus, (float) xs->getDoubleAttribute ("fxSendChorus", 0.0));

        loadProcessing (xs->getChildByName ("Processing"), strip.fx);
        if (loadPlugins)
            loadChain (*xs, strip.inserts, engine);
    }

    bool anyMonitor = false;
    for (auto* xb : root.getChildWithTagNameIterator ("Bus"))
    {
        const int b = busIndexFor (*xb, version);
        if (! juce::isPositiveAndBelow (b, kNumBuses))
            continue;

        auto& bus = engine.getBus (b);
        bus.active.store (xb->getBoolAttribute ("active", true));
        bus.customName = xb->getStringAttribute ("customName");
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
                if (! engine.setBusDevice (b, xb->getStringAttribute ("device"), error))
                    bus.deviceName = xb->getStringAttribute ("device");   // show as unplugged, keep the choice
                break;
        }
    }

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
                board.renamePad (p, xp->getStringAttribute ("name", file.getFileNameWithoutExtension()));
            else
                juce::Logger::writeToLog ("Soundboard pad " + juce::String (p + 1) + ": " + error);
        }
    }

    // Tap tempo: restore the saved BPM (old files keep the current one) and
    // push it into the global FX delay and every tempo-synced delay insert.
    engine.setBpm (root.getDoubleAttribute ("bpm", engine.getBpm()));
}
