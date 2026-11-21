#pragma once

// Loudness meter following ITU-R BS.1770-4 / EBU R128: K-weighting, momentary (400 ms),
// short-term (3 s) and gated integrated loudness, in LUFS. Stereo, both channels weight 1.
// Runs on the UI thread (fed from a bus's analysis FIFO); integrated keeps one value per
// 100 ms step, which is about 36k floats per hour.

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace omx
{
    class LoudnessMeter
    {
    public:
        void prepare (double sampleRate)
        {
            fs = sampleRate;
            designKWeighting();
            stepSamples = std::max (1, (int) std::lround (fs * 0.1));   // 100 ms
            reset();
        }

        void reset()
        {
            for (auto& ch : state)
                for (auto& st : ch)
                    st = {};
            stepSum = 0.0;
            stepCount = 0;
            steps.assign (30, 0.0);     // last 3 s of 100 ms mean-squares
            stepsFilled = 0;
            stepWrite = 0;
            blocks.clear();
        }

        void push (const float* left, const float* right, int numSamples)
        {
            const float* in[2] { left, right };
            for (int i = 0; i < numSamples; ++i)
            {
                double sum = 0.0;
                for (int c = 0; c < 2; ++c)
                {
                    double y = filter (stage1, state[c][0], in[c][i]);
                    y = filter (stage2, state[c][1], y);
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
                        blocks.push_back (meanOfLast (4));   // a 400 ms gating block, 75% overlap
                }
            }
        }

        double momentary() const  { return stepsFilled >= 4 ? toLufs (meanOfLast (4)) : -INFINITY; }
        double shortTerm() const  { return stepsFilled >= 30 ? toLufs (meanOfLast (30)) : -INFINITY; }

        double integrated() const
        {
            // Absolute gate at -70 LUFS, then a relative gate 10 LU below the mean of what's left.
            double sum = 0.0;
            int n = 0;
            for (double b : blocks)
                if (toLufs (b) > -70.0) { sum += b; ++n; }
            if (n == 0)
                return -INFINITY;

            const double relativeGate = toLufs (sum / n) - 10.0;
            sum = 0.0;
            n = 0;
            for (double b : blocks)
                if (toLufs (b) > -70.0 && toLufs (b) > relativeGate) { sum += b; ++n; }
            return n > 0 ? toLufs (sum / n) : -INFINITY;
        }

        static double toLufs (double meanSquare) { return meanSquare > 0.0 ? -0.691 + 10.0 * std::log10 (meanSquare) : -INFINITY; }

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

        // BS.1770 K-weighting for any sample rate (same derivation as libebur128).
        void designKWeighting()
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
        }

        double meanOfLast (int count) const
        {
            double s = 0.0;
            for (int k = 1; k <= count; ++k)
                s += steps[(size_t) ((stepWrite - k + (int) steps.size()) % (int) steps.size())];
            return s / count;
        }

        double fs = 48000.0;
        Coeffs stage1 {}, stage2 {};
        State state[2][2] {};
        int stepSamples = 4800, stepCount = 0, stepsFilled = 0, stepWrite = 0;
        double stepSum = 0.0;
        std::vector<double> steps, blocks;
    };
}
