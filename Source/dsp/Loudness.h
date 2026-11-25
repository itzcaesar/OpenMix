#pragma once

// Loudness meter following ITU-R BS.1770-4 / EBU R128: K-weighting, momentary (400 ms),
// short-term (3 s) and gated integrated loudness, in LUFS. Stereo, both channels weight 1.
// Runs on the UI thread (fed from a bus's analysis FIFO). Integrated loudness covers the last
// hour: the gating blocks also go into a 0.1 LU histogram (like libebur128), so each update
// costs a fixed ~750 steps instead of a rescan of up to 36k blocks.

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace omx
{
    // BS.1770 K-weighting for one stereo signal, any sample rate (same derivation as
    // libebur128): the "head" high shelf, then the RLB high-pass. Double precision, so
    // the 38 Hz high-pass stays exact. Shared by the meter and the bus normaliser.
    class KWeighting
    {
    public:
        void prepare (double fs)
        {
            {
                const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
                const double K = std::tan (std::numbers::pi * f0 / fs);
                const double Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
                const double a0 = 1.0 + K / Q + K * K;
                stage1 = { (Vh + Vb * K / Q + K * K) / a0, 2.0 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0,
                           2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
            }
            {
                const double f0 = 38.13547087602444, Q = 0.5003270373238773;
                const double K = std::tan (std::numbers::pi * f0 / fs);
                const double a0 = 1.0 + K / Q + K * K;
                stage2 = { 1.0, -2.0, 1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
            }
            reset();
        }

        void reset()
        {
            for (auto& ch : state)
                for (auto& st : ch)
                    st = {};
        }

        double process (double x, int channel)
        {
            return filter (stage2, state[channel][1], filter (stage1, state[channel][0], x));
        }

    private:
        struct Coeffs { double b0, b1, b2, a1, a2; };
        struct State  { double z1 = 0, z2 = 0; };

        static double filter (const Coeffs& c, State& s, double x)
        {
            const double y = c.b0 * x + s.z1;
            s.z1 = c.b1 * x - c.a1 * y + s.z2;
            s.z2 = c.b2 * x - c.a2 * y;
            return y;
        }

        Coeffs stage1 { 1, 0, 0, 0, 0 }, stage2 { 1, 0, 0, 0, 0 };
        State state[2][2] {};
    };

    class LoudnessMeter
    {
    public:
        static constexpr size_t maxBlocks = 36000;   // 1 hour of 100 ms blocks (bounded memory)

        void prepare (double sampleRate)
        {
            fs = sampleRate;
            kWeighting.prepare (fs);
            stepSamples = std::max (1, (int) std::lround (fs * 0.1));   // 100 ms
            blocks.reserve (1024);
            reset();
        }

        void reset()
        {
            kWeighting.reset();
            stepSum = 0.0;
            stepCount = 0;
            steps.assign (30, 0.0);     // last 3 s of 100 ms mean-squares
            stepsFilled = 0;
            stepWrite = 0;
            blocks.clear();
            blockWrite = 0;
            binCount.assign (numBins, 0);
            binEnergy.assign (numBins, 0.0);
            cachedIntegrated = -INFINITY;
        }

        void push (const float* left, const float* right, int numSamples)
        {
            const float* in[2] { left, right };
            for (int i = 0; i < numSamples; ++i)
            {
                double sum = 0.0;
                for (int c = 0; c < 2; ++c)
                {
                    const double y = kWeighting.process (in[c][i], c);
                    sum += y * y;
                }
                stepSum += sum;

                if (++stepCount == stepSamples)
                {
                    steps[(size_t) stepWrite] = stepSum / stepSamples;
                    stepWrite = (stepWrite + 1) % (int) steps.size();
                    stepsFilled = std::min (stepsFilled + 1, (int) steps.size());
                    stepSum = 0.0;
                    stepCount = 0;

                    if (stepsFilled >= 4)
                    {
                        const double b = meanOfLast (4);   // a 400 ms gating block, 75% overlap
                        if (blocks.size() < maxBlocks)
                            blocks.push_back (b);
                        else
                        {
                            addToHistogram (blocks[blockWrite], -1);   // the oldest block leaves the hour
                            blocks[blockWrite] = b;
                            blockWrite = (blockWrite + 1) % maxBlocks;
                        }
                        addToHistogram (b, +1);
                        cachedIntegrated = computeIntegrated();
                    }
                }
            }
        }

        double momentary() const  { return stepsFilled >= 4 ? floored (toLufs (meanOfLast (4))) : -INFINITY; }
        double shortTerm() const  { return stepsFilled >= 30 ? floored (toLufs (meanOfLast (30))) : -INFINITY; }
        double integrated() const { return cachedIntegrated; }

        // Below the -70 LUFS absolute gate a reading means nothing (dither, denormals): no signal.
        static double floored (double lufs) { return lufs > -70.0 ? lufs : -INFINITY; }

        static double toLufs (double meanSquare) { return meanSquare > 0.0 ? -0.691 + 10.0 * std::log10 (meanSquare) : -INFINITY; }

    private:
        double meanOfLast (int count) const
        {
            double s = 0.0;
            for (int k = 1; k <= count; ++k)
                s += steps[(size_t) ((stepWrite - k + (int) steps.size()) % (int) steps.size())];
            return s / count;
        }

        // Histogram of gating blocks above the -70 LUFS absolute gate, 0.1 LU per bin from
        // -70 to +5 LUFS. Each bin keeps the exact energy of its blocks, so means are exact;
        // only the relative gate's edge is resolved to 0.1 LU.
        static constexpr int numBins = 750;
        static constexpr double binLow = -70.0, binWidth = 0.1;

        void addToHistogram (double meanSquare, int sign)
        {
            const double lufs = toLufs (meanSquare);
            if (! (lufs > binLow))
                return;
            const int bin = std::clamp ((int) ((lufs - binLow) / binWidth), 0, numBins - 1);
            binCount[(size_t) bin] += sign;
            binEnergy[(size_t) bin] += sign * meanSquare;
        }

        double computeIntegrated() const
        {
            // Absolute gate at -70 LUFS (the histogram only holds blocks above it), then a
            // relative gate 10 LU below the mean of those.
            double sum = 0.0;
            long long n = 0;
            for (int i = 0; i < numBins; ++i) { sum += binEnergy[(size_t) i]; n += binCount[(size_t) i]; }
            if (n <= 0 || sum <= 0.0)
                return -INFINITY;

            const double relativeGate = toLufs (sum / (double) n) - 10.0;
            const int first = std::clamp ((int) std::ceil ((relativeGate - binLow) / binWidth), 0, numBins);
            sum = 0.0;
            n = 0;
            for (int i = first; i < numBins; ++i) { sum += binEnergy[(size_t) i]; n += binCount[(size_t) i]; }
            return n > 0 && sum > 0.0 ? toLufs (sum / (double) n) : -INFINITY;
        }

        double fs = 48000.0;
        KWeighting kWeighting;
        int stepSamples = 4800, stepCount = 0, stepsFilled = 0, stepWrite = 0;
        double stepSum = 0.0;
        std::vector<double> steps, blocks;
        std::vector<long long> binCount = std::vector<long long> (numBins, 0);
        std::vector<double> binEnergy = std::vector<double> (numBins, 0.0);
        size_t blockWrite = 0;
        double cachedIntegrated = -INFINITY;
    };
}
