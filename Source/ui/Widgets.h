#pragma once

#include <JuceHeader.h>

// Shared controls for the mixer UI. All colours come from Theme::p() at paint time.

void makeLamp (juce::TextButton&, const char* kind);      // "accent", "mute" or "key"
void makeClickable (juce::Component&);                    // pointing-hand cursor (R-39)

//==============================================================================
// Stereo LED-ladder meter with peak hold and a latching clip lamp (click or Enter resets it).
// The thin bar inside each ladder is the RMS level: peak shows transients, RMS shows how
// loud the signal actually sounds.
class LevelMeter : public juce::Component, public juce::SettableTooltipClient
{
public:
    LevelMeter (std::atomic<float>& l, std::atomic<float>& r,
                std::atomic<float>& rmsL, std::atomic<float>& rmsR);
    void tick();
    float getPeakHoldDb() const;   // louder channel's held peak, -100 when silent
    float getRmsDb() const;        // louder channel's RMS, -100 when silent
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override  { resetClip(); }
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override      { repaint(); }
    void focusLost (FocusChangeType) override        { repaint(); }

private:
    struct Channel { float level = 0.0f, hold = 0.0f, rms = 0.0f; int holdFrames = 0; };
    void update (Channel&, float peak, float rms);
    void resetClip() { clipped = false; repaint(); }
    void drawLadder (juce::Graphics&, juce::Rectangle<float>, const Channel&);

    std::atomic<float>& peakL;
    std::atomic<float>& peakR;
    std::atomic<float>& rmsL;
    std::atomic<float>& rmsR;
    Channel left, right;
    bool clipped = false;
};

//==============================================================================
// Slider with console-style keyboard steps: Up/Down 1 dB (Shift 0.1), PageUp/PageDown 6 dB,
// Home = 0 dB. JUCE's default step (the 0.1 dB interval) is too fine to be usable.
class DbSlider : public juce::Slider
{
public:
    using juce::Slider::Slider;
    bool keyPressed (const juce::KeyPress&) override;

    // Keyboard steps (Shift = keyStep / 10) and the Home value. Defaults suit dB controls.
    double keyStep = 1.0, pageStep = 6.0, homeValue = 0.0;
};

//==============================================================================
// Fader + printed dB scale + meter, the bottom of every strip and bus.
class FaderSection : public juce::Component
{
public:
    FaderSection (std::atomic<float>& peakL, std::atomic<float>& peakR,
                  std::atomic<float>& rmsL, std::atomic<float>& rmsR, const juce::String& ownerName);
    void resized() override;
    void paint (juce::Graphics&) override;
    void tick();

    static constexpr int preferredWidth = 104;   // scale, fader, gap, meter

    DbSlider fader { juce::Slider::LinearVertical, juce::Slider::TextBoxAbove };
    LevelMeter meter;

private:
    juce::Rectangle<int> peakArea;   // held-peak readout above the meter
    juce::String peakText;
};

//==============================================================================
// Scribble strip: the channel name on a piece of tape. Double-click, Enter or F2 to rename.
class TapeLabel : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit TapeLabel (bool editable);
    void setText (const juce::String&);
    void setTapeColour (int colourIndex)          { colourIndex_ = colourIndex; applyColours(); repaint(); }
    void setBarColour (juce::Colour c)            { barOverride = c; applyColours(); repaint(); }   // buses: fixed colour
    juce::String getText() const { return label.getText(); }
    std::function<void (const juce::String&)> onRename;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override   { repaint(); }
    void lookAndFeelChanged() override          { applyColours(); }

private:
    void applyColours();
    juce::Colour barColour() const;
    juce::Label label;
    juce::Colour barOverride;   // transparent = use the colour index
    bool editable;
    int colourIndex_ = 0;
};

//==============================================================================
// Picker button: small caption ("MAIN", "APP"), value text, chevron. Shows a warning edge
// plus text when its target is missing, never colour alone.
class ChipButton : public juce::Button
{
public:
    ChipButton() : juce::Button ({}) { makeClickable (*this); }
    void setContent (const juce::String& caption, const juce::String& text, bool problem);
    void paintButton (juce::Graphics&, bool isHighlighted, bool isDown) override;

private:
    juce::String caption, text;
    bool problem = false;
};

//==============================================================================
// A strip key (lamp or lit-key style) with a second action: right-click, M or Shift+Enter.
// onSpace, when set, replaces the normal click for the Space key (e.g. "toggle without opening").
class StripKey : public juce::TextButton
{
public:
    using juce::TextButton::TextButton;
    std::function<void()> onSecondary, onSpace;

    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
};

//==============================================================================
// Three-dot menu button (strip and bus options).
class MenuDotsButton : public juce::Button
{
public:
    MenuDotsButton() : juce::Button ({}) { makeClickable (*this); }
    void paintButton (juce::Graphics&, bool isHighlighted, bool isDown) override;
};

//==============================================================================
// One row of the insert list. States: empty ("Add plugin"), loading, loaded (on / bypassed),
// stopped (the plugin produced invalid audio). Keyboard: Enter open/add, Space bypass,
// Delete remove, Alt+Up/Down move, M or the context-menu key for the menu.
class PluginSlotRow : public juce::Component, public juce::SettableTooltipClient
{
public:
    enum class State { Empty, Loading, Loaded };

    PluginSlotRow();
    void setState (State, const juce::String& name, bool bypassed, bool faulted, int latencySamples, double sampleRate,
                   int mixPercent = 100, const juce::String& abTag = {});
    State getState() const noexcept { return state; }

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override   { repaint(); }

    std::function<void()> onMain, onBypass, onMenu, onRemove;
    std::function<void (int delta)> onMove;                 // keyboard move
    std::function<void (int screenY)> onDragTo;             // mouse drag, live
    std::function<void()> onDragEnd;

private:
    enum class Zone { None, Power, Main, Menu };
    Zone zoneAt (juce::Point<int>) const;
    juce::Rectangle<float> powerArea() const;

    State state = State::Empty;
    juce::String name;
    bool bypassed = false, faulted = false, dragging = false;
    juce::String latencyText, mixText, abTag;
    Zone hover = Zone::None;
};
