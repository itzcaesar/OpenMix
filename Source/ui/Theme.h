#pragma once

#include <JuceHeader.h>

// Design tokens for both themes. Components read Theme::p() at paint time, so switching theme
// is just setDark() + repaint. Direction and reasons: docs/DESIGN.md.
//
// Contrast (WCAG, checked with the antislop contrast script):
//   dark:  text/panel 13.9, textDim/panel 7.5, textFaint/panel 5.0, textFaint/window 5.7,
//          onAccent/accent 8.2 (amber, the only accent), name plates 4.5+ (textOn),
//          white/mute 4.5, controlEdge/panel 3.8 (3:1 bar)
//   light: text/panel 14.3, textDim/panel 8.1, textFaint/panel 5.7, textFaint/window 4.7,
//          onAccent/accent 8.2, name plates 4.5+ (textOn), white/mute 4.5, controlEdge/panel 3.6 (3:1 bar)
namespace Theme
{
    struct Palette
    {
        bool dark;

        juce::Colour window;       // desk surround
        juce::Colour panel;        // channel faceplate
        juce::Colour well;         // recessed areas: pad, fader slot
        juce::Colour meterWell;    // LED meter window (dark in both themes)
        juce::Colour selBg;        // recessed readout / selector surface
        juce::Colour raised;       // button / cap body
        juce::Colour line;         // decorative separators (not a control boundary)
        juce::Colour controlEdge;  // control boundary, >= 3:1 against panel

        juce::Colour text, textDim, textFaint;

        juce::Colour tape, tapeText, tapeEdge;   // scribble strip

        juce::Colour accent, onAccent, accentEdge;   // lamp amber
        juce::Colour mute, onMute;
        juce::Colour focus;

        juce::Colour meterLow, meterMid, meterHigh;
    };

    inline const Palette& darkPalette()
    {
        static const Palette p {
            true,
            juce::Colour (0xff232327), juce::Colour (0xff2b2b30), juce::Colour (0xff1d1d21),
            juce::Colour (0xff101013), juce::Colour (0xff1a1a1e), juce::Colour (0xff3a3a41),
            juce::Colour (0xff3d3d44), juce::Colour (0xff787884),
            juce::Colour (0xffececf0), juce::Colour (0xffb9b9c2), juce::Colour (0xff9696a1),
            juce::Colour (0xffe6dfcc), juce::Colour (0xff1e1e1e), juce::Colour (0xffe6dfcc),
            juce::Colour (0xfff0a23b), juce::Colour (0xff1a1a1a), juce::Colour (0xfff0a23b),
            juce::Colour (0xffd9383e), juce::Colour (0xffffffff),
            juce::Colour (0xffececf0),
            juce::Colour (0xff43d13d), juce::Colour (0xfff5c518), juce::Colour (0xffe5484d)
        };
        return p;
    }

    inline const Palette& lightPalette()
    {
        static const Palette p {
            false,
            juce::Colour (0xffcfcfd2), juce::Colour (0xffdedee0), juce::Colour (0xffc4c4c8),
            juce::Colour (0xff16161a), juce::Colour (0xfff4f4f6), juce::Colour (0xffefeff1),
            juce::Colour (0xffb9b9be), juce::Colour (0xff6d6d76),
            juce::Colour (0xff1c1c20), juce::Colour (0xff45454c), juce::Colour (0xff55555e),
            juce::Colour (0xfffbf6e6), juce::Colour (0xff1e1e1e), juce::Colour (0xff6d6d76),
            // Amber is low-contrast on the light panel, so "on" states also get a dark edge.
            juce::Colour (0xfff0a23b), juce::Colour (0xff1a1a1a), juce::Colour (0xff1c1c20),
            juce::Colour (0xffd9383e), juce::Colour (0xffffffff),
            juce::Colour (0xff1c1c20),
            juce::Colour (0xff43d13d), juce::Colour (0xfff5c518), juce::Colour (0xffe5484d)
        };
        return p;
    }

    // The live palette is a copy of the dark or light template. Components read
    // p() at paint time, so switching theme is setDark() + repaint, with no
    // per-component state.
    inline Palette& current()
    {
        static Palette cur = darkPalette();
        return cur;
    }

    inline const Palette& p()                { return current(); }
    inline bool isDark()                     { return p().dark; }

    inline void setDark (bool shouldBeDark)
    {
        current() = shouldBeDark ? darkPalette() : lightPalette();
    }

    // Compact density: narrower strips and buses, tighter gaps, shorter plugin rows.
    // Layout code reads the widths and gaps from here at layout time, so toggling it
    // is setCompact() + a relayout.
    inline bool& compactStorage()            { static bool c = false; return c; }
    inline bool isCompact()                  { return compactStorage(); }
    inline void setCompact (bool compact)    { compactStorage() = compact; }

    // Name-plate colours a user can pick per channel (identification, see DESIGN.md). The
    // plate is a solid bar; textOn() picks black or white text for contrast on each one.
    struct TapeColour { const char* name; juce::uint32 argb; };
    inline const TapeColour tapeColours[] {
        { "Grey",   0xff55555d },
        { "Teal",   0xff13808a },
        { "Blue",   0xff2846c8 },
        { "Purple", 0xff6a2ab8 },
        { "Pink",   0xffc0287f },
        { "Olive",  0xff7a7a1a },
        { "Orange", 0xffb0561a },
        { "Red",    0xffc42a2a },
        { "Green",  0xff267f3c },
    };
    constexpr int numTapeColours = (int) std::size (tapeColours);

    inline juce::Colour tapeColour (int index)
    {
        return juce::Colour (tapeColours[juce::isPositiveAndBelow (index, numTapeColours) ? index : 0].argb);
    }
    inline juce::Colour nameBarColour (int index) { return tapeColour (index); }

    // WCAG relative luminance, for picking text colours by measured contrast.
    inline double luminance (juce::Colour c)
    {
        auto lin = [] (float v) { return v <= 0.03928f ? v / 12.92 : std::pow ((v + 0.055) / 1.055, 2.4); };
        return 0.2126 * lin (c.getFloatRed()) + 0.7152 * lin (c.getFloatGreen()) + 0.0722 * lin (c.getFloatBlue());
    }

    // Black or white, whichever has the higher contrast on `background` (every plate colour
    // above gives at least 4.5:1 with one of them).
    inline juce::Colour textOn (juce::Colour background)
    {
        const double bg = luminance (background);
        const double withWhite = 1.05 / (bg + 0.05), withBlack = (bg + 0.05) / (luminance (juce::Colour (0xff141414)) + 0.05);
        return withWhite >= withBlack ? juce::Colours::white : juce::Colour (0xff141414);
    }
    inline juce::Colour nameBarTextColour (int index) { return textOn (nameBarColour (index)); }

    // Bus name bars: fixed saturated hues per slot, echoing the mockup
    // (A1 dark gold, B1 red, B2 green). White text reads on all of them.
    inline const juce::Colour busColours[] {
        juce::Colour (0xff7a6c12), juce::Colour (0xff8a6d1f), juce::Colour (0xff5f5610),
        juce::Colour (0xffc22e2e), juce::Colour (0xff1e7a4c), juce::Colour (0xff1e5f8a),
    };
    inline juce::Colour busColour (int index)
    {
        return busColours[(size_t) juce::jlimit (0, (int) std::size (busColours) - 1, index)];
    }

    // Spacing scale and shapes
    constexpr int s1 = 4, s2 = 8, s3 = 12, s4 = 16, s5 = 24;
    constexpr float controlRadius = 3.0f;
    constexpr float panelRadius = 5.0f;

    // Typography
    inline juce::Font ui (float size, bool semibold = false)
    {
        return juce::Font (juce::FontOptions ("Segoe UI", size, semibold ? juce::Font::bold : juce::Font::plain));
    }

    inline juce::Font mono (float size)
    {
        return juce::Font (juce::FontOptions ("Cascadia Mono", size, juce::Font::plain));
    }

    // Small-caps section legend ("INPUT", "INSERT")
    inline void drawLegend (juce::Graphics& g, const juce::String& label, juce::Rectangle<int> area,
                            juce::Justification just = juce::Justification::centredLeft)
    {
        g.setColour (p().textFaint);
        g.setFont (ui (10.0f, true).withExtraKerningFactor (0.04f));
        g.drawText (label.toUpperCase(), area, just);
    }

    // 2 px focus ring in the theme's focus colour (R-32)
    inline void drawFocusRing (juce::Graphics& g, juce::Rectangle<float> area, float radius = controlRadius)
    {
        g.setColour (p().focus);
        g.drawRoundedRectangle (area.reduced (1.0f), radius, 2.0f);
    }

    // Module panel behind a strip, bus or the soundboard: flat, with a one-pixel edge.
    inline void drawFaceplate (juce::Graphics& g, juce::Rectangle<float> area)
    {
        const auto& t = p();
        g.setColour (t.panel);
        g.fillRoundedRectangle (area, 3.0f);
        g.setColour (t.line);
        g.drawRoundedRectangle (area.reduced (0.5f), 3.0f, 1.0f);
    }
}
