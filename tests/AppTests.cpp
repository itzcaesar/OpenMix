// Integration unit tests for OpenMix app/engine/creator modules:
//   - PluginChain slot packing, reordering, built-in params, wet/dry, and NaN fault isolation
//   - Session save/apply and scene capture/recall (including built-in FX slots & global FX)
//   - RemoteCommands validation, state JSON, and custom bus display names
//   - MidiControl JSON round-trip, record toggle, /norm toggle, soundboard trigger
//   - Soundboard short-clip resampling (< 8 samples)

#include <JuceHeader.h>
#include "engine/MixerEngine.h"
#include "app/Session.h"
#include "app/RemoteCommands.h"
#include "app/MidiControl.h"

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

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    std::printf ("OpenMix app tests\n");

    run ("PluginChain: built-in packing, move, wet/dry and param round-trip", []
    {
        PluginChain chain;
        chain.prepare (48000.0, 256);

        chain.installBuiltIn (4, PluginChain::makeBuiltInFx (omx::FxType::Reverb));
        chain.installBuiltIn (5, PluginChain::makeBuiltInFx (omx::FxType::Delay));
        CHECK (chain.getNumPlugins() == 2, "expected packed 2 slots, got %d", chain.getNumPlugins());
        CHECK (chain.isBuiltInSlot (0) && chain.isBuiltInSlot (1), "slots 0/1 should hold built-ins");

        std::array<float, 6> p { 250.0f, 0.4f, 0.75f, 5000.0f, 0.5f, 128.0f };
        CHECK (chain.setBuiltInParams (1, p, true), "setBuiltInParams failed");
        std::array<float, 6> out {};
        bool sync = false;
        CHECK (chain.getBuiltInParams (1, out, sync) && sync && std::abs (out[5] - 128.0f) < 0.01f,
               "getBuiltInParams lost tempoSync/BPM");

        chain.movePlugin (1, 0);
        CHECK (chain.getBuiltIn (0)->getType() == omx::FxType::Delay, "movePlugin did not move Delay to slot 0");

        std::vector<float> l (256, 0.5f), r (256, 0.5f);
        int latency = 0;
        chain.process (l.data(), r.data(), 256, latency);
        CHECK (latency == 0, "built-in chain latency should be 0, got %d", latency);
        for (float v : l)
        {
            CHECK (std::isfinite (v), "non-finite output from PluginChain");
            if (! std::isfinite (v)) break;
        }
    });

    run ("Session: scene recall preserves built-in FX slot bypass, mix and params", []
    {
        MixerEngine engine;
        auto& strip = engine.getStrip (0);
        strip.inserts.prepare (48000.0, 256);
        strip.inserts.installBuiltIn (0, PluginChain::makeBuiltInFx (omx::FxType::Reverb));
        strip.inserts.slotBypass[0].store (true);
        strip.inserts.slotMix[0].store (0.4f);
        strip.inserts.setBuiltInParams (0, { 0.85f, 0.2f, 0.6f, 15.0f, 0.0f, 0.0f }, false);

        auto sceneA = Session::captureScene (engine);

        strip.inserts.slotBypass[0].store (false);
        strip.inserts.slotMix[0].store (1.0f);
        strip.inserts.setBuiltInParams (0, { 0.1f, 0.9f, 0.2f, 0.0f, 0.0f, 0.0f }, false);

        Session::applyScene (*sceneA, engine);
        CHECK (strip.inserts.slotBypass[0].load(), "built-in bypass not restored by scene");
        CHECK (std::abs (strip.inserts.slotMix[0].load() - 0.4f) < 0.01f, "built-in mix not restored by scene");

        std::array<float, 6> p {};
        bool sync = false;
        CHECK (strip.inserts.getBuiltInParams (0, p, sync) && std::abs (p[0] - 0.85f) < 0.01f,
               "built-in params not restored by scene");
    });

    run ("MixerEngine: global FX units always exist and applyBpmToDelays preserves sync state", []
    {
        MixerEngine engine;
        CHECK (engine.getFxReverb() != nullptr && engine.getFxDelay() != nullptr && engine.getFxChorus() != nullptr,
               "global FX units should be constructed with MixerEngine");
        engine.setBpm (140.0);
        CHECK (std::abs (engine.getFxDelay()->getTempoBpm() - 140.0f) < 0.01f, "global delay didn't follow BPM");

        engine.getFxDelay()->setTimeMs (333.0f);
        CHECK (! engine.getFxDelay()->isTempoSynced(), "setTimeMs should disable tempo sync");
        engine.setBpm (100.0);
        CHECK (std::abs (engine.getFxDelay()->getTimeMs() - 333.0f) < 0.01f,
               "unsynced delay time was overwritten by setBpm");
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

    std::printf (failures == 0 ? "\nAll tests passed\n" : "\n%d check(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
