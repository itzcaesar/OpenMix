// Integration unit tests for OpenMix app/engine/creator modules:
//   - VoicePadParams per-mode circle persistence
//   - RemoteCommands validation, state JSON, and custom bus display names
//   - MidiControl JSON round-trip, record toggle, /norm toggle, soundboard trigger
//   - Soundboard short-clip resampling (< 8 samples)

#include <JuceHeader.h>
#include "engine/MixerEngine.h"
#include "app/Session.h"
#include "app/RemoteCommands.h"
#include "app/MidiControl.h"
#include "app/GlobalHotkeys.h"
#include "app/MixHistory.h"
#include "creator/MediaDecode.h"

#include <cmath>
#include <cstdio>
#include <functional>

static int failures = 0;

#define CHECK(cond, ...)                                                    \
    do {                                                                    \
        if (! (cond)) {                                                     \
            ++failures;                                                     \
            std::printf ("  FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond); \
            std::printf (__VA_ARGS__);                                      \
            std::printf ("\n");                                             \
        }                                                                   \
    } while (false)

static void run (const char* name, const std::function<void()>& test)
{
    const int before = failures;
    test();
    std::printf ("%s %s\n", failures == before ? "[ ok ]" : "[FAIL]", name);
}

int main (int argc, char** argv)
{
    if (argc >= 6 && juce::String (argv[1]) == PluginSandbox::workerFlag)
    {
        juce::ScopedJuceInitialiser_GUI juceInit;
        PluginSandboxWorkerThread worker (argv[2], argv[3], juce::File (argv[4]),
                                          juce::String (argv[5]).getIntValue());
        if (! worker.startThread (juce::Thread::Priority::high))
            return 3;
        juce::MessageManager::getInstance()->runDispatchLoop();
        return 0;
    }

    juce::ScopedJuceInitialiser_GUI juceInit;
    std::printf ("OpenMix app tests\n");

    run ("VoicePadParams: each mode saves and restores its own circle position", []
    {
        VoicePadParams pad;
        pad.setModeAndSync (VoicePadParams::Voice);
        pad.setPosition (0.65f, 0.35f);

        pad.setModeAndSync (VoicePadParams::Pan);
        CHECK (std::abs (pad.x.load() - 0.0f) < 1e-5f && std::abs (pad.y.load() - (-1.0f)) < 1e-5f,
               "switching to Pan carried over Voice position");
        pad.setPosition (-0.45f, 0.25f);

        pad.setModeAndSync (VoicePadParams::Voice);
        CHECK (std::abs (pad.x.load() - 0.65f) < 1e-5f && std::abs (pad.y.load() - 0.35f) < 1e-5f,
               "Voice position was not saved");

        pad.setModeAndSync (VoicePadParams::Pan);
        CHECK (std::abs (pad.x.load() - (-0.45f)) < 1e-5f && std::abs (pad.y.load() - 0.25f) < 1e-5f,
               "Pan position was not saved");
    });

    run ("VoicePad: Voice mode tilts Lo/Hi, lifts presence, and the top doubles", []
    {
        // RMS of a sine after 0.5 s through the pad at (x, y).
        auto rms = [] (float x, float y, double hz)
        {
            VoicePadParams params;
            params.setPosition (x, y);
            VoicePadProcessor pad (params);
            const double fs = 48000.0;
            const int n = 24000;
            pad.prepare (fs, n);
            juce::AudioBuffer<float> buf (2, n);
            for (int i = 0; i < n; ++i)
            {
                const float s = 0.25f * (float) std::sin (juce::MathConstants<double>::twoPi * hz * i / fs);
                buf.setSample (0, i, s);
                buf.setSample (1, i, s);
            }
            pad.process (buf, n);
            return buf.getRMSLevel (0, n / 2, n / 2);
        };
        const float flat100 = rms (0, -1, 100), flat8k = rms (0, -1, 8000), flat3k = rms (0, -1, 3000);
        CHECK (rms (-1, -1, 100) > flat100 * 2.0f, "Lo should boost the lows by several dB");
        CHECK (rms (1, -1, 8000) > flat8k * 2.0f, "Hi should boost the highs by several dB");
        CHECK (rms (0, 0, 3000) > flat3k * 1.5f, "the middle of Y should lift presence");
        CHECK (std::abs (rms (0, 1, 300) - rms (0, 0, 300)) > 0.005f, "the top should add the doubler");
    });

    run ("RemoteCommands: validation, displayName, and soundboardNames", []
    {
        MixerEngine engine;
        engine.setBusName (0, "Headphones");

        CHECK (! applyRemoteCommand (engine, "/openmix/soundboard/99/play", 1.0f), "out-of-range pad should fail");
        CHECK (applyRemoteCommand (engine, "/openmix/strip/1/fader", -6.0f), "strip fader command failed");
        CHECK (std::abs (engine.getStrip (0).faderDb.load() - (-6.0f)) < 0.01f, "strip fader not updated");

        const auto jsonStr = remoteStateJson (engine);
        juce::var parsed;
        CHECK (juce::JSON::parse (jsonStr, parsed).wasOk(), "remoteStateJson invalid JSON");
        const auto buses = parsed["buses"];
        CHECK (buses.isArray() && buses[0]["name"].toString() == "A1"
               && buses[0]["displayName"].toString() == "Headphones",
               "bus displayName missing or wrong");
        CHECK (parsed["soundboardNames"].isArray() && parsed["soundboardNames"].size() == Soundboard::numPads,
               "soundboardNames missing");
    });

    run ("MidiControl: JSON round-trip, /norm note toggle, and record toggle", []
    {
        MixerEngine engine;
        MidiControl midi (engine);

        std::vector<MidiControl::Mapping> maps {
            { true, 1, 60, "bus/A1/norm" },
            { false, 1, 7, "strip/1/fader" },
            { true, 1, 61, "soundboard/1/play" }
        };
        const auto json = MidiControl::mappingsToJson (maps);
        const auto back = MidiControl::mappingsFromJson (json);
        CHECK (back.size() == 3 && back[0].target == "bus/A1/norm" && back[1].number == 7,
               "MIDI JSON round-trip failed");

        CHECK (! engine.getBus (0).normOn.load(), "norm should start off");
        midi.applyMapping (maps[0], juce::MidiMessage::noteOn (1, 60, (juce::uint8) 127));
        CHECK (engine.getBus (0).normOn.load(), "/norm note-on should turn norm ON");
        midi.applyMapping (maps[0], juce::MidiMessage::noteOn (1, 60, (juce::uint8) 127));
        CHECK (! engine.getBus (0).normOn.load(), "/norm second note-on should turn norm OFF");

        midi.applyMapping (maps[1], juce::MidiMessage::controllerEvent (1, 7, 127));
        CHECK (std::abs (engine.getStrip (0).faderDb.load() - 12.0f) < 0.1f, "CC 127 should set fader to +12 dB");

        midi.applyMapping (maps[2], juce::MidiMessage::noteOn (1, 61, (juce::uint8) 127));
    });

    run ("MidiControl: CC toggles follow the mixer, send and send-level targets work", []
    {
        MixerEngine engine;
        MidiControl midi (engine);
        engine.getBus (3).active.store (true);   // B1

        // A CC mute flips what the mixer has now, even after a mute made elsewhere.
        const MidiControl::Mapping mute { false, 1, 20, "strip/1/mute" };
        engine.getStrip (0).mute.store (true);   // muted in the app
        midi.applyMapping (mute, juce::MidiMessage::controllerEvent (1, 20, 127));
        CHECK (! engine.getStrip (0).mute.load(), "CC press should unmute a strip muted in the app");
        midi.applyMapping (mute, juce::MidiMessage::controllerEvent (1, 20, 127));
        CHECK (! engine.getStrip (0).mute.load(), "holding the CC above 64 should not flip again");
        midi.applyMapping (mute, juce::MidiMessage::controllerEvent (1, 20, 0));
        midi.applyMapping (mute, juce::MidiMessage::controllerEvent (1, 20, 127));
        CHECK (engine.getStrip (0).mute.load(), "release and press again should mute");

        // Send on/off and send level (strip/<n>/sendlevel/<bus>).
        const MidiControl::Mapping send { true, 1, 62, "strip/1/send/B1" };
        const bool before = engine.getStrip (0).busOn[3].load();
        midi.applyMapping (send, juce::MidiMessage::noteOn (1, 62, (juce::uint8) 127));
        CHECK (engine.getStrip (0).busOn[3].load() != before, "send note should toggle the B1 send");

        const MidiControl::Mapping level { false, 1, 21, "strip/1/sendlevel/B1" };
        midi.applyMapping (level, juce::MidiMessage::controllerEvent (1, 21, 0));
        CHECK (std::abs (engine.getStrip (0).sendDb[3].load() - (-60.0f)) < 0.1f, "CC 0 should set the send to -60 dB, got %f",
               engine.getStrip (0).sendDb[3].load());
    });

    run ("RemoteCommands: state without meters stays the same while levels move", []
    {
        MixerEngine engine;
        const auto a = remoteStateJson (engine, false);
        engine.getStrip (0).peakL.store (0.5f);
        CHECK (remoteStateJson (engine, false) == a, "meter-free state changed with a peak level");
        CHECK (remoteStateJson (engine, true).contains ("peakDb"), "full state should carry meters");
        applyRemoteCommand (engine, "/openmix/strip/1/fader", -3.0f);
        CHECK (remoteStateJson (engine, false) != a, "a fader move should change the state");
    });

    run ("Soundboard: very short clip (< 8 samples) resamples safely without crash", []
    {
        const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("openmix-short-test.wav");
        tmp.deleteFile();
        {
            std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (tmp);
            auto writer = std::unique_ptr<juce::AudioFormatWriter> (
                juce::WavAudioFormat().createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                                    .withSampleRate (44100.0)
                                                                    .withNumChannels (1)
                                                                    .withBitsPerSample (16)));
            juce::AudioBuffer<float> tiny (1, 4);
            for (int i = 0; i < 4; ++i)
                tiny.setSample (0, i, 0.25f * (float) (i + 1));
            CHECK (writer != nullptr && writer->writeFromAudioSampleBuffer (tiny, 0, 4), "failed writing tiny test WAV");
        }

        Soundboard board;
        board.prepare (48000.0);
        juce::String error;
        CHECK (board.loadPad (0, tmp, error), "loadPad failed on 4-sample WAV: %s", error.toRawUTF8());
        board.trigger (0);
        std::vector<float> l (64, 0.0f), r (64, 0.0f);
        CHECK (board.render (l.data(), r.data(), 64), "short pad did not render");
        for (int i = 0; i < 64; ++i)
        {
            CHECK (std::isfinite (l[i]) && std::isfinite (r[i]), "non-finite output from short pad");
            if (! std::isfinite (l[i])) break;
        }
        tmp.deleteFile();
    });

    run ("GlobalHotkeys: push to talk fires once per press, releases, and ignores key repeat", []
    {
        GlobalHotkeys hotkeys;
        std::vector<std::pair<int, bool>> events;   // (index, down)
        hotkeys.onAction = [&] (const GlobalHotkeys::Binding& b, bool down) { events.push_back ({ b.index, down }); };
        GlobalHotkeys::Binding ptt;
        ptt.keys.vk = 0x7B;   // F12, no modifiers
        ptt.action = GlobalHotkeys::Action::PushToTalk;
        ptt.index = 2;
        hotkeys.setBindings ({ ptt });

        auto pump = [] { juce::MessageManager::getInstance()->runDispatchLoopUntil (30); };
        hotkeys.handleKey (0x7B, true);
        hotkeys.handleKey (0x7B, true);   // Windows key repeat
        pump();
        CHECK (events.size() == 1 && events[0] == std::make_pair (2, true), "one press expected, got %d events", (int) events.size());
        hotkeys.handleKey (0x7B, false);
        pump();
        CHECK (events.size() == 2 && events[1] == std::make_pair (2, false), "release expected");
        hotkeys.handleKey (0x7A, true);   // F11: not bound
        pump();
        CHECK (events.size() == 2, "an unbound key fired");

        // Held while the hotkeys change: the strip is released, not left open.
        hotkeys.handleKey (0x7B, true);
        pump();
        hotkeys.setBindings ({});
        pump();
        CHECK (events.size() == 4 && events[3] == std::make_pair (2, false), "PTT not released when bindings changed");
    });

    run ("Session: a bus keeps its output device when it can't be opened", []
    {
        const auto xml = juce::parseXML (R"(<OpenMixSession version="2">
            <Bus name="B1" active="1" target="2" device="OpenMix test device that does not exist"/></OpenMixSession>)");
        MixerEngine engine;
        Session::apply (*xml, engine, false);
        auto& bus = engine.getBus (3);
        CHECK ((BusTarget) bus.target.load() == BusTarget::Device, "bus fell back to %d", bus.target.load());
        CHECK (bus.deviceName == "OpenMix test device that does not exist" && bus.sink == nullptr, "device choice lost");
    });

    run ("GlobalHotkeys: the hook thread starts and stops without hanging", []
    {
        GlobalHotkeys hotkeys;
        GlobalHotkeys::Binding b;
        b.keys.vk = 0x7B;   // F12
        for (int i = 0; i < 20; ++i)
        {
            hotkeys.setBindings ({ b });
            hotkeys.setBindings ({});
        }
        hotkeys.captureNext ([] (GlobalHotkeys::Keys) {});
        hotkeys.cancelCapture();
        hotkeys.setBindings ({ b });
        CHECK (hotkeys.getBindings().size() == 1, "binding kept");
    });   // destructor stops the running hook thread

    run ("RemoteCommands: NaN/Inf values are refused", []
    {
        MixerEngine engine;
        engine.getStrip (0).faderDb.store (-6.0f);
        CHECK (! applyRemoteCommand (engine, "/strip/1/fader", std::nanf ("")), "NaN fader accepted");
        CHECK (! applyRemoteCommand (engine, "/strip/1/fader", INFINITY), "Inf fader accepted");
        CHECK (engine.getStrip (0).faderDb.load() == -6.0f, "fader changed by a bad value");
    });

    run ("Bus feeds: loops are refused, scenes and removeBus handle them", []
    {
        MixerEngine engine;
        for (int b : { 0, 3, 4 })
            engine.addBus (b);

        CHECK (! engine.setBusFeed (3, 3, true), "a bus fed itself");
        CHECK (engine.setBusFeed (3, 0, true), "B1 -> A1 refused");
        CHECK (engine.setBusFeed (4, 3, true), "B2 -> B1 refused");
        CHECK (! engine.setBusFeed (0, 4, true), "A1 -> B2 makes a loop through B1 and was allowed");
        CHECK (engine.busFeedWouldLoop (0, 3) && ! engine.busFeedWouldLoop (4, 0), "loop check is wrong");

        engine.getBus (3).feedDb[0].store (-6.0f);
        auto scene = Session::captureScene (engine);
        engine.setBusFeed (3, 0, false);
        engine.getBus (3).feedDb[0].store (0.0f);
        Session::applyScene (*scene, engine);
        CHECK (engine.getBus (3).feedOn[0].load() && std::abs (engine.getBus (3).feedDb[0].load() + 6.0f) < 0.01f,
               "scene did not restore B1 -> A1 at -6 dB");

        engine.removeBus (3);
        CHECK (! engine.getBus (3).feedOn[0].load() && ! engine.getBus (4).feedOn[3].load(),
               "removing B1 left feeds to or from it");
    });

    run ("Bus groups: B borrows free A slots after B4, never the other way", []
    {
        MixerEngine engine;
        for (int b = 0; b < kNumBuses; ++b)
            engine.removeBus (b);
        engine.addBus (0);   // A1
        engine.addBus (1);   // A2

        int added = 0;
        for (int slot; (slot = engine.findNextFreeSlotInGroup (true)) >= 0; ++added)
            engine.addBus (slot, true);
        CHECK (added == 6, "B should take its 4 slots plus the 2 free A slots, took %d", added);
        CHECK (engine.findNextFreeSlotInGroup (false) < 0, "A got a slot B borrowed");
        CHECK (engine.getBus (6).borrowed && engine.getBusName (6) == "B5" && engine.getBusName (2) == "B6",
               "borrowed slots should be named B5 (A4's slot) and B6 (A3's slot)");
        CHECK (engine.isVirtualBus (6) && ! engine.isVirtualBus (1), "groups follow the borrow flag");

        const auto order = engine.getBusDisplayOrder();
        CHECK (order[0] == 0 && order[1] == 1 && order[2] == 3 && order[5] == 7 && order[6] == 6 && order[7] == 2,
               "display order should be A1 A2, B1-B4, B5 B6");

        engine.removeBus (6);
        CHECK (! engine.getBus (6).borrowed && engine.findNextFreeSlotInGroup (false) == 6,
               "a removed borrowed slot should go back to A");
        CHECK (engine.getBusName (2) == "B5", "B6 should become B5 once the earlier borrowed bus is gone");
    });

    run ("Copy / paste strip: settings come along, source and mute stay, no self-ducking", []
    {
        MixerEngine engine;
        auto& a = engine.getStrip (0);
        auto& b = engine.getStrip (1);
        a.name = "Mic"; a.colour = 3; a.icon = 4;
        a.faderDb.store (-7.0f); a.gainDb.store (4.0f); a.mono.store (true);
        a.busOn[3].store (true); a.sendDb[3].store (-9.0f); a.sendPre[3].store (true);
        a.fx.compOn.store (true);
        a.fx.duckOn.store (true); a.fx.duckSource.store (1);   // ducks under the strip it's pasted onto
        a.mute.store (true);
        engine.setStripSourceMedia (0);
        b.mute.store (false);

        auto copied = Session::copyStrip (engine, 0);
        CHECK (copied != nullptr, "copy");
        Session::pasteStrip (*copied, engine, 1);

        CHECK (b.name == "Mic" && b.colour == 3 && b.icon == 4, "name, colour and icon");
        CHECK (b.faderDb.load() == -7.0f && b.gainDb.load() == 4.0f && b.mono.load(), "levels and mono");
        CHECK (b.busOn[3].load() && b.sendDb[3].load() == -9.0f && b.sendPre[3].load(), "sends");
        CHECK (b.fx.compOn.load(), "processing");
        CHECK (! b.mute.load(), "mute is not pasted");
        CHECK (b.sourceType.load() != (int) SourceType::Media, "source is not pasted");
        CHECK (! b.fx.duckOn.load() && b.fx.duckSource.load() == -1, "a strip must not duck under itself");
        CHECK (Session::copyStrip (engine, kNumStrips) == nullptr, "bad index");
    });

    run ("MixHistory: undo, redo, a new change drops redo, settle keeps it", []
    {
        MixHistory h;
        CHECK (h.offer ("a") && ! h.offer ("a") && h.offer ("b") && h.offer ("c"), "offer");
        CHECK (h.undo() == "b" && h.undo() == "a" && h.undo().isEmpty(), "undo order");
        CHECK (h.redo() == "b", "redo");
        h.settle ("b2");                       // round trip changed the text slightly
        CHECK (! h.offer ("b2") && h.canRedo(), "settle should not count as a new step");
        h.offer ("x");
        CHECK (! h.canRedo() && h.undo() == "b2", "a new change should drop redo");
        for (int i = 0; i < 300; ++i)
            h.offer (juce::String (i));
        int steps = 0;
        while (h.canUndo()) { h.undo(); ++steps; }
        CHECK (steps == (int) MixHistory::maxSteps, "history is capped");
    });

    run ("Session: scenes round-trip the mix, apply restores strips, buses and presets", []
    {
        MixerEngine engine;
        auto& strip = engine.getStrip (1);
        strip.faderDb.store (-7.5f);
        strip.sendPre[3].store (true);
        strip.sendDb[3].store (-4.0f);
        strip.fx.compOn.store (true);
        engine.getBus (0).normOn.store (true);
        const auto scene = Session::captureScene (engine);

        strip.faderDb.store (0.0f);
        strip.sendPre[3].store (false);
        strip.fx.compOn.store (false);
        engine.getBus (0).normOn.store (false);
        Session::applyScene (*scene, engine);
        CHECK (strip.faderDb.load() == -7.5f, "scene fader %f", strip.faderDb.load());
        CHECK (strip.sendPre[3].load() && strip.sendDb[3].load() == -4.0f, "scene send");
        CHECK (strip.fx.compOn.load(), "scene processing");
        CHECK (engine.getBus (0).normOn.load(), "scene bus normalisation");

        // A version 2 file, written the way Session::save writes it.
        const auto xml = juce::parseXML (R"(<OpenMixSession version="2">
            <Strip index="2" active="1" name="Mic" sourceType="0" gain="3" fader="-2" mute="1" busB1="1" preB1="1" sendB1="-5">
              <Processing gateOn="1" gateThreshold="-44"/>
            </Strip>
            <Bus name="B2" active="1" customName="Stream" fader="-1" target="0"/>
            <StripPresets>
              <Preset name="Voice" fader="-3" busA1="1"><Processing eqOn="1" eqMid="2.5"/></Preset>
            </StripPresets>
          </OpenMixSession>)");
        MixerEngine fresh;
        Session::apply (*xml, fresh, false);
        auto& mic = fresh.getStrip (2);
        CHECK (mic.active.load() && ! fresh.getStrip (0).active.load(), "only listed strips are active");
        CHECK (mic.name == "Mic" && mic.gainDb.load() == 3.0f && mic.faderDb.load() == -2.0f && mic.mute.load(), "strip fields");
        CHECK (mic.busOn[3].load() && mic.sendPre[3].load() && mic.sendDb[3].load() == -5.0f, "strip send to B1");
        CHECK (mic.fx.gateOn.load() && mic.fx.gateThresholdDb.load() == -44.0f, "strip processing");
        CHECK (fresh.getBus (4).active.load() && fresh.getBusName (4) == "Stream" && fresh.getBus (4).faderDb.load() == -1.0f, "bus B2");
        CHECK (fresh.getMonitorBus() == 0, "no monitor in the file: A1 by default");
        const auto* preset = fresh.findStripPreset ("Voice");
        CHECK (preset != nullptr && preset->faderDb == -3.0f && preset->eqOn && preset->eqDb[omx::kEqLegacyMid] == 2.5f, "strip preset");

        // Version 1 saved buses by position: 0 = A1, 1 = A2, 2 = B1.
        const auto v1 = juce::parseXML (R"(<OpenMixSession><Bus index="2" fader="-9"/></OpenMixSession>)");
        MixerEngine old;
        Session::apply (*v1, old, false);
        CHECK (old.getBus (3).faderDb.load() == -9.0f, "v1 bus 2 should map to B1");

        // A 3-band EQ from before lands in bands 1, 4 and 8 and sounds the same.
        const auto oldEq = juce::parseXML (R"(<OpenMixSession version="2"><Strip index="0" active="1" sourceType="0">
              <Processing eqOn="1" eqLow="4" eqMid="-3" eqMidHz="2500" eqHigh="2"/></Strip></OpenMixSession>)");
        MixerEngine legacy;
        Session::apply (*oldEq, legacy, false);
        const auto& fx = legacy.getStrip (0).fx;
        CHECK (fx.eqDb[0].load() == 4.0f && fx.eqHz[0].load() == 120.0f, "old low shelf");
        CHECK (fx.eqDb[3].load() == -3.0f && fx.eqHz[3].load() == 2500.0f && fx.eqQ[3].load() == 1.0f, "old mid");
        CHECK (fx.eqDb[7].load() == 2.0f && fx.eqHz[7].load() == 8000.0f, "old high shelf");
        CHECK (fx.eqDb[1].load() == 0.0f && fx.eqDb[5].load() == 0.0f, "new bands start flat");

        // And the 8 bands round-trip through a scene.
        auto& eqStrip = legacy.getStrip (0);
        eqStrip.fx.eqDb[5].store (-6.5f); eqStrip.fx.eqHz[5].store (4200.0f); eqStrip.fx.eqQ[5].store (3.0f);
        const auto eqScene = Session::captureScene (legacy);
        eqStrip.fx.resetEq();
        Session::applyScene (*eqScene, legacy);
        CHECK (eqStrip.fx.eqDb[5].load() == -6.5f && eqStrip.fx.eqHz[5].load() == 4200.0f && eqStrip.fx.eqQ[5].load() == 3.0f,
               "band 6 lost in a scene");
    });

    run ("PluginChain: moving and removing keep each plugin's bypass and mix", []
    {
        struct Dummy : juce::AudioPluginInstance
        {
            explicit Dummy (juce::String n) : name (std::move (n)) {}
            juce::String name;
            void fillInPluginDescription (juce::PluginDescription& d) const override { d.name = name; }
            const juce::String getName() const override { return name; }
            void prepareToPlay (double, int) override {}
            void releaseResources() override {}
            void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
            double getTailLengthSeconds() const override { return 0; }
            bool acceptsMidi() const override { return false; }
            bool producesMidi() const override { return false; }
            juce::AudioProcessorEditor* createEditor() override { return nullptr; }
            bool hasEditor() const override { return false; }
            int getNumPrograms() override { return 1; }
            int getCurrentProgram() override { return 0; }
            void setCurrentProgram (int) override {}
            const juce::String getProgramName (int) override { return {}; }
            void changeProgramName (int, const juce::String&) override {}
            void getStateInformation (juce::MemoryBlock&) override {}
            void setStateInformation (const void*, int) override {}
        };

        PluginChain chain;
        chain.prepare (48000.0, 256);
        for (const char* n : { "a", "b", "c" })
            chain.installPlugin (5, std::make_unique<Dummy> (n));   // past the end: packs to the next slot
        CHECK (chain.getNumPlugins() == 3, "packed count %d", chain.getNumPlugins());
        chain.slotBypass[0].store (true);
        chain.slotMix[2].store (0.25f);

        chain.movePlugin (0, 2);   // a b c -> b c a
        CHECK (chain.getPlugin (0)->getName() == "b" && chain.getPlugin (2)->getName() == "a", "move order");
        CHECK (chain.slotBypass[2].load() && ! chain.slotBypass[0].load(), "bypass follows the plugin");
        CHECK (chain.slotMix[1].load() == 0.25f, "mix follows the plugin");

        chain.removePlugin (0);    // b c a -> c a
        CHECK (chain.getNumPlugins() == 2 && chain.getPlugin (0)->getName() == "c", "remove packs");
        CHECK (chain.slotMix[0].load() == 0.25f && chain.slotBypass[1].load(), "settings move up with plugins");
        CHECK (chain.slotMix[2].load() == 1.0f && ! chain.slotBypass[2].load(), "freed slot is reset");
    });

#if defined (OPENMIX_ENABLE_SANDBOX_TEST_HOOKS)
    run ("PluginSandbox: worker crash is isolated and the chain recovers", []
    {
        std::atomic<int> crashedSlot { -1 };
        PluginSandbox sandbox ([&] (int slot) { crashedSlot.store (slot, std::memory_order_release); });
        juce::XmlElement config ("PluginSandbox");
        config.setAttribute ("sampleRate", 48000.0);
        config.setAttribute ("blockSize", 64);
        config.setAttribute ("testCrashOnProcess", true);
        sandbox.configure (config.toString(), 0);

        std::vector<float> left (64, 0.125f), right (64, -0.125f);
        bool recovered = false;
        bool outputMatchesInput = true;
        for (int attempt = 0; attempt < 1200 && ! recovered; ++attempt)
        {
            std::fill (left.begin(), left.end(), 0.125f);
            std::fill (right.begin(), right.end(), -0.125f);
            if (sandbox.processBlock (left.data(), right.data(), (int) left.size())
                && crashedSlot.load (std::memory_order_acquire) == 0)
            {
                for (size_t i = 0; i < left.size(); ++i)
                    outputMatchesInput = outputMatchesInput && std::abs (left[i] - 0.125f) < 1.0e-6f
                                                         && std::abs (right[i] + 0.125f) < 1.0e-6f;
                recovered = true;
            }
            juce::Thread::sleep (5);
        }

        CHECK (crashedSlot.load (std::memory_order_acquire) == 0, "worker crash was not attributed to slot 0");
        CHECK (recovered, "worker did not restart and return a block");
        CHECK (outputMatchesInput, "restarted worker did not preserve the dry signal");
    });
#endif

    run ("Soundboard: stopping a pad fades out instead of cutting", []
    {
        const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("openmix-fade-test.wav");
        tmp.deleteFile();
        {
            std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (tmp);
            auto writer = std::unique_ptr<juce::AudioFormatWriter> (juce::WavAudioFormat().createWriterFor (stream,
                juce::AudioFormatWriterOptions{}.withSampleRate (48000.0).withNumChannels (1).withBitsPerSample (24)));
            juce::AudioBuffer<float> dc (1, 48000);
            for (int i = 0; i < dc.getNumSamples(); ++i)
                dc.setSample (0, i, 0.5f);
            writer->writeFromAudioSampleBuffer (dc, 0, dc.getNumSamples());
        }
        Soundboard board;
        board.prepare (48000.0);
        juce::String error;
        CHECK (board.loadPad (0, tmp, error), "load: %s", error.toRawUTF8());
        std::vector<float> l (128), r (128);
        board.trigger (0);
        board.render (l.data(), r.data(), 128);
        board.trigger (0);   // stop
        std::fill (l.begin(), l.end(), 0.0f); std::fill (r.begin(), r.end(), 0.0f);
        board.render (l.data(), r.data(), 128);
        CHECK (l[0] > 0.45f && l[127] < l[0] && l[127] > 0.0f, "fade starts loud and falls: %f -> %f", l[0], l[127]);
        for (int b = 0; b < 4; ++b) { std::fill (l.begin(), l.end(), 0.0f); board.render (l.data(), r.data(), 128); }
        CHECK (! board.isPlaying (0) && l[127] == 0.0f, "silent after the 5 ms fade");
        tmp.deleteFile();
    });

    run ("Media Foundation reader: rate, length, and the right samples after seeking either way", []
    {
        // A ramp, so every sample says where it is: left = i / n, right = -left.
        const auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("openmix-mf-ramp.wav");
        f.deleteFile();
        constexpr int n = 88200;
        {
            std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (f);
            auto writer = std::unique_ptr<juce::AudioFormatWriter> (juce::WavAudioFormat().createWriterFor (stream,
                juce::AudioFormatWriterOptions{}.withSampleRate (44100.0).withNumChannels (2).withBitsPerSample (24)));
            juce::AudioBuffer<float> ramp (2, n);
            for (int i = 0; i < n; ++i)
            {
                ramp.setSample (0, i, (float) i / n);
                ramp.setSample (1, i, -(float) i / n);
            }
            writer->writeFromAudioSampleBuffer (ramp, 0, n);
        }

        auto reader = createMediaFoundationReader (f);
        CHECK (reader != nullptr, "Media Foundation opens a WAV");
        CHECK (reader->sampleRate == 44100.0 && reader->numChannels == 2, "format %f / %u", reader->sampleRate, reader->numChannels);
        CHECK (std::abs (reader->lengthInSamples - n) < 100, "length %lld", (long long) reader->lengthInSamples);

        juce::AudioBuffer<float> out (2, 256);
        for (const int start : { 60000, 1000, 1256, 30000 })   // ahead, back, straight on, back again
        {
            reader->read (&out, 0, 256, start, true, true);
            float worst = 0.0f;
            for (int i = 0; i < 256; ++i)
                worst = juce::jmax (worst, std::abs (out.getSample (0, i) - (float) (start + i) / n),
                                           std::abs (out.getSample (1, i) + (float) (start + i) / n));
            CHECK (worst < 1.0e-4f, "samples at %d off by %f", start, worst);
        }
        CHECK (createMediaFoundationReader (juce::File ("C:/no/such/file.mp4")) == nullptr, "a missing file gives no reader");
        reader.reset();
        f.deleteFile();
    });

    // Needs a real video, so it runs only with OPENMIX_TEST_VIDEO set to an MP4 / MKV / MOV path.
    if (const auto video = juce::SystemStats::getEnvironmentVariable ("OPENMIX_TEST_VIDEO", {}); video.isNotEmpty())
        run ("Media Foundation: a video file's sound and pictures", [video]
        {
            auto reader = createMediaFoundationReader (juce::File (video));
            CHECK (reader != nullptr && reader->lengthInSamples > 0, "the video's audio opens");
            juce::AudioBuffer<float> out (2, 4096);
            reader->read (&out, 0, 4096, reader->lengthInSamples / 2, true, true);
            juce::Range<float> peak[2];
            reader->readMaxLevels (0, reader->lengthInSamples, peak, 2);
            std::printf ("       audio %.0f Hz, %u ch, peak %.4f\n", reader->sampleRate, reader->numChannels,
                         juce::jmax (peak[0].getEnd(), -peak[0].getStart()));

            VideoFrames frames;
            CHECK (frames.open (juce::File (video)), "the video stream opens");
            frames.setTime (5.0);
            juce::Image img;
            for (int tries = 0; tries < 300 && ! img.isValid(); ++tries)
            {
                juce::Thread::sleep (10);
                img = frames.getFrame();
            }
            CHECK (img.isValid() && img.getWidth() > 0 && img.getWidth() <= 960, "a frame decodes (%d x %d)", img.getWidth(), img.getHeight());
            // Saved so you can look at it: right way up, not stretched.
            const auto png = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("openmix-video-frame.png");
            png.deleteFile();
            if (juce::FileOutputStream out (png); out.openedOk())
                juce::PNGImageFormat().writeImageToStream (img, out);
            std::printf ("       frame %d x %d saved to %s\n", img.getWidth(), img.getHeight(), png.getFullPathName().toRawUTF8());
        });

    run ("MediaPlayer: plays a file, next / previous / repeat, removing tracks keeps the current one", []
    {
        auto makeWav = [] (const juce::String& name, float level)
        {
            const auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (name);
            f.deleteFile();
            std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (f);
            auto writer = std::unique_ptr<juce::AudioFormatWriter> (juce::WavAudioFormat().createWriterFor (stream,
                juce::AudioFormatWriterOptions{}.withSampleRate (44100.0).withNumChannels (2).withBitsPerSample (24)));
            juce::AudioBuffer<float> dc (2, 44100);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < dc.getNumSamples(); ++i)
                    dc.setSample (c, i, level);
            writer->writeFromAudioSampleBuffer (dc, 0, dc.getNumSamples());
            return f;
        };
        const auto a = makeWav ("openmix-media-a.wav", 0.25f), b = makeWav ("openmix-media-b.wav", 0.5f),
                   c = makeWav ("openmix-media-c.wav", 0.75f);

        MediaPlayer player;
        player.prepare (48000.0, 512);
        player.addFiles ({ a, b, c, juce::File ("C:/no/such/file.wav") });
        CHECK (player.getPlaylist().size() == 3, "missing files are left out: %d", player.getPlaylist().size());

        juce::String error;
        CHECK (player.playIndex (1, error) && player.isPlaying(), "play b: %s", error.toRawUTF8());
        std::vector<float> l (512), r (512);
        float level = 0.0f;
        for (int tries = 0; tries < 50 && level < 0.4f; ++tries)   // the read-ahead thread fills up first
        {
            juce::Thread::sleep (10);
            player.render (l.data(), r.data(), 512);
            level = l[256];
        }
        CHECK (std::abs (level - 0.5f) < 0.01f, "b plays at its level (resampled 44.1k -> 48k): %f", level);

        player.next();
        CHECK (player.getCurrentIndex() == 2, "next");
        player.repeat = MediaPlayer::RepeatAll;
        player.next();
        CHECK (player.getCurrentIndex() == 0, "repeat all wraps to the first track");
        player.repeat = MediaPlayer::RepeatOff;
        player.next(); player.next(); player.next();
        CHECK (player.getCurrentIndex() == 2, "repeat off stops at the last track");

        player.removeAt (0);
        CHECK (player.getCurrentIndex() == 1 && player.getTrackName() == "openmix-media-c", "removing an earlier track keeps the current one");
        player.removeAt (1);
        CHECK (player.getCurrentIndex() == -1 && ! player.isPlaying(), "removing the current track stops");
        player.clearPlaylist();
        for (auto& f : { a, b, c }) f.deleteFile();
    });

    run ("Soundboard: pad volume, loop, restart and hold modes, pages up to pad 32", []
    {
        // A 100-sample DC clip so a loop wraps inside one block.
        const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("openmix-mode-test.wav");
        tmp.deleteFile();
        {
            std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (tmp);
            auto writer = std::unique_ptr<juce::AudioFormatWriter> (juce::WavAudioFormat().createWriterFor (stream,
                juce::AudioFormatWriterOptions{}.withSampleRate (48000.0).withNumChannels (1).withBitsPerSample (24)));
            juce::AudioBuffer<float> dc (1, 100);
            for (int i = 0; i < dc.getNumSamples(); ++i)
                dc.setSample (0, i, 0.5f);
            writer->writeFromAudioSampleBuffer (dc, 0, dc.getNumSamples());
        }
        Soundboard board;
        board.prepare (48000.0);
        juce::String error;
        const int last = Soundboard::numPads - 1;
        CHECK (board.loadPad (last, tmp, error), "load pad 32: %s", error.toRawUTF8());
        std::vector<float> l (256), r (256);
        auto block = [&] { std::fill (l.begin(), l.end(), 0.0f); std::fill (r.begin(), r.end(), 0.0f); board.render (l.data(), r.data(), 256); };

        board.setPadVolume (last, -6.0f);
        board.trigger (last);
        block();
        CHECK (std::abs (l[10] - 0.5f * juce::Decibels::decibelsToGain (-6.0f)) < 0.01f, "pad volume: %f", l[10]);
        CHECK (l[200] == 0.0f && ! board.isPlaying (last), "play once ends after the clip");

        board.setPadVolume (last, 0.0f);
        board.setPadMode (last, Soundboard::Loop);
        board.trigger (last);
        block();
        CHECK (l[150] > 0.45f && l[250] > 0.45f && board.isPlaying (last), "loop wraps round");
        board.trigger (last);   // stop
        for (int i = 0; i < 4; ++i) block();
        CHECK (! board.isPlaying (last), "a second press stops a loop");

        board.setPadMode (last, Soundboard::Restart);
        board.trigger (last);
        board.trigger (last);
        block();
        CHECK (l[0] > 0.45f, "restart keeps playing on a second press");

        board.setPadMode (last, Soundboard::Loop);
        board.trigger (last);
        block();
        board.setPadMode (last, Soundboard::Hold);   // a looping sound, then hold: release must stop it
        board.release (last);
        for (int i = 0; i < 4; ++i) block();
        CHECK (! board.isPlaying (last), "letting go stops a Hold pad");
        tmp.deleteFile();
    });

    std::printf (failures == 0 ? "\nAll tests passed\n" : "\n%d check(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
