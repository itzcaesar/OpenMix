#include "ui/StripView.h"
#include "ui/Theme.h"
#include "app/Session.h"
#include "engine/AppCapture.h"

StripView::StripView (MixerEngine& e, int i)
    : engine (e), strip (e.getStrip (i)), index (i),
      inserts (e, strip.inserts),
      pad (strip.pad, "Input " + juce::String (i + 1)),
      faderSection (strip.peakL, strip.peakR, strip.rmsL, strip.rmsR, "Input " + juce::String (i + 1))
{
    // Tab moves through this strip top to bottom before moving to the next strip.
    setFocusContainerType (FocusContainerType::keyboardFocusContainer);

    tape.onRename = [this] (const juce::String& text)
    {
        strip.name = text == "Input " + juce::String (index + 1) ? juce::String() : text;
        refreshFromStrip();
    };
    addAndMakeVisible (tape);

    menuButton.onClick = [this] { showStripMenu(); };
    addAndMakeVisible (menuButton);

    sourceButton.onClick = [this] { showSourceMenu(); };
    addAndMakeVisible (sourceButton);

    // Input gain: a horizontal slider directly under the source name, -24..+24 dB
    // with a value readout; double-click (or Home) resets to 0 dB.
    gainSlider.setRange (-24.0, 24.0, 0.1);
    gainSlider.setDoubleClickReturnValue (true, 0.0);
    gainSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
    gainSlider.textFromValueFunction = [] (double v) { return (v > 0.05 ? "+" : "") + juce::String (v, 1) + " dB"; };
    gainSlider.valueFromTextFunction = [] (const juce::String& s) { return s.getDoubleValue(); };
    gainSlider.setTooltip ("Input gain. Drag, scroll or use the arrow keys. Double-click or Home for 0 dB.");
    gainSlider.setWantsKeyboardFocus (true);
    makeClickable (gainSlider);
    gainSlider.onValueChange = [this] { strip.gainDb.store ((float) gainSlider.getValue()); };
    addAndMakeVisible (gainSlider);

    // Polarity invert and mono, below the gain slider (also in the strip menu).
    makeLamp (invertButton, "key");
    invertButton.setTitle ("Invert polarity");
    invertButton.setTooltip ("Invert the polarity of this strip (Ø).");
    invertButton.onClick = [this]
    {
        strip.invertPolarity.store (! strip.invertPolarity.load());
        refreshFromStrip();
    };
    addAndMakeVisible (invertButton);

    makeLamp (monoButton, "key");
    monoButton.setTitle ("Mono");
    monoButton.setTooltip ("Sum this strip to mono.");
    monoButton.onClick = [this]
    {
        strip.mono.store (! strip.mono.load());
        refreshFromStrip();
    };
    addAndMakeVisible (monoButton);

    // Filter: HPF/LPF knobs with on/off, before the processing keys.
    hpfKnob.setRange (20.0, 1000.0, 1.0);
    hpfKnob.setSkewFactorFromMidPoint (80.0);
    hpfKnob.setDoubleClickReturnValue (true, 80.0);
    hpfKnob.setPopupDisplayEnabled (true, true, this);
    hpfKnob.textFromValueFunction = [] (double v) { return juce::String ((int) v) + " Hz"; };
    hpfKnob.setTitle ("High-pass filter cutoff");
    hpfKnob.setWantsKeyboardFocus (true);
    makeClickable (hpfKnob);
    hpfKnob.onValueChange = [this] { strip.setHpfCutoff ((float) hpfKnob.getValue()); updateFilter(); };
    addAndMakeVisible (hpfKnob);

    lpfKnob.setRange (1000.0, 20000.0, 10.0);
    lpfKnob.setSkewFactorFromMidPoint (8000.0);
    lpfKnob.setDoubleClickReturnValue (true, 18000.0);
    lpfKnob.setPopupDisplayEnabled (true, true, this);
    lpfKnob.textFromValueFunction = [] (double v) { return juce::String (v / 1000.0, 1) + " kHz"; };
    lpfKnob.setTitle ("Low-pass filter cutoff");
    lpfKnob.setWantsKeyboardFocus (true);
    makeClickable (lpfKnob);
    lpfKnob.onValueChange = [this] { strip.setLpfCutoff ((float) lpfKnob.getValue()); updateFilter(); };
    addAndMakeVisible (lpfKnob);

    makeLamp (hpfButton, "key");
    hpfButton.onClick = [this]
    {
        strip.setHpfEnabled (! strip.isHpfEnabled());
        updateFilter();
    };
    addAndMakeVisible (hpfButton);

    makeLamp (lpfButton, "key");
    lpfButton.onClick = [this]
    {
        strip.setLpfEnabled (! strip.isLpfEnabled());
        updateFilter();
    };
    addAndMakeVisible (lpfButton);

    // Strip presets: save this strip's sound under a name, recall it here (or on
    // any other strip). Applying one never renames, re-routes or mutes the strip.
    presetCombo.setTitle ("Strip preset");
    presetCombo.setTooltip ("Recall a saved strip preset. Save the current settings with the Save button.");
    presetCombo.onChange = [this] { applySelectedPreset(); };
    makeClickable (presetCombo);
    addAndMakeVisible (presetCombo);

    presetSaveButton.setTitle ("Save the current strip settings as a preset");
    presetSaveButton.setTooltip ("Save the current strip settings under a name.");
    presetSaveButton.onClick = [this] { showSavePresetDialog(); };
    makeClickable (presetSaveButton);
    addAndMakeVisible (presetSaveButton);

    presetDeleteButton.setTitle ("Delete the selected strip preset");
    presetDeleteButton.setTooltip ("Delete the preset shown in the list.");
    presetDeleteButton.onClick = [this]
    {
        const auto name = selectedPreset;
        if (name.isEmpty() || engine.findStripPreset (name) == nullptr)
            return;
        if (engine.deleteStripPreset (name))
        {
            selectedPreset.clear();
            refreshPresets();
            Session::save (engine);
        }
    };
    makeClickable (presetDeleteButton);
    addAndMakeVisible (presetDeleteButton);

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

    addAndMakeVisible (pad);

    // Sends: click toggles, right-click / M / Shift+Enter opens level and pre/post.
    for (int b = 0; b < kNumBuses; ++b)
    {
        auto& btn = sendButtons[(size_t) b];
        makeLamp (btn, "accent");
        btn.onClick = [this, b] { strip.busOn[b].store (sendButtons[(size_t) b].getToggleState()); };
        btn.onSecondary = [this, b] { openSendPanel (b); };
        addChildComponent (btn);
    }

    makeLamp (muteButton, "mute");
    muteButton.onClick = [this] { strip.mute.store (muteButton.getToggleState()); };
    addAndMakeVisible (muteButton);

    makeLamp (soloButton, "accent");
    soloButton.onClick = [this] { strip.solo.store (soloButton.getToggleState()); };
    addAndMakeVisible (soloButton);

    // Solo-safe: while anything is soloed, this strip is never muted by the solo
    // action on the monitor bus (it still has to be routed to the monitor bus).
    makeLamp (safeButton, "key");
    safeButton.setTitle ("Solo-safe");
    safeButton.setTooltip ("Solo-safe: this strip keeps playing on the monitor bus while another strip is soloed.");
    safeButton.onClick = [this]
    {
        strip.setSoloSafe (! strip.isSoloSafe());
        updateKeys();
    };
    addAndMakeVisible (safeButton);

    // FX sends: post-fader sends to the engine's global reverb / delay / chorus.
    static const char* fxNames[] { "Reverb", "Delay", "Chorus" };
    for (int f = 0; f < omx::kNumFxSends; ++f)
    {
        auto& slider = fxSendSliders[(size_t) f];
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setRange (0.0, 1.0, 0.01);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setPopupDisplayEnabled (true, true, this);
        slider.textFromValueFunction = [] (double v) { return juce::String ((int) std::round (v * 100.0)) + " %"; };
        slider.setTitle (juce::String (fxNames[f]) + " send");
        slider.setTooltip (juce::String (fxNames[f]) + " send (post-fader). The effect return is mixed into "
                           "every bus this strip is routed to.");
        slider.setWantsKeyboardFocus (true);
        makeClickable (slider);
        slider.onValueChange = [this, f] { strip.setFxSendLevel (f, (float) fxSendSliders[(size_t) f].getValue()); };
        addAndMakeVisible (slider);
    }

    latencyLabel.setFont (Theme::mono (11.0f));
    latencyLabel.setColour (juce::Label::textColourId, Theme::p().textFaint);
    latencyLabel.setJustificationType (juce::Justification::centredRight);
    latencyLabel.setTitle ("Total channel latency");
    addAndMakeVisible (latencyLabel);

    faderSection.fader.onValueChange = [this] { strip.faderDb.store ((float) faderSection.fader.getValue()); };
    addAndMakeVisible (faderSection);

    refreshFromStrip();
}

StripView::~StripView() = default;

juce::String StripView::displayName() const
{
    return strip.name.isNotEmpty() ? strip.name : "Input " + juce::String (index + 1);
}

void StripView::refreshFromStrip()
{
    const auto name = displayName();
    tape.setText (name);
    tape.setTapeColour (strip.colour);
    juce::StringArray badges;
    if (strip.mono.load())           badges.add ("MONO");
    if (strip.invertPolarity.load()) badges.add (juce::String::fromUTF8 ("\xc3\x98"));   // polarity
    tape.setBadges (badges.joinIntoString ("  "));
    setTitle (name + " strip");
    menuButton.setTitle (name + " options");
    menuButton.setTooltip ("Rename, tape colour, mono, polarity, remove.");

    gainSlider.setTitle (name + " gain");
    gainSlider.setValue (strip.gainDb.load(), juce::dontSendNotification);
    gainSlider.updateText();

    invertButton.setToggleState (strip.invertPolarity.load(), juce::dontSendNotification);
    monoButton.setToggleState (strip.mono.load(), juce::dontSendNotification);
    updateFilter();

    for (int f = 0; f < omx::kNumFxSends; ++f)
        fxSendSliders[(size_t) f].setValue (strip.getFxSendLevel (f), juce::dontSendNotification);
    refreshPresets();

    latencyLabel.setText ("lat " + juce::String (strip.getTotalLatencyMs(), 1) + " ms", juce::dontSendNotification);
    latencyLabel.setTooltip ("Total latency of this channel (noise removal + plugins): "
                             + juce::String (strip.getTotalLatencySamples()) + " samples, "
                             + juce::String (strip.getTotalLatencyMs(), 1)
                             + " ms. The other strips are delayed to match it.");

    faderSection.fader.setTitle (name + " fader");
    faderSection.fader.setValue (strip.faderDb.load(), juce::dontSendNotification);

    muteButton.setTitle ("Mute " + name);
    muteButton.setTooltip ("Mute " + name + " on every bus.");
    soloButton.setTitle ("Solo " + name);
    soloButton.setTooltip ("Listen to " + name + " alone on the monitor bus. Other buses (stream, calls) are not affected.");

    updateKeys();
    pad.refreshFromParams();
    inserts.refresh();
    updateSourceButton();
}

void StripView::refreshDevice()
{
    updateSourceButton();
}

void StripView::tick()
{
    faderSection.tick();

    inserts.tick();

    // The strip's total latency (noise removal + plugins) is printed next to "Inserts"
    // and in the fader bay; it is recomputed live from the current state.
    if (++tickCount % 15 == 0)
    {
        latencyLabel.setText ("lat " + juce::String (strip.getTotalLatencyMs(), 1) + " ms", juce::dontSendNotification);
        if (const int latency = strip.latencySamples.load(); latency != lastLatency)
        {
            lastLatency = latency;
            repaint (insertsLegend);
        }
    }
}

//==============================================================================
// Keys, panels, strip menu

void StripView::updateKeys()
{
    const auto name = displayName();
    muteButton.setToggleState (strip.mute.load(), juce::dontSendNotification);
    soloButton.setToggleState (strip.solo.load(), juce::dontSendNotification);
    safeButton.setToggleState (strip.isSoloSafe(), juce::dontSendNotification);
    safeButton.setTooltip (strip.isSoloSafe()
        ? "Solo-safe is on: this strip keeps playing on the monitor bus while another strip is soloed."
        : "Solo-safe: this strip keeps playing on the monitor bus while another strip is soloed.");

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
        const bool busActive = engine.getBus (b).active.load();
        const float level = strip.sendDb[b].load();
        const bool pre = strip.sendPre[b].load();

        // A level other than unity, and pre-fader, are printed on the key.
        // The bus's own name is shown, so a renamed bus reads correctly here.
        const auto busName = engine.getBusName (b);
        juce::String text (busName);
        if (std::abs (level) >= 0.05f)
            text << " " << (level <= -60.0f ? juce::String ("-inf") : (level > 0 ? "+" : "") + juce::String (level, 0));
        if (pre)
            text << " P";
        btn.setButtonText (text);
        btn.setToggleState (strip.busOn[b].load(), juce::dontSendNotification);
        btn.setTitle ("Send " + name + " to " + busName);
        btn.setTooltip ("Send " + name + " to " + busName + ". Right-click (or M) for level and pre-fader."
                        + (pre ? " Pre-fader." : ""));
        btn.setVisible (busActive);
    }
}

void StripView::updateFilter()
{
    const bool hpfOn = strip.isHpfEnabled(), lpfOn = strip.isLpfEnabled();

    hpfKnob.setValue (strip.getHpfCutoff(), juce::dontSendNotification);
    hpfKnob.updateText();
    hpfKnob.setEnabled (hpfOn);
    hpfButton.setToggleState (hpfOn, juce::dontSendNotification);
    hpfButton.setTitle ("High-pass filter, " + juce::String (hpfOn ? "on" : "off"));
    hpfButton.setTooltip ("High-pass filter " + juce::String (hpfOn ? "on" : "off") + " at "
                          + juce::String ((int) strip.getHpfCutoff()) + " Hz. Click to switch it on or off; "
                          "drag the knob for the cutoff.");
    hpfKnob.setTooltip ("High-pass filter cutoff: " + juce::String ((int) strip.getHpfCutoff())
                        + " Hz. Double-click for 80 Hz.");

    lpfKnob.setValue (strip.getLpfCutoff(), juce::dontSendNotification);
    lpfKnob.updateText();
    lpfKnob.setEnabled (lpfOn);
    lpfButton.setToggleState (lpfOn, juce::dontSendNotification);
    lpfButton.setTitle ("Low-pass filter, " + juce::String (lpfOn ? "on" : "off"));
    lpfButton.setTooltip ("Low-pass filter " + juce::String (lpfOn ? "on" : "off") + " at "
                          + juce::String (strip.getLpfCutoff() / 1000.0, 1) + " kHz. Click to switch it on or off; "
                          "drag the knob for the cutoff.");
    lpfKnob.setTooltip ("Low-pass filter cutoff: " + juce::String (strip.getLpfCutoff() / 1000.0, 1)
                        + " kHz. Double-click for 18 kHz.");
}

void StripView::refreshPresets()
{
    const auto names = engine.getStripPresetNames();
    presetCombo.clear (juce::dontSendNotification);
    for (int i = 0; i < names.size(); ++i)
        presetCombo.addItem (names[i], i + 1);

    if (const int id = names.indexOf (selectedPreset); id >= 0)
        presetCombo.setSelectedId (id + 1, juce::dontSendNotification);
    else
        presetCombo.setText ("Preset...", juce::dontSendNotification);
}

void StripView::applySelectedPreset()
{
    const auto name = presetCombo.getText().trim();
    if (name.isEmpty() || name == "Preset..." || engine.findStripPreset (name) == nullptr)
        return;

    selectedPreset = name;
    const int missing = engine.applyStripPreset (name, index);
    refreshFromStrip();
    Session::save (engine);

    if (missing > 0)
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Preset applied",
            "Preset \"" + name + "\" was applied, but " + juce::String (missing)
            + (missing == 1 ? " plugin" : " plugins")
            + " could not be found and were skipped. The rest of the settings were applied.");
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
            safe->refreshPresets();
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
    panel->onChange = [safe] { if (safe != nullptr) safe->updateKeys(); };

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
    menu.addSubMenu ("Tape colour", colours);

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
    Theme::drawFaceplate (g, getLocalBounds().toFloat());

    Theme::drawLegend (g, "Input", inputLegend);
    Theme::drawLegend (g, "Filter", filterLegend);
    Theme::drawLegend (g, "Processing", processLegend);
    Theme::drawLegend (g, "Preset", presetLegend);
    Theme::drawLegend (g, "Inserts", insertsLegend);
    if (const int lat = strip.latencySamples.load(); lat > 0 && engine.getSampleRate() > 0)
    {
        g.setFont (Theme::mono (11.0f));
        g.setColour (Theme::p().textFaint);
        g.drawText ("latency " + juce::String (1000.0 * lat / engine.getSampleRate(), 1) + " ms",
                    insertsLegend, juce::Justification::centredRight);
    }

    Theme::drawLegend (g, "Voice pad", padLegend);
    Theme::drawLegend (g, "Sends", sendsLegend);
    Theme::drawLegend (g, "FX sends", fxLegend);

    // FX send labels: REV / DLY / CHO beside their sliders.
    static const char* fxLabels[] { "REV", "DLY", "CHO" };
    g.setFont (Theme::mono (10.0f));
    g.setColour (Theme::p().textFaint);
    for (int f = 0; f < omx::kNumFxSends; ++f)
        if (! fxSendLabelAreas[(size_t) f].isEmpty())
            g.drawText (fxLabels[f], fxSendLabelAreas[(size_t) f], juce::Justification::centredLeft);

    // A hairline above the fader bay, the same height on every column.
    g.setColour (Theme::p().line);
    g.fillRect (Theme::s3, (bayTop > 0 ? bayTop : getHeight() / 2) - Theme::s3 / 2 - 1, getWidth() - 2 * Theme::s3, 1);

    // Bold solid-colour name bar along the bottom of the strip, in the strip's
    // own colour (dark text on it, as on the tape).
    if (! nameBarArea.isEmpty())
    {
        g.setColour (Theme::tapeColour (strip.colour));
        g.fillRect (nameBarArea);
        g.setColour (Theme::p().tapeText);
        g.setFont (Theme::ui (13.0f, true));
        g.drawText (displayName(), nameBarArea.reduced (Theme::s2, 0), juce::Justification::centredLeft);
    }
}

void StripView::resized()
{
    const juce::ScopedValueSetter<bool> layoutGuard (inLayout, true);
    auto r = getLocalBounds().reduced (Theme::s3, Theme::s3);
    const int split = bayTop > 0 ? bayTop : getHeight() / 2;
    auto top = r.withBottom (split - Theme::s3);
    auto bay = r.withTop (split);
    constexpr int legendH = 16, gap = 8;

    // Header: name, options.
    auto titleRow = top.removeFromTop (28);
    menuButton.setBounds (titleRow.removeFromRight (28));
    titleRow.removeFromRight (Theme::s1);
    tape.setBounds (titleRow);
    top.removeFromTop (Theme::s2);

    // INPUT: source selector, gain slider under the source name, Ø / MONO below it.
    inputLegend = top.removeFromTop (legendH);
    sourceButton.setBounds (top.removeFromTop (30));
    top.removeFromTop (Theme::s1);
    gainSlider.setBounds (top.removeFromTop (24));
    top.removeFromTop (Theme::s1);
    {
        auto row = top.removeFromTop (20);
        invertButton.setBounds (row.removeFromLeft (44));
        row.removeFromLeft (Theme::s1);
        monoButton.setBounds (row);
    }
    top.removeFromTop (gap);

    // FILTER: HPF/LPF on/off keys with a cutoff knob each.
    filterLegend = top.removeFromTop (legendH);
    {
        auto row = top.removeFromTop (40);
        const int btnW = 38, g = 4;
        const int knobW = (row.getWidth() - 2 * btnW - 3 * g) / 2;
        hpfButton.setBounds (row.removeFromLeft (btnW));
        row.removeFromLeft (g);
        hpfKnob.setBounds (row.removeFromLeft (knobW));
        row.removeFromLeft (g);
        lpfButton.setBounds (row.removeFromLeft (btnW));
        row.removeFromLeft (g);
        lpfKnob.setBounds (row);
    }
    top.removeFromTop (gap);

    // PROCESSING keys (NR, Gate, EQ, Comp, DS, Duck) as before.
    processLegend = top.removeFromTop (legendH);
    {
        auto row = top.removeFromTop (24);
        const int n = (int) processKeys.size();
        const int kw = (row.getWidth() - (n - 1) * 2) / n;
        for (auto& key : processKeys)
        {
            key.setBounds (row.removeFromLeft (kw));
            row.removeFromLeft (2);
        }
    }
    top.removeFromTop (gap);

    // PRESET: recall list + Save / Delete.
    presetLegend = top.removeFromTop (legendH);
    {
        auto row = top.removeFromTop (26);
        presetDeleteButton.setBounds (row.removeFromRight (56));
        row.removeFromRight (Theme::s1);
        presetSaveButton.setBounds (row.removeFromRight (48));
        row.removeFromRight (Theme::s1);
        presetCombo.setBounds (row);
    }
    top.removeFromTop (gap);

    // Compact pad, pinned to the bottom of the top section: wide and short (it's a gesture
    // surface, it doesn't need to be square). The inserts get everything between, with
    // at least two rows reserved for them when the pad is sized.
    const int rackMin = InsertRack::heightForRows (2);
    const int padH = juce::jlimit (32, 88, juce::jmin ((int) (top.getWidth() * 0.42f),
                                                       top.getHeight() - legendH - rackMin - gap - legendH - VoicePadComponent::headerHeight));
    pad.setBounds (top.removeFromBottom (padH + VoicePadComponent::headerHeight));
    padLegend = top.removeFromBottom (legendH);
    top.removeFromBottom (gap);

    // Inserts: as many rows as fit; the list scrolls past that.
    insertsLegend = top.removeFromTop (legendH);
    const int fit = (top.getHeight() + InsertRack::rowGap()) / (InsertRack::rowHeight() + InsertRack::rowGap());
    inserts.setBounds (top.removeFromTop (InsertRack::heightForRows (juce::jlimit (1, kNumSlots, juce::jmin (fit, inserts.getVisibleRowCount())))));

    // Fader bay: the name bar along the very bottom, then mute / solo / solo-safe,
    // then the latency readout, then the fader with the send keys beside it.
    nameBarArea = bay.removeFromBottom (22);
    auto muteRow = bay.removeFromBottom (28);
    {
        const int n = 3, g = Theme::s1;
        const int bw = (muteRow.getWidth() - (n - 1) * g) / n;
        muteButton.setBounds (muteRow.removeFromLeft (bw));
        muteRow.removeFromLeft (g);
        soloButton.setBounds (muteRow.removeFromLeft (bw));
        muteRow.removeFromLeft (g);
        safeButton.setBounds (muteRow);
    }
    bay.removeFromBottom (Theme::s1);
    latencyLabel.setBounds (bay.removeFromBottom (16));
    bay.removeFromBottom (Theme::s2);

    auto sends = bay.removeFromRight (juce::jmax (56, bay.getWidth() - FaderSection::preferredWidth - Theme::s2));
    bay.removeFromRight (Theme::s2);
    faderSection.setBounds (bay);

    sendsLegend = sends.removeFromTop (20);
    for (int b = 0; b < kNumBuses; ++b)
    {
        if (! engine.getBus (b).active.load())
            continue;
        sendButtons[(size_t) b].setBounds (sends.removeFromTop (26));
        sends.removeFromTop (Theme::s1);
    }
    sends.removeFromTop (Theme::s1);

    // FX sends: three small level sliders under the bus sends.
    fxLegend = sends.removeFromTop (legendH);
    for (int f = 0; f < omx::kNumFxSends; ++f)
    {
        auto row = sends.removeFromTop (18);
        fxSendLabelAreas[(size_t) f] = row.removeFromLeft (30);
        fxSendSliders[(size_t) f].setBounds (row);
        sends.removeFromTop (2);
    }
}
