#include "app/Session.h"
#include "engine/MixerEngine.h"
#include "ui/Theme.h"

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

void Session::save (MixerEngine& engine)
{
    juce::XmlElement root ("OpenMixSession");
    root.setAttribute ("version", sessionVersion);
    root.setAttribute ("theme", Theme::isDark() ? "dark" : "light");

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
        xs->setAttribute ("appOutputId", strip.appOutputDeviceId);
        xs->setAttribute ("appOutputName", strip.appOutputDeviceName);
        xs->setAttribute ("gain", strip.gainDb.load());
        xs->setAttribute ("fader", strip.faderDb.load());
        xs->setAttribute ("mute", strip.mute.load());
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

        saveProcessing (*xs, strip.fx);

        for (int slot = 0; slot < kNumSlots; ++slot)
        {
            auto* plugin = strip.getPlugin (slot);
            if (plugin == nullptr)
                continue;

            auto* xp = xs->createNewChildElement ("Slot");
            xp->setAttribute ("index", slot);
            xp->setAttribute ("bypass", strip.slotBypass[(size_t) slot].load());
            xp->setAttribute ("mix", strip.slotMix[(size_t) slot].load());
            xp->addChildElement (plugin->getPluginDescription().createXml().release());

            juce::MemoryBlock state;
            plugin->getStateInformation (state);
            xp->setAttribute ("state", state.toBase64Encoding());
        }
    }

    for (int b = 0; b < kNumBuses; ++b)
    {
        auto& bus = engine.getBus (b);
        auto* xb = root.createNewChildElement ("Bus");
        xb->setAttribute ("name", kBusNames[b]);
        xb->setAttribute ("active", bus.active.load());
        xb->setAttribute ("monitor", bus.monitor.load());
        xb->setAttribute ("fader", bus.faderDb.load());
        xb->setAttribute ("mute", bus.mute.load());
        xb->setAttribute ("output", bus.outputPair.load());
        xb->setAttribute ("target", bus.target.load());
        xb->setAttribute ("device", bus.deviceName);
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
        }

        strip.name = xs->getStringAttribute ("name");
        strip.colour = xs->getIntAttribute ("colour", 0);
        strip.gainDb.store ((float) xs->getDoubleAttribute ("gain", 0.0));
        strip.faderDb.store ((float) xs->getDoubleAttribute ("fader", 0.0));
        strip.mute.store (xs->getBoolAttribute ("mute", false));
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

        loadProcessing (xs->getChildByName ("Processing"), strip.fx);

        for (auto* xp : xs->getChildWithTagNameIterator ("Slot"))
        {
            if (! loadPlugins)
                break;

            const int slot = xp->getIntAttribute ("index", -1);
            auto* xd = xp->getChildByName ("PLUGIN");
            if (! juce::isPositiveAndBelow (slot, kNumSlots) || xd == nullptr)
                continue;

            juce::PluginDescription desc;
            if (! desc.loadFromXml (*xd))
                continue;

            juce::String pluginError;
            if (! strip.loadPlugin (slot, desc, engine.getFormatManager(), pluginError))
            {
                juce::Logger::writeToLog ("Could not restore " + desc.name + ": " + pluginError);
                continue;
            }

            // Slots are packed, so the plugin may sit lower than its saved index.
            const int installed = strip.getNumPlugins() - 1;
            strip.slotBypass[(size_t) installed].store (xp->getBoolAttribute ("bypass", false));
            strip.slotMix[(size_t) installed].store ((float) xp->getDoubleAttribute ("mix", 1.0));

            juce::MemoryBlock state;
            if (state.fromBase64Encoding (xp->getStringAttribute ("state")) && state.getSize() > 0)
                strip.getPlugin (installed)->setStateInformation (state.getData(), (int) state.getSize());
        }
    }

    bool anyMonitor = false;
    for (auto* xb : root.getChildWithTagNameIterator ("Bus"))
    {
        const int b = busIndexFor (*xb, version);
        if (! juce::isPositiveAndBelow (b, kNumBuses))
            continue;

        auto& bus = engine.getBus (b);
        bus.active.store (xb->getBoolAttribute ("active", true));
        bus.monitor.store (xb->getBoolAttribute ("monitor", false));
        anyMonitor = anyMonitor || bus.monitor.load();
        bus.faderDb.store ((float) xb->getDoubleAttribute ("fader", 0.0));
        bus.mute.store (xb->getBoolAttribute ("mute", false));

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
}
