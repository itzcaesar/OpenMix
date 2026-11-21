#pragma once

#include <JuceHeader.h>

// Design tokens for both themes. Components read Theme::p() at paint time, so switching theme
// is just setDark() + repaint. Direction and reasons: docs/DESIGN.md.
//
// Contrast (WCAG, checked with the antislop contrast script):
//   dark:  text/panel 13.9, textDim/panel 7.5, textFaint/panel 5.0, textFaint/window 5.7,
//          onAccent/accent 8.2, tapeText/tape 12.5, white/mute 4.5, controlEdge/panel 3.8 (3:1 bar)
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
            juce::Colour (0xff141414), juce::Colour (0xff202020), juce::Colour (0xff0f0f0f),
            juce::Colour (0xff0b0b0b), juce::Colour (0xff2c2c2c), juce::Colour (0xff333333),
            juce::Colour (0xff7a7a7a),
            juce::Colour (0xffededed), juce::Colour (0xffb0b0b0), juce::Colour (0xff8f8f8f),
            juce::Colour (0xffe6dfcc), juce::Colour (0xff1e1e1e), juce::Colour (0xffe6dfcc),
            juce::Colour (0xfff0a23b), juce::Colour (0xff1a1a1a), juce::Colour (0xfff0a23b),
            juce::Colour (0xffd93a40), juce::Colour (0xffffffff),
            juce::Colour (0xffededed),
            juce::Colour (0xff3ec46d), juce::Colour (0xfff0a23b), juce::Colour (0xffe5484d)
        };
        return p;
    }

    inline const Palette& lightPalette()
    {
        static const Palette p {
            false,
            juce::Colour (0xffd8d6d1), juce::Colour (0xffeceae5), juce::Colour (0xffd9d6cf),
            juce::Colour (0xff1c1c1c), juce::Colour (0xfff8f7f4), juce::Colour (0xffc6c3bb),
            juce::Colour (0xff7d7a73),
            juce::Colour (0xff1b1b1b), juce::Colour (0xff45443f), juce::Colour (0xff5c5a55),
            juce::Colour (0xfffbf6e6), juce::Colour (0xff1e1e1e), juce::Colour (0xff7d7a73),
            // Amber is only 1.9:1 on the light panel, so "on" states also get a dark edge.
            juce::Colour (0xfff0a23b), juce::Colour (0xff1a1a1a), juce::Colour (0xff1b1b1b),
            juce::Colour (0xffd93a40), juce::Colour (0xffffffff),
            juce::Colour (0xff1b1b1b),
            juce::Colour (0xff3ec46d), juce::Colour (0xfff0a23b), juce::Colour (0xffe5484d)
        };
        return p;
    }

    inline const Palette*& currentPalette()
    {
        static const Palette* current = &darkPalette();
        return current;
    }

    inline const Palette& p()                { return *currentPalette(); }
    inline bool isDark()                     { return p().dark; }
    inline void setDark (bool shouldBeDark)  { currentPalette() = shouldBeDark ? &darkPalette() : &lightPalette(); }

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

    // Spacing scale and shapes
    constexpr int s1 = 4, s2 = 8, s3 = 12, s4 = 16, s5 = 24;
    constexpr float controlRadius = 3.0f;
    constexpr float panelRadius = 6.0f;

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
        g.setFont (ui (11.5f, true).withExtraKerningFactor (0.06f));
        g.drawText (label.toUpperCase(), area, just);
    }

    // 2 px focus ring in the theme's focus colour (R-32)
    inline void drawFocusRing (juce::Graphics& g, juce::Rectangle<float> area, float radius = controlRadius)
    {
        g.setColour (p().focus);
        g.drawRoundedRectangle (area.reduced (1.0f), radius, 2.0f);
    }

    // Faceplate behind a strip or bus
    inline void drawFaceplate (juce::Graphics& g, juce::Rectangle<float> area)
    {
        g.setColour (p().panel);
        g.fillRoundedRectangle (area, panelRadius);
        g.setColour (p().line);
        g.drawRoundedRectangle (area.reduced (0.5f), panelRadius, 1.0f);
    }
}
