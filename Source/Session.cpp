#include "Session.h"
#include "MixerEngine.h"

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

void Session::save (MixerEngine& engine)
{
    juce::XmlElement root ("OpenMixSession");
    root.setAttribute ("version", 1);

    if (auto device = engine.getDeviceManager().createStateXml())
        root.addChildElement (device.release());

    for (int s = 0; s < kNumStrips; ++s)
    {
        auto& strip = engine.getStrip (s);
        auto* xs = root.createNewChildElement ("Strip");
        xs->setAttribute ("index", s);
        xs->setAttribute ("name", strip.name);
        xs->setAttribute ("input", strip.inputChannel.load());
        xs->setAttribute ("sourceType", strip.sourceType.load());
        xs->setAttribute ("sourceDevice", strip.sourceDeviceName);
        xs->setAttribute ("sourceApp", strip.sourceAppExe);
        xs->setAttribute ("gain", strip.gainDb.load());
        xs->setAttribute ("fader", strip.faderDb.load());
        xs->setAttribute ("mute", strip.mute.load());
        xs->setAttribute ("padX", strip.pad.x.load());
        xs->setAttribute ("padY", strip.pad.y.load());
        xs->setAttribute ("padMode", strip.pad.mode.load());
        for (int b = 0; b < kNumBuses; ++b)
            xs->setAttribute (juce::String ("bus") + kBusNames[b], strip.busOn[b].load());

        for (int slot = 0; slot < kNumSlots; ++slot)
        {
            auto* plugin = strip.getPlugin (slot);
            if (plugin == nullptr)
                continue;

            auto* xp = xs->createNewChildElement ("Slot");
            xp->setAttribute ("index", slot);
            xp->setAttribute ("bypass", strip.slotBypass[(size_t) slot].load());
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
        xb->setAttribute ("index", b);
        xb->setAttribute ("fader", bus.faderDb.load());
        xb->setAttribute ("mute", bus.mute.load());
        xb->setAttribute ("output", bus.outputPair.load());
        xb->setAttribute ("target", bus.target.load());
        xb->setAttribute ("device", bus.deviceName);
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

void Session::apply (const juce::XmlElement& root, MixerEngine& engine)
{
    for (auto* xs : root.getChildWithTagNameIterator ("Strip"))
    {
        const int s = xs->getIntAttribute ("index", -1);
        if (! juce::isPositiveAndBelow (s, kNumStrips))
            continue;

        auto& strip = engine.getStrip (s);
        juce::String error;
        switch ((SourceType) xs->getIntAttribute ("sourceType", (int) SourceType::Main))
        {
            case SourceType::None:   engine.setStripSourceNone (s); break;
            case SourceType::Main:   engine.setStripSourceMain (s, xs->getIntAttribute ("input", -1)); break;
            case SourceType::Device: engine.setStripSourceDevice (s, xs->getStringAttribute ("sourceDevice"), error); break;
            case SourceType::App:    engine.setStripSourceApp (s, xs->getStringAttribute ("sourceApp"), error); break;
        }

        strip.name = xs->getStringAttribute ("name");
        strip.gainDb.store ((float) xs->getDoubleAttribute ("gain", 0.0));
        strip.faderDb.store ((float) xs->getDoubleAttribute ("fader", 0.0));
        strip.mute.store (xs->getBoolAttribute ("mute", false));
        strip.pad.x.store ((float) xs->getDoubleAttribute ("padX", 0.0));
        strip.pad.y.store ((float) xs->getDoubleAttribute ("padY", -1.0));
        strip.pad.mode.store (xs->getIntAttribute ("padMode", 0));
        for (int b = 0; b < kNumBuses; ++b)
            strip.busOn[b].store (xs->getBoolAttribute (juce::String ("bus") + kBusNames[b], b == 0));

        for (auto* xp : xs->getChildWithTagNameIterator ("Slot"))
        {
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
                DBG ("Could not restore " << desc.name << ": " << pluginError);
                continue;
            }

            strip.slotBypass[(size_t) slot].store (xp->getBoolAttribute ("bypass", false));

            juce::MemoryBlock state;
            if (state.fromBase64Encoding (xp->getStringAttribute ("state")) && state.getSize() > 0)
                strip.getPlugin (slot)->setStateInformation (state.getData(), (int) state.getSize());
        }
    }

    for (auto* xb : root.getChildWithTagNameIterator ("Bus"))
    {
        const int b = xb->getIntAttribute ("index", -1);
        if (! juce::isPositiveAndBelow (b, kNumBuses))
            continue;

        auto& bus = engine.getBus (b);
        bus.faderDb.store ((float) xb->getDoubleAttribute ("fader", 0.0));
        bus.mute.store (xb->getBoolAttribute ("mute", false));
        juce::String error;
        switch ((BusTarget) xb->getIntAttribute ("target", (int) BusTarget::MainPair))
        {
            case BusTarget::Off:      engine.setBusOff (b); break;
            case BusTarget::MainPair: engine.setBusMainPair (b, xb->getIntAttribute ("output", b)); break;
            case BusTarget::Device:
                if (! engine.setBusDevice (b, xb->getStringAttribute ("device"), error))
                    bus.deviceName = xb->getStringAttribute ("device");   // show as offline, keep the choice
                break;
        }
    }
}
