#pragma once

#include <JuceHeader.h>

// Design tokens. Components use these names, never raw colours.
namespace Theme
{
    // Surfaces (darkest -> lightest)
    inline const juce::Colour background { 0xff0b1120 };   // window
    inline const juce::Colour panel      { 0xff151d2e };   // strip / bus cards
    inline const juce::Colour panelDark  { 0xff0f1626 };   // wells: pad, meters, slots
    inline const juce::Colour raised     { 0xff1f2a40 };   // buttons, chips
    inline const juce::Colour outline    { 0xff2b3750 };   // card + control borders
    inline const juce::Colour grid       { 0xff222d44 };   // pad grid, scale ticks

    // Text
    inline const juce::Colour text       { 0xfff1f5f9 };
    inline const juce::Colour textDim    { 0xff94a3b8 };
    inline const juce::Colour textFaint  { 0xff64748b };

    // Accents
    inline const juce::Colour accent     { 0xff38bdf8 };   // primary interactive (sky)
    inline const juce::Colour busA       { 0xff38bdf8 };   // hardware buses
    inline const juce::Colour busB       { 0xffa78bfa };   // virtual buses
    inline const juce::Colour mute       { 0xffef4444 };
    inline const juce::Colour warning    { 0xfff59e0b };
    inline const juce::Colour success    { 0xff22c55e };

    // Meters
    inline const juce::Colour meterLow   { 0xff22c55e };
    inline const juce::Colour meterHigh  { 0xfff59e0b };
    inline const juce::Colour meterClip  { 0xffef4444 };

    // Per-strip identity colours (stripe at the top of each strip)
    inline const juce::Colour stripColours[] { juce::Colour (0xff38bdf8), juce::Colour (0xff34d399),
                                               juce::Colour (0xfff472b6), juce::Colour (0xfffbbf24),
                                               juce::Colour (0xffa78bfa), juce::Colour (0xfffb923c) };

    // Spacing / shape
    constexpr int gap = 8;
    constexpr float radius = 8.0f;
    constexpr float smallRadius = 5.0f;

    // Typography
    inline juce::Font ui (float size, bool bold = false)
    {
        return juce::Font (juce::FontOptions ("Segoe UI", size, bold ? juce::Font::bold : juce::Font::plain));
    }

    inline juce::Font mono (float size)
    {
        return juce::Font (juce::FontOptions ("Cascadia Mono", size, juce::Font::plain));
    }

    // Small upper-case section label ("INPUTS", "OUTPUTS")
    inline void drawSectionLabel (juce::Graphics& g, const juce::String& label, juce::Rectangle<int> area)
    {
        g.setColour (textFaint);
        g.setFont (ui (12.0f, true).withExtraKerningFactor (0.12f));
        g.drawText (label.toUpperCase(), area, juce::Justification::centredLeft);
    }
}
