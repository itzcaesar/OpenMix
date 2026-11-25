#include "ui/Panels.h"
#include "ui/Theme.h"
#include "dsp/Biquad.h"
#include "dsp/Dynamics.h"

#include <array>
#include <cmath>
#include <numbers>

juce::String processorShortName (Processor p)
{
    switch (p)
    {
        case Processor::Denoise: return "NR";
        case Processor::Gate:    return "Gate";
        case Processor::Eq:      return "EQ";
        case Processor::Comp:    return "Comp";
        case Processor::DeEss:   return "DS";
        case Processor::Duck:    return "Duck";
    }
    return {};
}

juce::String processorName (Processor p)
{
    switch (p)
    {
        case Processor::Denoise: return "Noise removal";
        case Processor::Gate:    return "Noise gate";
        case Processor::Eq:      return "EQ";
        case Processor::Comp:    return "Compressor";
        case Processor::DeEss:   return "De-esser";
        case Processor::Duck:    return "Ducking";
    }
    return {};
}

std::atomic<bool>& processorOn (ProcessingParams& fx, Processor p)
{
    switch (p)
    {
        case Processor::Denoise: return fx.denoiseOn;
        case Processor::Gate:    return fx.gateOn;
        case Processor::Eq:      return fx.eqOn;
        case Processor::Comp:    return fx.compOn;
        case Processor::DeEss:   return fx.deessOn;
        case Processor::Duck:    return fx.duckOn;
    }
    return fx.gateOn;
}

namespace
{
    struct OverviewItem
    {
        juce::String label;
        juce::String value;
        bool highlight = false;
    };

    float freqToX (float hz, float x0, float w, float fMin, float fMax) noexcept
    {
        const float norm = std::log (std::clamp (hz, fMin, fMax) / fMin) / std::log (fMax / fMin);
        return x0 + norm * w;
    }

    float xToFreq (float x, float x0, float w, float fMin, float fMax) noexcept
    {
        const float norm = std::clamp ((x - x0) / std::max (1.0f, w), 0.0f, 1.0f);
        return fMin * std::pow (fMax / fMin, norm);
    }

    float dbToY (float db, float y0, float h, float dbMin, float dbMax) noexcept
    {
        const float norm = std::clamp ((db - dbMin) / std::max (0.01f, dbMax - dbMin), 0.0f, 1.0f);
        return y0 + (1.0f - norm) * h;
    }

    float yToDb (float y, float y0, float h, float dbMin, float dbMax) noexcept
    {
        const float norm = std::clamp (1.0f - (y - y0) / std::max (1.0f, h), 0.0f, 1.0f);
        return dbMin + norm * (dbMax - dbMin);
    }

    float linearToX (float v, float x0, float w, float vMin, float vMax) noexcept
    {
        const float norm = std::clamp ((v - vMin) / std::max (0.01f, vMax - vMin), 0.0f, 1.0f);
        return x0 + norm * w;
    }

    float xToLinear (float x, float x0, float w, float vMin, float vMax) noexcept
    {
        const float norm = std::clamp ((x - x0) / std::max (1.0f, w), 0.0f, 1.0f);
        return vMin + norm * (vMax - vMin);
    }

    float biquadDbAt (const Biquad& q, double freqHz, double fs) noexcept
    {
        const double rate = fs > 0.0 ? fs : 48000.0;
        const double f = std::clamp (freqHz, 10.0, rate * 0.45);
        const double w = 2.0 * std::numbers::pi * f / rate;
        const double cw = std::cos (w);
        const double c2w = std::cos (2.0 * w);
        const double b0 = q.b0, b1 = q.b1, b2 = q.b2;
        const double a1 = q.a1, a2 = q.a2;
        const double num = b0 * b0 + b1 * b1 + b2 * b2 + 2.0 * (b0 * b1 + b1 * b2) * cw + 2.0 * b0 * b2 * c2w;
        const double den = 1.0 + a1 * a1 + a2 * a2 + 2.0 * (a1 + a1 * a2) * cw + 2.0 * a2 * c2w;
        const double mag2 = den > 1.0e-18 ? std::max (num / den, 1.0e-12) : 1.0e-12;
        return (float) (10.0 * std::log10 (mag2));
    }

    juce::String formatHzShort (float hz)
    {
        if (hz >= 1000.0f)
            return juce::String (hz / 1000.0f, hz >= 10000.0f ? 0 : 1) + "k";
        return juce::String (juce::roundToInt (hz));
    }

    juce::String formatSignedDb (float db)
    {
        if (std::abs (db) < 0.05f)
            return "0.0";
        return (db > 0.0f ? "+" : "") + juce::String (db, 1);
    }

    juce::Rectangle<int> drawVisualFrame (juce::Graphics& g, juce::Rectangle<int> bounds,
                                          std::initializer_list<OverviewItem> items, bool active)
    {
        const auto& t = Theme::p();
        const auto outer = bounds.toFloat();
        g.setColour (t.meterWell);
        g.fillRoundedRectangle (outer, Theme::controlRadius);
        g.setColour (t.line);
        g.drawRoundedRectangle (outer.reduced (0.5f), Theme::controlRadius, 1.0f);

        auto overview = bounds.removeFromTop (24).reduced (8, 0);
        g.setColour (t.line);
        g.fillRect (bounds.getX() + 1, bounds.getY(), bounds.getWidth() - 2, 1);

        const int badgeW = 54;
        auto badgeArea = overview.removeFromRight (badgeW);
        const auto badgeRect = badgeArea.withSizeKeepingCentre (50, 15).toFloat();
        g.setColour (active ? t.accent.withAlpha (0.18f) : t.raised.withAlpha (0.7f));
        g.fillRoundedRectangle (badgeRect, 3.0f);
        g.setColour (active ? t.accent : t.textFaint);
        g.setFont (Theme::ui (8.5f, true).withExtraKerningFactor (0.04f));
        g.drawText (active ? "ACTIVE" : "BYPASS", badgeRect, juce::Justification::centred);

        const int count = (int) items.size();
        if (count > 0 && overview.getWidth() > 40)
        {
            const int cellW = overview.getWidth() / count;
            for (const auto& item : items)
            {
                auto cell = overview.removeFromLeft (cellW);
                const int lblW = juce::jmin (cell.getWidth() / 2,
                                             juce::GlyphArrangement::getStringWidthInt (Theme::ui (8.5f, true), item.label) + 4);
                g.setFont (Theme::ui (8.5f, true));
                g.setColour (t.textFaint);
                g.drawText (item.label, cell.removeFromLeft (lblW), juce::Justification::centredLeft, true);

                g.setFont (Theme::mono (10.0f));
                g.setColour (! active ? t.textFaint : (item.highlight ? t.accent : t.text));
                g.drawText (item.value, cell, juce::Justification::centredLeft, true);
            }
        }

        return bounds.reduced (10, 7);
    }

    void drawNodeHandle (juce::Graphics& g, juce::Point<float> pt, const juce::String& tag,
                         bool active, bool hovered, bool dragging, bool diamond = false)
    {
        const auto& t = Theme::p();
        const auto col = active ? t.accent : t.textDim;
        const float r = dragging ? 6.5f : (hovered ? 6.0f : 5.0f);

        if (hovered || dragging)
        {
            g.setColour (col.withAlpha (0.26f));
            g.fillEllipse (pt.x - (r + 3.5f), pt.y - (r + 3.5f), (r + 3.5f) * 2.0f, (r + 3.5f) * 2.0f);
        }

        if (diamond)
        {
            juce::Path d;
            d.startNewSubPath (pt.x, pt.y - r);
            d.lineTo (pt.x + r, pt.y);
            d.lineTo (pt.x, pt.y + r);
            d.lineTo (pt.x - r, pt.y);
            d.closeSubPath();
            g.setColour (dragging ? col : t.panel);
            g.fillPath (d);
            g.setColour (col);
            g.strokePath (d, juce::PathStrokeType (1.5f));
        }
        else
        {
            g.setColour (dragging ? col : t.panel);
            g.fillEllipse (pt.x - r, pt.y - r, r * 2.0f, r * 2.0f);
            g.setColour (col);
            g.drawEllipse (pt.x - r, pt.y - r, r * 2.0f, r * 2.0f, 1.6f);
        }

        if (tag.isNotEmpty())
        {
            g.setFont (Theme::ui (8.0f, true));
            g.setColour (dragging ? t.onAccent : (active ? t.text : t.textDim));
            g.drawText (tag, juce::Rectangle<float> (pt.x - 10.0f, pt.y - r - 12.0f, 20.0f, 11.0f),
                        juce::Justification::centred);
        }
    }

    void drawFloatingTag (juce::Graphics& g, juce::Rectangle<float> plot, juce::Point<float> anchor,
                          const juce::String& text)
    {
        const auto& t = Theme::p();
        const auto font = Theme::mono (10.0f);
        const int w = juce::GlyphArrangement::getStringWidthInt (font, text) + 10;
        const int h = 16;
        float x = anchor.x + 10.0f;
        if (x + (float) w > plot.getRight())
            x = anchor.x - 10.0f - (float) w;
        float y = anchor.y - 22.0f;
        if (y < plot.getY())
            y = anchor.y + 8.0f;

        const juce::Rectangle<float> box (x, y, (float) w, (float) h);
        g.setColour (t.panel.withAlpha (0.94f));
        g.fillRoundedRectangle (box, 3.0f);
        g.setColour (t.accent);
        g.drawRoundedRectangle (box, 3.0f, 1.0f);
        g.setFont (font);
        g.setColour (t.text);
        g.drawText (text, box, juce::Justification::centred);
    }

    void drawRightReductionBar (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title,
                                float reductionDb, float maxDb, bool active, const juce::Colour& barCol)
    {
        const auto& t = Theme::p();
        g.setFont (Theme::ui (8.0f, true));
        g.setColour (t.textFaint);
        g.drawText (title, area.removeFromTop (12), juce::Justification::centred);

        auto readout = area.removeFromBottom (14);
        auto track = area.reduced (12, 3).toFloat();
        g.setColour (t.well);
        g.fillRoundedRectangle (track, 2.0f);
        g.setColour (t.line);
        g.drawRoundedRectangle (track.reduced (0.5f), 2.0f, 1.0f);

        const float frac = juce::jlimit (0.0f, 1.0f, reductionDb / std::max (1.0f, maxDb));
        if (frac > 0.005f)
        {
            auto inner = track.reduced (1.0f);
            g.setColour (active ? barCol : barCol.withAlpha (0.4f));
            g.fillRoundedRectangle (inner.withHeight (inner.getHeight() * frac), 1.5f);
        }

        g.setFont (Theme::mono (9.5f));
        g.setColour (active && reductionDb > 0.1f ? barCol : t.textFaint);
        g.drawText (reductionDb < 0.05f ? juce::String ("0.0") : "-" + juce::String (reductionDb, 1),
                    readout, juce::Justification::centred);
    }

    //==========================================================================
    // Interactive EQ visual (RBJ biquads + draggable band nodes + overview)

    class EqVisual final : public juce::Component
    {
    public:
        EqVisual (MixerEngine& e, ProcessingParams& p, bool hasFilters, std::function<void()> onEdit)
            : engine (e), fx (p), showFilters (hasFilters), notifyEdit (std::move (onEdit))
        {
            makeClickable (*this);
            setTooltip ("Drag Low, Mid, High (and active HPF/LPF) handles directly on the curve. Double-click a handle to reset.");
        }

        void paint (juce::Graphics& g) override
        {
            const auto& t = Theme::p();
            const bool active = fx.eqOn.load();
            const bool hpfOn = showFilters && fx.hpfOn.load();
            const bool lpfOn = showFilters && fx.lpfOn.load();
            const float lowDb = fx.eqLowDb.load();
            const float midDb = fx.eqMidDb.load();
            const float midHz = fx.eqMidHz.load();
            const float highDb = fx.eqHighDb.load();
            const float hpfHz = fx.hpfHz.load();
            const float lpfHz = fx.lpfHz.load();

            juce::Rectangle<int> inner;
            if (showFilters)
            {
                inner = drawVisualFrame (g, getLocalBounds(),
                    {
                        { "HPF",  hpfOn ? formatHzShort (hpfHz) + " Hz" : "Off", hpfOn },
                        { "LOW",  formatSignedDb (lowDb),  std::abs (lowDb) >= 0.1f },
                        { "MID",  formatSignedDb (midDb) + "@" + formatHzShort (midHz), std::abs (midDb) >= 0.1f },
                        { "HIGH", formatSignedDb (highDb), std::abs (highDb) >= 0.1f },
                        { "LPF",  lpfOn ? formatHzShort (lpfHz) + " Hz" : "Off", lpfOn }
                    }, active || hpfOn || lpfOn);
            }
            else
            {
                inner = drawVisualFrame (g, getLocalBounds(),
                    {
                        { "LOW",  formatSignedDb (lowDb) + " dB",  std::abs (lowDb) >= 0.1f },
                        { "MID",  formatSignedDb (midDb) + " @ " + formatHzShort (midHz), std::abs (midDb) >= 0.1f },
                        { "HIGH", formatSignedDb (highDb) + " dB", std::abs (highDb) >= 0.1f }
                    }, active);
            }

            inner.removeFromBottom (13);   // X frequency axis labels
            inner.removeFromLeft (24);     // Y dB axis labels
            plot = inner.toFloat();
            if (plot.getWidth() < 40.0f || plot.getHeight() < 30.0f)
                return;

            // Grid: frequency verticals
            const float fGrid[] { 30.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f };
            for (const float f : fGrid)
            {
                const float x = freqToX (f, plot.getX(), plot.getWidth(), kFMin, kFMax);
                g.setColour (t.line.withAlpha (f == 100.0f || f == 1000.0f || f == 10000.0f ? 0.55f : 0.25f));
                g.fillRect (x, plot.getY(), 1.0f, plot.getHeight());
            }
            const std::pair<float, const char*> fLabels[] {
                { 50.0f, "50" }, { 100.0f, "100" }, { 500.0f, "500" },
                { 1000.0f, "1k" }, { 5000.0f, "5k" }, { 10000.0f, "10k" }
            };
            g.setFont (Theme::mono (8.5f));
            g.setColour (t.textFaint);
            for (const auto& [f, lbl] : fLabels)
            {
                const float x = freqToX (f, plot.getX(), plot.getWidth(), kFMin, kFMax);
                g.drawText (lbl, juce::Rectangle<float> (x - 16.0f, plot.getBottom() + 2.0f, 32.0f, 11.0f),
                            juce::Justification::centred);
            }

            // Grid: dB horizontals
            for (const float db : { -12.0f, -6.0f, 0.0f, 6.0f, 12.0f })
            {
                const float y = dbToY (db, plot.getY(), plot.getHeight(), kDbMin, kDbMax);
                g.setColour (db == 0.0f ? t.controlEdge.withAlpha (0.55f) : t.line.withAlpha (0.3f));
                g.fillRect (plot.getX(), y, plot.getWidth(), 1.0f);
                if (db == -12.0f || db == 0.0f || db == 12.0f)
                {
                    g.setColour (t.textFaint);
                    g.drawText (db > 0 ? "+12" : db < 0 ? "-12" : "0",
                                juce::Rectangle<float> (plot.getX() - 25.0f, y - 6.0f, 22.0f, 12.0f),
                                juce::Justification::centredRight);
                }
            }

            const double sr = engine.getSampleRate() > 0.0 ? engine.getSampleRate() : 48000.0;
            Biquad qLow, qMid, qHigh, qHpf, qLpf;
            qLow.setLowShelf (sr, 120.0, lowDb);
            qMid.setPeak (sr, std::clamp (midHz, 100.0f, 10000.0f), 1.0, midDb);
            qHigh.setHighShelf (sr, 8000.0, highDb);
            if (hpfOn) qHpf.setHighPass (sr, hpfHz);
            if (lpfOn) qLpf.setLowPass (sr, lpfHz);

            const int steps = std::clamp ((int) plot.getWidth(), 80, 260);
            const float zeroY = dbToY (0.0f, plot.getY(), plot.getHeight(), kDbMin, kDbMax);
            juce::Path totalCurve, fillArea, midBandCurve;

            for (int i = 0; i <= steps; ++i)
            {
                const float frac = (float) i / (float) steps;
                const float x = plot.getX() + frac * plot.getWidth();
                const float hz = kFMin * std::pow (kFMax / kFMin, frac);

                const float midPart = biquadDbAt (qMid, hz, sr);
                float totalDb = (active ? (biquadDbAt (qLow, hz, sr) + midPart + biquadDbAt (qHigh, hz, sr)) : 0.0f);
                if (hpfOn) totalDb += biquadDbAt (qHpf, hz, sr);
                if (lpfOn) totalDb += biquadDbAt (qLpf, hz, sr);

                const float y = dbToY (std::clamp (totalDb, kDbMin, kDbMax), plot.getY(), plot.getHeight(), kDbMin, kDbMax);
                const float ym = dbToY (std::clamp (midPart, kDbMin, kDbMax), plot.getY(), plot.getHeight(), kDbMin, kDbMax);

                if (i == 0)
                {
                    totalCurve.startNewSubPath (x, y);
                    fillArea.startNewSubPath (x, zeroY);
                    fillArea.lineTo (x, y);
                    midBandCurve.startNewSubPath (x, ym);
                }
                else
                {
                    totalCurve.lineTo (x, y);
                    fillArea.lineTo (x, y);
                    midBandCurve.lineTo (x, ym);
                }
            }
            fillArea.lineTo (plot.getRight(), zeroY);
            fillArea.closeSubPath();

            g.saveState();
            g.reduceClipRegion (plot.toNearestInt());

            const auto curveCol = (active || hpfOn || lpfOn) ? t.accent : t.textFaint;
            g.setColour (curveCol.withAlpha (active ? 0.16f : 0.08f));
            g.fillPath (fillArea);

            if (active && std::abs (midDb) > 0.1f)
            {
                g.setColour (t.accent.withAlpha (0.28f));
                g.strokePath (midBandCurve, juce::PathStrokeType (1.0f));
            }

            g.setColour (curveCol);
            g.strokePath (totalCurve, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.restoreState();

            // Band handles
            if (hpfOn)
                drawNodeHandle (g, nodePos (Node::Hpf), "HP", true, hovered == Node::Hpf, dragging == Node::Hpf, true);
            if (lpfOn)
                drawNodeHandle (g, nodePos (Node::Lpf), "LP", true, hovered == Node::Lpf, dragging == Node::Lpf, true);
            drawNodeHandle (g, nodePos (Node::Low),  "L", active, hovered == Node::Low,  dragging == Node::Low);
            drawNodeHandle (g, nodePos (Node::Mid),  "M", active, hovered == Node::Mid,  dragging == Node::Mid);
            drawNodeHandle (g, nodePos (Node::High), "H", active, hovered == Node::High, dragging == Node::High);

            if (dragging != Node::None)
            {
                juce::String readout;
                switch (dragging)
                {
                    case Node::Hpf:  readout = "HPF " + formatHzShort (hpfHz) + " Hz"; break;
                    case Node::Lpf:  readout = "LPF " + formatHzShort (lpfHz) + " Hz"; break;
                    case Node::Low:  readout = "Low " + formatSignedDb (lowDb) + " dB"; break;
                    case Node::Mid:  readout = formatHzShort (midHz) + " Hz  " + formatSignedDb (midDb) + " dB"; break;
                    case Node::High: readout = "High " + formatSignedDb (highDb) + " dB"; break;
                    case Node::None: break;
                }
                drawFloatingTag (g, plot, nodePos (dragging), readout);
            }
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            const auto h = hitTestNode (e.position);
            if (h != hovered)
            {
                hovered = h;
                repaint();
            }
        }

        void mouseExit (const juce::MouseEvent&) override
        {
            if (hovered != Node::None)
            {
                hovered = Node::None;
                repaint();
            }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            dragging = hitTestNode (e.position);
            hovered = dragging;
            if (dragging != Node::None)
                applyDrag (e.position);
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (dragging != Node::None)
                applyDrag (e.position);
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            if (dragging != Node::None)
            {
                dragging = Node::None;
                repaint();
            }
        }

        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            const auto node = hitTestNode (e.position);
            switch (node)
            {
                case Node::Low:  fx.eqLowDb.store (0.0f); break;
                case Node::Mid:  fx.eqMidDb.store (0.0f); fx.eqMidHz.store (1000.0f); break;
                case Node::High: fx.eqHighDb.store (0.0f); break;
                case Node::Hpf:  fx.hpfHz.store (80.0f); break;
                case Node::Lpf:  fx.lpfHz.store (18000.0f); break;
                case Node::None: return;
            }
            if (notifyEdit) notifyEdit();
            repaint();
        }

    private:
        enum class Node { None, Hpf, Low, Mid, High, Lpf };
        static constexpr float kFMin = 20.0f, kFMax = 20000.0f, kDbMin = -15.0f, kDbMax = 15.0f;

        juce::Point<float> nodePos (Node n) const noexcept
        {
            switch (n)
            {
                case Node::Hpf:  return { freqToX (fx.hpfHz.load(),   plot.getX(), plot.getWidth(), kFMin, kFMax),
                                          dbToY (-3.0f, plot.getY(), plot.getHeight(), kDbMin, kDbMax) };
                case Node::Lpf:  return { freqToX (fx.lpfHz.load(),   plot.getX(), plot.getWidth(), kFMin, kFMax),
                                          dbToY (-3.0f, plot.getY(), plot.getHeight(), kDbMin, kDbMax) };
                case Node::Low:  return { freqToX (120.0f,            plot.getX(), plot.getWidth(), kFMin, kFMax),
                                          dbToY (fx.eqLowDb.load(), plot.getY(), plot.getHeight(), kDbMin, kDbMax) };
                case Node::Mid:  return { freqToX (fx.eqMidHz.load(), plot.getX(), plot.getWidth(), kFMin, kFMax),
                                          dbToY (fx.eqMidDb.load(), plot.getY(), plot.getHeight(), kDbMin, kDbMax) };
                case Node::High: return { freqToX (8000.0f,           plot.getX(), plot.getWidth(), kFMin, kFMax),
                                          dbToY (fx.eqHighDb.load(), plot.getY(), plot.getHeight(), kDbMin, kDbMax) };
                case Node::None: break;
            }
            return {};
        }

        Node hitTestNode (juce::Point<float> pt) const noexcept
        {
            if (plot.isEmpty())
                return Node::None;
            Node best = Node::None;
            float bestD = 18.0f;
            auto test = [&] (Node n)
            {
                const float d = pt.getDistanceFrom (nodePos (n));
                if (d < bestD) { bestD = d; best = n; }
            };
            test (Node::Mid);
            test (Node::Low);
            test (Node::High);
            if (showFilters && fx.hpfOn.load()) test (Node::Hpf);
            if (showFilters && fx.lpfOn.load()) test (Node::Lpf);
            return best;
        }

        void applyDrag (juce::Point<float> pt)
        {
            auto quantize = [] (float v, float step) { return std::round (v / step) * step; };
            switch (dragging)
            {
                case Node::Low:
                    fx.eqLowDb.store (std::clamp (quantize (yToDb (pt.y, plot.getY(), plot.getHeight(), kDbMin, kDbMax), 0.1f), -12.0f, 12.0f));
                    break;
                case Node::Mid:
                    fx.eqMidHz.store (std::clamp (quantize (xToFreq (pt.x, plot.getX(), plot.getWidth(), kFMin, kFMax), 1.0f), 100.0f, 10000.0f));
                    fx.eqMidDb.store (std::clamp (quantize (yToDb (pt.y, plot.getY(), plot.getHeight(), kDbMin, kDbMax), 0.1f), -12.0f, 12.0f));
                    break;
                case Node::High:
                    fx.eqHighDb.store (std::clamp (quantize (yToDb (pt.y, plot.getY(), plot.getHeight(), kDbMin, kDbMax), 0.1f), -12.0f, 12.0f));
                    break;
                case Node::Hpf:
                    fx.hpfHz.store (std::clamp (quantize (xToFreq (pt.x, plot.getX(), plot.getWidth(), kFMin, kFMax), 1.0f), 20.0f, 1000.0f));
                    break;
                case Node::Lpf:
                    fx.lpfHz.store (std::clamp (quantize (xToFreq (pt.x, plot.getX(), plot.getWidth(), kFMin, kFMax), 10.0f), 1000.0f, 20000.0f));
                    break;
                case Node::None:
                    return;
            }
            if (notifyEdit) notifyEdit();
            repaint();
        }

        MixerEngine& engine;
        ProcessingParams& fx;
        bool showFilters;
        std::function<void()> notifyEdit;
        juce::Rectangle<float> plot;
        Node hovered = Node::None, dragging = Node::None;
    };

    //==========================================================================
    // Interactive Compressor visual (transfer curve + threshold/ratio + live GR)

    class CompVisual final : public juce::Component
    {
    public:
        CompVisual (ProcessingParams& p, std::function<float()> grFn, std::function<void()> onEdit)
            : fx (p), getGrDb (std::move (grFn)), notifyEdit (std::move (onEdit))
        {
            makeClickable (*this);
            setTooltip ("Drag T left/right for Threshold, or R up/down for Ratio. Double-click to reset.");
        }

        void paint (juce::Graphics& g) override
        {
            const auto& t = Theme::p();
            const bool active = fx.compOn.load();
            const float thr = fx.compThresholdDb.load();
            const float ratio = fx.compRatio.load();
            omx::Compressor c;
            c.thresholdDb = thr;
            c.ratio = ratio;
            const float makeup = c.makeupDb();
            const float gr = getGrDb ? getGrDb() : 0.0f;

            auto inner = drawVisualFrame (g, getLocalBounds(),
                {
                    { "THR",    juce::String (thr, 1) + " dB", false },
                    { "RATIO",  juce::String (ratio, 1) + ":1", false },
                    { "MAKEUP", "+" + juce::String (makeup, 1) + " dB", makeup > 0.2f },
                    { "GR",     gr < 0.05f ? juce::String ("0.0 dB") : "-" + juce::String (gr, 1) + " dB", gr > 0.2f }
                }, active);

            auto grCol = inner.removeFromRight (44);
            drawRightReductionBar (g, grCol, "GR", gr, 24.0f, active, t.meterMid);

            inner.removeFromRight (6);
            inner.removeFromBottom (12);
            inner.removeFromLeft (24);
            plot = inner.toFloat();
            if (plot.getWidth() < 40.0f || plot.getHeight() < 30.0f)
                return;

            // Grid (-60..0 in, -60..+12 out)
            for (const float db : { -48.0f, -36.0f, -24.0f, -12.0f, 0.0f })
            {
                const float x = linearToX (db, plot.getX(), plot.getWidth(), kInMin, kInMax);
                g.setColour (t.line.withAlpha (db == -18.0f ? 0.5f : 0.3f));
                g.fillRect (x, plot.getY(), 1.0f, plot.getHeight());
                g.setFont (Theme::mono (8.0f));
                g.setColour (t.textFaint);
                g.drawText (juce::String ((int) db), juce::Rectangle<float> (x - 12.0f, plot.getBottom() + 1.0f, 24.0f, 11.0f),
                            juce::Justification::centred);
            }
            for (const float db : { -48.0f, -24.0f, 0.0f, 12.0f })
            {
                const float y = dbToY (db, plot.getY(), plot.getHeight(), kOutMin, kOutMax);
                g.setColour (db == 0.0f ? t.controlEdge.withAlpha (0.5f) : t.line.withAlpha (0.3f));
                g.fillRect (plot.getX(), y, plot.getWidth(), 1.0f);
                if (db == -48.0f || db == -24.0f || db == 0.0f)
                {
                    g.setFont (Theme::mono (8.0f));
                    g.setColour (t.textFaint);
                    g.drawText (juce::String ((int) db), juce::Rectangle<float> (plot.getX() - 25.0f, y - 5.0f, 22.0f, 10.0f),
                                juce::Justification::centredRight);
                }
            }

            // 1:1 unity reference
            {
                const float x0 = linearToX (-60.0f, plot.getX(), plot.getWidth(), kInMin, kInMax);
                const float y0 = dbToY (-60.0f, plot.getY(), plot.getHeight(), kOutMin, kOutMax);
                const float x1 = linearToX (0.0f, plot.getX(), plot.getWidth(), kInMin, kInMax);
                const float y1 = dbToY (0.0f, plot.getY(), plot.getHeight(), kOutMin, kOutMax);
                g.setColour (t.line.withAlpha (0.65f));
                g.drawLine (x0, y0, x1, y1, 1.0f);
            }

            // Threshold guide
            const float tx = linearToX (thr, plot.getX(), plot.getWidth(), kInMin, kInMax);
            g.setColour (t.accent.withAlpha (0.25f));
            g.fillRect (tx, plot.getY(), 1.0f, plot.getHeight());

            // Transfer curve + fill above/below
            juce::Path curve;
            constexpr int N = 72;
            for (int i = 0; i <= N; ++i)
            {
                const float inDb = kInMin + (kInMax - kInMin) * ((float) i / (float) N);
                const float outDb = active
                    ? (inDb - omx::Compressor::staticReductionDb (inDb, thr, ratio) + makeup)
                    : inDb;
                const float x = linearToX (inDb, plot.getX(), plot.getWidth(), kInMin, kInMax);
                const float y = dbToY (std::clamp (outDb, kOutMin, kOutMax), plot.getY(), plot.getHeight(), kOutMin, kOutMax);
                if (i == 0) curve.startNewSubPath (x, y);
                else        curve.lineTo (x, y);
            }

            g.setColour (active ? t.accent : t.textFaint);
            g.strokePath (curve, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            drawNodeHandle (g, threshNodePos(), "T", active, hovered == Node::Thresh, dragging == Node::Thresh);
            drawNodeHandle (g, ratioNodePos(),  "R", active, hovered == Node::Ratio,  dragging == Node::Ratio, true);

            if (dragging != Node::None)
            {
                drawFloatingTag (g, plot, dragging == Node::Thresh ? threshNodePos() : ratioNodePos(),
                                 dragging == Node::Thresh
                                    ? ("Thresh " + juce::String (thr, 1) + " dB")
                                    : ("Ratio " + juce::String (ratio, 1) + ":1"));
            }
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            const auto h = hitTestNode (e.position);
            if (h != hovered) { hovered = h; repaint(); }
        }

        void mouseExit (const juce::MouseEvent&) override
        {
            if (hovered != Node::None) { hovered = Node::None; repaint(); }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            dragging = hitTestNode (e.position);
            hovered = dragging;
            dragStartY = e.position.y;
            dragStartRatio = fx.compRatio.load();
            if (dragging == Node::Thresh)
                applyDrag (e.position);
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (dragging != Node::None)
                applyDrag (e.position);
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            if (dragging != Node::None) { dragging = Node::None; repaint(); }
        }

        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            const auto h = hitTestNode (e.position);
            if (h == Node::Thresh)      fx.compThresholdDb.store (-18.0f);
            else if (h == Node::Ratio)  fx.compRatio.store (3.0f);
            else return;
            if (notifyEdit) notifyEdit();
            repaint();
        }

    private:
        enum class Node { None, Thresh, Ratio };
        static constexpr float kInMin = -60.0f, kInMax = 0.0f, kOutMin = -60.0f, kOutMax = 12.0f;

        float evalOutDb (float inDb) const noexcept
        {
            omx::Compressor c;
            c.thresholdDb = fx.compThresholdDb.load();
            c.ratio = fx.compRatio.load();
            return std::clamp (inDb - omx::Compressor::staticReductionDb (inDb, c.thresholdDb, c.ratio) + c.makeupDb(), kOutMin, kOutMax);
        }

        juce::Point<float> threshNodePos() const noexcept
        {
            const float thr = fx.compThresholdDb.load();
            return { linearToX (thr, plot.getX(), plot.getWidth(), kInMin, kInMax),
                     dbToY (evalOutDb (thr), plot.getY(), plot.getHeight(), kOutMin, kOutMax) };
        }

        juce::Point<float> ratioNodePos() const noexcept
        {
            const float rx = std::min (-2.0f, fx.compThresholdDb.load() + 14.0f);
            return { linearToX (rx, plot.getX(), plot.getWidth(), kInMin, kInMax),
                     dbToY (evalOutDb (rx), plot.getY(), plot.getHeight(), kOutMin, kOutMax) };
        }

        Node hitTestNode (juce::Point<float> pt) const noexcept
        {
            if (plot.isEmpty()) return Node::None;
            const float dt = pt.getDistanceFrom (threshNodePos());
            const float dr = pt.getDistanceFrom (ratioNodePos());
            if (dt < 18.0f && dt <= dr) return Node::Thresh;
            if (dr < 18.0f) return Node::Ratio;
            return Node::None;
        }

        void applyDrag (juce::Point<float> pt)
        {
            if (dragging == Node::Thresh)
            {
                const float thr = std::round (xToLinear (pt.x, plot.getX(), plot.getWidth(), kInMin, kInMax) * 2.0f) * 0.5f;
                fx.compThresholdDb.store (std::clamp (thr, -60.0f, 0.0f));
            }
            else if (dragging == Node::Ratio)
            {
                const float dy = pt.y - dragStartY;
                const float next = std::round (std::clamp (dragStartRatio * std::pow (1.022f, dy), 1.0f, 20.0f) * 10.0f) * 0.1f;
                fx.compRatio.store (next);
            }
            if (notifyEdit) notifyEdit();
            repaint();
        }

        ProcessingParams& fx;
        std::function<float()> getGrDb;
        std::function<void()> notifyEdit;
        juce::Rectangle<float> plot;
        Node hovered = Node::None, dragging = Node::None;
        float dragStartY = 0.0f, dragStartRatio = 3.0f;
    };

    //==========================================================================
    // Interactive Gate visual (hysteresis band + range floor + open/closed state)

    class GateVisual final : public juce::Component
    {
    public:
        GateVisual (ProcessingParams& p, std::function<float()> grFn, std::function<void()> onEdit)
            : fx (p), getGrDb (std::move (grFn)), notifyEdit (std::move (onEdit))
        {
            makeClickable (*this);
            setTooltip ("Drag T left/right for Threshold, or F up/down for Range floor. Double-click to reset.");
        }

        void paint (juce::Graphics& g) override
        {
            const auto& t = Theme::p();
            const bool active = fx.gateOn.load();
            const float thr = fx.gateThresholdDb.load();
            const float range = fx.gateRangeDb.load();
            const float red = getGrDb ? getGrDb() : 0.0f;
            const bool closed = active && red > 1.0f;

            auto inner = drawVisualFrame (g, getLocalBounds(),
                {
                    { "THR",   juce::String (thr, 1) + " dB", false },
                    { "HYST",  juce::String (thr - 4.0f, 1) + " dB", false },
                    { "RANGE", juce::String (range, 1) + " dB", false },
                    { "GATE",  ! active ? "OFF" : (closed ? "CLOSED" : "OPEN"), active }
                }, active);

            auto grCol = inner.removeFromRight (44);
            drawRightReductionBar (g, grCol, "ATTEN", red, 24.0f, active, closed ? t.accent : t.meterLow);

            inner.removeFromRight (6);
            inner.removeFromBottom (12);
            inner.removeFromLeft (24);
            plot = inner.toFloat();
            if (plot.getWidth() < 40.0f || plot.getHeight() < 30.0f)
                return;

            for (const float db : { -60.0f, -40.0f, -20.0f, 0.0f })
            {
                const float x = linearToX (db, plot.getX(), plot.getWidth(), kMinDb, kMaxDb);
                const float y = dbToY (db, plot.getY(), plot.getHeight(), kMinDb, kMaxDb);
                g.setColour (t.line.withAlpha (0.3f));
                g.fillRect (x, plot.getY(), 1.0f, plot.getHeight());
                g.fillRect (plot.getX(), y, plot.getWidth(), 1.0f);
                g.setFont (Theme::mono (8.0f));
                g.setColour (t.textFaint);
                g.drawText (juce::String ((int) db), juce::Rectangle<float> (x - 12.0f, plot.getBottom() + 1.0f, 24.0f, 11.0f),
                            juce::Justification::centred);
                if (db != 0.0f)
                    g.drawText (juce::String ((int) db), juce::Rectangle<float> (plot.getX() - 25.0f, y - 5.0f, 22.0f, 10.0f),
                                juce::Justification::centredRight);
            }

            // 4 dB hysteresis band [thr - 4, thr]
            const float xClose = linearToX (std::max (kMinDb, thr - 4.0f), plot.getX(), plot.getWidth(), kMinDb, kMaxDb);
            const float xOpen  = linearToX (thr, plot.getX(), plot.getWidth(), kMinDb, kMaxDb);
            g.setColour (t.accent.withAlpha (0.14f));
            g.fillRect (xClose, plot.getY(), std::max (2.0f, xOpen - xClose), plot.getHeight());

            // Transfer line (gated below thr, 1:1 above thr)
            const float leftOut = std::max (kMinDb, kMinDb + range);
            const float preThrOut = std::clamp (thr + range, kMinDb, kMaxDb);
            juce::Path p;
            p.startNewSubPath (linearToX (kMinDb, plot.getX(), plot.getWidth(), kMinDb, kMaxDb),
                               dbToY (active ? leftOut : kMinDb, plot.getY(), plot.getHeight(), kMinDb, kMaxDb));
            p.lineTo (xOpen, dbToY (active ? preThrOut : thr, plot.getY(), plot.getHeight(), kMinDb, kMaxDb));
            p.lineTo (xOpen, dbToY (thr, plot.getY(), plot.getHeight(), kMinDb, kMaxDb));
            p.lineTo (linearToX (kMaxDb, plot.getX(), plot.getWidth(), kMinDb, kMaxDb),
                      dbToY (kMaxDb, plot.getY(), plot.getHeight(), kMinDb, kMaxDb));

            g.setColour (active ? t.accent : t.textFaint);
            g.strokePath (p, juce::PathStrokeType (2.0f));

            drawNodeHandle (g, threshPos(), "T", active, hovered == Node::Thresh, dragging == Node::Thresh);
            drawNodeHandle (g, rangePos(),  "F", active, hovered == Node::Range,  dragging == Node::Range, true);

            if (dragging != Node::None)
            {
                drawFloatingTag (g, plot, dragging == Node::Thresh ? threshPos() : rangePos(),
                                 dragging == Node::Thresh
                                    ? ("Thresh " + juce::String (thr, 1) + " dB")
                                    : ("Range " + juce::String (range, 1) + " dB"));
            }
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            const auto h = hitTestNode (e.position);
            if (h != hovered) { hovered = h; repaint(); }
        }

        void mouseExit (const juce::MouseEvent&) override
        {
            if (hovered != Node::None) { hovered = Node::None; repaint(); }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            dragging = hitTestNode (e.position);
            hovered = dragging;
            dragStartY = e.position.y;
            dragStartRange = fx.gateRangeDb.load();
            if (dragging == Node::Thresh)
                applyDrag (e.position);
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (dragging != Node::None)
                applyDrag (e.position);
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            if (dragging != Node::None) { dragging = Node::None; repaint(); }
        }

        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            const auto h = hitTestNode (e.position);
            if (h == Node::Thresh)     fx.gateThresholdDb.store (-50.0f);
            else if (h == Node::Range) fx.gateRangeDb.store (-40.0f);
            else return;
            if (notifyEdit) notifyEdit();
            repaint();
        }

    private:
        enum class Node { None, Thresh, Range };
        static constexpr float kMinDb = -80.0f, kMaxDb = 0.0f;

        juce::Point<float> threshPos() const noexcept
        {
            const float thr = fx.gateThresholdDb.load();
            return { linearToX (thr, plot.getX(), plot.getWidth(), kMinDb, kMaxDb),
                     dbToY (thr, plot.getY(), plot.getHeight(), kMinDb, kMaxDb) };
        }

        juce::Point<float> rangePos() const noexcept
        {
            const float thr = fx.gateThresholdDb.load();
            const float rx = std::max (-76.0f, thr - 14.0f);
            const float ry = std::clamp (rx + fx.gateRangeDb.load(), kMinDb, kMaxDb);
            return { linearToX (rx, plot.getX(), plot.getWidth(), kMinDb, kMaxDb),
                     dbToY (ry, plot.getY(), plot.getHeight(), kMinDb, kMaxDb) };
        }

        Node hitTestNode (juce::Point<float> pt) const noexcept
        {
            if (plot.isEmpty()) return Node::None;
            const float dt = pt.getDistanceFrom (threshPos());
            const float dr = pt.getDistanceFrom (rangePos());
            if (dt < 18.0f && dt <= dr) return Node::Thresh;
            if (dr < 18.0f) return Node::Range;
            return Node::None;
        }

        void applyDrag (juce::Point<float> pt)
        {
            if (dragging == Node::Thresh)
            {
                const float thr = std::round (xToLinear (pt.x, plot.getX(), plot.getWidth(), kMinDb, kMaxDb) * 2.0f) * 0.5f;
                fx.gateThresholdDb.store (std::clamp (thr, -80.0f, 0.0f));
            }
            else if (dragging == Node::Range)
            {
                const float dDb = (pt.y - dragStartY) / std::max (1.0f, plot.getHeight()) * (kMaxDb - kMinDb);
                const float next = std::round ((dragStartRange - dDb) * 2.0f) * 0.5f;
                fx.gateRangeDb.store (std::clamp (next, -80.0f, 0.0f));
            }
            if (notifyEdit) notifyEdit();
            repaint();
        }

        ProcessingParams& fx;
        std::function<float()> getGrDb;
        std::function<void()> notifyEdit;
        juce::Rectangle<float> plot;
        Node hovered = Node::None, dragging = Node::None;
        float dragStartY = 0.0f, dragStartRange = -40.0f;
    };

    //==========================================================================
    // Interactive De-esser visual (6.5k detector window + dynamic 4.5k high shelf)

    class DeEssVisual final : public juce::Component
    {
    public:
        DeEssVisual (MixerEngine& e, ProcessingParams& p, std::function<float()> grFn, std::function<void()> onEdit)
            : engine (e), fx (p), getGrDb (std::move (grFn)), notifyEdit (std::move (onEdit))
        {
            makeClickable (*this);
            setTooltip ("Drag the Max cut node vertically, or drag the left Threshold marker. Double-click to reset.");
        }

        void paint (juce::Graphics& g) override
        {
            const auto& t = Theme::p();
            const bool active = fx.deessOn.load();
            const float thr = fx.deessThresholdDb.load();
            const float maxCut = fx.deessMaxDb.load();
            const float liveCut = getGrDb ? std::min (maxCut, getGrDb()) : 0.0f;

            auto inner = drawVisualFrame (g, getLocalBounds(),
                {
                    { "DETECT", "6.5k BP", false },
                    { "THR",    juce::String (thr, 1) + " dB", false },
                    { "MAX",    "-" + juce::String (maxCut, 1) + " dB", false },
                    { "CUT",    liveCut < 0.05f ? juce::String ("0.0 dB") : "-" + juce::String (liveCut, 1) + " dB", liveCut > 0.2f }
                }, active);

            // Left interactive Threshold column (-60..0 dB)
            threshLane = inner.removeFromLeft (38).toFloat();
            {
                g.setFont (Theme::ui (8.0f, true));
                g.setColour (t.textFaint);
                g.drawText ("THR", threshLane.withHeight (11.0f), juce::Justification::centred);
                auto lane = threshLane.withTrimmedTop (13.0f).withTrimmedBottom (13.0f).reduced (11.0f, 2.0f);
                g.setColour (t.well);
                g.fillRoundedRectangle (lane, 2.0f);
                const float ty = dbToY (thr, lane.getY(), lane.getHeight(), -60.0f, 0.0f);
                g.setColour (active ? t.accent : t.textFaint);
                g.fillRoundedRectangle (lane.getX() - 3.0f, ty - 2.0f, lane.getWidth() + 6.0f, 4.0f, 1.5f);
                g.setFont (Theme::mono (9.0f));
                g.setColour (t.textDim);
                g.drawText (juce::String (thr, 0), threshLane.withTop (threshLane.getBottom() - 12.0f), juce::Justification::centred);
            }

            inner.removeFromLeft (6);
            inner.removeFromBottom (12);
            plot = inner.toFloat();
            if (plot.getWidth() < 40.0f || plot.getHeight() < 30.0f)
                return;

            for (const float f : { 1000.0f, 2000.0f, 4500.0f, 6500.0f, 10000.0f })
            {
                const float x = freqToX (f, plot.getX(), plot.getWidth(), kFMin, kFMax);
                g.setColour (f == 6500.0f ? t.accent.withAlpha (0.28f) : t.line.withAlpha (0.3f));
                g.fillRect (x, plot.getY(), 1.0f, plot.getHeight());
                g.setFont (Theme::mono (8.0f));
                g.setColour (f == 6500.0f ? t.accent : t.textFaint);
                g.drawText (formatHzShort (f), juce::Rectangle<float> (x - 14.0f, plot.getBottom() + 1.0f, 28.0f, 11.0f),
                            juce::Justification::centred);
            }
            const float zeroY = dbToY (0.0f, plot.getY(), plot.getHeight(), kDbMin, kDbMax);
            g.setColour (t.controlEdge.withAlpha (0.45f));
            g.fillRect (plot.getX(), zeroY, plot.getWidth(), 1.0f);

            const double sr = engine.getSampleRate() > 0.0 ? engine.getSampleRate() : 48000.0;
            Biquad bp, maxShelf, liveShelf;
            bp.setBandPass (sr, 6500.0, 1.4);
            maxShelf.setHighShelf (sr, 4500.0, -maxCut);
            liveShelf.setHighShelf (sr, 4500.0, active ? -liveCut : 0.0);

            juce::Path bpArea, maxCurve, liveCurve, liveFill;
            constexpr int N = 96;
            for (int i = 0; i <= N; ++i)
            {
                const float frac = (float) i / (float) N;
                const float x = plot.getX() + frac * plot.getWidth();
                const float hz = kFMin * std::pow (kFMax / kFMin, frac);

                const float bpWeight = std::clamp ((biquadDbAt (bp, hz, sr) + 18.0f) / 18.0f, 0.0f, 1.0f);
                const float by = plot.getBottom() - bpWeight * (plot.getHeight() * 0.25f);
                const float my = dbToY (std::clamp (biquadDbAt (maxShelf, hz, sr), kDbMin, kDbMax),
                                        plot.getY(), plot.getHeight(), kDbMin, kDbMax);
                const float ly = dbToY (std::clamp (biquadDbAt (liveShelf, hz, sr), kDbMin, kDbMax),
                                        plot.getY(), plot.getHeight(), kDbMin, kDbMax);

                if (i == 0)
                {
                    bpArea.startNewSubPath (x, plot.getBottom());
                    bpArea.lineTo (x, by);
                    maxCurve.startNewSubPath (x, my);
                    liveCurve.startNewSubPath (x, ly);
                    liveFill.startNewSubPath (x, zeroY);
                    liveFill.lineTo (x, ly);
                }
                else
                {
                    bpArea.lineTo (x, by);
                    maxCurve.lineTo (x, my);
                    liveCurve.lineTo (x, ly);
                    liveFill.lineTo (x, ly);
                }
            }
            bpArea.lineTo (plot.getRight(), plot.getBottom());
            bpArea.closeSubPath();
            liveFill.lineTo (plot.getRight(), zeroY);
            liveFill.closeSubPath();

            g.saveState();
            g.reduceClipRegion (plot.toNearestInt());
            g.setColour (t.textFaint.withAlpha (0.14f));
            g.fillPath (bpArea);

            g.setColour ((active ? t.accent : t.textFaint).withAlpha (0.45f));
            g.strokePath (maxCurve, juce::PathStrokeType (1.2f));

            g.setColour (t.accent.withAlpha (active ? 0.24f : 0.08f));
            g.fillPath (liveFill);
            g.setColour (active ? t.accent : t.textFaint);
            g.strokePath (liveCurve, juce::PathStrokeType (2.0f));
            g.restoreState();

            drawNodeHandle (g, maxPos(), "M", active, hovered, dragging == DragPart::MaxCut);
            if (dragging != DragPart::None)
            {
                drawFloatingTag (g, plot, dragging == DragPart::MaxCut ? maxPos() : juce::Point<float> (plot.getX() + 8.0f, plot.getCentreY()),
                                 dragging == DragPart::MaxCut
                                    ? ("Max cut -" + juce::String (maxCut, 1) + " dB")
                                    : ("Thresh " + juce::String (thr, 1) + " dB"));
            }
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            const bool h = plot.contains (e.position) && e.position.getDistanceFrom (maxPos()) < 18.0f;
            if (h != hovered) { hovered = h; repaint(); }
        }

        void mouseExit (const juce::MouseEvent&) override
        {
            if (hovered) { hovered = false; repaint(); }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (threshLane.contains (e.position))
                dragging = DragPart::Thresh;
            else if (plot.contains (e.position))
                dragging = DragPart::MaxCut;
            else
                dragging = DragPart::None;

            if (dragging != DragPart::None)
                applyDrag (e.position);
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (dragging != DragPart::None)
                applyDrag (e.position);
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            if (dragging != DragPart::None) { dragging = DragPart::None; repaint(); }
        }

        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            if (threshLane.contains (e.position))
                fx.deessThresholdDb.store (-30.0f);
            else if (plot.contains (e.position))
                fx.deessMaxDb.store (10.0f);
            else
                return;
            if (notifyEdit) notifyEdit();
            repaint();
        }

    private:
        enum class DragPart { None, Thresh, MaxCut };
        static constexpr float kFMin = 500.0f, kFMax = 20000.0f, kDbMin = -22.0f, kDbMax = 4.0f;

        juce::Point<float> maxPos() const noexcept
        {
            return { freqToX (10000.0f, plot.getX(), plot.getWidth(), kFMin, kFMax),
                     dbToY (-fx.deessMaxDb.load(), plot.getY(), plot.getHeight(), kDbMin, kDbMax) };
        }

        void applyDrag (juce::Point<float> pt)
        {
            if (dragging == DragPart::Thresh)
            {
                auto lane = threshLane.withTrimmedTop (13.0f).withTrimmedBottom (13.0f);
                const float thr = std::round (yToDb (pt.y, lane.getY(), lane.getHeight(), -60.0f, 0.0f) * 2.0f) * 0.5f;
                fx.deessThresholdDb.store (std::clamp (thr, -60.0f, 0.0f));
            }
            else if (dragging == DragPart::MaxCut)
            {
                const float cut = std::round ((-yToDb (pt.y, plot.getY(), plot.getHeight(), kDbMin, kDbMax)) * 2.0f) * 0.5f;
                fx.deessMaxDb.store (std::clamp (cut, 0.0f, 20.0f));
            }
            if (notifyEdit) notifyEdit();
            repaint();
        }

        MixerEngine& engine;
        ProcessingParams& fx;
        std::function<float()> getGrDb;
        std::function<void()> notifyEdit;
        juce::Rectangle<float> plot, threshLane;
        bool hovered = false;
        DragPart dragging = DragPart::None;
    };

    //==========================================================================
    // Interactive Ducking visual (sidechain trigger curve + live sidechain input)

    class DuckVisual final : public juce::Component
    {
    public:
        DuckVisual (MixerEngine& e, ProcessingParams& p, std::function<float()> grFn, std::function<void()> onEdit)
            : engine (e), fx (p), getGrDb (std::move (grFn)), notifyEdit (std::move (onEdit))
        {
            makeClickable (*this);
            setTooltip ("Drag the node left/right for Threshold and up/down for Depth. Double-click to reset.");
        }

        void paint (juce::Graphics& g) override
        {
            const auto& t = Theme::p();
            const bool active = fx.duckOn.load();
            const int src = fx.duckSource.load();
            juce::String srcName = "None";
            float srcDb = -80.0f;
            if (juce::isPositiveAndBelow (src, kNumStrips) && engine.getStrip (src).active.load())
            {
                auto& s = engine.getStrip (src);
                srcName = s.name.isNotEmpty() ? s.name : "Input " + juce::String (src + 1);
                srcDb = std::max (-80.0f, juce::Decibels::gainToDecibels (s.sidechainLevel.load(), -80.0f));
            }

            const float thr = fx.duckThresholdDb.load();
            const float depth = fx.duckDepthDb.load();
            const float gr = getGrDb ? getGrDb() : 0.0f;

            auto inner = drawVisualFrame (g, getLocalBounds(),
                {
                    { "SRC",   srcName, src >= 0 },
                    { "THR",   juce::String (thr, 1) + " dB", false },
                    { "DEPTH", juce::String (depth, 1) + " dB", false },
                    { "DUCK",  gr < 0.05f ? juce::String ("0.0 dB") : "-" + juce::String (gr, 1) + " dB", gr > 0.2f }
                }, active);

            inner.removeFromBottom (12);
            inner.removeFromLeft (24);
            plot = inner.toFloat();
            if (plot.getWidth() < 40.0f || plot.getHeight() < 30.0f)
                return;

            for (const float db : { -60.0f, -40.0f, -20.0f, 0.0f })
            {
                const float x = linearToX (db, plot.getX(), plot.getWidth(), -80.0f, 0.0f);
                g.setColour (t.line.withAlpha (0.3f));
                g.fillRect (x, plot.getY(), 1.0f, plot.getHeight());
                g.setFont (Theme::mono (8.0f));
                g.setColour (t.textFaint);
                g.drawText (juce::String ((int) db), juce::Rectangle<float> (x - 12.0f, plot.getBottom() + 1.0f, 24.0f, 11.0f),
                            juce::Justification::centred);
            }
            for (const float db : { -40.0f, -20.0f, 0.0f })
            {
                const float y = dbToY (db, plot.getY(), plot.getHeight(), -40.0f, 0.0f);
                g.setColour (t.line.withAlpha (0.3f));
                g.fillRect (plot.getX(), y, plot.getWidth(), 1.0f);
                g.setFont (Theme::mono (8.0f));
                g.setColour (t.textFaint);
                g.drawText (juce::String ((int) db), juce::Rectangle<float> (plot.getX() - 25.0f, y - 5.0f, 22.0f, 10.0f),
                            juce::Justification::centredRight);
            }

            // Live sidechain level bar
            if (src >= 0)
            {
                const float sx = linearToX (srcDb, plot.getX(), plot.getWidth(), -80.0f, 0.0f);
                const bool triggered = active && srcDb >= thr;
                g.setColour ((triggered ? t.accent : t.meterLow).withAlpha (0.24f));
                g.fillRect (plot.getX(), plot.getY(), sx - plot.getX(), plot.getHeight());
                g.setColour (triggered ? t.accent : t.meterLow);
                g.fillRect (sx - 1.0f, plot.getY(), 2.0f, plot.getHeight());
            }

            // Step curve
            const float tx = linearToX (thr, plot.getX(), plot.getWidth(), -80.0f, 0.0f);
            const float y0 = dbToY (0.0f, plot.getY(), plot.getHeight(), -40.0f, 0.0f);
            const float yd = dbToY (active ? depth : 0.0f, plot.getY(), plot.getHeight(), -40.0f, 0.0f);

            juce::Path p;
            p.startNewSubPath (plot.getX(), y0);
            p.lineTo (tx, y0);
            p.lineTo (tx, yd);
            p.lineTo (plot.getRight(), yd);
            g.setColour (active ? t.accent : t.textFaint);
            g.strokePath (p, juce::PathStrokeType (2.0f));

            const auto node = nodePos();
            drawNodeHandle (g, node, "D", active, hovered, dragging);
            if (dragging)
                drawFloatingTag (g, plot, node, juce::String (thr, 1) + " dB  " + juce::String (depth, 1) + " dB");
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            const bool h = ! plot.isEmpty() && e.position.getDistanceFrom (nodePos()) < 18.0f;
            if (h != hovered) { hovered = h; repaint(); }
        }

        void mouseExit (const juce::MouseEvent&) override
        {
            if (hovered) { hovered = false; repaint(); }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (plot.contains (e.position))
            {
                dragging = true;
                applyDrag (e.position);
            }
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (dragging)
                applyDrag (e.position);
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            if (dragging) { dragging = false; repaint(); }
        }

        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            if (! plot.contains (e.position)) return;
            fx.duckThresholdDb.store (-40.0f);
            fx.duckDepthDb.store (-12.0f);
            if (notifyEdit) notifyEdit();
            repaint();
        }

    private:
        juce::Point<float> nodePos() const noexcept
        {
            return { linearToX (fx.duckThresholdDb.load(), plot.getX(), plot.getWidth(), -80.0f, 0.0f),
                     dbToY (fx.duckDepthDb.load(), plot.getY(), plot.getHeight(), -40.0f, 0.0f) };
        }

        void applyDrag (juce::Point<float> pt)
        {
            const float thr = std::round (xToLinear (pt.x, plot.getX(), plot.getWidth(), -80.0f, 0.0f) * 2.0f) * 0.5f;
            const float depth = std::round (yToDb (pt.y, plot.getY(), plot.getHeight(), -40.0f, 0.0f) * 2.0f) * 0.5f;
            fx.duckThresholdDb.store (std::clamp (thr, -80.0f, 0.0f));
            fx.duckDepthDb.store (std::clamp (depth, -40.0f, 0.0f));
            if (notifyEdit) notifyEdit();
            repaint();
        }

        MixerEngine& engine;
        ProcessingParams& fx;
        std::function<float()> getGrDb;
        std::function<void()> notifyEdit;
        juce::Rectangle<float> plot;
        bool hovered = false, dragging = false;
    };

    //==========================================================================
    // Interactive Noise Removal visual (live voice-activity sparkline + amount)

    class DenoiseVisual final : public juce::Component
    {
    public:
        DenoiseVisual (ProcessingParams& p, std::function<void()> onEdit)
            : fx (p), notifyEdit (std::move (onEdit))
        {
            makeClickable (*this);
            setTooltip ("Shows live voice detection vs steady noise. Drag across the amount lane to set suppression.");
        }

        void pushSample()
        {
            const float vp = fx.denoiseOn.load() && ! fx.denoiseUnavailable.load()
                ? juce::jlimit (0.0f, 1.0f, fx.voiceProbability.load()) : 0.0f;
            history[(size_t) head] = vp;
            head = (head + 1) % kHist;
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            const auto& t = Theme::p();
            const bool unavail = fx.denoiseUnavailable.load();
            const bool active = fx.denoiseOn.load() && ! unavail;
            const int pct = juce::roundToInt (fx.denoiseAmount.load() * 100.0f);
            const int voicePct = juce::roundToInt (fx.voiceProbability.load() * 100.0f);

            auto inner = drawVisualFrame (g, getLocalBounds(),
                {
                    { "MODEL", unavail ? "ERR" : "RNNoise", ! unavail },
                    { "AMT",   juce::String (pct) + "%", active },
                    { "VOICE", active ? (juce::String (voicePct) + "%") : "--", active && voicePct >= 40 },
                    { "LAT",   juce::String (fx.denoiseLatencyMs.load()) + " ms", false }
                }, active);

            // Right interactive Amount column
            amountLane = inner.removeFromRight (46).toFloat();
            {
                g.setFont (Theme::ui (8.0f, true));
                g.setColour (t.textFaint);
                g.drawText ("AMT", amountLane.withHeight (11.0f), juce::Justification::centred);
                auto track = amountLane.withTrimmedTop (13.0f).withTrimmedBottom (13.0f).reduced (14.0f, 2.0f);
                g.setColour (t.well);
                g.fillRoundedRectangle (track, 2.0f);
                g.setColour (t.line);
                g.drawRoundedRectangle (track.reduced (0.5f), 2.0f, 1.0f);
                const float amt = fx.denoiseAmount.load();
                const float fillH = track.getHeight() * amt;
                if (amt > 0.005f)
                {
                    g.setColour ((active ? t.accent : t.textFaint).withAlpha (0.85f));
                    g.fillRoundedRectangle (track.getX() + 1.0f, track.getBottom() - fillH + 1.0f,
                                            track.getWidth() - 2.0f, std::max (2.0f, fillH - 2.0f), 1.5f);
                }
                g.setFont (Theme::mono (9.0f));
                g.setColour (active ? t.text : t.textFaint);
                g.drawText (juce::String (pct) + "%", amountLane.withTop (amountLane.getBottom() - 12.0f), juce::Justification::centred);
            }

            inner.removeFromRight (8);
            inner.removeFromBottom (12);
            plot = inner.toFloat();
            if (plot.getWidth() < 40.0f || plot.getHeight() < 30.0f)
                return;

            for (const float pctMark : { 0.25f, 0.50f, 0.75f })
            {
                const float y = plot.getBottom() - pctMark * plot.getHeight();
                g.setColour (t.line.withAlpha (pctMark == 0.50f ? 0.45f : 0.25f));
                g.fillRect (plot.getX(), y, plot.getWidth(), 1.0f);
            }
            g.setFont (Theme::ui (8.0f, true));
            g.setColour (t.textFaint);
            g.drawText ("VOICE PROBABILITY HISTORY",
                        juce::Rectangle<float> (plot.getX(), plot.getBottom() + 1.0f, plot.getWidth(), 11.0f),
                        juce::Justification::centredLeft);

            juce::Path curve, fill;
            for (int i = 0; i < kHist; ++i)
            {
                const int idx = (head + i) % kHist;
                const float v = history[(size_t) idx];
                const float x = plot.getX() + ((float) i / (float) (kHist - 1)) * plot.getWidth();
                const float y = plot.getBottom() - v * plot.getHeight();
                if (i == 0)
                {
                    curve.startNewSubPath (x, y);
                    fill.startNewSubPath (x, plot.getBottom());
                    fill.lineTo (x, y);
                }
                else
                {
                    curve.lineTo (x, y);
                    fill.lineTo (x, y);
                }
            }
            fill.lineTo (plot.getRight(), plot.getBottom());
            fill.closeSubPath();

            g.setColour ((active ? t.meterLow : t.textFaint).withAlpha (0.22f));
            g.fillPath (fill);
            g.setColour (active ? t.meterLow : t.textFaint);
            g.strokePath (curve, juce::PathStrokeType (1.8f));
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (amountLane.contains (e.position) || plot.contains (e.position))
            {
                dragging = true;
                applyDrag (e.position);
            }
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (dragging)
                applyDrag (e.position);
        }

        void mouseUp (const juce::MouseEvent&) override { dragging = false; }

        void mouseDoubleClick (const juce::MouseEvent&) override
        {
            fx.denoiseAmount.store (1.0f);
            if (notifyEdit) notifyEdit();
            repaint();
        }

    private:
        void applyDrag (juce::Point<float> pt)
        {
            auto track = amountLane.withTrimmedTop (13.0f).withTrimmedBottom (13.0f);
            const float amt = std::clamp (1.0f - (pt.y - track.getY()) / std::max (1.0f, track.getHeight()), 0.0f, 1.0f);
            fx.denoiseAmount.store (std::round (amt * 100.0f) * 0.01f);
            if (notifyEdit) notifyEdit();
            repaint();
        }

        static constexpr int kHist = 96;
        ProcessingParams& fx;
        std::function<void()> notifyEdit;
        std::array<float, kHist> history {};
        int head = 0;
        juce::Rectangle<float> plot, amountLane;
        bool dragging = false;
    };
}

//==============================================================================
// PanelBase

PanelBase::PanelBase (const juce::String& t, std::atomic<bool>* flag) : title (t), onFlag (flag)
{
    setFocusContainerType (FocusContainerType::keyboardFocusContainer);
    setTitle (t);

    if (onFlag != nullptr)
    {
        makeLamp (onButton, "accent");
        onButton.setToggleState (onFlag->load(), juce::dontSendNotification);
        onButton.setTitle (t + " on");
        onButton.onClick = [this]
        {
            onFlag->store (onButton.getToggleState());
            repaintVisual();
            if (onChange) onChange();
        };
        addAndMakeVisible (onButton);
    }

    startTimerHz (30);
}

void PanelBase::addKnob (const KnobSpec& spec)
{
    Knob k;
    k.label = spec.label;
    k.target = &spec.target;
    k.scale = spec.scale;
    k.slider = std::make_unique<DbSlider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
    auto* s = k.slider.get();

    s->setRange (spec.min, spec.max, spec.step);
    if (spec.skewMid > 0.0)
        s->setSkewFactorFromMidPoint (spec.skewMid);
    s->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 18);
    const auto suffix = spec.suffix;
    s->textFromValueFunction = [suffix] (double v)
    {
        const bool whole = std::abs (v) >= 100.0 || suffix == " %";
        return juce::String (v, whole ? 0 : 1) + suffix;
    };
    const double span = spec.max - spec.min;
    s->keyStep  = spec.suffix == " dB" ? 1.0 : juce::jmax (spec.step, span / 50.0);
    s->pageStep = spec.suffix == " dB" ? 6.0 : span / 10.0;
    s->homeValue = spec.min <= 0.0 && spec.max >= 0.0 && spec.suffix == " dB" ? 0.0
                 : spec.skewMid > 0.0 ? spec.skewMid : spec.max;
    s->setDoubleClickReturnValue (true, s->homeValue);

    s->setValue (spec.target.load() * spec.scale, juce::dontSendNotification);
    s->updateText();
    s->setTitle (title + " " + spec.label);
    s->setWantsKeyboardFocus (true);
    makeClickable (*s);

    auto* target = &spec.target;
    const double scale = spec.scale;
    s->onValueChange = [this, s, target, scale]
    {
        target->store ((float) (s->getValue() / scale));
        repaintVisual();
        if (onChange) onChange();
    };

    addAndMakeVisible (s);
    knobs.push_back (std::move (k));
}

void PanelBase::syncKnobsFromTargets()
{
    for (auto& k : knobs)
    {
        if (k.target != nullptr && k.slider != nullptr)
        {
            k.slider->setValue (k.target->load() * k.scale, juce::dontSendNotification);
            k.slider->updateText();
        }
    }
}

void PanelBase::setMeter (std::atomic<float>* gainMeter, const juce::String& label)
{
    meterSource = gainMeter;
    meterLabel = label;
}

void PanelBase::setVisual (std::unique_ptr<juce::Component> v, int height)
{
    visual = std::move (v);
    visualHeight = height;
    if (visual != nullptr)
        addAndMakeVisible (*visual);
}

void PanelBase::repaintVisual()
{
    if (visual != nullptr)
        visual->repaint();
}

void PanelBase::timerCallback()
{
    if (meterSource != nullptr)
    {
        const float reduction = -juce::Decibels::gainToDecibels (meterSource->exchange (1.0f), -60.0f);
        meterDb = juce::jmax (reduction, meterDb * 0.85f);
        repaint (meterArea);
    }
    tick();
}

void PanelBase::paint (juce::Graphics& g)
{
    const auto& t = Theme::p();

    g.setColour (t.text);
    g.setFont (Theme::ui (15.0f, true));
    g.drawText (title, titleArea, juce::Justification::centredLeft);

    g.setFont (Theme::ui (11.5f, true).withExtraKerningFactor (0.04f));
    g.setColour (t.textFaint);
    for (const auto& k : knobs)
        g.drawText (k.label.toUpperCase(), k.slider->getBounds().withHeight (16).translated (0, -16),
                    juce::Justification::centred);

    if (meterSource != nullptr)
    {
        auto r = meterArea;
        g.setFont (Theme::ui (11.5f, true));
        g.setColour (t.textFaint);
        g.drawText (meterLabel.toUpperCase(), r.removeFromLeft (110), juce::Justification::centredLeft);

        auto readout = r.removeFromRight (60);
        auto bar = r.toFloat().withSizeKeepingCentre ((float) r.getWidth(), 8.0f);
        g.setColour (t.meterWell);
        g.fillRoundedRectangle (bar, 2.0f);
        g.setColour (t.meterMid);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, meterDb / 24.0f)), 2.0f);

        g.setFont (Theme::mono (12.0f));
        g.setColour (t.textDim);
        g.drawText (meterDb < 0.05f ? juce::String ("0.0 dB") : "-" + juce::String (meterDb, 1) + " dB",
                    readout, juce::Justification::centredRight);
    }

    if (help.isNotEmpty())
    {
        g.setFont (Theme::ui (12.0f));
        g.setColour (t.textDim);
        g.drawFittedText (help, helpArea, juce::Justification::topLeft, 3);
    }
}

void PanelBase::resized()
{
    auto r = getLocalBounds().reduced (Theme::s3);

    auto top = r.removeFromTop (30);
    if (onFlag != nullptr)
        onButton.setBounds (top.removeFromRight (64));
    titleArea = top;
    r.removeFromTop (Theme::s2);

    if (visual != nullptr && visualHeight > 0)
    {
        visual->setBounds (r.removeFromTop (visualHeight));
        r.removeFromTop (Theme::s2);
    }

    layoutExtra (r);

    if (! knobs.empty())
    {
        r.removeFromTop (16);   // knob labels
        auto row = r.removeFromTop (76);
        const int w = row.getWidth() / (int) knobs.size();
        for (auto& k : knobs)
            k.slider->setBounds (row.removeFromLeft (w).reduced (4, 0));
        r.removeFromTop (Theme::s2);
    }

    meterArea = meterSource != nullptr ? r.removeFromTop (20) : juce::Rectangle<int>();
    if (meterSource != nullptr)
        r.removeFromTop (Theme::s2);
    helpArea = r;
}

//==============================================================================
// ProcessorPanel

ProcessorPanel::ProcessorPanel (MixerEngine& e, ProcessingParams& params, Processor p, int strip)
    : PanelBase (processorName (p), &processorOn (params, p)),
      engine (e), fx (params), kind (p), stripIndex (strip)
{
    const bool onBus = stripIndex < 0;
    auto onVisualEdit = [this] { syncFromVisual(); };
    auto grFn = [this] { return getSmoothedReductionDb(); };

    int width = 390;
    int height = 390;

    switch (p)
    {
        case Processor::Denoise:
            setVisual (std::make_unique<DenoiseVisual> (fx, onVisualEdit), 146);
            addKnob ({ "Amount", 0, 100, 1, " %", fx.denoiseAmount, 0.0, 100.0 });
            setMeter (nullptr, {});
            setHelp (fx.denoiseUnavailable.load()
                       ? "Noise removal couldn't start. Restart OpenMix and try again."
                       : "Removes steady background noise from voice. Adds "
                         + juce::String (fx.denoiseLatencyMs.load()) + " ms latency (other strips align automatically).");
            height = 374;
            break;

        case Processor::Gate:
            setVisual (std::make_unique<GateVisual> (fx, grFn, onVisualEdit), 156);
            addKnob ({ "Threshold", -80, 0, 0.5, " dB", fx.gateThresholdDb });
            addKnob ({ "Range", -80, 0, 0.5, " dB", fx.gateRangeDb });
            setMeter (&fx.gateGain, "Reduction");
            setHelp ("Attenuates the strip while signal falls below Threshold (with 4 dB hysteresis).");
            height = 410;
            break;

        case Processor::Eq:
            width = 460;
            setVisual (std::make_unique<EqVisual> (engine, fx, ! onBus, onVisualEdit), 184);
            addKnob ({ "Low", -12, 12, 0.1, " dB", fx.eqLowDb });
            addKnob ({ "Mid", -12, 12, 0.1, " dB", fx.eqMidDb });
            addKnob ({ "Mid freq", 100, 10000, 1, " Hz", fx.eqMidHz, 1000.0 });
            addKnob ({ "High", -12, 12, 0.1, " dB", fx.eqHighDb });
            setHelp (onBus ? "Bus EQ: 120 Hz low shelf, sweepable mid bell, 8 kHz high shelf. Drag handles on the graph."
                           : "Low shelf 120 Hz, sweepable mid bell, high shelf 8 kHz. Drag nodes directly or switch HPF/LPF below.");
            height = 398;
            if (! onBus)
            {
                makeLamp (hpfToggle, "key");
                hpfToggle.setToggleState (fx.hpfOn.load(), juce::dontSendNotification);
                hpfToggle.setTitle ("High-pass filter");
                hpfToggle.setTooltip ("High-pass filter: cut rumble below the cutoff.");
                hpfToggle.onClick = [this]
                {
                    fx.hpfOn.store (hpfToggle.getToggleState());
                    hpfCutoffSlider.setEnabled (hpfToggle.getToggleState());
                    repaintVisual();
                    if (onChange) onChange();
                };
                addAndMakeVisible (hpfToggle);

                hpfCutoffSlider.setRange (20.0, 1000.0, 1.0);
                hpfCutoffSlider.setSkewFactorFromMidPoint (80.0);
                hpfCutoffSlider.setDoubleClickReturnValue (true, 80.0);
                hpfCutoffSlider.textFromValueFunction = [] (double v) { return juce::String ((int) v) + " Hz"; };
                hpfCutoffSlider.setValue (fx.hpfHz.load(), juce::dontSendNotification);
                hpfCutoffSlider.setEnabled (fx.hpfOn.load());
                hpfCutoffSlider.setTitle ("High-pass filter cutoff");
                hpfCutoffSlider.setTooltip ("High-pass filter cutoff. Double-click for 80 Hz.");
                hpfCutoffSlider.setWantsKeyboardFocus (true);
                makeClickable (hpfCutoffSlider);
                hpfCutoffSlider.onValueChange = [this]
                {
                    fx.hpfHz.store ((float) hpfCutoffSlider.getValue());
                    repaintVisual();
                };
                addAndMakeVisible (hpfCutoffSlider);

                makeLamp (lpfToggle, "key");
                lpfToggle.setToggleState (fx.lpfOn.load(), juce::dontSendNotification);
                lpfToggle.setTitle ("Low-pass filter");
                lpfToggle.setTooltip ("Low-pass filter: cut hiss above the cutoff.");
                lpfToggle.onClick = [this]
                {
                    fx.lpfOn.store (lpfToggle.getToggleState());
                    lpfCutoffSlider.setEnabled (lpfToggle.getToggleState());
                    repaintVisual();
                    if (onChange) onChange();
                };
                addAndMakeVisible (lpfToggle);

                lpfCutoffSlider.setRange (1000.0, 20000.0, 10.0);
                lpfCutoffSlider.setSkewFactorFromMidPoint (8000.0);
                lpfCutoffSlider.setDoubleClickReturnValue (true, 18000.0);
                lpfCutoffSlider.textFromValueFunction = [] (double v) { return juce::String (v / 1000.0, 1) + " kHz"; };
                lpfCutoffSlider.setValue (fx.lpfHz.load(), juce::dontSendNotification);
                lpfCutoffSlider.setEnabled (fx.lpfOn.load());
                lpfCutoffSlider.setTitle ("Low-pass filter cutoff");
                lpfCutoffSlider.setTooltip ("Low-pass filter cutoff. Double-click for 18 kHz.");
                lpfCutoffSlider.setWantsKeyboardFocus (true);
                makeClickable (lpfCutoffSlider);
                lpfCutoffSlider.onValueChange = [this]
                {
                    fx.lpfHz.store ((float) lpfCutoffSlider.getValue());
                    repaintVisual();
                };
                addAndMakeVisible (lpfCutoffSlider);

                height = 466;
            }
            break;

        case Processor::Comp:
            setVisual (std::make_unique<CompVisual> (fx, grFn, onVisualEdit), 156);
            addKnob ({ "Threshold", -60, 0, 0.5, " dB", fx.compThresholdDb });
            addKnob ({ "Ratio", 1, 20, 0.1, ":1", fx.compRatio, 4.0 });
            setMeter (&fx.compGain, "Reduction");
            setHelp (onBus ? "Evens out the bus mix with automatic make-up gain; safety limiter catches peaks after it."
                           : "Evens out loud and quiet passages with a 6 dB soft knee and automatic make-up gain.");
            height = 410;
            break;

        case Processor::DeEss:
            setVisual (std::make_unique<DeEssVisual> (engine, fx, grFn, onVisualEdit), 156);
            addKnob ({ "Threshold", -60, 0, 0.5, " dB", fx.deessThresholdDb });
            addKnob ({ "Max cut", 0, 20, 0.5, " dB", fx.deessMaxDb });
            setMeter (&fx.deessGain, "Reduction");
            setHelp ("Monitors sibilance around 6.5 kHz and ducks a 4.5 kHz high shelf when active.");
            height = 410;
            break;

        case Processor::Duck:
            setVisual (std::make_unique<DuckVisual> (engine, fx, grFn, onVisualEdit), 156);
            addKnob ({ "Depth", -40, 0, 0.5, " dB", fx.duckDepthDb });
            addKnob ({ "Threshold", -80, 0, 0.5, " dB", fx.duckThresholdDb });
            setMeter (&fx.duckGain, "Ducking");
            setHelp ("Ducks this strip while the selected sidechain strip rises above Threshold.");

            duckLabel.setText ("Duck under", juce::dontSendNotification);
            duckLabel.setFont (Theme::ui (13.0f));
            addAndMakeVisible (duckLabel);

            duckSource.addItem ("Nothing", 1);
            for (int s = 0; s < kNumStrips; ++s)
            {
                auto& other = engine.getStrip (s);
                if (other.active.load() && s != stripIndex)
                    duckSource.addItem (other.name.isNotEmpty() ? other.name : "Input " + juce::String (s + 1), s + 2);
            }
            duckSource.setSelectedId (fx.duckSource.load() + 2, juce::dontSendNotification);
            if (duckSource.getSelectedId() == 0)
                duckSource.setSelectedId (1, juce::dontSendNotification);
            duckSource.setTitle ("Duck under which strip");
            duckSource.onChange = [this]
            {
                fx.duckSource.store (duckSource.getSelectedId() - 2);
                repaintVisual();
                if (onChange) onChange();
            };
            makeClickable (duckSource);
            addAndMakeVisible (duckSource);
            height = 450;
            break;
    }

    setSize (width, height);
}

void ProcessorPanel::syncFromVisual()
{
    syncKnobsFromTargets();
    if (kind == Processor::Eq && stripIndex >= 0)
    {
        hpfCutoffSlider.setValue (fx.hpfHz.load(), juce::dontSendNotification);
        hpfCutoffSlider.updateText();
        lpfCutoffSlider.setValue (fx.lpfHz.load(), juce::dontSendNotification);
        lpfCutoffSlider.updateText();
    }
    if (onChange)
        onChange();
}

void ProcessorPanel::layoutExtra (juce::Rectangle<int>& r)
{
    if (kind == Processor::Duck)
    {
        auto row = r.removeFromTop (30);
        duckLabel.setBounds (row.removeFromLeft (90));
        duckSource.setBounds (row);
        r.removeFromTop (Theme::s3);
        return;
    }

    if (kind == Processor::Eq && stripIndex >= 0)
    {
        auto row = r.removeFromTop (28);
        hpfToggle.setBounds (row.removeFromLeft (56));
        row.removeFromLeft (6);
        hpfCutoffSlider.setBounds (row);
        r.removeFromTop (4);
        auto row2 = r.removeFromTop (28);
        lpfToggle.setBounds (row2.removeFromLeft (56));
        row2.removeFromLeft (6);
        lpfCutoffSlider.setBounds (row2);
        r.removeFromTop (Theme::s2);
    }
}

void ProcessorPanel::tick()
{
    if (kind == Processor::Denoise && fx.denoiseOn.load() && ! fx.denoiseUnavailable.load())
    {
        const auto voice = juce::roundToInt (fx.voiceProbability.load() * 100.0f);
        setHelp ("Removes steady background noise from voice. Adds "
                 + juce::String (fx.denoiseLatencyMs.load()) + " ms latency. Voice detected: "
                 + juce::String (voice) + "%.");
    }

    // Repaint dynamic meters/histories inside visuals smoothly at 30 Hz.
    if (auto* dv = dynamic_cast<DenoiseVisual*> (getVisual()))
        dv->pushSample();
    else if (kind != Processor::Eq)
        repaintVisual();
}

//==============================================================================
// SlotMixPanel

SlotMixPanel::SlotMixPanel (const juce::String& pluginName, std::atomic<float>& mix)
    : PanelBase (pluginName + ": wet/dry", nullptr)
{
    addKnob ({ "Mix", 0, 100, 1, " %", mix, 0.0, 100.0 });
    setHelp ("100% is the plugin only. Lower values blend in the dry signal, delayed to match the "
             "plugin's latency (parallel compression, subtle reverb).");
    setSize (300, 210);
}

//==============================================================================
// SendPanel

SendPanel::SendPanel (MixerEngine& e, int stripIndex, int busIndex)
    : PanelBase ("Send to " + e.getBusName (busIndex), &e.getStrip (stripIndex).busOn[busIndex]),
      strip (e.getStrip (stripIndex)), bus (busIndex)
{
    addKnob ({ "Level", -60, 12, 0.1, " dB", strip.sendDb[busIndex], -12.0 });

    makeLamp (preButton, "accent");
    preButton.setToggleState (strip.sendPre[busIndex].load(), juce::dontSendNotification);
    preButton.setTitle ("Pre-fader send");
    preButton.onClick = [this]
    {
        strip.sendPre[bus].store (preButton.getToggleState());
        if (onChange) onChange();
    };
    addAndMakeVisible (preButton);

    setHelp ("Pre-fader: this send ignores the strip's fader, so you can give this bus "
             "(for example Discord) its own mix.");
    setSize (300, 230);
}

void SendPanel::layoutExtra (juce::Rectangle<int>& r)
{
    preButton.setBounds (r.removeFromTop (30).withWidth (120));
    r.removeFromTop (Theme::s3);
}
