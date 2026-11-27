#include "ui/SoundboardPanel.h"
#include "ui/Theme.h"
#include "ui/OpenMixLookAndFeel.h"

// Pad colours (the bar along the bottom of a loaded pad). A pad without its own colour takes
// one by position.
static constexpr juce::uint32 padAccents[] {
    0xfff0a23b, 0xffe5484d, 0xff3ec46d, 0xff5b9ef0,
    0xffefb3c6, 0xfff3d36b, 0xffa7d7a0, 0xff8f8f9a
};
static const char* const padColourNames[] { "Orange", "Red", "Green", "Blue", "Pink", "Yellow", "Sage", "Grey" };
static const char* const modeNames[] { "Play once", "Restart", "Loop", "Hold to play" };
static const char* const modeShort[] { "", "Restart", "Loop", "Hold" };
static const char* const pageNames[] { "A", "B", "C", "D" };

static bool isAudioFile (const juce::String& path)
{
    return juce::File (path).hasFileExtension ("wav;mp3;flac;ogg;aif;aiff");
}

//==============================================================================
// One pad: a button with the sound's name and length, lit while it plays, with a progress bar.
// It fires on mouse down (a soundboard should be instant); letting go ends a Hold pad.

class SoundboardPanel::Pad : public juce::Button
{
public:
    Pad() : juce::Button ({})
    {
        makeClickable (*this);
        setWantsKeyboardFocus (true);
        setTriggeredOnMouseDown (true);
    }

    std::function<void()> onSecondary, onRelease;
    int index = 0;
    juce::String name;
    double seconds = 0.0;
    int mode = Soundboard::OneShot, colour = -1;
    float volumeDb = 0.0f, progress = 0.0f;
    bool playing = false;

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
            return;   // right-click opens the menu on mouse up instead of playing
        juce::Button::mouseDown (e);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            setState (buttonNormal);
            if (onSecondary) onSecondary();
            return;
        }
        juce::Button::mouseUp (e);
        if (onRelease) onRelease();
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
        const int key = index % Soundboard::padsPerPage + 1;
        const auto padColour = juce::Colour (padAccents[(size_t) (colour >= 0 ? colour % 8 : index % 8)]);

        if (empty)
        {
            auto fill = t.well;
            if (down)             fill = fill.darker (0.12f);
            else if (highlighted) fill = t.dark ? fill.brighter (0.08f) : fill.darker (0.04f);
            g.setColour (fill);
            g.fillRoundedRectangle (r, Theme::controlRadius);
            g.setColour (highlighted ? t.controlEdge : t.line);
            g.drawRoundedRectangle (r, Theme::controlRadius, 1.0f);

            auto inner = getLocalBounds().reduced (8, 6);
            g.setColour (t.textFaint);
            g.setFont (Theme::mono (10.5f));
            g.drawText (juce::String (key), inner.removeFromTop (14), juce::Justification::centredRight);
            g.setColour (highlighted ? t.text : t.textDim);
            g.setFont (Theme::ui (12.0f));
            g.drawText ("+ Load", inner, juce::Justification::centred);
        }
        else
        {
            auto fill = playing ? t.accent : t.selBg;
            if (down)                          fill = fill.darker (0.12f);
            else if (highlighted && ! playing) fill = t.dark ? fill.brighter (0.10f) : fill.darker (0.04f);

            g.setColour (fill);
            g.fillRoundedRectangle (r, Theme::controlRadius);

            // Bottom bar: the pad's colour, or how far it has played while it plays.
            auto stripeArea = r.withTop (r.getBottom() - 4.0f);
            juce::Path stripe;
            stripe.addRoundedRectangle (stripeArea.getX(), stripeArea.getY(), stripeArea.getWidth(), stripeArea.getHeight(),
                                        Theme::controlRadius, Theme::controlRadius, false, false, true, true);
            if (playing)
            {
                g.setColour (t.onAccent.withAlpha (0.28f));
                g.fillPath (stripe);
                g.saveState();
                g.reduceClipRegion (stripeArea.withWidth (stripeArea.getWidth() * juce::jlimit (0.0f, 1.0f, progress)).toNearestInt());
                g.setColour (t.onAccent);
                g.fillPath (stripe);
                g.restoreState();
            }
            else
            {
                g.setColour (padColour);
                g.fillPath (stripe);
            }

            g.setColour (playing ? t.accentEdge : t.controlEdge);
            g.drawRoundedRectangle (r, Theme::controlRadius, playing ? 1.5f : 1.0f);

            auto inner = getLocalBounds().reduced (8, 5).withTrimmedBottom (4);
            g.setColour (playing ? t.onAccent : t.text);
            g.setFont (Theme::ui (12.0f, true));
            g.drawFittedText (name, inner.removeFromTop (juce::jmax (14, inner.getHeight() - 14)),
                              juce::Justification::centredLeft, 2, 0.85f);

            juce::String detail = juce::String (seconds, 1) + " s";
            if (mode != Soundboard::OneShot) detail << juce::String::fromUTF8 (" \xc2\xb7 ") << modeShort[mode];
            if (std::abs (volumeDb) >= 0.05f) detail << juce::String::fromUTF8 (" \xc2\xb7 ") << (volumeDb > 0 ? "+" : "") << juce::String (volumeDb, 1) << " dB";
            detail << juce::String::fromUTF8 (" \xc2\xb7 Key ") << key;
            g.setColour (playing ? t.onAccent.withAlpha (0.8f) : t.textFaint);
            g.setFont (Theme::mono (10.0f));
            g.drawFittedText (detail, inner, juce::Justification::centredLeft, 1, 0.8f);
        }

        if (hasKeyboardFocus (false))
            Theme::drawFocusRing (g, getLocalBounds().toFloat());
    }
};

//==============================================================================

SoundboardPanel::SoundboardPanel (MixerEngine& e)
    : engine (e), board (e.getSoundboard()),
      meter (e.getSoundboard().peakL, e.getSoundboard().peakR,
             e.getSoundboard().rmsL, e.getSoundboard().rmsR)
{
    setFocusContainerType (FocusContainerType::keyboardFocusContainer);
    setWantsKeyboardFocus (true);

    tape.setText ("Soundboard");
    addAndMakeVisible (tape);

    for (int slot = 0; slot < Soundboard::padsPerPage; ++slot)
    {
        auto pad = std::make_unique<Pad>();
        auto* p = pad.get();
        pad->onClick = [this, p]
        {
            if (board.getPadInfo (p->index).file.isEmpty())
                loadPad (p->index);
            else
                board.trigger (p->index);
        };
        pad->onRelease = [this, p] { board.release (p->index); };
        pad->onSecondary = [this, p] { showPadMenu (p->index); };
        addAndMakeVisible (*pad);
        pads.push_back (std::move (pad));
    }

    for (int pg = 0; pg < (int) pageButtons.size(); ++pg)
    {
        auto& btn = pageButtons[(size_t) pg];
        btn.setButtonText (pageNames[pg]);
        makeLamp (btn, "key");
        btn.setTitle ("Soundboard page " + juce::String (pageNames[pg]));
        btn.setTooltip ("Page " + juce::String (pageNames[pg]) + ": pads " + juce::String (pg * 8 + 1) + "-" + juce::String (pg * 8 + 8)
                        + ". Keys 1-8 play the page shown.");
        btn.onClick = [this, pg] { setPage (pg); };
        addAndMakeVisible (btn);
    }

    volume.setRange (-60.0, 12.0, 0.1);
    volume.setSkewFactorFromMidPoint (-12.0);
    volume.setDoubleClickReturnValue (true, 0.0);
    volume.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 20);
    volume.textFromValueFunction = [] (double v) { return v <= -60.0 ? juce::String ("-inf") : (v > 0 ? "+" : "") + juce::String (v, 1); };
    volume.valueFromTextFunction = [] (const juce::String& s) { return s.trim().startsWithIgnoreCase ("-inf") ? -60.0 : s.getDoubleValue(); };
    volume.setTitle ("Soundboard volume");
    volume.setTooltip ("Soundboard volume in dB. Double-click or Home for 0 dB.");
    volume.setWantsKeyboardFocus (true);
    makeClickable (volume);
    volume.onValueChange = [this]
    {
        board.volumeDb.store ((float) volume.getValue());
        if (onChanged) onChanged();
    };
    addAndMakeVisible (volume);

    meter.setTitle ("Soundboard meter");
    addAndMakeVisible (meter);

    stopButton.setTooltip ("Stop every pad that's playing.");
    stopButton.onClick = [this] { board.stopAll(); };
    makeClickable (stopButton);
    addAndMakeVisible (stopButton);

    for (int b = 0; b < kNumBuses; ++b)
    {
        auto& btn = sendButtons[(size_t) b];
        btn.setButtonText (engine.getBusName (b));
        makeLamp (btn, "accent");
        btn.setTitle ("Play the soundboard on " + engine.getBusName (b));
        btn.onClick = [this, b] { board.busOn[b].store (sendButtons[(size_t) b].getToggleState()); if (onChanged) onChanged(); };
        addChildComponent (btn);
    }

    refresh();
    startTimerHz (30);
}

SoundboardPanel::~SoundboardPanel() = default;

void SoundboardPanel::setPage (int newPage)
{
    page = juce::jlimit (0, (int) pageButtons.size() - 1, newPage);
    refresh();
}

void SoundboardPanel::refresh()
{
    for (int slot = 0; slot < Soundboard::padsPerPage; ++slot)
    {
        const int i = padAt (slot);
        const auto info = board.getPadInfo (i);
        auto& pad = *pads[(size_t) slot];
        pad.index = i;
        pad.name = info.name;
        pad.seconds = info.seconds;
        pad.mode = info.mode;
        pad.colour = info.colour;
        pad.volumeDb = info.volumeDb;
        pad.playing = board.isPlaying (i);
        const juce::String key (slot + 1);
        pad.setTitle (info.name.isEmpty() ? "Pad " + juce::String (i + 1) + ", empty. Press Enter to load a sound."
                                          : "Pad " + juce::String (i + 1) + ", " + info.name + ", " + modeNames[info.mode]);
        pad.setTooltip (info.name.isEmpty()
            ? juce::String ("Load a sound (WAV, MP3, FLAC, Ogg, up to 60 s), or drop a file here.")
            : info.mode == Soundboard::Hold ? "Hold the pad or key " + key + " to play. Right-click (or M) for volume, mode, colour and more."
            : info.mode == Soundboard::Restart ? "Click or press " + key + " to play from the start. Right-click (or M) for volume, mode, colour and more."
            : "Click or press " + key + " to play, again to stop. Right-click (or M) for volume, mode, colour and more.");
        pad.repaint();
    }

    for (int pg = 0; pg < (int) pageButtons.size(); ++pg)
        pageButtons[(size_t) pg].setToggleState (pg == page, juce::dontSendNotification);

    volume.setValue (board.volumeDb.load(), juce::dontSendNotification);
    for (int b = 0; b < kNumBuses; ++b)
    {
        sendButtons[(size_t) b].setButtonText (engine.getBusName (b));
        sendButtons[(size_t) b].setTitle ("Play the soundboard on " + engine.getBusName (b));
        sendButtons[(size_t) b].setToggleState (board.busOn[b].load(), juce::dontSendNotification);
        sendButtons[(size_t) b].setVisible (engine.getBus (b).active.load());
    }
    resized();
    repaint();
}

void SoundboardPanel::timerCallback()
{
    meter.tick();

    for (auto& p : pads)
    {
        const bool now = board.isPlaying (p->index);
        const float progress = board.getProgress (p->index);
        if (now != p->playing || (now && std::abs (progress - p->progress) > 0.002f))
        {
            p->playing = now;
            p->progress = progress;
            p->repaint();
        }
    }

    // A page key lights while any of its pads plays, so a sound on a hidden page is visible.
    for (int pg = 0; pg < (int) pageButtons.size(); ++pg)
    {
        bool any = false;
        for (int s = 0; s < Soundboard::padsPerPage && ! any; ++s)
            any = board.isPlaying (pg * Soundboard::padsPerPage + s);
        const juce::String text = juce::String (pageNames[pg]) + (any && pg != page ? juce::String::fromUTF8 ("\xe2\x80\xa2") : juce::String());
        if (pageButtons[(size_t) pg].getButtonText() != text)
            pageButtons[(size_t) pg].setButtonText (text);
    }
}

bool SoundboardPanel::pressKey (const juce::KeyPress& k)
{
    const int c = k.getKeyCode();
    if (c < '1' || c >= '1' + Soundboard::padsPerPage || k.getModifiers().isAnyModifierKeyDown())
        return false;

    const int slot = c - '1';
    if (keyHeld[(size_t) slot] == 0)   // key repeat while held does nothing
    {
        keyHeld[(size_t) slot] = padAt (slot) + 1;
        board.trigger (padAt (slot));
    }
    return true;
}

void SoundboardPanel::releaseKeys()
{
    for (int slot = 0; slot < Soundboard::padsPerPage; ++slot)
        if (keyHeld[(size_t) slot] != 0 && ! juce::KeyPress::isKeyCurrentlyDown ('1' + slot))
        {
            board.release (keyHeld[(size_t) slot] - 1);
            keyHeld[(size_t) slot] = 0;
        }
}

bool SoundboardPanel::keyPressed (const juce::KeyPress& k)
{
    return pressKey (k);
}

bool SoundboardPanel::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (isAudioFile (f))
            return true;
    return false;
}

void SoundboardPanel::filesDropped (const juce::StringArray& files, int x, int y)
{
    // Dropped on a pad: that pad, then the next ones for more files. Elsewhere: the page's empty pads.
    int target = -1;
    for (auto& p : pads)
        if (p->getBounds().contains (x, y))
            target = p->index;

    juce::StringArray errors;
    int next = target >= 0 ? target : padAt (0);
    for (auto& f : files)
    {
        if (! isAudioFile (f))
            continue;
        if (target < 0)
            while (next < Soundboard::numPads && board.getPadInfo (next).file.isNotEmpty())
                ++next;
        if (next >= Soundboard::numPads)
            break;
        juce::String error;
        if (! board.loadPad (next, juce::File (f), error))
            errors.add (error);
        ++next;
    }

    if (! errors.isEmpty())
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Couldn't load every sound", errors.joinIntoString ("\n"));
    refresh();
    if (onChanged) onChanged();
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

void SoundboardPanel::showPadVolume (int index)
{
    auto slider = std::make_unique<DbSlider> (juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
    slider->setRange (-60.0, 12.0, 0.1);
    slider->setSkewFactorFromMidPoint (-12.0);
    slider->setDoubleClickReturnValue (true, 0.0);
    slider->setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 20);
    slider->textFromValueFunction = [] (double v) { return v <= -60.0 ? juce::String ("-inf") : (v > 0 ? "+" : "") + juce::String (v, 1); };
    slider->valueFromTextFunction = [] (const juce::String& s) { return s.trim().startsWithIgnoreCase ("-inf") ? -60.0 : s.getDoubleValue(); };
    slider->setValue (board.getPadInfo (index).volumeDb, juce::dontSendNotification);
    slider->setTitle ("Pad " + juce::String (index + 1) + " volume");
    slider->setTooltip ("This pad's volume in dB, before the soundboard volume. Double-click or Home for 0 dB.");
    slider->setSize (260, 32);
    juce::Component::SafePointer<SoundboardPanel> safe (this);
    auto* s = slider.get();
    s->onValueChange = [safe, s, index]
    {
        if (safe == nullptr)
            return;
        safe->board.setPadVolume (index, (float) s->getValue());
        safe->refresh();
        if (safe->onChanged) safe->onChanged();
    };

    for (auto& p : pads)
        if (p->index == index)
        {
            juce::CallOutBox::launchAsynchronously (std::move (slider), p->getScreenBounds(), nullptr);
            s->grabKeyboardFocus();
            break;
        }
}

void SoundboardPanel::showPadMenu (int index)
{
    const auto info = board.getPadInfo (index);
    const bool loaded = info.file.isNotEmpty();
    auto changed = [this] { refresh(); if (onChanged) onChanged(); };

    juce::PopupMenu menu, modes, colours;
    for (int m = 0; m < Soundboard::numModes; ++m)
        modes.addItem (modeNames[m], true, info.mode == m, [this, index, m, changed] { board.setPadMode (index, m); changed(); });
    colours.addItem ("By position", true, info.colour < 0, [this, index, changed] { board.setPadColour (index, -1); changed(); });
    for (int c = 0; c < 8; ++c)
        colours.addItem (padColourNames[c], true, info.colour == c, [this, index, c, changed] { board.setPadColour (index, c); changed(); });

    menu.addItem (loaded ? "Replace sound..." : "Load sound...", [this, index] { loadPad (index); });
    menu.addItem ("Rename...", loaded, false, [this, index] { renamePad (index); });
    menu.addItem ("Volume...", loaded, false, [this, index] { showPadVolume (index); });
    menu.addSubMenu ("Mode", modes, loaded);
    menu.addSubMenu ("Colour", colours, loaded);
    menu.addSeparator();
    menu.addItem ("Clear", loaded, false, [this, index, changed] { board.clearPad (index); changed(); });

    juce::Component* target = this;
    for (auto& p : pads)
        if (p->index == index)
            target = p.get();
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target));
}

void SoundboardPanel::paint (juce::Graphics& g)
{
    Theme::drawFaceplate (g, getLocalBounds().toFloat());
    Theme::drawLegend (g, "Page " + juce::String (pageNames[page]) + " (keys 1-8)", padsLegend);
    Theme::drawLegend (g, "Volume", volumeLegend);
    Theme::drawLegend (g, "Play on", sendsLegend);
}

void SoundboardPanel::resized()
{
    auto r = getLocalBounds().reduced (Theme::s3);
    constexpr int rowH = 28;

    // Left: name, page keys and Stop all.
    auto left = r.removeFromLeft (148);
    tape.setBounds (left.removeFromTop (rowH));
    stopButton.setBounds (left.removeFromBottom (rowH));
    {
        auto pagesRow = left.withSizeKeepingCentre (left.getWidth(), juce::jmin (left.getHeight() - 2 * Theme::s1, 24));
        const int n = (int) pageButtons.size();
        const int w = (pagesRow.getWidth() - (n - 1) * Theme::s1) / n;
        for (int pg = 0; pg < n; ++pg)
        {
            pageButtons[(size_t) pg].setBounds (pagesRow.removeFromLeft (w));
            pagesRow.removeFromLeft (Theme::s1);
        }
    }
    r.removeFromLeft (Theme::s4);

    // Right: volume on top, the buses it plays on below.
    int visibleSends = 0;
    for (auto& b : sendButtons)
        visibleSends += b.isVisible() ? 1 : 0;
    const int keyW = 44;
    const int rightW = 56 + juce::jmax (180, visibleSends * (keyW + Theme::s1));
    auto right = r.removeFromRight (rightW);
    r.removeFromRight (Theme::s3);

    auto volumeRow = right.removeFromTop (rowH);
    volumeLegend = volumeRow.removeFromLeft (56);
    volume.setBounds (volumeRow);
    auto sendsRow = right.removeFromBottom (rowH);
    sendsLegend = sendsRow.removeFromLeft (56);
    for (int i : kBusOrder)
    {
        auto& b = sendButtons[(size_t) i];
        if (! b.isVisible())
            continue;
        b.setBounds (sendsRow.removeFromLeft (keyW));
        sendsRow.removeFromLeft (Theme::s1);
    }

    // Meter between the pads and the controls, full height.
    meter.setBounds (r.removeFromRight (16));
    r.removeFromRight (Theme::s3);

    // Pads: the page's 8 in a row across the middle.
    padsLegend = r.removeFromTop (16);
    r.removeFromTop (Theme::s1);
    const int n = (int) pads.size(), gap = Theme::s1 + 2;
    const int pw = (r.getWidth() - (n - 1) * gap) / n;
    for (int i = 0; i < n; ++i)
        pads[(size_t) i]->setBounds (r.getX() + i * (pw + gap), r.getY(), pw, r.getHeight());
}
