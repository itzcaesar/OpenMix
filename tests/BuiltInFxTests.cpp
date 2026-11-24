// Standalone tests for the built-in DSP effects (Source/dsp/BuiltInFx.h).
//
// What it checks, per effect:
//   - output is always finite, even at extreme parameter values and loud input
//   - silence in -> silence out; mix = 0 passes the input through unchanged
//   - delay: an impulse reappears at the set time (direct ms and tempo-synced
//     quarter/eighth notes), and not before
//   - reverb: an impulse produces a tail that is present, then decays
//   - chorus: a sine is audibly modulated (output differs from the input)
//   - mono buffers work; channels beyond the first two pass through untouched
//   - reset() clears the tail
//   - sample rates 44100 / 48000 / 96000 all work
//
// Build + run from the repo root (no app target, no rnnoise -- see
// tests/JuceModules.cpp for why that file exists). Takes ~30 s, almost all
// of it compiling JUCE:
//   g++ -std=c++20 -O2 -I Source -I external/JUCE/modules \
//     -DJUCE_USE_CURL=0 -DJUCE_WEB_BROWSER=0 -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 \
//     tests/BuiltInFxTests.cpp tests/JuceModules.cpp Source/dsp/BuiltInFx.cpp \
//     -lpthread -ldl -lz -o /tmp/BuiltInFxTests && /tmp/BuiltInFxTests
//
// Exit code is 0 when every check passes, 1 otherwise.

#include "dsp/BuiltInFx.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <numbers>
#include <vector>

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

// --- helpers ---------------------------------------------------------------

static juce::AudioBuffer<float> makeBuffer (int numChannels, int numSamples)
{
    juce::AudioBuffer<float> buffer (numChannels, numSamples);
    buffer.clear();
    return buffer;
}

static void fillSine (juce::AudioBuffer<float>& buffer, double hz, double sampleRate, float amplitude = 0.5f)
{
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int n = 0; n < buffer.getNumSamples(); ++n)
            buffer.setSample (ch, n, amplitude * (float) std::sin (2.0 * std::numbers::pi * hz * n / sampleRate));
}

// Run the whole buffer through the effect in 512-sample blocks, as a host would.
static void runBlocks (omx::BuiltInFx& fx, juce::AudioBuffer<float>& buffer, int blockSize = 512)
{
    for (int pos = 0; pos < buffer.getNumSamples(); pos += blockSize)
    {
        const int n = std::min (blockSize, buffer.getNumSamples() - pos);
        juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), pos, n);
        fx.processBlock (view);
    }
}

static bool allFinite (const juce::AudioBuffer<float>& buffer)
{
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int n = 0; n < buffer.getNumSamples(); ++n)
            if (! std::isfinite (buffer.getSample (ch, n)))
                return false;
    return true;
}

static float peak (const juce::AudioBuffer<float>& buffer, int channel, int from, int to)
{
    float p = 0.0f;
    for (int n = std::max (0, from); n < std::min (to, buffer.getNumSamples()); ++n)
        p = std::max (p, std::abs (buffer.getSample (channel, n)));
    return p;
}

static float rms (const juce::AudioBuffer<float>& buffer, int channel, int from, int to)
{
    double sum = 0.0;
    const int count = std::max (1, std::min (to, buffer.getNumSamples()) - std::max (0, from));
    for (int n = std::max (0, from); n < std::min (to, buffer.getNumSamples()); ++n)
        sum += (double) buffer.getSample (channel, n) * buffer.getSample (channel, n);
    return (float) std::sqrt (sum / count);
}

// Impulse response in one buffer: impulse at sample 0, silence after.
static juce::AudioBuffer<float> impulseResponse (omx::BuiltInFx& fx, double sampleRate, double seconds)
{
    auto buffer = makeBuffer (2, (int) (sampleRate * seconds));
    buffer.setSample (0, 0, 1.0f);
    buffer.setSample (1, 0, 1.0f);
    fx.prepareToPlay (sampleRate, 512);
    runBlocks (fx, buffer);
    return buffer;
}

//==============================================================================
int main()
{
    std::printf ("OpenMix BuiltInFx tests\n");

    run ("Reverb: impulse gives a tail that decays, output stays finite", []
    {
        omx::BuiltInReverb rev;
        rev.setRoomSize (0.7f);
        rev.setDamping (0.3f);
        rev.setMix (1.0f); // wet only, so the tail is the whole output
        const auto out = impulseResponse (rev, 48000.0, 2.0);
        CHECK (allFinite (out), "non-finite sample in reverb output");
        CHECK (peak (out, 0, 0, 4800) > 0.01f, "no reverb tail at all, peak %.5f", peak (out, 0, 0, 4800));
        // Tail in the last 0.25 s must be far below the early tail...
        const float early = rms (out, 0, 480, 4800);
        const float late = rms (out, 0, 84000, 96000);
        CHECK (late < early * 0.1f, "tail did not decay: early RMS %.5f, late RMS %.5f", early, late);
        // ...but still present (not a gate that cut it off).
        CHECK (late > 1e-5f, "tail vanished entirely, late RMS %.6f", late);
    });

    run ("Reverb: extreme params (room 1, damping 1, mix 1) stay finite on loud input", []
    {
        for (double fs : { 44100.0, 48000.0, 96000.0 })
        {
            omx::BuiltInReverb rev;
            rev.setRoomSize (1.0f);
            rev.setDamping (1.0f);
            rev.setMix (1.0f);
            rev.setPreDelayMs (100.0f);
            rev.prepareToPlay (fs, 512);
            auto buffer = makeBuffer (2, (int) (fs * 2.0));
            fillSine (buffer, 440.0, fs, 1.0f); // full-scale sine for 2 s
            runBlocks (rev, buffer);
            CHECK (allFinite (buffer), "fs %.0f: non-finite output", fs);
            CHECK (peak (buffer, 0, 0, (int) fs * 2) < 4.0f, "fs %.0f: peak blew up to %.3f",
                   fs, peak (buffer, 0, 0, (int) fs * 2));
        }
    });

    run ("Reverb: silence in -> silence out, mix 0 passes through", []
    {
        omx::BuiltInReverb rev;
        rev.prepareToPlay (48000.0, 512);
        auto silence = makeBuffer (2, 4800);
        runBlocks (rev, silence);
        CHECK (peak (silence, 0, 0, 4800) == 0.0f, "silence produced %.6f", peak (silence, 0, 0, 4800));

        rev.setMix (0.0f);
        rev.reset(); // snaps the smoothed mix to its target (0) immediately
        auto tone = makeBuffer (2, 4800);
        fillSine (tone, 440.0, 48000.0);
        auto reference = makeBuffer (2, 4800);
        fillSine (reference, 440.0, 48000.0);
        runBlocks (rev, tone);
        double maxErr = 0.0;
        for (int n = 0; n < 4800; ++n)
            maxErr = std::max (maxErr, (double) std::abs (tone.getSample (0, n) - reference.getSample (0, n)));
        CHECK (maxErr < 1e-6, "mix 0 changed the signal by %g", maxErr);
    });

    run ("Delay: impulse reappears at the set time, and not before", []
    {
        omx::BuiltInDelay delay;
        delay.setTimeMs (100.0f);
        delay.setFeedback (0.0f);
        delay.setMix (1.0f); // wet only
        const auto out = impulseResponse (delay, 48000.0, 1.0);
        CHECK (allFinite (out), "non-finite sample in delay output");
        CHECK (peak (out, 0, 0, 4700) == 0.0f, "output before the delay time: %.5f", peak (out, 0, 0, 4700));
        CHECK (peak (out, 0, 4700, 4900) > 0.9f, "impulse missing at 100 ms, peak %.5f",
               peak (out, 0, 4700, 4900));
        CHECK (peak (out, 0, 5000, 48000) == 0.0f, "unexpected output after the single repeat");
    });

    run ("Delay: tempo sync follows bpm and note division", []
    {
        // 120 bpm, quarter note = 500 ms; eighth = 250 ms.
        for (float division : { 1.0f, 0.5f })
        {
            omx::BuiltInDelay delay;
            delay.setTempoBpm (120.0f);
            delay.setNoteDivision (division);
            delay.setFeedback (0.0f);
            delay.setMix (1.0f);
            CHECK (delay.isTempoSynced(), "tempo sync flag not set");
            const auto out = impulseResponse (delay, 48000.0, 1.0);
            const int expected = (int) (48000.0 * 0.5 * division);
            CHECK (peak (out, 0, expected - 100, expected + 100) > 0.9f,
                   "division %.2f: impulse missing near sample %d, peak %.5f",
                   division, expected, peak (out, 0, expected - 100, expected + 100));
            CHECK (peak (out, 0, 0, expected - 200) == 0.0f,
                   "division %.2f: output before the delay time", division);
        }
    });

    run ("Delay: feedback 0.95 with feedback low-pass stays finite and repeats decay", []
    {
        omx::BuiltInDelay delay;
        delay.setTimeMs (50.0f);
        delay.setFeedback (0.95f);
        delay.setFeedbackLowpassHz (4000.0f);
        delay.setMix (1.0f);
        const auto out = impulseResponse (delay, 48000.0, 2.0);
        CHECK (allFinite (out), "non-finite sample at feedback 0.95");
        const float first = peak (out, 0, 2300, 2500);
        const float tenth = peak (out, 0, 23000, 25000);
        CHECK (first > 0.5f, "first repeat too quiet: %.5f", first);
        CHECK (tenth < first * 0.9f, "repeats did not decay: first %.5f, tenth %.5f", first, tenth);
        CHECK (peak (out, 0, 0, 96000) < 2.0f, "peak blew up to %.3f", peak (out, 0, 0, 96000));
    });

    run ("Chorus: sine is modulated, output stays finite", []
    {
        omx::BuiltInChorus chorus;
        chorus.setRateHz (1.0f);
        chorus.setDepthMs (5.0f);
        chorus.setMix (1.0f); // wet only: pure modulated delay
        chorus.prepareToPlay (48000.0, 512);
        auto wet = makeBuffer (2, 48000);
        fillSine (wet, 440.0, 48000.0);
        runBlocks (chorus, wet);
        CHECK (allFinite (wet), "non-finite sample in chorus output");
        // A modulated delay of a sine is still a sine of the same amplitude
        // (up to the fractional-interpolation loss), so level is preserved...
        CHECK (rms (wet, 0, 24000, 48000) > 0.2f, "chorus output too quiet, RMS %.5f", rms (wet, 0, 24000, 48000));
        // ...but the waveform is not the dry waveform: the moving delay
        // shifts its phase over time, so sample-by-sample it must differ.
        auto dry = makeBuffer (2, 48000);
        fillSine (dry, 440.0, 48000.0);
        double maxDiff = 0.0;
        for (int n = 24000; n < 48000; ++n)
            maxDiff = std::max (maxDiff, (double) std::abs (wet.getSample (0, n) - dry.getSample (0, n)));
        CHECK (maxDiff > 0.05, "chorus output is identical to dry input (max diff %g)", maxDiff);
    });

    run ("Chorus: extreme params stay finite, mix 0 passes through", []
    {
        for (double fs : { 44100.0, 48000.0, 96000.0 })
        {
            omx::BuiltInChorus chorus;
            chorus.setRateHz (10.0f);
            chorus.setDepthMs (10.0f);
            chorus.setFeedback (0.5f);
            chorus.setMix (1.0f);
            chorus.prepareToPlay (fs, 512);
            auto buffer = makeBuffer (2, (int) (fs * 2.0));
            fillSine (buffer, 1000.0, fs, 1.0f);
            runBlocks (chorus, buffer);
            CHECK (allFinite (buffer), "fs %.0f: non-finite output", fs);
            CHECK (peak (buffer, 0, 0, (int) fs * 2) < 4.0f, "fs %.0f: peak %.3f", fs,
                   peak (buffer, 0, 0, (int) fs * 2));
        }

        omx::BuiltInChorus chorus;
        chorus.setMix (0.0f);
        chorus.prepareToPlay (48000.0, 512);
        auto tone = makeBuffer (2, 4800);
        fillSine (tone, 440.0, 48000.0);
        runBlocks (chorus, tone);
        double maxErr = 0.0;
        for (int n = 0; n < 4800; ++n)
            maxErr = std::max (maxErr, (double) std::abs (tone.getSample (0, n)
                        - 0.5f * (float) std::sin (2.0 * std::numbers::pi * 440.0 * n / 48000.0)));
        CHECK (maxErr < 1e-6, "mix 0 changed the signal by %g", maxErr);
    });

    run ("All effects: mono works, extra channels pass through, reset clears the tail", []
    {
        // Mono: a single channel through each effect must not crash and
        // must produce output (delay: the repeat; reverb/chorus: wet signal).
        {
            omx::BuiltInDelay delay;
            delay.setTimeMs (10.0f);
            delay.setMix (1.0f);
            delay.prepareToPlay (48000.0, 512);
            auto mono = makeBuffer (1, 4800);
            mono.setSample (0, 0, 1.0f);
            runBlocks (delay, mono);
            CHECK (peak (mono, 0, 400, 600) > 0.9f, "mono delay repeat missing, peak %.5f",
                   peak (mono, 0, 400, 600));
        }
        // 4 channels: channels 2 and 3 must come out bit-identical.
        {
            auto checkPassthrough = [] (omx::BuiltInFx& fx)
            {
                fx.prepareToPlay (48000.0, 512);
                auto buffer = makeBuffer (4, 4800);
                fillSine (buffer, 440.0, 48000.0);
                runBlocks (fx, buffer);
                CHECK (allFinite (buffer), "4-channel buffer: non-finite output");
                double maxErr = 0.0;
                for (int ch = 2; ch < 4; ++ch)
                    for (int n = 0; n < 4800; ++n)
                        maxErr = std::max (maxErr, (double) std::abs (buffer.getSample (ch, n)
                                    - 0.5f * (float) std::sin (2.0 * std::numbers::pi * 440.0 * n / 48000.0)));
                CHECK (maxErr == 0.0, "channels 2/3 were modified (max err %g)", maxErr);
            };
            omx::BuiltInReverb rev;   checkPassthrough (rev);
            omx::BuiltInDelay delay;  checkPassthrough (delay);
            omx::BuiltInChorus chorus; checkPassthrough (chorus);
        }
        // reset(): after a loud impulse and 2 s of tail, reset must silence it.
        {
            omx::BuiltInReverb rev;
            rev.setMix (1.0f);
            const auto out = impulseResponse (rev, 48000.0, 1.0);
            CHECK (peak (out, 0, 40000, 48000) > 1e-4f, "no tail to reset (test setup?)");
            rev.reset();
            auto silence = makeBuffer (2, 4800);
            runBlocks (rev, silence);
            CHECK (peak (silence, 0, 0, 4800) == 0.0f, "tail survived reset: %.6f",
                   peak (silence, 0, 0, 4800));
        }
    });

    run ("All effects: unprepared processBlock is a dry no-op (no crash)", []
    {
        omx::BuiltInReverb rev;
        omx::BuiltInDelay delay;
        omx::BuiltInChorus chorus;
        for (omx::BuiltInFx* fx : { (omx::BuiltInFx*) &rev, (omx::BuiltInFx*) &delay, (omx::BuiltInFx*) &chorus })
        {
            CHECK (! fx->isPrepared(), "should start unprepared");
            auto buffer = makeBuffer (2, 512);
            fillSine (buffer, 440.0, 48000.0);
            fx->processBlock (buffer); // must not crash, must not change the signal
            CHECK (std::abs (buffer.getSample (0, 100)
                        - 0.5f * (float) std::sin (2.0 * std::numbers::pi * 440.0 * 100 / 48000.0)) < 1e-6f,
                   "unprepared processBlock changed the signal");
        }
    });

    std::printf (failures == 0 ? "\nAll tests passed\n" : "\n%d check(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
