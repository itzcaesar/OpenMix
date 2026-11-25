#include "ui/AnalyserPanel.h"
#include "ui/Widgets.h"
#include "ui/Theme.h"

static juce::String lufsText (double v)
{
    return std::isfinite (v) ? juce::String (v, 1) + " LUFS" : juce::String ("no signal");
}

//==============================================================================
// MiniSpectrum

MiniSpectrum::MiniSpectrum (std::shared_ptr<BusAnalysis> a) : analysis (std::move (a))
{
    bands.fill (-100.0f);
    setWantsKeyboardFocus (true);
    makeClickable (*this);
    setTooltip ("Live spectrum of this bus. Click (or Enter) for the full analyser and loudness.");
}

void MiniSpectrum::update()
{
    const auto& h = analysis->history;
    const int n = (int) h.size();
    for (int i = 0; i < fftSize; ++i)
        fftData[(size_t) i] = h[(size_t) ((analysis->historyPos - fftSize + i + n * 2) % n)];
    std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);
    window.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
    fft.performFrequencyOnlyForwardTransform (fftData.data());

    const double sr = analysis->sampleRate > 0.0 ? analysis->sampleRate : 48000.0;
    for (int b = 0; b < numBands; ++b)
    {
        // Log-spaced band edges, 30 Hz .. 18 kHz; each band shows its loudest bin.
        const double lo = 30.0 * std::pow (600.0, (double) b / numBands);
        const double hi = 30.0 * std::pow (600.0, (double) (b + 1) / numBands);
        const int binLo = juce::jlimit (1, fftSize / 2 - 1, (int) (lo * fftSize / sr));
        const int binHi = juce::jlimit (binLo, fftSize / 2 - 1, (int) (hi * fftSize / sr));
        float peak = 0.0f;
        for (int i = binLo; i <= binHi; ++i)
            peak = juce::jmax (peak, fftData[(size_t) i]);
        const float db = juce::Decibels::gainToDecibels (peak * 4.0f / (float) fftSize, -100.0f);
        bands[(size_t) b] = juce::jmax (db, bands[(size_t) b] - 3.0f);
    }
    repaint();
}

void MiniSpectrum::paint (juce::Graphics& g)
{
    const auto& t = Theme::p();
    auto outer = getLocalBounds().toFloat();
    g.setColour (t.meterWell);
    g.fillRoundedRectangle (outer, Theme::controlRadius);
    g.setColour (t.line);
    g.drawRoundedRectangle (outer.reduced (0.5f), Theme::controlRadius, 1.0f);

    auto r = outer.reduced (4.0f, 4.0f);
    g.setColour (t.line.withAlpha (0.45f));
    for (int gy = 1; gy < 4; ++gy)
    {
        const float y = r.getY() + r.getHeight() * (float) gy * 0.25f;
        g.drawHorizontalLine (juce::roundToInt (y), r.getX(), r.getRight());
    }

    // Bars, -72 .. 0 dB, in the meters' green (it is a level display, like the meters).
    const float bw = r.getWidth() / numBands;
    for (int b = 0; b < numBands; ++b)
    {
        const float prop = juce::jlimit (0.0f, 1.0f, (bands[(size_t) b] + 72.0f) / 72.0f);
        const float h = prop * r.getHeight();
        g.setColour (t.meterLow.withAlpha (0.12f));
        g.fillRect (r.getX() + b * bw + 0.5f, r.getY(), bw - 1.0f, r.getHeight());
        if (h > 0.5f)
        {
            g.setColour (t.meterLow);
            g.fillRect (r.getX() + b * bw + 0.5f, r.getBottom() - h, bw - 1.0f, h);
        }
    }

    if (hasKeyboardFocus (false))
        Theme::drawFocusRing (g, getLocalBounds().toFloat());
}

bool MiniSpectrum::keyPressed (const juce::KeyPress& k)
{
    if ((k == juce::KeyPress::returnKey || k == juce::KeyPress::spaceKey) && onClick)
    {
        onClick();
        return true;
    }
    return false;
}

//==============================================================================
// AnalyserPanel

AnalyserPanel::AnalyserPanel (const juce::String& busName, std::shared_ptr<BusAnalysis> a)
    : title (busName + ": analyser and loudness"), analysis (std::move (a))
{
    setTitle (title);
    levels.fill (-100.0f);

    resetButton.setTooltip ("Start measuring integrated loudness from now.");
    resetButton.onClick = [this] { analysis->loudness.reset(); repaint(); };
    makeClickable (resetButton);
    addAndMakeVisible (resetButton);

    setSize (460, 330);
    startTimerHz (30);
}

void AnalyserPanel::timerCallback()
{
    // Copy the newest fftSize samples (oldest first), window them, transform.
    const auto& h = analysis->history;
    const int n = (int) h.size();
    for (int i = 0; i < fftSize; ++i)
        fftData[(size_t) i] = h[(size_t) ((analysis->historyPos - fftSize + i + n * 2) % n)];
    std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);
    window.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
    fft.performFrequencyOnlyForwardTransform (fftData.data());

    for (int i = 0; i < fftSize / 2; ++i)
    {
        // Hann window loses 6 dB; scale so a full-scale sine reads about 0 dB.
        const float db = juce::Decibels::gainToDecibels (fftData[(size_t) i] * 4.0f / (float) fftSize, -100.0f);
        levels[(size_t) i] = juce::jmax (db, levels[(size_t) i] - 1.5f);   // peak with gentle fall
    }
    repaint();
}

void AnalyserPanel::paint (juce::Graphics& g)
{
    const auto& t = Theme::p();

    g.setColour (t.text);
    g.setFont (Theme::ui (15.0f, true));
    g.drawText (title, titleArea, juce::Justification::centredLeft);

    // Plot: log frequency 20 Hz .. 20 kHz, -90 .. 0 dB
    const auto plot = plotArea.toFloat();
    g.setColour (t.meterWell);
    g.fillRoundedRectangle (plot, Theme::controlRadius);

    const double sr = analysis->sampleRate > 0.0 ? analysis->sampleRate : 48000.0;
    auto xForHz = [&] (double hz) { return plot.getX() + plot.getWidth() * (float) (std::log10 (hz / 20.0) / 3.0); };
    auto yForDb = [&] (float db)  { return plot.getY() + plot.getHeight() * juce::jlimit (0.0f, 1.0f, -db / 90.0f); };

    g.setFont (Theme::mono (10.5f));
    for (double hz : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
    {
        const float x = xForHz (hz);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
        g.setColour (t.textFaint);
        g.drawText (hz >= 1000.0 ? juce::String ((int) (hz / 1000)) + "k" : juce::String ((int) hz),
                    juce::Rectangle<float> (x - 20, plot.getBottom() + 2, 40, 14), juce::Justification::centred);
    }
    for (float db : { -20.0f, -40.0f, -60.0f, -80.0f })
    {
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawHorizontalLine ((int) yForDb (db), plot.getX(), plot.getRight());
    }

    juce::Path spectrum;
    bool started = false;
    for (int i = 1; i < fftSize / 2; ++i)
    {
        const double hz = i * sr / fftSize;
        if (hz < 20.0 || hz > 20000.0)
            continue;
        const float x = xForHz (hz), y = yForDb (levels[(size_t) i]);
        if (! started) { spectrum.startNewSubPath (x, y); started = true; }
        else           spectrum.lineTo (x, y);
    }
    g.setColour (t.meterLow);
    g.strokePath (spectrum, juce::PathStrokeType (1.5f));

    // Loudness numbers
    auto r = numbersArea;
    const auto& l = analysis->loudness;
    const std::pair<const char*, double> values[] { { "Momentary", l.momentary() }, { "Short-term", l.shortTerm() }, { "Integrated", l.integrated() } };
    const int w = r.getWidth() / 3;
    for (const auto& [label, value] : values)
    {
        auto cell = r.removeFromLeft (w);
        Theme::drawLegend (g, label, cell.removeFromTop (16));
        g.setColour (t.text);
        g.setFont (Theme::mono (14.0f));
        g.drawText (lufsText (value), cell, juce::Justification::centredLeft);
    }
}

void AnalyserPanel::resized()
{
    auto r = getLocalBounds().reduced (Theme::s3);
    auto top = r.removeFromTop (30);
    resetButton.setBounds (top.removeFromRight (140));
    titleArea = top;
    r.removeFromTop (Theme::s2);
    numbersArea = r.removeFromBottom (40);
    r.removeFromBottom (Theme::s3 + 14);
    plotArea = r;
}
