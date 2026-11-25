// Core DSP / real-time tests. Plain C++, no JUCE: build target OpenMixCoreTests, run with ctest.

#include "dsp/AudioSafety.h"
#include "dsp/Biquad.h"
#include "engine/ClockBridge.h"
#include "engine/CpuMeter.h"
#include "engine/StripLogic.h"
#include "engine/TapTempo.h"
#include "dsp/DelayLine.h"
#include "dsp/Dynamics.h"
#include "dsp/Denoiser.h"
#include "dsp/Loudness.h"
#include "dsp/Metering.h"

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

    run ("DelayLine: exact delay, zero delay is transparent, clamps to max", []
    {
        DelayLine d;
        d.prepare (100);
        std::vector<float> l (300), r (300);
        for (size_t i = 0; i < l.size(); ++i) { l[i] = (float) i; r[i] = -(float) i; }

        // Process in odd-sized blocks to cross the ring-buffer wrap.
        for (int pos = 0; pos < 300; pos += 37)
            d.process (l.data() + pos, r.data() + pos, std::min (37, 300 - pos), 48);

        bool ok = true;
        for (int i = 0; i < 300; ++i)
        {
            const float expected = i < 48 ? 0.0f : (float) (i - 48);
            ok = ok && l[(size_t) i] == expected && r[(size_t) i] == -expected;
        }
        CHECK (ok, "48-sample delay not exact");

        DelayLine z;
        z.prepare (10);
        float a[4] { 1, 2, 3, 4 }, b[4] { 5, 6, 7, 8 };
        z.process (a, b, 4, 0);
        CHECK (a[0] == 1 && a[3] == 4 && b[2] == 7, "zero delay changed the signal");

        DelayLine c;
        c.prepare (8);
        std::vector<float> x (40), y (40);
        for (size_t i = 0; i < x.size(); ++i) x[i] = y[i] = (float) i;
        c.process (x.data(), y.data(), 40, 1000);   // asks for more than the maximum
        CHECK (x[20] == 12.0f, "expected clamp to 8 samples, got %f", x[20]);
    });

    auto sine = [] (float amplitude, double hz, int n, double fs = 48000.0)
    {
        std::vector<float> v ((size_t) n);
        for (int i = 0; i < n; ++i)
            v[(size_t) i] = amplitude * (float) std::sin (2.0 * std::numbers::pi * hz * i / fs);
        return v;
    };
    auto peakOf = [] (const std::vector<float>& v, size_t from)
    {
        float p = 0.0f;
        for (size_t i = from; i < v.size(); ++i) p = std::max (p, std::abs (v[i]));
        return p;
    };

    run ("Biquad HPF: 50 Hz cut by a 200 Hz high-pass, 1 kHz passes, DC blocked", [&]
    {
        Biquad hp;
        hp.setHighPass (48000.0, 200.0);
        auto lo = sine (0.5f, 50.0, 48000);
        for (auto& v : lo) v = hp.process (v, 0);
        CHECK (omx::toDb (peakOf (lo, 24000) / 0.5f) < -20.0f, "50 Hz through 200 Hz HPF: %+.2f dB", omx::toDb (peakOf (lo, 24000) / 0.5f));

        hp.reset();
        auto hi = sine (0.5f, 1000.0, 48000);
        for (auto& v : hi) v = hp.process (v, 0);
        CHECK (std::abs (omx::toDb (peakOf (hi, 24000) / 0.5f)) < 0.5f, "1 kHz through 200 Hz HPF: %+.2f dB", omx::toDb (peakOf (hi, 24000) / 0.5f));

        hp.reset();
        std::vector<float> dc (48000, 1.0f);
        for (auto& v : dc) v = hp.process (v, 0);
        CHECK (std::abs (dc.back()) < 1e-3f, "DC leaked through HPF: %g", dc.back());
    });

    run ("Biquad LPF: 100 Hz passes a 5 kHz low-pass, 10/20 kHz are cut", [&]
    {
        Biquad lp;
        lp.setLowPass (48000.0, 5000.0);
        auto lo = sine (0.5f, 100.0, 48000);
        for (auto& v : lo) v = lp.process (v, 0);
        CHECK (std::abs (omx::toDb (peakOf (lo, 24000) / 0.5f)) < 0.5f, "100 Hz through 5 kHz LPF: %+.2f dB", omx::toDb (peakOf (lo, 24000) / 0.5f));

        // 2nd-order Butterworth: ~-12 dB one octave above the cutoff, ~-24 dB two octaves up.
        lp.reset();
        auto hi = sine (0.5f, 10000.0, 48000);
        for (auto& v : hi) v = lp.process (v, 0);
        CHECK (omx::toDb (peakOf (hi, 24000) / 0.5f) < -12.0f, "10 kHz through 5 kHz LPF: %+.2f dB", omx::toDb (peakOf (hi, 24000) / 0.5f));

        lp.reset();
        auto top = sine (0.5f, 20000.0, 48000);
        for (auto& v : top) v = lp.process (v, 0);
        CHECK (omx::toDb (peakOf (top, 24000) / 0.5f) < -20.0f, "20 kHz through 5 kHz LPF: %+.2f dB", omx::toDb (peakOf (top, 24000) / 0.5f));
    });

    run ("Biquad HPF/LPF: stable at cutoff extremes, 44.1/48/96 kHz", []
    {
        Lcg rng;
        for (double fs : { 44100.0, 48000.0, 96000.0 })
            for (double fc : { 20.0, 1000.0, 20000.0 })
            {
                Biquad hp, lp;
                hp.setHighPass (fs, fc);
                lp.setLowPass (fs, fc);
                float peak = 0.0f;
                for (int i = 0; i < (int) fs; ++i)
                {
                    const float y = lp.process (hp.process ((float) rng.next(), 0), 0);
                    peak = std::max (peak, std::abs (y));
                }
                CHECK (std::isfinite (peak) && peak < 8.0f, "fs %.0f fc %.0f peak %f", fs, fc, peak);
            }
    });

    run ("Solo-safe: soloed or solo-safe-and-routed is audible on the monitor bus", []
    {
        using omx::stripAudibleOnMonitorBus;
        CHECK (stripAudibleOnMonitorBus (true, false, false), "soloed strip must be audible even if not routed");
        CHECK (stripAudibleOnMonitorBus (false, true, true), "solo-safe strip routed to the monitor bus must stay audible");
        CHECK (! stripAudibleOnMonitorBus (false, true, false), "solo-safe strip not routed to the monitor bus stays silent");
        CHECK (! stripAudibleOnMonitorBus (false, false, true), "plain strip is muted by someone else's solo");
        CHECK (! stripAudibleOnMonitorBus (false, false, false), "plain unrouted strip is silent");
    });

    run ("Latency: strip total is denoiser + inserts, negatives treated as 0", []
    {
        using omx::stripTotalLatencySamples;
        CHECK (stripTotalLatencySamples (960, 128) == 1088, "960 + 128 should be 1088");
        CHECK (stripTotalLatencySamples (0, 0) == 0, "no latency should be 0");
        CHECK (stripTotalLatencySamples (-5, 10) == 10, "negative denoiser latency should clamp to 0");
        CHECK (stripTotalLatencySamples (960, -3) == 960, "negative plugin latency should clamp to 0");
    });

    run ("StripPreset: defaults and copy round-trip keep every field", []
    {
        omx::StripPreset p;
        CHECK (omx::kPresetNumBuses == 6 && omx::kPresetNumSlots == 6, "preset capacity must match the engine");
        CHECK (! p.hpfOn && p.hpfHz == 80.0f && ! p.lpfOn && p.lpfHz == 18000.0f, "filter defaults wrong");
        CHECK (p.numSlots == 0 && p.duckSource == -1 && p.padY == -1.0f, "preset defaults wrong");
        CHECK (p.busOn[0] && ! p.busOn[1], "preset default routing should be A1 only");

        p.gainDb = 6.0f; p.faderDb = -3.0f; p.soloSafe = true;
        p.hpfOn = true; p.hpfHz = 120.0f; p.lpfOn = true; p.lpfHz = 12000.0f;
        p.eqOn = true; p.eqLowDb = 3.0f; p.eqMidHz = 800.0f;
        p.compThresholdDb = -24.0f; p.duckOn = true; p.duckSource = 2;
        p.padX = 0.5f; p.padY = 0.25f; p.padMode = 1;
        p.busOn[3] = true; p.sendDb[3] = -6.0f; p.sendPre[3] = true;
        p.numSlots = 2;
        p.slots[0] = { "C:\\Plugins\\Comp.vst3", "Comp", "VST3", false, 0.8f };
        p.slots[1] = { "C:\\Plugins\\Verb.vst3", "Verb", "VST3", true, 1.0f };

        const omx::StripPreset q = p;   // copy, as Session save/load and the engine store do
        CHECK (q.gainDb == 6.0f && q.faderDb == -3.0f && q.soloSafe, "level/solo-safe lost in copy");
        CHECK (q.hpfOn && q.hpfHz == 120.0f && q.lpfOn && q.lpfHz == 12000.0f, "filter settings lost in copy");
        CHECK (q.eqOn && q.eqLowDb == 3.0f && q.eqMidHz == 800.0f, "EQ lost in copy");
        CHECK (q.compThresholdDb == -24.0f && q.duckOn && q.duckSource == 2, "comp/duck lost in copy");
        CHECK (q.padX == 0.5f && q.padY == 0.25f && q.padMode == 1, "VoicePad lost in copy");
        CHECK (q.busOn[3] && q.sendDb[3] == -6.0f && q.sendPre[3], "send lost in copy");
        CHECK (q.numSlots == 2 && q.slots[0].identifier == "C:\\Plugins\\Comp.vst3" && q.slots[0].mix == 0.8f
               && q.slots[1].name == "Verb" && q.slots[1].bypass, "insert slots lost in copy");
    });

    run ("TapTempo: steady taps give the right BPM, clamped to 40..240", []
    {
        omx::TapTempo t;
        CHECK (t.getBpm() == 120.0, "default BPM should be 120, got %f", t.getBpm());
        CHECK (t.getTapCount() == 0, "no taps yet");

        t.tapAt (10.0);
        CHECK (t.getBpm() == 120.0, "one tap must not change the BPM");
        CHECK (t.getTapCount() == 1, "tap count should be 1");

        for (int i = 1; i <= 4; ++i)   // 0.5 s intervals -> 120 BPM
            t.tapAt (10.0 + 0.5 * i);
        CHECK (std::abs (t.getBpm() - 120.0) < 0.01, "0.5 s taps should be 120 BPM, got %f", t.getBpm());

        omx::TapTempo fast;
        fast.tapAt (0.0);
        for (int i = 1; i <= 4; ++i)
            fast.tapAt (0.25 * i);   // 0.25 s intervals -> 240 BPM (the clamp)
        CHECK (std::abs (fast.getBpm() - 240.0) < 0.01, "0.25 s taps should be 240 BPM, got %f", fast.getBpm());

        omx::TapTempo slow;
        slow.tapAt (0.0);
        for (int i = 1; i <= 4; ++i)
            slow.tapAt (2.0 * i);   // 2 s intervals -> 30 BPM, clamped to 40
        CHECK (std::abs (slow.getBpm() - 40.0) < 0.01, "2 s taps should clamp to 40 BPM, got %f", slow.getBpm());

        // A pause over 2 s restarts the sequence: the old slow taps must not
        // drag the new fast tempo down. The reset tap itself leaves the BPM
        // alone; the next tap (0.5 s later) sets it to 120.
        slow.tapAt (10.5);   // 2.5 s after the last tap: resets the history
        CHECK (slow.getTapCount() == 1 && std::abs (slow.getBpm() - 40.0) < 0.01,
               "the reset tap should start a fresh sequence without changing the BPM yet");
        slow.tapAt (11.0);
        CHECK (std::abs (slow.getBpm() - 120.0) < 0.01, "after a long pause the BPM should restart at 120, got %f", slow.getBpm());

        // Duplicate / out-of-order timestamps are ignored, never fatal.
        omx::TapTempo dup;
        dup.tapAt (5.0);
        dup.tapAt (5.0);
        dup.tapAt (4.0);
        CHECK (dup.getTapCount() == 1 && dup.getBpm() == 120.0, "duplicate taps changed the state");

        // setBpm clamps and clears the tap history.
        omx::TapTempo set;
        set.tapAt (0.0); set.tapAt (0.5);
        set.setBpm (300.0);
        CHECK (set.getBpm() == 240.0 && set.getTapCount() == 0, "setBpm(300) should clamp to 240 and clear taps");
        set.setBpm (10.0);
        CHECK (set.getBpm() == 40.0, "setBpm(10) should clamp to 40");

        CHECK (std::abs (omx::TapTempo::bpmFromInterval (0.5) - 120.0) < 0.01, "bpmFromInterval(0.5 s)");
        CHECK (omx::TapTempo::bpmFromInterval (0.0) == 120.0, "zero interval should give the default");
    });

    run ("CpuMeter: EMA smoothing converges, bad samples are ignored", []
    {
        CHECK (std::abs (omx::CpuMeter::smooth (0.0f, 50.0f) - 5.0f) < 1e-6f, "one EMA step from 0 at 50%% should be 5");
        CHECK (std::abs (omx::CpuMeter::smooth (50.0f, 50.0f) - 50.0f) < 1e-6f, "EMA at the target should stay there");

        omx::CpuMeter m;
        CHECK (m.getPercent() == 0.0f, "fresh meter should read 0");
        for (int i = 0; i < 200; ++i)
            m.addSample (0.005, 0.010);   // half of real time -> 50%
        CHECK (std::abs (m.getPercent() - 50.0f) < 0.5f, "meter should converge to 50%%, got %f", m.getPercent());

        // Overload reads over 100%, so it stays visible.
        omx::CpuMeter over;
        for (int i = 0; i < 200; ++i)
            over.addSample (0.020, 0.010);
        CHECK (std::abs (over.getPercent() - 200.0f) < 1.0f, "2x real time should read ~200%%, got %f", over.getPercent());

        // A zero buffer duration (or negative time) is not a sample.
        omx::CpuMeter bad;
        bad.addSample (0.005, 0.0);
        bad.addSample (-0.001, 0.010);
        CHECK (bad.getPercent() == 0.0f, "bad samples changed the meter");

        bad.addSample (0.001, 0.010);
        bad.reset();
        CHECK (bad.getPercent() == 0.0f, "reset should return to 0");
    });

    run ("Bus groups: slot-based, never name-based (custom names cannot move a bus)", []
    {
        using omx::isVirtualBusSlot;
        using omx::firstSlotInGroup;
        for (int b = 0; b < 3; ++b)
            CHECK (! isVirtualBusSlot (b), "slot %d should be Hardware", b);
        for (int b = 3; b < 6; ++b)
            CHECK (isVirtualBusSlot (b), "slot %d should be Virtual", b);
        CHECK (! isVirtualBusSlot (-1) && isVirtualBusSlot (99), "out-of-range slots classify by index, not by name");
        CHECK (firstSlotInGroup (false) == 0 && firstSlotInGroup (true) == 3, "group starts should be 0 and 3");
    });

    run ("StripPreset: built-in FX slot and FX sends survive a copy round-trip", []
    {
        CHECK (omx::kNumFxSends == 3 && omx::kFxReverb == 0 && omx::kFxDelay == 1 && omx::kFxChorus == 2,
               "FX send indices must be Reverb 0 / Delay 1 / Chorus 2");

        omx::StripPreset p;
        p.fxSend[omx::kFxReverb] = 0.5f;
        p.fxSend[omx::kFxDelay] = 0.25f;
        p.fxSend[omx::kFxChorus] = 0.0f;

        p.numSlots = 2;
        p.slots[0] = { "C:\\Plugins\\Comp.vst3", "Comp", "VST3", false, 0.8f };
        p.slots[1].isBuiltIn = true;
        p.slots[1].fxType = "Delay";
        p.slots[1].name = "Delay";
        p.slots[1].fxTempoSync = true;
        p.slots[1].fxParams = { 0.0f, 0.35f, 0.3f, 8000.0f, 0.5f, 132.0f };   // time unused when synced
        p.slots[1].bypass = true;
        p.slots[1].mix = 0.9f;

        const omx::StripPreset q = p;
        CHECK (q.fxSend[omx::kFxReverb] == 0.5f && q.fxSend[omx::kFxDelay] == 0.25f && q.fxSend[omx::kFxChorus] == 0.0f,
               "FX sends lost in copy");
        CHECK (q.numSlots == 2 && ! q.slots[0].isBuiltIn && q.slots[0].identifier == "C:\\Plugins\\Comp.vst3",
               "VST slot lost in copy");
        CHECK (q.slots[1].isBuiltIn && q.slots[1].fxType == "Delay" && q.slots[1].fxTempoSync,
               "built-in slot lost its type / tempo sync in copy");
        CHECK (q.slots[1].fxParams[1] == 0.35f && q.slots[1].fxParams[4] == 0.5f && q.slots[1].fxParams[5] == 132.0f,
               "built-in delay params lost in copy");
        CHECK (q.slots[1].bypass && q.slots[1].mix == 0.9f, "built-in slot bypass/mix lost in copy");
    });

    run ("Gate: open on signal, closes to the range on room noise", [&]
    {
        omx::Gate gate;
        gate.prepare (48000.0);
        gate.thresholdDb = -40.0f;
        gate.rangeDb = -40.0f;

        auto loud = sine (0.5f, 300.0, 48000), loudR = loud;
        gate.process (loud.data(), loudR.data(), (int) loud.size());
        CHECK (peakOf (loud, 24000) > 0.49f, "loud signal attenuated: %f", peakOf (loud, 24000));

        // -60 dBFS room noise. The gate needs ~0.35 s to decide the loud part is over (detector
        // release + 80 ms hold), then fades over ~120 ms, so measure after 1.5 s.
        auto quiet = sine (0.001f, 300.0, 96000), quietR = quiet;
        gate.process (quiet.data(), quietR.data(), (int) quiet.size());
        const float expected = 0.001f * omx::toGain (-40.0f);
        CHECK (peakOf (quiet, 72000) < expected * 1.2f, "room noise not reduced: %g (expected ~%g)", peakOf (quiet, 72000), expected);
    });

    run ("Compressor: static curve and make-up gain", []
    {
        using C = omx::Compressor;
        CHECK (C::staticReductionDb (-40.0f, -18.0f, 4.0f) == 0.0f, "below the knee should not compress");
        const float above = C::staticReductionDb (-6.0f, -18.0f, 4.0f);   // 12 dB over, 4:1 -> 9 dB
        CHECK (std::abs (above - 9.0f) < 0.01f, "12 dB over at 4:1 reduced by %f dB", above);
        const float knee = C::staticReductionDb (-18.0f, -18.0f, 4.0f);   // at threshold, inside the knee
        CHECK (knee > 0.0f && knee < 1.0f, "soft knee at threshold: %f", knee);

        C comp;
        comp.thresholdDb = -20.0f;
        comp.ratio = 2.0f;
        CHECK (std::abs (comp.makeupDb() - 5.0f) < 0.01f, "auto make-up %f dB", comp.makeupDb());
    });

    run ("EQ: flat at 0 dB, low shelf boosts the lows", [&]
    {
        omx::Eq3 eq;
        eq.prepare (48000.0);
        auto l = sine (0.5f, 1000.0, 9600), r = l, ref = l;
        eq.process (l.data(), r.data(), (int) l.size());
        double maxErr = 0.0;
        for (size_t i = 0; i < l.size(); ++i) maxErr = std::max (maxErr, (double) std::abs (l[i] - ref[i]));
        CHECK (maxErr < 1e-4, "flat EQ changed the signal by %g", maxErr);

        eq.lowDb = 6.0f;
        eq.prepare (48000.0);
        auto lo = sine (0.25f, 50.0, 48000), loR = lo;
        eq.process (lo.data(), loR.data(), (int) lo.size());
        const float gainDb = omx::toDb (peakOf (lo, 24000) / 0.25f);
        CHECK (std::abs (gainDb - 6.0f) < 0.7f, "50 Hz with +6 dB low shelf: %+.2f dB", gainDb);
    });

    run ("De-esser: cuts 7 kHz sibilance, leaves 300 Hz alone", [&]
    {
        omx::DeEsser ds;
        ds.prepare (48000.0);
        ds.thresholdDb = -30.0f;
        ds.maxReductionDb = 10.0f;

        auto ess = sine (0.5f, 7000.0, 48000), essR = ess;
        const float minGain = ds.process (ess.data(), essR.data(), (int) ess.size());
        CHECK (minGain < omx::toGain (-3.0f), "loud 7 kHz should be reduced, min gain %f", minGain);

        ds.reset();
        auto voice = sine (0.5f, 300.0, 48000), voiceR = voice;
        ds.process (voice.data(), voiceR.data(), (int) voice.size());
        const float lowDb = omx::toDb (peakOf (voice, 24000) / 0.5f);
        CHECK (std::abs (lowDb) < 0.5f, "300 Hz changed by %+.2f dB", lowDb);
    });

    run ("Denoiser: exactly 960 samples of latency at 48 kHz, resampled path reports its delay", [&]
    {
        omx::Denoiser dn;
        dn.prepare (48000.0);
        CHECK (dn.isAvailable(), "should run at 48 kHz");

        std::vector<float> l (2400, 0.0f), r (2400, 0.0f);
        l[100] = r[100] = 0.5f;
        dn.process (l.data(), r.data(), (int) l.size(), 0.0f, false);   // amount 0 = delayed dry only
        CHECK (dn.getLatency() == omx::Denoiser::latency48k, "48 kHz latency %d", dn.getLatency());
        CHECK (std::abs (l[100 + omx::Denoiser::latency48k] - 0.5f) < 1e-6f, "impulse not delayed by 960: %f", l[1060]);
        CHECK (std::abs (l[100]) < 1e-9f, "output before the latency should be silent");

        std::vector<float> s (4800, 0.0f), s2 (4800, 0.0f);
        dn.reset();
        dn.process (s.data(), s2.data(), (int) s.size(), 1.0f, false);   // silence in, silence out
        CHECK (peakOf (s, 0) < 1e-3f, "silence produced %g", peakOf (s, 0));

        omx::Denoiser rs;
        rs.prepare (44100.0);
        CHECK (rs.isAvailable() && rs.isResampling(), "should resample at 44.1 kHz");
        std::vector<float> il (8820, 0.0f), ir (8820, 0.0f);
        il[200] = ir[200] = 0.5f;
        for (int i = 0; i < (int) il.size(); i += 256)   // device-sized blocks
            rs.process (il.data() + i, ir.data() + i, std::min (256, (int) il.size() - i), 0.0f, false);
        int peakAt = 0;
        for (int i = 1; i < (int) il.size(); ++i)
            if (std::abs (il[(size_t) i]) > std::abs (il[(size_t) peakAt])) peakAt = i;
        CHECK (std::abs (peakAt - (200 + rs.getLatency())) <= 2, "44.1k impulse at %d, reported latency %d", peakAt - 200, rs.getLatency());
        CHECK (std::abs (il[(size_t) peakAt]) > 0.35f, "44.1k impulse lost level: %f", il[(size_t) peakAt]);
    });

    run ("Loudness: EBU reference, -23 dBFS 1 kHz stereo sine reads -23 LUFS", [&]
    {
        omx::LoudnessMeter m;
        m.prepare (48000.0);
        const float amp = std::pow (10.0f, -23.0f / 20.0f);   // peak -23 dBFS (EBU Tech 3341 case 1)
        auto tone = sine (amp, 1000.0, 48000 * 5);
        for (size_t i = 0; i < tone.size(); i += 480)
            m.push (tone.data() + i, tone.data() + i, 480);
        const double lufs = m.integrated();
        CHECK (std::abs (lufs - (-23.0)) < 0.1, "integrated %.2f LUFS", lufs);
        CHECK (std::abs (m.shortTerm() - (-23.0)) < 0.1, "short-term %.2f LUFS", m.shortTerm());
    });

    run ("RMS meter: sine reads -3 dB below its peak, silence reads 0", [&]
    {
        // A 0.5 peak sine has RMS 0.5/sqrt(2) = -9.03 dBFS; a 1.0 peak sine has RMS -3.01 dBFS.
        auto tone = sine (1.0f, 1000.0, 48000);
        omx::RmsMeter m;
        m.prepare (48000.0);
        float level = 0.0f;
        for (size_t i = 0; i < tone.size(); i += 480)
            level = m.push (tone.data() + i, 480);
        CHECK (std::abs (20.0 * std::log10 (level) - (-3.01)) < 0.5, "sine RMS %.2f dBFS", 20.0 * std::log10 (level));
        CHECK (std::abs (omx::blockRms (tone.data(), (int) tone.size()) - 0.7071f) < 0.01f, "block RMS %f", omx::blockRms (tone.data(), (int) tone.size()));

        omx::RmsMeter quiet;
        quiet.prepare (48000.0);
        std::vector<float> silence (4800, 0.0f);
        CHECK (quiet.push (silence.data(), (int) silence.size()) == 0.0f, "silence should read 0");
    });

    run ("Loudness normaliser: quiet bus is turned up, loud bus down, clamped to +/-12 dB", []
    {
        // A -26 LUFS bus (RMS ~0.05) normalised to -14 LUFS wants +12 dB, clamped there.
        omx::LoudnessNormaliser norm;
        norm.prepare (48000.0);
        float gain = 1.0f;
        for (int i = 0; i < 200; ++i)   // ~2 s of blocks, let the 0.5 s smoothing settle
            gain = norm.process (0.05f, -14.0f, 480);
        CHECK (std::abs (20.0 * std::log10 (gain) - 12.0) < 0.5, "quiet bus gain %+.2f dB", 20.0 * std::log10 (gain));

        // A -2 LUFS bus (RMS ~0.9) normalised to -14 wants -12 dB, clamped there.
        omx::LoudnessNormaliser loud;
        loud.prepare (48000.0);
        for (int i = 0; i < 200; ++i)
            gain = loud.process (0.9f, -14.0f, 480);
        CHECK (std::abs (20.0 * std::log10 (gain) - (-12.0)) < 0.5, "loud bus gain %+.2f dB", 20.0 * std::log10 (gain));

        // Silence holds the last gain instead of turning the noise floor up.
        const float held = loud.process (0.0f, -14.0f, 480);
        CHECK (std::abs (held - gain) < 0.01f, "silence changed the gain to %f", held);

        // reset() clears targetGain too, so silence right after reset stays at unity.
        loud.reset();
        const float afterReset = loud.process (0.0f, -14.0f, 480);
        CHECK (std::abs (afterReset - 1.0f) < 1e-6f, "reset left stale targetGain (%f)", afterReset);

        // A bus already at target stays at unity.
        omx::LoudnessNormaliser flat;
        flat.prepare (48000.0);
        const float rmsAtMinus14 = (float) std::pow (10.0, (-14.0 + 0.691) / 20.0);
        for (int i = 0; i < 200; ++i)
            gain = flat.process (rmsAtMinus14, -14.0f, 480);
        CHECK (std::abs (gain - 1.0f) < 0.05f, "on-target bus gain %f", gain);
    });

    run ("SafetyLimiter: sanitises NaN/Inf inputs without propagating them", []
    {
        AudioSafety::SafetyLimiter lim;
        lim.prepare (48000.0);
        float l[4] { 0.5f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -0.5f };
        float r[4] { 0.5f, -std::numeric_limits<float>::infinity(), 0.25f, std::numeric_limits<float>::quiet_NaN() };
        lim.process (l, r, 4);
        CHECK (! AudioSafety::containsNonFinite (l, 4) && ! AudioSafety::containsNonFinite (r, 4),
               "SafetyLimiter propagated NaN/Inf");
    });

    run ("DelayLine: delay change crossfades instead of jumping", []
    {
        DelayLine d;
        d.prepare (1000);
        std::vector<float> l (512, 1.0f), r (512, 1.0f);
        d.process (l.data(), r.data(), 512, 0);
        // Switch delay from 0 to 100 on an all-ones signal: a jump would output 0 at i=0,
        // whereas crossfading starts from the old delay's sample (1.0) and blends smoothly.
        std::vector<float> l2 (128, 1.0f), r2 (128, 1.0f);
        d.process (l2.data(), r2.data(), 128, 100);
        CHECK (std::abs (l2[0] - 1.0f) < 1e-5f, "delay transition clicked at sample 0: %f", l2[0]);
    });

    run ("Biquad: extreme frequencies above Nyquist are clamped and stay stable", []
    {
        Biquad p, ls, hs, bp;
        p.setPeak (44100.0, 30000.0, 1.0, 6.0);
        ls.setLowShelf (44100.0, 30000.0, 6.0);
        hs.setHighShelf (44100.0, 30000.0, 6.0);
        bp.setBandPass (44100.0, 30000.0, 1.4);
        float x = 1.0f;
        for (int i = 0; i < 4096; ++i)
        {
            x = bp.process (hs.process (ls.process (p.process (i == 0 ? 0.5f : 0.0f, 0), 0), 0), 0);
            CHECK (std::isfinite (x), "biquad went non-finite at sample %d", i);
            if (! std::isfinite (x)) break;
        }
    });

    std::printf (failures == 0 ? "\nAll tests passed\n" : "\n%d check(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
