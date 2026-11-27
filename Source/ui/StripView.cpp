#include "ui/StripView.h"
#include "ui/Theme.h"
#include "app/Session.h"
#include "engine/AppCapture.h"

// A small rotary knob for the narrow strip (green value ring, see OpenMixLookAndFeel).
static void setupKnob (DbSlider& k, double lo, double hi, double mid, double home)
{
    k.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    k.setRange (lo, hi, 0.1);
    k.setSkewFactorFromMidPoint (mid);
    k.setDoubleClickReturnValue (true, home);
    k.homeValue = home;
    k.setMouseDragSensitivity (160);
    k.setWantsKeyboardFocus (true);
    makeClickable (k);
}

static juce::String dbText (double v)
{
    return v <= -60.0 ? juce::String ("-inf") : (v > 0.05 ? "+" : "") + juce::String (v, 1);
}

namespace
{
    std::array<StripView*, kNumStrips> allStrips {};   // for selection, linked edits and dragging keys
    int selectAnchor = -1;                              // the strip a Shift-click range starts from
    bool linking = false;                               // applying an edit to the other selected strips
    std::unique_ptr<juce::XmlElement> stripClipboard;   // Copy strip
}

static juce::String hzText (double hz)
{
    return hz >= 1000.0 ? juce::String (hz / 1000.0, hz >= 10000.0 ? 0 : 1) + "k" : juce::String (juce::roundToInt (hz));
}

StripView::StripView (MixerEngine& e, int i)
    : engine (e), strip (e.getStrip (i)), index (i),
      inserts (e, strip.inserts),
      faderSection (strip.peakL, strip.peakR, strip.rmsL, strip.rmsR, "Input " + juce::String (i + 1))
{
    allStrips[(size_t) i] = this;

    // Tab moves through this strip top to bottom before moving to the next strip.
    setFocusContainerType (FocusContainerType::keyboardFocusContainer);

    menuButton.onClick = [this] { showStripMenu(); };
    addAndMakeVisible (menuButton);

    sourceButton.onClick = [this] { showSourceMenu(); };
    addAndMakeVisible (sourceButton);

    // Input gain, -24..+24 dB. Double-click or Home resets to 0 dB.
    setupKnob (gainKnob, -24.0, 24.0, 0.0, 0.0);
    gainKnob.getProperties().set ("bipolar", true);
    gainKnob.setTooltip ("Input gain. Drag, scroll or use the arrow keys. Double-click or Home for 0 dB.");
    gainKnob.onValueChange = [this]
    {
        const float before = strip.gainDb.load();
        strip.gainDb.store ((float) gainKnob.getValue());
        repaint (gainLabel);
        forOtherSelected ([d = strip.gainDb.load() - before] (StripView& o)
        {
            o.gainKnob.setValue (juce::jlimit (-24.0, 24.0, o.gainKnob.getValue() + d), juce::sendNotificationSync);
        });
    };
    addAndMakeVisible (gainKnob);

    // High-pass: all the way left is off. Low-pass: all the way right is off.
    setupKnob (hpfKnob, 20.0, 1000.0, 120.0, 20.0);
    hpfKnob.keyStep = 10.0; hpfKnob.pageStep = 100.0;
    hpfKnob.setTooltip ("High-pass filter: cuts rumble below this frequency. All the way left is off. Double-click for off.");
    hpfKnob.onValueChange = [this]
    {
        const double v = hpfKnob.getValue();
        strip.setHpfCutoff ((float) v);
        strip.setHpfEnabled (v > 20.5);
        hpfKnob.getProperties().set ("off", v <= 20.5);
        repaint (hpfLabel);
        forOtherSelected ([v] (StripView& o) { o.hpfKnob.setValue (v, juce::sendNotificationSync); });
    };
    addAndMakeVisible (hpfKnob);

    setupKnob (lpfKnob, 1000.0, 20000.0, 6000.0, 20000.0);
    lpfKnob.keyStep = 500.0; lpfKnob.pageStep = 2000.0;
    lpfKnob.setTooltip ("Low-pass filter: cuts hiss above this frequency. All the way right is off. Double-click for off.");
    lpfKnob.onValueChange = [this]
    {
        const double v = lpfKnob.getValue();
        strip.setLpfCutoff ((float) v);
        strip.setLpfEnabled (v < 19990.0);
        lpfKnob.getProperties().set ("off", v >= 19990.0);
        repaint (lpfLabel);
        forOtherSelected ([v] (StripView& o) { o.lpfKnob.setValue (v, juce::sendNotificationSync); });
    };
    addAndMakeVisible (lpfKnob);

    // Polarity invert and mono (also in the strip menu).
    makeLamp (invertButton, "key");
    invertButton.setTitle ("Invert polarity");
    invertButton.setTooltip (juce::String::fromUTF8 ("Invert the polarity of this strip (\xc3\x98)."));
    invertButton.onClick = [this]
    {
        strip.invertPolarity.store (! strip.invertPolarity.load());
        refreshFromStrip();
        forOtherSelected ([on = strip.invertPolarity.load()] (StripView& o) { o.strip.invertPolarity.store (on); o.refreshFromStrip(); });
    };
    addAndMakeVisible (invertButton);

    makeLamp (monoButton, "key");
    monoButton.setTitle ("Mono");
    monoButton.setTooltip ("Sum this strip to mono.");
    monoButton.onClick = [this]
    {
        strip.mono.store (! strip.mono.load());
        refreshFromStrip();
        forOtherSelected ([on = strip.mono.load()] (StripView& o) { o.strip.mono.store (on); o.refreshFromStrip(); });
    };
    addAndMakeVisible (monoButton);

    inserts.onRowCountChanged = [this] { if (! inLayout && getHeight() > 0) resized(); };
    addAndMakeVisible (inserts);

    // Processing keys: click opens the panel, Space switches the processor on/off directly.
    for (int k = 0; k < (int) processKeys.size(); ++k)
    {
        auto& key = processKeys[(size_t) k];
        const auto proc = (Processor) k;
        key.setButtonText (processorShortName (proc));
        makeLamp (key, "key");
        key.onClick = [this, k] { openProcessorPanel (k); };
        key.onSpace = [this, proc]
        {
            auto& flag = processorOn (strip.fx, proc);
            flag.store (! flag.load());
            updateKeys();
        };
        addAndMakeVisible (key);
    }

    // Sends: the key switches the send, the knob sets its level; right-click / M on the key
    // opens level and pre/post.
    for (int b = 0; b < kNumBuses; ++b)
    {
        auto& btn = sendButtons[(size_t) b];
        makeLamp (btn, "key");
        btn.setClickingTogglesState (true);
        btn.onClick = [this, b]
        {
            const bool on = sendButtons[(size_t) b].getToggleState();
            strip.busOn[b].store (on);
            updateKeys();
            forOtherSelected ([b, on] (StripView& o) { o.sendButtons[(size_t) b].setToggleState (on, juce::sendNotification); });
        };
        btn.onSecondary = [this, b] { openSendPanel (b); };
        addChildComponent (btn);

        auto& knob = sendKnobs[(size_t) b];
        setupKnob (knob, -60.0, 12.0, -12.0, 0.0);
        knob.onValueChange = [this, b]
        {
            const double v = sendKnobs[(size_t) b].getValue();
            strip.sendDb[b].store ((float) v);
            sendKnobs[(size_t) b].setTooltip ("Send level " + dbText (v) + " dB. Double-click or Home for 0 dB.");
            forOtherSelected ([b, v] (StripView& o) { o.sendKnobs[(size_t) b].setValue (v, juce::sendNotificationSync); });
        };
        addChildComponent (knob);
    }

    padButton.setTooltip ("Voice pad: tone and echo, pan, colour and space on one XY pad.");
    makeClickable (padButton);
    padButton.onClick = [this] { openVoicePad(); };
    addAndMakeVisible (padButton);

    makeLamp (muteButton, "mutekey");
    muteButton.setClickingTogglesState (true);
    muteButton.onClick = [this]
    {
        strip.mute.store (muteButton.getToggleState());
        forOtherSelected ([on = muteButton.getToggleState()] (StripView& o) { o.muteButton.setToggleState (on, juce::sendNotification); });
    };
    muteButton.addMouseListener (this, false);   // drag across strips
    addAndMakeVisible (muteButton);

    makeLamp (soloButton, "solokey");
    soloButton.setClickingTogglesState (true);
    soloButton.onClick = [this]
    {
        strip.solo.store (soloButton.getToggleState());
        forOtherSelected ([on = soloButton.getToggleState()] (StripView& o) { o.soloButton.setToggleState (on, juce::sendNotification); });
    };
    soloButton.addMouseListener (this, false);
    addAndMakeVisible (soloButton);

    makeLamp (safeButton, "key");
    safeButton.setClickingTogglesState (true);
    safeButton.setTooltip ("Solo-safe: while anything is soloed, this strip keeps playing on the monitor bus.");
    safeButton.onClick = [this]
    {
        strip.setSoloSafe (safeButton.getToggleState());
        forOtherSelected ([on = safeButton.getToggleState()] (StripView& o) { o.safeButton.setToggleState (on, juce::sendNotification); });
    };
    safeButton.addMouseListener (this, false);
    addAndMakeVisible (safeButton);

    faderSection.fader.onValueChange = [this]
    {
        const float before = strip.faderDb.load();
        strip.faderDb.store ((float) faderSection.fader.getValue());
        forOtherSelected ([d = strip.faderDb.load() - before] (StripView& o)
        {
            auto& f = o.faderSection.fader;
            f.setValue (juce::jlimit (f.getMinimum(), f.getMaximum(), f.getValue() + d), juce::sendNotificationSync);
        });
    };
    addAndMakeVisible (faderSection);

    tape.onRename = [this] (const juce::String& text)
    {
        strip.name = text == "Input " + juce::String (index + 1) ? juce::String() : text;
        refreshFromStrip();
    };
    addAndMakeVisible (tape);
    tape.addMouseListener (this, true);   // clicking the name plate selects the strip

    refreshFromStrip();
}

StripView::~StripView()
{
    allStrips[(size_t) index] = nullptr;
}

//==============================================================================
// Selection and linked edits

void StripView::setSelected (bool b)
{
    if (selected != b)
    {
        selected = b;
        repaint();
    }
}

void StripView::clearSelection()
{
    for (auto* v : allStrips)
        if (v != nullptr)
            v->setSelected (false);
}

void StripView::select (const juce::ModifierKeys& mods)
{
    if (mods.isShiftDown() && selectAnchor >= 0)
    {
        const int lo = juce::jmin (selectAnchor, index), hi = juce::jmax (selectAnchor, index);
        for (auto* v : allStrips)
            if (v != nullptr)
                v->setSelected (v->isVisible() && v->index >= lo && v->index <= hi);
        return;
    }
    selectAnchor = index;
    if (mods.isCommandDown())
    {
        setSelected (! selected);
        return;
    }
    // A plain click selects only this strip; clicking the only selected strip deselects it.
    int count = 0;
    for (auto* v : allStrips)
        count += (v != nullptr && v->selected) ? 1 : 0;
    const bool only = selected && count == 1;
    for (auto* v : allStrips)
        if (v != nullptr)
            v->setSelected (v == this && ! only);
}

void StripView::forOtherSelected (const std::function<void (StripView&)>& fn)
{
    if (! selected || linking)
        return;
    const juce::ScopedValueSetter<bool> guard (linking, true);
    for (auto* v : allStrips)
        if (v != nullptr && v != this && v->selected && v->isVisible())
            fn (*v);
}

void StripView::mouseDown (const juce::MouseEvent& e)
{
    for (int k = 0; k < 3; ++k)
        if (e.eventComponent == &paintKey (k))
        {
            paintKind = k;
            paintValue = ! paintKey (k).getToggleState();
            painting = false;
            lastPaintAt = e.getScreenPosition();
            return;
        }
    paintKind = -1;
    if (e.eventComponent == this || e.eventComponent == &tape || tape.isParentOf (e.eventComponent))
        select (e.mods);
}

void StripView::mouseDrag (const juce::MouseEvent& e)
{
    if (paintKind < 0)
        return;
    const auto at = e.getScreenPosition();
    auto& origin = paintKey (paintKind);
    if (! painting && ! origin.getScreenBounds().contains (at))
    {
        painting = true;   // left the key: from now on it is a drag across strips
        origin.setToggleState (paintValue, juce::sendNotification);
    }
    if (painting)
    {
        // Every key along the path since the last event: a fast drag skips over keys.
        const juce::Line<int> path (lastPaintAt, at);
        for (auto* v : allStrips)
            if (v != nullptr && v->isVisible())
            {
                auto& key = v->paintKey (paintKind);
                if (key.getToggleState() != paintValue && key.getScreenBounds().intersects (path))
                    key.setToggleState (paintValue, juce::sendNotification);
            }
    }
    lastPaintAt = at;
}

void StripView::mouseUp (const juce::MouseEvent&)
{
    // Released back on the first key: its own click toggled it again, so set it back.
    if (paintKind >= 0 && painting && paintKey (paintKind).getToggleState() != paintValue)
        paintKey (paintKind).setToggleState (paintValue, juce::sendNotification);
    paintKind = -1;
    painting = false;
}

void StripView::paintOverChildren (juce::Graphics& g)
{
    if (selected)
    {
        g.setColour (Theme::p().accent);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 6.0f, 2.0f);
    }
}

void StripView::copyStrip()
{
    stripClipboard = Session::copyStrip (engine, index);
}

void StripView::pasteStrip()
{
    if (stripClipboard == nullptr)
        return;
    std::vector<StripView*> targets { this };
    forOtherSelected ([&targets] (StripView& o) { targets.push_back (&o); });
    for (auto* v : targets)
    {
        v->inserts.closeAllEditors();   // their plugins are about to be replaced
        Session::pasteStrip (*stripClipboard, engine, v->index);
        v->refreshFromStrip();
    }
    Session::save (engine);
}

juce::String StripView::displayName() const
{
    return strip.name.isNotEmpty() ? strip.name : "Input " + juce::String (index + 1);
}

void StripView::refreshFromStrip()
{
    const auto name = displayName();
    tape.setText (name);
    tape.setTapeColour (strip.colour);
    tape.setIcon (strip.icon);
    setTitle (name + " strip");
    menuButton.setTitle (name + " options");
    menuButton.setTooltip ("Rename, colour, icon, presets, copy and paste, remove.");

    gainKnob.setTitle (name + " gain");
    gainKnob.setValue (strip.gainDb.load(), juce::dontSendNotification);
    hpfKnob.setTitle (name + " high-pass filter");
    hpfKnob.setValue (strip.fx.hpfOn.load() ? strip.fx.hpfHz.load() : 20.0, juce::dontSendNotification);
    hpfKnob.getProperties().set ("off", ! strip.fx.hpfOn.load());
    lpfKnob.setTitle (name + " low-pass filter");
    lpfKnob.setValue (strip.fx.lpfOn.load() ? strip.fx.lpfHz.load() : 20000.0, juce::dontSendNotification);
    lpfKnob.getProperties().set ("off", ! strip.fx.lpfOn.load());

    invertButton.setToggleState (strip.invertPolarity.load(), juce::dontSendNotification);
    monoButton.setToggleState (strip.mono.load(), juce::dontSendNotification);

    faderSection.fader.setTitle (name + " fader");
    faderSection.fader.setValue (strip.faderDb.load(), juce::dontSendNotification);
    faderSection.meter.setTitle (name + " meter");
    padButton.setTitle (name + " voice pad");

    muteButton.setTitle ("Mute " + name);
    muteButton.setTooltip ("Mute " + name + " on every bus.");
    soloButton.setTitle ("Solo " + name);
    soloButton.setTooltip ("Listen to " + name + " alone on the monitor bus. Other buses (stream, calls) are not affected.");
    safeButton.setTitle ("Solo-safe " + name);

    updateKeys();
    inserts.refresh();
    updateSourceButton();
    repaint();
}

void StripView::refreshDevice()
{
    updateSourceButton();
}

void StripView::tick()
{
    faderSection.tick();
    inserts.tick();

    const double sr = engine.getSampleRate();
    const auto text = "LATENCY " + juce::String (sr > 0.0 ? 1000.0 * strip.latencySamples.load() / sr : 0.0, 1) + " ms";
    if (text != latencyText)
    {
        latencyText = text;
        repaint (latencyArea);
    }
}

//==============================================================================
// Keys, panels, strip menu

void StripView::updateKeys()
{
    const auto name = displayName();
    muteButton.setToggleState (strip.mute.load(), juce::dontSendNotification);
    soloButton.setToggleState (strip.solo.load(), juce::dontSendNotification);
    safeButton.setToggleState (strip.soloSafe.load(), juce::dontSendNotification);

    for (int k = 0; k < (int) processKeys.size(); ++k)
    {
        const auto proc = (Processor) k;
        const bool on = processorOn (strip.fx, proc).load();
        auto& key = processKeys[(size_t) k];
        key.setToggleState (on, juce::dontSendNotification);
        key.setTitle (processorName (proc) + (on ? ", on" : ", off"));
        key.setTooltip (processorName (proc) + (on ? " (on)" : " (off)") + ". Click for settings, Space to switch on or off.");
    }

    for (int b = 0; b < kNumBuses; ++b)
    {
        auto& btn = sendButtons[(size_t) b];
        auto& knob = sendKnobs[(size_t) b];
        const bool busActive = engine.getBus (b).active.load();
        const bool on = strip.busOn[b].load();
        const bool pre = strip.sendPre[b].load();

        // The bus's own name is shown, so a renamed bus reads correctly here.
        const auto busName = engine.getBusName (b);
        btn.setButtonText (busName + (pre ? " P" : ""));
        btn.setToggleState (on, juce::dontSendNotification);
        btn.setTitle ("Send " + name + " to " + busName);
        btn.setTooltip ("Send " + name + " to " + busName + ". Right-click (or M) for level and pre-fader."
                        + (pre ? " Pre-fader." : ""));
        btn.setVisible (busActive);

        knob.setValue (strip.sendDb[b].load(), juce::dontSendNotification);
        knob.setTitle ("Send level " + name + " to " + busName);
        knob.setTooltip ("Send level " + dbText (strip.sendDb[b].load()) + " dB. Double-click or Home for 0 dB.");
        knob.getProperties().set ("off", ! on);
        knob.setVisible (busActive);
        knob.repaint();
    }
}

void StripView::applyPreset (const juce::String& name)
{
    if (name.isEmpty() || engine.findStripPreset (name) == nullptr)
        return;

    selectedPreset = name;
    // The preset replaces the insert chain: close the plugin windows first, or they would
    // keep pointing at plugins that are about to be deleted.
    inserts.closeAllEditors();
    const int missing = engine.applyStripPreset (name, index);
    refreshFromStrip();
    Session::save (engine);

    if (missing > 0)
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Preset applied",
            "Preset \"" + name + "\" was applied, but " + juce::String (missing)
            + (missing == 1 ? " plugin" : " plugins")
            + " could not be found and were skipped. The rest of the settings were applied.");
}

void StripView::deletePreset (const juce::String& name)
{
    if (name.isEmpty() || engine.findStripPreset (name) == nullptr)
        return;
    if (engine.deleteStripPreset (name))
    {
        if (selectedPreset == name)
            selectedPreset.clear();
        Session::save (engine);
    }
}

void StripView::showSavePresetDialog()
{
    auto* w = new juce::AlertWindow ("Save strip preset",
                                     "Saves this strip's sound (gain, fader, processing, inserts, sends, "
                                     "VoicePad) under a name. It never saves the name, source or mute state, "
                                     "so you can recall it on any strip.",
                                     juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", selectedPreset.isNotEmpty() ? selectedPreset : displayName() + " preset", "Preset name");
    w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<StripView> safe (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int result)
    {
        if (safe == nullptr || result != 1)
            return;
        const auto name = w->getTextEditorContents ("name").trim();
        if (name.isEmpty())
            return;
        if (safe->engine.saveStripPreset (name, safe->index))
        {
            safe->selectedPreset = name;
            Session::save (safe->engine);
        }
    }), true);
}

juce::Component* StripView::popupParent()
{
    return getTopLevelComponent();
}

void StripView::openProcessorPanel (int k)
{
    auto panel = std::make_unique<ProcessorPanel> (engine, strip.fx, (Processor) k, index);
    juce::Component::SafePointer<StripView> safe (this);
    panel->onChange = [safe] { if (safe != nullptr) safe->refreshFromStrip(); };   // HPF/LPF knobs too

    auto& key = processKeys[(size_t) k];
    auto* top = popupParent();
    juce::CallOutBox::launchAsynchronously (std::move (panel), top->getLocalArea (&key, key.getLocalBounds()), top);
}

void StripView::openSendPanel (int b)
{
    auto panel = std::make_unique<SendPanel> (engine, index, b);
    juce::Component::SafePointer<StripView> safe (this);
    panel->onChange = [safe] { if (safe != nullptr) safe->updateKeys(); };

    auto& key = sendButtons[(size_t) b];
    auto* top = popupParent();
    juce::CallOutBox::launchAsynchronously (std::move (panel), top->getLocalArea (&key, key.getLocalBounds()), top);
}

void StripView::showStripMenu()
{
    juce::PopupMenu menu;
    menu.addItem ("Rename", [this] { tape.keyPressed (juce::KeyPress (juce::KeyPress::F2Key)); });

    juce::PopupMenu colours;
    for (int c = 0; c < Theme::numTapeColours; ++c)
        colours.addItem (Theme::tapeColours[c].name, true, strip.colour == c, [this, c]
        {
            strip.colour = c;
            refreshFromStrip();
        });
    menu.addSubMenu ("Colour", colours);

    juce::PopupMenu icons;
    for (int i = 0; i < StripIcons::count; ++i)
        icons.addItem (StripIcons::name (i), true, strip.icon == i, [this, i]
        {
            strip.icon = i;
            refreshFromStrip();
            Session::save (engine);
        });
    menu.addSubMenu ("Icon", icons);

    menu.addSeparator();
    menu.addItem ("Mono", true, strip.mono.load(), [this]
    {
        strip.mono.store (! strip.mono.load());
        refreshFromStrip();
    });
    menu.addItem ("Invert polarity", true, strip.invertPolarity.load(), [this]
    {
        strip.invertPolarity.store (! strip.invertPolarity.load());
        refreshFromStrip();
    });
    menu.addItem ("Solo-safe", true, strip.isSoloSafe(), [this]
    {
        strip.setSoloSafe (! strip.isSoloSafe());
        refreshFromStrip();
    });
    menu.addSeparator();

    // Strip presets live here now (no PRESET section on the strip): save the
    // current settings under a name, or recall / delete a saved one.
    juce::PopupMenu presets;
    presets.addItem ("Save preset...", [this] { showSavePresetDialog(); });
    const auto presetNames = engine.getStripPresetNames();
    if (! presetNames.isEmpty())
    {
        presets.addSeparator();
        for (const auto& name : presetNames)
            presets.addItem (name, true, selectedPreset == name, [this, name] { applyPreset (name); });
        juce::PopupMenu del;
        for (const auto& name : presetNames)
            del.addItem (name, true, false, [this, name] { deletePreset (name); });
        presets.addSubMenu ("Delete preset", del);
    }
    menu.addSubMenu ("Strip presets", presets);
    menu.addItem ("Copy strip", [this] { copyStrip(); });
    menu.addItem (selected ? "Paste strip to the selected strips" : "Paste strip", stripClipboard != nullptr, false, [this] { pasteStrip(); });

    menu.addSeparator();
    menu.addItem ("Remove strip...", engine.getNumActiveStrips() > 1, false, [this] { confirmRemove(); });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (menuButton));
}

void StripView::confirmRemove()
{
    const auto name = displayName();
    juce::Component::SafePointer<StripView> safe (this);
    juce::AlertWindow::showAsync (
        juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::QuestionIcon).withTitle ("Remove " + name + "?")
                .withMessage ("Its plugins, source and settings are cleared. This can't be undone.").withButton ("Remove", 1).withButton ("Cancel", 0).withAssociatedComponent (this),
        [safe] (int result)
        {
            if (safe == nullptr || result != 1)
                return;

            safe->inserts.closeAllEditors();
            safe->engine.removeStrip (safe->index);
            if (safe->onLayoutChanged)
                safe->onLayoutChanged();
        });
}

//==============================================================================
// Source

void StripView::updateSourceButton()
{
    juce::String caption, text;
    bool problem = false;

    switch ((SourceType) strip.sourceType.load())
    {
        case SourceType::None:
            caption = "OFF";
            text = "No input";
            break;

        case SourceType::Main:
        {
            caption = "MAIN";
            const auto names = engine.getActiveInputNames();
            const int ch = strip.inputChannel.load();
            problem = ! juce::isPositiveAndBelow (ch, names.size());
            const int right = strip.inputChannelRight.load();
            text = ! problem ? (juce::isPositiveAndBelow (right, names.size()) ? names[ch] + " + " + names[right] : names[ch])
                 : names.isEmpty() ? juce::String ("Choose a source")
                                   : juce::String ("Channel missing");
            break;
        }

        case SourceType::Device:
            caption = "DEVICE";
            problem = strip.getExternalSource() == nullptr;
            text = strip.sourceDeviceName + (problem ? " (unplugged)" : "");
            break;

        case SourceType::App:
            caption = "APP";
            problem = strip.getExternalSource() == nullptr;
            text = strip.sourceAppExe.upToLastOccurrenceOf (".", false, true) + (problem ? " (not running)" : "");
            break;

        case SourceType::Loopback:
            caption = "LOOP";
            problem = strip.getExternalSource() == nullptr;
            text = strip.loopbackName + (problem ? " (unavailable)" : "");
            break;

        case SourceType::Media:
        {
            caption = "MEDIA";
            const auto track = engine.getMediaPlayer().getTrackName();
            text = track.isNotEmpty() ? track : juce::String ("Media player");
            break;
        }
    }

    sourceButton.setContent (caption, text, problem);
    sourceButton.setTitle (displayName() + " source: " + text);
}

void StripView::showSourceMenu()
{
    juce::PopupMenu menu;
    const auto current = (SourceType) strip.sourceType.load();

    menu.addItem ("No input", true, current == SourceType::None, [this]
    {
        engine.setStripSourceNone (index);
        updateSourceButton();
    });

    menu.addSectionHeader ("Main device");
    const auto mainInputs = engine.getActiveInputNames();
    if (mainInputs.isEmpty())
        menu.addItem ("No inputs enabled. Turn some on in Audio settings.", false, false, nullptr);

    const bool isStereo = strip.inputChannelRight.load() >= 0;
    for (int i = 0; i < mainInputs.size(); ++i)
        menu.addItem (mainInputs[i], true, current == SourceType::Main && ! isStereo && strip.inputChannel.load() == i, [this, i]
        {
            engine.setStripSourceMain (index, i);
            updateSourceButton();
        });

    if (mainInputs.size() >= 2)
    {
        juce::PopupMenu pairs;
        for (int i = 0; i + 1 < mainInputs.size(); i += 2)
            pairs.addItem (mainInputs[i] + " + " + mainInputs[i + 1], true,
                           current == SourceType::Main && isStereo && strip.inputChannel.load() == i, [this, i]
            {
                engine.setStripSourceMain (index, i, i + 1);
                updateSourceButton();
            });
        menu.addSubMenu ("Stereo pair", pairs);
    }

    menu.addSectionHeader ("Other sources");

    juce::PopupMenu devices;
    const auto inputNames = engine.getWindowsInputNames();
    if (inputNames.isEmpty())
        devices.addItem ("No Windows input devices found", false, false, nullptr);
    for (const auto& name : inputNames)
        devices.addItem (name, true, current == SourceType::Device && strip.sourceDeviceName == name, [this, name]
        {
            juce::String error;
            if (! engine.setStripSourceDevice (index, name, error))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Couldn't open " + name, error);
            updateSourceButton();
        });
    menu.addSubMenu ("Windows input device", devices);

    juce::PopupMenu apps;
    const auto running = listAudioApps();
    if (running.empty())
        apps.addItem ("No apps are playing sound. Start one, then open this menu again.", false, false, nullptr);

    for (const auto& app : running)
    {
        const juce::String exe (app.exeName.c_str());
        const auto label = exe.upToLastOccurrenceOf (".", false, true) + (app.active ? "  (playing)" : "");
        apps.addItem (label, true, current == SourceType::App && strip.sourceAppExe.equalsIgnoreCase (exe), [this, exe]
        {
            juce::String error;
            if (! engine.setStripSourceApp (index, exe, error))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Couldn't capture " + exe, error);
            updateSourceButton();
        });
    }
    menu.addSubMenu ("Application audio", apps);

    // Everything an output device plays. Devices OpenMix plays into are left out: capturing
    // them on a strip that feeds the same bus would loop the sound back (howling feedback).
    juce::PopupMenu loops;
    const auto own = engine.getOwnOutputDeviceNames();
    loops.addSectionHeader ("Capture everything a device plays");
    for (const auto& d : listRenderDevices())
    {
        const juce::String id (d.id.c_str()), name (d.name.c_str());
        const bool usedByOpenMix = own.contains (name);
        loops.addItem (usedByOpenMix ? name + "  (OpenMix plays here: would feed back)" : name, ! usedByOpenMix,
                       current == SourceType::Loopback && strip.loopbackId == id, [this, id, name]
        {
            juce::String error;
            if (! engine.setStripSourceLoopback (index, id, name, error))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Couldn't capture " + name, error);
            updateSourceButton();
        });
    }
    menu.addSubMenu ("Windows output (loopback)", loops);

    menu.addItem ("Media player", true, current == SourceType::Media, [this]
    {
        engine.setStripSourceMedia (index);
        updateSourceButton();
        if (onOpenMediaPlayer) onOpenMediaPlayer();
    });

    // Where the captured app's own sound goes, so it isn't heard twice.
    if (current == SourceType::App)
    {
        juce::PopupMenu route;
        route.addItem ("Leave it on its usual output", true, strip.appOutputDeviceId.isEmpty(), [this]
        {
            juce::String error;
            engine.setStripAppOutput (index, {}, {}, error);
            updateSourceButton();
        });
        route.addSectionHeader ("Send its own output to (pick one you don't listen to)");
        for (const auto& d : listRenderDevices())
        {
            const juce::String id (d.id.c_str()), name (d.name.c_str());
            route.addItem (name, true, strip.appOutputDeviceId == id, [this, id, name]
            {
                juce::String error;
                if (! engine.setStripAppOutput (index, id, name, error))
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                            "Couldn't move the app's output", error);
                updateSourceButton();
            });
        }
        menu.addSubMenu ("App's own output", route);
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (sourceButton).withMinimumWidth (sourceButton.getWidth()));
}

//==============================================================================
// Paint + layout

void StripView::paint (juce::Graphics& g)
{
    const auto& t = Theme::p();
    Theme::drawFaceplate (g, getLocalBounds().toFloat());

    Theme::drawLegend (g, "Input " + juce::String (index + 1), inputLegend, juce::Justification::centred);
    Theme::drawLegend (g, "Processing", processLegend, juce::Justification::centred);
    Theme::drawLegend (g, "Insert", insertsLegend, juce::Justification::centred);
    Theme::drawLegend (g, "Sends", sendsLegend, juce::Justification::centred);

    g.setColour (t.line);
    for (const int y : dividerYs)
        g.fillRect (0, y, getWidth(), 1);

    // Knob captions above, values below.
    auto knobText = [&] (juce::Rectangle<int> area, const char* name, const juce::String& value, bool off)
    {
        if (area.isEmpty())
            return;
        g.setFont (Theme::ui (10.0f, true));
        g.setColour (t.textFaint);
        g.drawText (name, area.removeFromTop (11), juce::Justification::centred);
        area.removeFromTop (gainKnob.getHeight());
        g.setFont (Theme::mono (10.0f));
        g.setColour (off ? t.textFaint : t.textDim);
        g.drawText (value, area, juce::Justification::centred);
    };
    knobText (gainLabel, "GAIN", dbText (strip.gainDb.load()), false);
    knobText (hpfLabel, "HPF", strip.fx.hpfOn.load() ? hzText (strip.fx.hpfHz.load()) : juce::String ("off"), ! strip.fx.hpfOn.load());
    knobText (lpfLabel, "LPF", strip.fx.lpfOn.load() ? hzText (strip.fx.lpfHz.load()) : juce::String ("off"), ! strip.fx.lpfOn.load());

    // A hairline above the fader bay, the same height on every column.
    g.setColour (t.line);
    g.fillRect (0, (bayTop > 0 ? bayTop : getHeight() / 2) - 4, getWidth(), 1);

    g.setFont (Theme::ui (9.5f, true));
    g.setColour (t.textFaint);
    g.drawText (latencyText, latencyArea, juce::Justification::centred);
}

void StripView::resized()
{
    const juce::ScopedValueSetter<bool> layoutGuard (inLayout, true);
    constexpr int sidePad = 5, legendH = 12, gap = 4, keyH = 19;
    auto r = getLocalBounds().reduced (sidePad, sidePad);
    const int split = bayTop > 0 ? bayTop : getHeight() / 2;
    auto top = r.withBottom (split - sidePad);
    auto bay = r.withTop (split);
    dividerYs.clear();

    // INPUT n (legend) + options, source, knobs, polarity / mono.
    {
        auto row = top.removeFromTop (16);
        menuButton.setBounds (row.removeFromRight (18));
        inputLegend = row.withTrimmedLeft (18);
    }
    top.removeFromTop (2);
    sourceButton.setBounds (top.removeFromTop (22));
    top.removeFromTop (gap);
    {
        constexpr int knobD = 28;
        auto row = top.removeFromTop (11 + knobD + 11);
        const int w = row.getWidth() / 3;
        juce::Rectangle<int>* labels[] { &gainLabel, &hpfLabel, &lpfLabel };
        DbSlider* knobs[] { &gainKnob, &hpfKnob, &lpfKnob };
        for (int k = 0; k < 3; ++k)
        {
            auto cell = k == 2 ? row : row.removeFromLeft (w);
            *labels[k] = cell;
            knobs[k]->setBounds (cell.withTrimmedTop (11).withHeight (knobD).withSizeKeepingCentre (knobD, knobD));
        }
    }
    top.removeFromTop (gap);
    {
        auto row = top.removeFromTop (keyH);
        const int bw = (row.getWidth() - 3) / 2;
        invertButton.setBounds (row.removeFromLeft (bw));
        row.removeFromLeft (3);
        monoButton.setBounds (row);
    }
    top.removeFromTop (gap);
    dividerYs.push_back (top.getY() - 2);

    // PROCESSING keys, 2 columns x 3 rows.
    processLegend = top.removeFromTop (legendH);
    {
        auto grid = top.removeFromTop (3 * keyH + 2 * 3);
        const int kw = (grid.getWidth() - 3) / 2;
        for (int k = 0; k < (int) processKeys.size(); ++k)
            processKeys[(size_t) k].setBounds (grid.getX() + (k % 2) * (kw + 3), grid.getY() + (k / 2) * (keyH + 3),
                                               k % 2 == 0 ? kw : grid.getWidth() - kw - 3, keyH);
    }
    top.removeFromTop (gap);
    dividerYs.push_back (top.getY() - 2);

    // Bottom of the top section: SENDS (one row per active bus), then the voice pad button.
    const int nSends = engine.getNumActiveBuses();
    constexpr int sendH = 22;
    padButton.setBounds (top.removeFromBottom (keyH));
    top.removeFromBottom (gap);
    auto sendsBlock = top.removeFromBottom (legendH + nSends * sendH);
    top.removeFromBottom (gap);
    dividerYs.push_back (sendsBlock.getY() - 3);

    sendsLegend = sendsBlock.removeFromTop (legendH);
    for (int b : kBusOrder)
    {
        if (! engine.getBus (b).active.load())
            continue;
        auto row = sendsBlock.removeFromTop (sendH);
        sendKnobs[(size_t) b].setBounds (row.removeFromRight (sendH));
        row.removeFromRight (3);
        sendButtons[(size_t) b].setBounds (row.reduced (0, 1));
    }

    // INSERT: whatever is left in the middle.
    insertsLegend = top.removeFromTop (legendH);
    const int insertsAvail = top.getHeight();
    const int rackH = insertsAvail <= 0 ? 0
        : juce::jlimit (InsertRack::heightForRows (1), InsertRack::heightForRows (kNumSlots), insertsAvail);
    inserts.setBounds (rackH > 0 ? top.removeFromTop (rackH) : juce::Rectangle<int>());

    // Fader bay: name plate, latency, M / S / SAFE along the bottom, fader + meter above.
    tape.setBounds (bay.removeFromBottom (24));
    latencyArea = bay.removeFromBottom (14);
    auto keys = bay.removeFromBottom (20);
    {
        const int w = (keys.getWidth() - 6) / 3;
        muteButton.setBounds (keys.removeFromLeft (w));
        keys.removeFromLeft (3);
        soloButton.setBounds (keys.removeFromLeft (w));
        keys.removeFromLeft (3);
        safeButton.setBounds (keys);
    }
    bay.removeFromBottom (3);
    faderSection.setBounds (bay);
}

void StripView::openVoicePad()
{
    auto padView = std::make_unique<VoicePadComponent> (strip.pad, displayName());
    padView->setSize (240, VoicePadComponent::headerHeight + 170);
    auto* top = popupParent();
    juce::CallOutBox::launchAsynchronously (std::move (padView), top->getLocalArea (&padButton, padButton.getLocalBounds()), top);
}
