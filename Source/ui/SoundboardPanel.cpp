#include "ui/SoundboardPanel.h"
#include "ui/Theme.h"
#include "ui/OpenMixLookAndFeel.h"

//==============================================================================
// One pad: a button with the sound's name and length, and a lamp while it plays.

class SoundboardPanel::Pad : public juce::Button
{
public:
    Pad (int idx) : juce::Button ({}), index (idx)
    {
        makeClickable (*this);
        setWantsKeyboardFocus (true);
    }

    std::function<void()> onSecondary;
    juce::String name;
    double seconds = 0.0;
    bool playing = false;

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() && onSecondary)
        {
            setState (buttonNormal);
            onSecondary();
            return;
        }
        juce::Button::mouseUp (e);
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if ((k.getKeyCode() == 'M' || k.getKeyCode() == 'm') && onSecondary) { onSecondary(); return true; }
        return juce::Button::keyPressed (k);
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        const auto& t = Theme::p();
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        const bool empty = name.isEmpty();

        auto fill = empty ? t.panel : t.raised;
        if (down)             fill = fill.darker (0.12f);
        else if (highlighted) fill = t.dark ? fill.brighter (0.10f) : fill.darker (0.04f);
        g.setColour (fill);
        g.fillRoundedRectangle (r, Theme::controlRadius);

        if (empty)
        {
            juce::Path outline, dashed;
            outline.addRoundedRectangle (r, Theme::controlRadius);
            const float dashes[] { 4.0f, 3.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dashes, 2);
            g.setColour (t.controlEdge);
            g.fillPath (dashed);
            g.setColour (t.textDim);
            g.setFont (Theme::ui (13.0f));
            g.drawText (juce::String (index + 1) + "   + Load sound", r, juce::Justification::centred);
        }
        else
        {
            g.setColour (playing ? t.accentEdge : t.controlEdge);
            g.drawRoundedRectangle (r, Theme::controlRadius, playing ? 2.0f : 1.0f);

            auto inner = getLocalBounds().reduced (10, 8);
            auto top = inner.removeFromTop (14);
            OpenMixLookAndFeel::drawLamp (g, top.removeFromLeft (10).withSizeKeepingCentre (9, 9).toFloat(), playing, t.accent);
            g.setColour (t.textFaint);
            g.setFont (Theme::mono (11.0f));
            g.drawText (juce::String (index + 1), top.removeFromRight (16), juce::Justification::centredRight);

            g.setColour (t.text);
            g.setFont (Theme::ui (14.0f, true));
            g.drawFittedText (name, inner.removeFromTop (inner.getHeight() - 14), juce::Justification::centredLeft, 2);
            g.setColour (t.textDim);
            g.setFont (Theme::mono (11.0f));
            g.drawText (juce::String (seconds, 1) + " s", inner, juce::Justification::centredLeft);
        }

        if (hasKeyboardFocus (false))
            Theme::drawFocusRing (g, getLocalBounds().toFloat());
    }

private:
    int index;
};

//==============================================================================

SoundboardPanel::SoundboardPanel (MixerEngine& e) : engine (e), board (e.getSoundboard())
{
    setFocusContainerType (FocusContainerType::keyboardFocusContainer);
    setWantsKeyboardFocus (true);

    for (int i = 0; i < Soundboard::numPads; ++i)
    {
        auto pad = std::make_unique<Pad> (i);
        pad->onClick = [this, i]
        {
            if (board.getPadInfo (i).name.isEmpty() && board.getPadInfo (i).file.isEmpty())
                loadPad (i);
            else
                board.trigger (i);
        };
        pad->onSecondary = [this, i] { showPadMenu (i); };
        addAndMakeVisible (*pad);
        pads.push_back (std::move (pad));
    }

    volume.setRange (-60.0, 12.0, 0.1);
    volume.setSkewFactorFromMidPoint (-12.0);
    volume.setDoubleClickReturnValue (true, 0.0);
    volume.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
    volume.textFromValueFunction = [] (double v) { return v <= -60.0 ? juce::String ("-inf") : (v > 0 ? "+" : "") + juce::String (v, 1) + " dB"; };
    volume.setTitle ("Soundboard volume");
    volume.setWantsKeyboardFocus (true);
    makeClickable (volume);
    volume.onValueChange = [this] { board.volumeDb.store ((float) volume.getValue()); if (onChanged) onChanged(); };
    addAndMakeVisible (volume);

    for (int b = 0; b < kNumBuses; ++b)
    {
        auto& btn = sendButtons[(size_t) b];
        btn.setButtonText (kBusNames[b]);
        makeLamp (btn, "accent");
        btn.setTitle ("Play the soundboard on " + juce::String (kBusNames[b]));
        btn.onClick = [this, b] { board.busOn[b].store (sendButtons[(size_t) b].getToggleState()); if (onChanged) onChanged(); };
        addChildComponent (btn);
    }

    refresh();
    setSize (560, 360);
    startTimerHz (15);
}

void SoundboardPanel::refresh()
{
    for (int i = 0; i < Soundboard::numPads; ++i)
    {
        const auto info = board.getPadInfo (i);
        auto& pad = *pads[(size_t) i];
        pad.name = info.name;
        pad.seconds = info.seconds;
        pad.setTitle (info.name.isEmpty() ? "Pad " + juce::String (i + 1) + ", empty. Press Enter to load a sound."
                                          : "Pad " + juce::String (i + 1) + ", " + info.name);
        pad.setTooltip (info.name.isEmpty() ? "Load a sound (WAV, MP3, FLAC, Ogg, up to 60 s)."
                                            : "Click or press " + juce::String (i + 1) + " to play, again to stop. "
                                              "Right-click (or M) to load, rename or clear.");
        pad.repaint();
    }

    volume.setValue (board.volumeDb.load(), juce::dontSendNotification);
    for (int b = 0; b < kNumBuses; ++b)
    {
        sendButtons[(size_t) b].setToggleState (board.busOn[b].load(), juce::dontSendNotification);
        sendButtons[(size_t) b].setVisible (engine.getBus (b).active.load());
    }
    resized();
}

void SoundboardPanel::timerCallback()
{
    for (int i = 0; i < Soundboard::numPads; ++i)
    {
        auto& pad = *pads[(size_t) i];
        const bool now = board.isPlaying (i);
        if (now != pad.playing)
        {
            pad.playing = now;
            pad.repaint();
        }
    }
}

bool SoundboardPanel::keyPressed (const juce::KeyPress& k)
{
    const int c = k.getKeyCode();
    if (c >= '1' && c <= '0' + Soundboard::numPads && ! k.getModifiers().isAnyModifierKeyDown())
    {
        board.trigger (c - '1');
        return true;
    }
    return false;
}

void SoundboardPanel::loadPad (int index)
{
    chooser = std::make_unique<juce::FileChooser> ("Load a sound for pad " + juce::String (index + 1),
                                                   juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                   "*.wav;*.mp3;*.flac;*.ogg;*.aif;*.aiff");
    juce::Component::SafePointer<SoundboardPanel> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe, index] (const juce::FileChooser& fc)
        {
            if (safe == nullptr || fc.getResult() == juce::File())
                return;

            juce::String error;
            if (! safe->board.loadPad (index, fc.getResult(), error))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Couldn't load the sound", error);
            safe->refresh();
            if (safe->onChanged) safe->onChanged();
        });
}

void SoundboardPanel::renamePad (int index)
{
    auto* w = new juce::AlertWindow ("Rename pad " + juce::String (index + 1), {}, juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", board.getPadInfo (index).name, "Name");
    w->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<SoundboardPanel> safe (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w, index] (int result)
    {
        if (safe == nullptr || result != 1)
            return;
        const auto name = w->getTextEditorContents ("name").trim();
        if (name.isNotEmpty())
            safe->board.renamePad (index, name);
        safe->refresh();
        if (safe->onChanged) safe->onChanged();
    }), true);
}

void SoundboardPanel::showPadMenu (int index)
{
    const bool loaded = board.getPadInfo (index).file.isNotEmpty();
    juce::PopupMenu menu;
    menu.addItem (loaded ? "Replace sound..." : "Load sound...", [this, index] { loadPad (index); });
    menu.addItem ("Rename...", loaded, false, [this, index] { renamePad (index); });
    menu.addSeparator();
    menu.addItem ("Clear", loaded, false, [this, index]
    {
        board.clearPad (index);
        refresh();
        if (onChanged) onChanged();
    });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (pads[(size_t) index].get()));
}

void SoundboardPanel::paint (juce::Graphics& g)
{
    g.fillAll (Theme::p().panel);
    Theme::drawLegend (g, "Pads (keys 1-8)", padsLegend);
    Theme::drawLegend (g, "Volume", volumeLegend);
    Theme::drawLegend (g, "Play on", sendsLegend);
}

void SoundboardPanel::resized()
{
    auto r = getLocalBounds().reduced (Theme::s4);

    padsLegend = r.removeFromTop (20);
    auto grid = r.removeFromTop (juce::jmax (120, r.getHeight() - 110));
    const int cols = 4, rows = 2, gap = Theme::s2;
    const int pw = (grid.getWidth() - (cols - 1) * gap) / cols, ph = (grid.getHeight() - (rows - 1) * gap) / rows;
    for (int i = 0; i < (int) pads.size(); ++i)
        pads[(size_t) i]->setBounds (grid.getX() + (i % cols) * (pw + gap), grid.getY() + (i / cols) * (ph + gap), pw, ph);
    r.removeFromTop (Theme::s3);

    auto row = r.removeFromTop (20);
    volumeLegend = row.removeFromLeft (row.getWidth() / 2);
    sendsLegend = row;
    auto controls = r.removeFromTop (30);
    volume.setBounds (controls.removeFromLeft (controls.getWidth() / 2 - Theme::s2));
    controls.removeFromLeft (Theme::s3);
    for (auto& b : sendButtons)
    {
        if (! b.isVisible())
            continue;
        b.setBounds (controls.removeFromLeft (52));
        controls.removeFromLeft (Theme::s1);
    }
}

//==============================================================================

SoundboardWindow::SoundboardWindow (MixerEngine& engine)
    : DocumentWindow ("Soundboard", Theme::p().panel, DocumentWindow::closeButton)
{
    setUsingNativeTitleBar (true);
    panel = new SoundboardPanel (engine);
    setContentOwned (panel, true);
    setResizable (true, false);
    setResizeLimits (420, 300, 1200, 800);
    centreWithSize (getWidth(), getHeight());
}
