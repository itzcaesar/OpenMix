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
