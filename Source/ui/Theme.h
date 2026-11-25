#pragma once

#include <JuceHeader.h>

// Design tokens for both themes. Components read Theme::p() at paint time, so switching theme
// is just setDark() + repaint. Direction and reasons: docs/DESIGN.md.
//
// Contrast (WCAG, checked with the antislop contrast script):
//   dark:  text/panel 13.9, textDim/panel 7.5, textFaint/panel 5.0, textFaint/window 5.7,
//          onAccent/accent 8.2 (amber; the other accents are 6.3-8.2), tapeText/tape 12.5,
//          white/mute 4.5, controlEdge/panel 3.8 (3:1 bar)
//   light: text/panel 14.3, textDim/panel 8.1, textFaint/panel 5.7, textFaint/window 4.7,
//          onAccent/accent 8.2, tapeText/tape 15.4, white/mute 4.5, controlEdge/panel 3.6 (3:1 bar)
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

    // Accent colours a user can pick (Settings > Accent colour). The accent is the lamp
    // colour: "on" sends, the voice-pad puck, the active pad mode, toggle lamps. All five
    // are light enough that dark text on them stays above 4.5:1 (amber 8.2, sky ~6.3,
    // green ~7.7, violet ~6.4, cyan ~8.2), so onAccent is always the dark text.
    struct AccentColour { const char* name; juce::uint32 argb; };
    inline const AccentColour accentColours[] {
        { "Amber",  0xfff0a23b },   // the default lamp amber
        { "Sky",    0xff5b9ef0 },
        { "Green",  0xff3ec46d },
        { "Violet", 0xffa78bfa },
        { "Cyan",   0xff39c2d7 },
    };
    constexpr int numAccentColours = (int) std::size (accentColours);

    // The live palette is a copy of the dark or light template, with the chosen accent
    // applied. Components read p() at paint time, so changing the accent or theme is
    // setDark()/setAccent() + repaint, with no per-component state.
    inline Palette& current()
    {
        static Palette cur = darkPalette();
        return cur;
    }

    inline const Palette& p()                { return current(); }
    inline bool isDark()                     { return p().dark; }

    inline int& accentIndexStorage()         { static int i = 0; return i; }
    inline int accentIndex()                 { return accentIndexStorage(); }

    inline void applyAccent()
    {
        auto& c = current();
        const auto accent = juce::Colour (accentColours[accentIndex()].argb);
        c.accent = accent;
        c.onAccent = accent.getPerceivedBrightness() > 0.45f ? juce::Colour (0xff1a1a1a)
                                                             : juce::Colours::white;
        // In the light theme an accent is low-contrast on the panel, so "on" states
        // also get a dark edge (the same reason the amber edge is dark there).
        c.accentEdge = c.dark ? accent : juce::Colour (0xff1b1b1b);
    }

    inline void setDark (bool shouldBeDark)
    {
        current() = shouldBeDark ? darkPalette() : lightPalette();
        applyAccent();
    }

    inline void setAccent (int index)
    {
        accentIndexStorage() = juce::jlimit (0, numAccentColours - 1, index);
        applyAccent();
    }

    // Compact density: narrower strips and buses, tighter gaps, shorter plugin rows.
    // Layout code reads the widths and gaps from here at layout time, so toggling it
    // is setCompact() + a relayout.
    inline bool& compactStorage()            { static bool c = false; return c; }
    inline bool isCompact()                  { return compactStorage(); }
    inline void setCompact (bool compact)    { compactStorage() = compact; }

    // Scribble-strip tape colours a user can pick per channel (identification, see DESIGN.md).
    // All are light, so the dark marker text stays above 9:1 on every one of them.
    struct TapeColour { const char* name; juce::uint32 argb; };   // argb 0 = theme's default tape
    inline const TapeColour tapeColours[] {
        { "Masking tape", 0 },
        { "Yellow",       0xfff3d36b },
        { "Green",        0xffa7d7a0 },
        { "Blue",         0xffa6c6ea },
        { "Pink",         0xffefb3c6 },
        { "Grey",         0xffc8c8c8 },
    };
    constexpr int numTapeColours = (int) std::size (tapeColours);

    inline juce::Colour tapeColour (int index)
    {
        if (index <= 0 || index >= numTapeColours)
            return p().tape;
        return juce::Colour (tapeColours[index].argb);
    }

    // Bold channel-colour name bars (mockup style): the tape colour saturated and
    // darkened, with text chosen for contrast (dark on light bars, white on dark ones).
    inline juce::Colour nameBarColour (int index)
    {
        const auto c = tapeColour (index);
        return c.withSaturation (juce::jmin (1.0f, c.getSaturation() * 1.8f))
                .withBrightness (c.getBrightness() * 0.55f);
    }
    inline juce::Colour nameBarTextColour (int index)
    {
        return nameBarColour (index).getPerceivedBrightness() > 0.45f
                 ? juce::Colour (0xff1a1a1a) : juce::Colours::white;
    }

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

    // Silk-screened panel legend ("INPUTS", "INSERTS")
    inline void drawLegend (juce::Graphics& g, const juce::String& label, juce::Rectangle<int> area,
                            juce::Justification just = juce::Justification::centredLeft)
    {
        g.setColour (p().textFaint);
        g.setFont (ui (10.5f, true).withExtraKerningFactor (0.10f));
        g.drawText (label.toUpperCase(), area, just);
    }

    // 2 px focus ring in the theme's focus colour (R-32)
    inline void drawFocusRing (juce::Graphics& g, juce::Rectangle<float> area, float radius = controlRadius)
    {
        g.setColour (p().focus);
        g.drawRoundedRectangle (area.reduced (1.0f), radius, 2.0f);
    }

    // Module panel behind a strip, bus or the soundboard. A slight top-to-bottom shade and a
    // one-pixel top highlight give each module depth against the window (the one gradient
    // in the UI besides control caps; see DESIGN.md).
    inline void drawFaceplate (juce::Graphics& g, juce::Rectangle<float> area)
    {
        const auto& t = p();
        const auto top = t.dark ? juce::Colour (0xff303035) : juce::Colour (0xffe6e6e8);
        g.setGradientFill (juce::ColourGradient::vertical (top, area.getY(), t.panel, area.getY() + juce::jmin (area.getHeight(), 280.0f)));
        g.fillRoundedRectangle (area, panelRadius);
        g.setColour (t.line);
        g.drawRoundedRectangle (area.reduced (0.5f), panelRadius, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (t.dark ? 0.05f : 0.45f));
        g.fillRect (area.getX() + panelRadius, area.getY() + 1.0f, area.getWidth() - 2.0f * panelRadius, 1.0f);
    }
}
