// Core DSP / real-time tests. Plain C++, no JUCE: build target OpenMixCoreTests, run with ctest.

#include "AudioSafety.h"
#include "Biquad.h"
#include "ClockBridge.h"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <functional>
#include <limits>
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

// Deterministic pseudo-random numbers in [-1, 1]
struct Lcg
{
    uint32_t state = 12345;
    double next() { state = state * 1664525u + 1013904223u; return (state >> 8) / double (1u << 23) - 1.0; }
};

//==============================================================================
// Clock bridge simulation: a writer and a reader running on different (drifting, jittery)
// clocks, interleaved in simulated time like two real device threads.
struct BridgeResult
{
    int underrunsAfterWarmup = 0;
    int overruns = 0;
    std::vector<float> output;   // reader output after warm-up (left channel)
};

static BridgeResult simulateBridge (double writerNominal, double writerActual, int writerBlock,
                                    double readerRate, int readerBlock, double seconds,
                                    double jitterMs, double toneHz)
{
    ClockBridge bridge;
    BridgeResult result;
    Lcg rng;

    std::vector<float> wl ((size_t) writerBlock), wr ((size_t) writerBlock);
    std::vector<float> rl ((size_t) readerBlock), rr ((size_t) readerBlock);

    double writerSched = 0.0, readerSched = 0.0, phase = 0.0;
    double nextWriter = 0.0, nextReader = 0.0;
    const double warmup = 1.0;
    int underrunsAtWarmup = -1;

    while (std::min (nextWriter, nextReader) < seconds)
    {
        if (nextWriter <= nextReader)
        {
            for (int i = 0; i < writerBlock; ++i)
            {
                // The tone is generated at the writer's *actual* rate, as real hardware would.
                wl[(size_t) i] = wr[(size_t) i] = (float) std::sin (phase);
                phase += 2.0 * std::numbers::pi * toneHz / writerActual;
            }
            const float* chans[2] { wl.data(), wr.data() };
            bridge.write (chans, 2, writerBlock, writerNominal);

            writerSched += writerBlock / writerActual;
            nextWriter = writerSched + rng.next() * jitterMs * 0.001;
        }
        else
        {
            bridge.read (rl.data(), rr.data(), readerBlock, readerRate);

            if (nextReader >= warmup)
            {
                if (underrunsAtWarmup < 0)
                    underrunsAtWarmup = bridge.underruns.load();
                result.output.insert (result.output.end(), rl.begin(), rl.end());
            }

            readerSched += readerBlock / readerRate;
            nextReader = readerSched + rng.next() * jitterMs * 0.001;
        }
    }

    result.underrunsAfterWarmup = bridge.underruns.load() - std::max (0, underrunsAtWarmup);
    result.overruns = bridge.overruns.load();
    return result;
}

static int countZeroCrossings (const std::vector<float>& x, size_t from)
{
    int n = 0;
    for (size_t i = from + 1; i < x.size(); ++i)
        if ((x[i - 1] < 0.0f) != (x[i] < 0.0f))
            ++n;
    return n;
}

static double rms (const std::vector<float>& x, size_t from)
{
    double s = 0.0;
    for (size_t i = from; i < x.size(); ++i)
        s += (double) x[i] * x[i];
    return std::sqrt (s / double (x.size() - from));
}

//==============================================================================
int main()
{
    std::printf ("OpenMix core tests\n");

    run ("ClockBridge: +200 ppm drift, jitter, 10 min, no dropouts", []
    {
        const auto r = simulateBridge (48000.0, 48000.0 * (1.0 + 200e-6), 480, 48000.0, 512, 600.0, 1.0, 1000.0);
        CHECK (r.underrunsAfterWarmup == 0, "underruns after warm-up: %d", r.underrunsAfterWarmup);
        CHECK (r.overruns == 0, "overruns: %d", r.overruns);
    });

    run ("ClockBridge: -200 ppm drift, jitter, 10 min, no dropouts", []
    {
        const auto r = simulateBridge (48000.0, 48000.0 * (1.0 - 200e-6), 441, 48000.0, 128, 600.0, 1.0, 1000.0);
        CHECK (r.underrunsAfterWarmup == 0, "underruns after warm-up: %d", r.underrunsAfterWarmup);
        CHECK (r.overruns == 0, "overruns: %d", r.overruns);
    });

    run ("ClockBridge: heavy 3 ms jitter recovers and stays stable", []
    {
        const auto r = simulateBridge (48000.0, 48000.0, 480, 48000.0, 480, 600.0, 3.0, 1000.0);
        // The adaptive margin may need a couple of underruns to learn this much jitter.
        CHECK (r.underrunsAfterWarmup <= 3, "underruns after warm-up: %d", r.underrunsAfterWarmup);
        CHECK (r.overruns == 0, "overruns: %d", r.overruns);
    });

    run ("ClockBridge: 48k -> 44.1k keeps pitch and level", []
    {
        const auto r = simulateBridge (48000.0, 48000.0, 480, 44100.0, 441, 20.0, 0.5, 1000.0);
        const size_t from = 44100 * 2;   // let the controller settle
        const double seconds = double (r.output.size() - from) / 44100.0;
        const double measuredHz = countZeroCrossings (r.output, from) / (2.0 * seconds);
        CHECK (std::abs (measuredHz - 1000.0) < 1.0, "measured %.3f Hz", measuredHz);
        const double level = rms (r.output, from);
        CHECK (std::abs (level - std::sqrt (0.5)) < 0.01, "rms %.4f", level);
        CHECK (r.underrunsAfterWarmup == 0, "underruns: %d", r.underrunsAfterWarmup);
    });

    run ("ClockBridge: 44.1k -> 48k keeps pitch", []
    {
        const auto r = simulateBridge (44100.0, 44100.0, 441, 48000.0, 512, 20.0, 0.5, 440.0);
        const size_t from = 48000 * 2;
        const double seconds = double (r.output.size() - from) / 48000.0;
        const double measuredHz = countZeroCrossings (r.output, from) / (2.0 * seconds);
        CHECK (std::abs (measuredHz - 440.0) < 0.5, "measured %.3f Hz", measuredHz);
    });

    run ("ClockBridge: silent until primed, then fades in (no click)", []
    {
        ClockBridge bridge;
        std::vector<float> ones (480, 1.0f), l (64), r (64);
        const float* chans[2] { ones.data(), ones.data() };

        bridge.read (l.data(), r.data(), 64, 48000.0);
        CHECK (l[0] == 0.0f && l[63] == 0.0f, "expected silence before any data");

        for (int i = 0; i < 4; ++i)
            bridge.write (chans, 2, 480, 48000.0);
        bridge.read (l.data(), r.data(), 64, 48000.0);
        CHECK (l[0] < 0.05f, "first sample after priming should start near 0, got %f", l[0]);
        CHECK (l[63] > l[0], "fade-in should rise");
    });

    run ("SafetyLimiter: +12 dB input never exceeds the ceiling", []
    {
        AudioSafety::SafetyLimiter lim;
        lim.prepare (48000.0);
        std::vector<float> l (48000), r (48000);
        for (size_t i = 0; i < l.size(); ++i)
            l[i] = r[i] = 4.0f * (float) std::sin (2.0 * std::numbers::pi * 100.0 * double (i) / 48000.0);

        lim.process (l.data(), r.data(), (int) l.size());
        float peak = 0.0f;
        for (float v : l) peak = std::max (peak, std::abs (v));
        CHECK (peak <= AudioSafety::SafetyLimiter::defaultCeiling + 1e-6f, "peak %f", peak);
    });

    run ("SafetyLimiter: quiet signal passes untouched", []
    {
        AudioSafety::SafetyLimiter lim;
        lim.prepare (48000.0);
        std::vector<float> l (4800), r (4800), ref (4800);
        for (size_t i = 0; i < l.size(); ++i)
            ref[i] = l[i] = r[i] = 0.5f * (float) std::sin (2.0 * std::numbers::pi * 440.0 * double (i) / 48000.0);

        const float minGain = lim.process (l.data(), r.data(), (int) l.size());
        CHECK (minGain == 1.0f, "gain reduced to %f", minGain);
        double maxErr = 0.0;
        for (size_t i = 0; i < l.size(); ++i) maxErr = std::max (maxErr, (double) std::abs (l[i] - ref[i]));
        CHECK (maxErr < 1e-7, "max error %g", maxErr);
    });

    run ("NaN/Inf detection", []
    {
        float ok[4] { 0.0f, -1.0f, 1.0e-40f /* denormal */, 3.0f };
        float nan[4] { 0.0f, std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f };
        float inf[4] { 0.0f, 0.0f, 0.0f, -std::numeric_limits<float>::infinity() };
        CHECK (! AudioSafety::containsNonFinite (ok, 4), "finite data flagged");
        CHECK (AudioSafety::containsNonFinite (nan, 4), "NaN missed");
        CHECK (AudioSafety::containsNonFinite (inf, 4), "Inf missed");
    });

    run ("DcBlocker: removes offset, keeps 1 kHz", []
    {
        AudioSafety::DcBlocker dc;
        dc.prepare (48000.0);
        std::vector<float> x (96000, 0.5f);
        dc.process (x.data(), (int) x.size(), 0);
        double mean = 0.0;
        for (size_t i = x.size() - 4800; i < x.size(); ++i) mean += x[i];
        mean /= 4800.0;
        CHECK (std::abs (mean) < 1e-3, "residual DC %g", mean);

        dc.reset();
        std::vector<float> s (96000);
        for (size_t i = 0; i < s.size(); ++i) s[i] = (float) std::sin (2.0 * std::numbers::pi * 1000.0 * double (i) / 48000.0);
        dc.process (s.data(), (int) s.size(), 0);
        const double level = rms (s, 48000);
        CHECK (std::abs (level / std::sqrt (0.5) - 1.0) < 0.001, "1 kHz level ratio %.5f", level / std::sqrt (0.5));
    });

    run ("Biquad: 0 dB peak is transparent", []
    {
        Biquad b;
        b.setPeak (48000.0, 1000.0, 0.9, 0.0);
        double maxErr = 0.0;
        for (int i = 0; i < 1000; ++i)
        {
            const float x = i == 0 ? 1.0f : 0.0f;
            maxErr = std::max (maxErr, (double) std::abs (b.process (x, 0) - x));
        }
        CHECK (maxErr < 1e-6, "max error %g", maxErr);
    });

    run ("Biquad: shelf gains are correct at DC", []
    {
        Biquad lo, hi;
        lo.setLowShelf (48000.0, 250.0, 6.0);
        hi.setHighShelf (48000.0, 3000.0, 6.0);
        float yl = 0.0f, yh = 0.0f;
        for (int i = 0; i < 48000; ++i) { yl = lo.process (1.0f, 0); yh = hi.process (1.0f, 0); }
        CHECK (std::abs (yl - std::pow (10.0f, 6.0f / 20.0f)) < 0.01f, "low shelf DC gain %f", yl);
        CHECK (std::abs (yh - 1.0f) < 0.01f, "high shelf DC gain %f", yh);
    });

    run ("Biquad: stable for the pad's full range at 44.1/48/96 kHz", []
    {
        Lcg rng;
        for (double fs : { 44100.0, 48000.0, 96000.0 })
            for (double gain : { -6.0, 6.0 })
            {
                Biquad a, b, c;
                a.setLowShelf (fs, 250.0, gain);
                b.setHighShelf (fs, 3000.0, gain);
                c.setPeak (fs, 5000.0, 0.9, std::abs (gain));
                float peak = 0.0f;
                for (int i = 0; i < (int) fs; ++i)
                {
                    const float y = c.process (b.process (a.process ((float) rng.next(), 0), 0), 0);
                    peak = std::max (peak, std::abs (y));
                }
                CHECK (std::isfinite (peak) && peak < 8.0f, "fs %.0f gain %.0f peak %f", fs, gain, peak);
            }
    });

    std::printf (failures == 0 ? "\nAll tests passed\n" : "\n%d check(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
