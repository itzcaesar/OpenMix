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

    // Input gain: a horizontal slider directly under the source name, -24..+24 dB;
    // the value is printed above it (mockup style), double-click (or Home) resets to 0 dB.
    gainSlider.setRange (-24.0, 24.0, 0.1);
    gainSlider.setDoubleClickReturnValue (true, 0.0);
    gainSlider.textFromValueFunction = [] (double v) { return (v > 0.05 ? "+" : "") + juce::String (v, 1) + " dB"; };
    gainSlider.valueFromTextFunction = [] (const juce::String& s) { return s.getDoubleValue(); };
    gainSlider.setTooltip ("Input gain. Drag, scroll or use the arrow keys. Double-click or Home for 0 dB.");
    gainSlider.setWantsKeyboardFocus (true);
    makeClickable (gainSlider);
    gainSlider.onValueChange = [this]
    {
        strip.gainDb.store ((float) gainSlider.getValue());
        repaint (gainHeadArea);
    };
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

    // FX sends: post-fader sends to the engine's global reverb / delay / chorus.
    static const char* fxNames[] { "Reverb", "Delay", "Chorus" };
    static const char* fxShort[] { "REV", "DLY", "CHO" };
    for (int f = 0; f < omx::kNumFxSends; ++f)
    {
        auto& btn = fxEditButtons[(size_t) f];
        btn.setButtonText (fxShort[f]);
        btn.getProperties().set ("micro", true);
        btn.setTitle (juce::String (fxNames[f]) + " settings");
        btn.setTooltip (juce::String ("Configure global ") + fxNames[f] + " settings.");
        btn.onClick = [this, f] { openGlobalFxPanel (f); };
        makeClickable (btn);
        addAndMakeVisible (btn);

        auto& slider = fxSendSliders[(size_t) f];
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setRange (0.0, 1.0, 0.01);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setPopupDisplayEnabled (true, true, this);
        slider.textFromValueFunction = [] (double v) { return juce::String ((int) std::round (v * 100.0)) + " %"; };
        slider.setTitle (juce::String (fxNames[f]) + " send");
        slider.setTooltip (juce::String (fxNames[f]) + " send (post-fader). Click " + fxShort[f]
                           + " to edit the global " + fxNames[f] + " effect.");
        slider.setWantsKeyboardFocus (true);
        makeClickable (slider);
        slider.onValueChange = [this, f] { strip.setFxSendLevel (f, (float) fxSendSliders[(size_t) f].getValue()); };
        addAndMakeVisible (slider);
    }

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

    for (int f = 0; f < omx::kNumFxSends; ++f)
        fxSendSliders[(size_t) f].setValue (strip.getFxSendLevel (f), juce::dontSendNotification);

    repaint (gainHeadArea);

    faderSection.fader.setTitle (name + " fader");
    faderSection.fader.setValue (strip.faderDb.load(), juce::dontSendNotification);
    faderSection.meter.setTitle (name + " meter");
    pad.setTitle (name + " voice pad");

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
}

//==============================================================================
// Keys, panels, strip menu

void StripView::updateKeys()
{
    const auto name = displayName();
    muteButton.setToggleState (strip.mute.load(), juce::dontSendNotification);
    soloButton.setToggleState (strip.solo.load(), juce::dontSendNotification);

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

void StripView::applyPreset (const juce::String& name)
{
    if (name.isEmpty() || engine.findStripPreset (name) == nullptr)
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

void StripView::openGlobalFxPanel (int fxIndex)
{
    auto panel = std::make_unique<GlobalFxPanel> (engine, fxIndex);
    auto& btn = fxEditButtons[(size_t) fxIndex];
    auto* top = popupParent();
    juce::CallOutBox::launchAsynchronously (std::move (panel), top->getLocalArea (&btn, btn.getLocalBounds()), top);
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
    Theme::drawLegend (g, "Processing", processLegend);
    Theme::drawLegend (g, "Inserts", insertsLegend);
    Theme::drawLegend (g, "Voice pad", padLegend);
    Theme::drawLegend (g, "Sends", sendsLegend);
    Theme::drawLegend (g, "FX sends", fxLegend);

    // 1 px dividers between the sections (mockup style: sections touch, divided
    // only by a hairline, with generous padding inside each one).
    g.setColour (Theme::p().line);
    for (const int y : dividerYs)
        g.fillRect (7, y, getWidth() - 14, 1);

    // Gain head: "GAIN" left, the value right; scale under the slider.
    if (! gainHeadArea.isEmpty())
    {
        const double v = strip.gainDb.load();
        g.setFont (Theme::ui (9.0f, true));
        g.setColour (Theme::p().textFaint);
        g.drawText ("GAIN", gainHeadArea, juce::Justification::centredLeft);
        g.setFont (Theme::mono (11.0f));
        g.setColour (Theme::p().text);
        g.drawText ((v > 0.05 ? "+" : "") + juce::String (v, 1) + " dB",
                    gainHeadArea, juce::Justification::centredRight);
    }
    if (! gainScaleArea.isEmpty())
    {
        g.setFont (Theme::mono (8.0f));
        g.setColour (Theme::p().textFaint);
        g.drawText ("-24", gainScaleArea, juce::Justification::centredLeft);
        g.drawText ("0", gainScaleArea, juce::Justification::centred);
        g.drawText ("+24", gainScaleArea, juce::Justification::centredRight);
    }

    // Channel icon row: a small audio mark above the fader bay (the mockup's
    // per-channel icon row, kept as a quiet divider here).
    if (! iconRowArea.isEmpty())
    {
        const auto cx = iconRowArea.getCentreX();
        const auto cy = iconRowArea.getCentreY();
        g.setColour (Theme::p().textFaint);
        g.fillRect (cx - 9, cy - 4, 3, 8);
        g.fillRect (cx - 1, cy - 7, 3, 14);
        g.fillRect (cx + 7, cy - 5, 3, 10);
    }

    // A hairline above the fader bay, the same height on every column.
    g.setColour (Theme::p().line);
    g.fillRect (7, (bayTop > 0 ? bayTop : getHeight() / 2) - 4, getWidth() - 14, 1);

    // Bold solid-colour name bar along the bottom of the strip, in the strip's
    // own channel colour with text chosen for contrast (mockup style).
    if (! nameBarArea.isEmpty())
    {
        g.setColour (Theme::nameBarColour (strip.colour));
        g.fillRect (nameBarArea);
        g.setColour (Theme::nameBarTextColour (strip.colour));
        g.setFont (Theme::ui (13.0f, true));
        g.drawText (displayName(), nameBarArea.reduced (Theme::s2, 0), juce::Justification::centred);
    }
}

void StripView::resized()
{
    const juce::ScopedValueSetter<bool> layoutGuard (inLayout, true);
    // Mockup proportions: 7 px side padding, sections touching with only a 1 px
    // divider between them, 14 px legends, 6 px between sections.
    constexpr int sidePad = 7;
    auto r = getLocalBounds().reduced (sidePad, sidePad);
    const int split = bayTop > 0 ? bayTop : getHeight() / 2;
    auto top = r.withBottom (split - sidePad);
    auto bay = r.withTop (split);
    constexpr int legendH = 14, gap = 6;
    dividerYs.clear();

    // Header: name, options.
    auto titleRow = top.removeFromTop (28);
    menuButton.setBounds (titleRow.removeFromRight (28));
    titleRow.removeFromRight (Theme::s1);
    tape.setBounds (titleRow);
    dividerYs.push_back (top.getY());
    top.removeFromTop (gap);

    // INPUT: source selector, gain slider under the source name with its value
    // above it, Ø / MONO below it (two equal toggles, mockup style).
    inputLegend = top.removeFromTop (legendH);
    sourceButton.setBounds (top.removeFromTop (26));
    top.removeFromTop (gap);
    gainHeadArea = top.removeFromTop (14);
    gainSlider.setBounds (top.removeFromTop (24));
    gainScaleArea = top.removeFromTop (10);
    top.removeFromTop (gap);
    {
        auto row = top.removeFromTop (24);
        const int bw = (row.getWidth() - Theme::s1) / 2;
        invertButton.setBounds (row.removeFromLeft (bw));
        row.removeFromLeft (Theme::s1);
        monoButton.setBounds (row);
    }
    dividerYs.push_back (top.getY());
    top.removeFromTop (gap);

    // PROCESSING keys (NR, Gate, EQ, Comp, DS, Duck).
    processLegend = top.removeFromTop (legendH);
    {
        auto row = top.removeFromTop (26);
        const int n = (int) processKeys.size();
        const int kw = (row.getWidth() - (n - 1) * 2) / n;
        for (auto& key : processKeys)
        {
            key.setBounds (row.removeFromLeft (kw));
            row.removeFromLeft (2);
        }
    }
    dividerYs.push_back (top.getY());
    top.removeFromTop (gap);

    // The voice pad and icon row sit at the bottom of the top section;
    // the inserts take whatever is left in the middle (at least one row --
    // the list scrolls past that, and expands up to all 6 slots on tall windows).
    const int padH = Theme::isCompact() ? 76 : 94;
    const int padSectionH = legendH + VoicePadComponent::headerHeight + padH;
    constexpr int iconH = 21;

    iconRowArea = top.removeFromBottom (iconH);
    top.removeFromBottom (gap);
    auto padBlock = top.removeFromBottom (padSectionH);

    insertsLegend = top.removeFromTop (legendH);
    const int insertsAvail = top.getHeight() - gap;
    const int rackH = insertsAvail <= 0 ? 0
        : juce::jlimit (InsertRack::heightForRows (1), InsertRack::heightForRows (kNumSlots), insertsAvail);
    inserts.setBounds (rackH > 0 ? top.removeFromTop (rackH) : juce::Rectangle<int>());
    dividerYs.push_back (padBlock.getY() - gap);

    // VOICE PAD: mode buttons + a pad tall enough to read (94 px, 76 compact).
    padLegend = padBlock.removeFromTop (legendH);
    pad.setBounds (padBlock);
    dividerYs.push_back (iconRowArea.getY());

    // Fader bay: name bar along the bottom, Mute / Solo above it, and above
    // that the fader + meter with the sends beside it -- bus keys in a
    // 2-column grid, FX send sliders underneath. The sends column takes what
    // is left after the fader's preferred width (at least 80 px, 64 compact,
    // so the keys stay readable); the fader track narrows instead of the keys.
    nameBarArea = bay.removeFromBottom (28);
    bay.removeFromBottom (gap);
    auto muteRow = bay.removeFromBottom (28);
    {
        const int bw = (muteRow.getWidth() - Theme::s1) / 2;
        muteButton.setBounds (muteRow.removeFromLeft (bw));
        muteRow.removeFromLeft (Theme::s1);
        soloButton.setBounds (muteRow);
    }
    bay.removeFromBottom (gap);

    const int sendsMin = Theme::isCompact() ? 78 : 92;
    const int sendsW = juce::jlimit (sendsMin, 118, bay.getWidth() - FaderSection::preferredWidth - 6);
    auto sends = bay.removeFromRight (sendsW);
    bay.removeFromRight (6);
    faderSection.setBounds (bay);

    const int activeBuses = engine.getNumActiveBuses();
    const int sendRows = juce::jmax (1, (activeBuses + 1) / 2);
    const int gridH = sendRows * 25 + (sendRows - 1) * 3;
    sendsLegend = sends.removeFromTop (legendH);
    {
        auto grid = sends.removeFromTop (gridH);
        const int cellW = (grid.getWidth() - 3) / 2;
        int placed = 0;
        for (int b = 0; b < kNumBuses; ++b)
        {
            if (! engine.getBus (b).active.load())
                continue;
            const int col = placed % 2, row = placed / 2;
            sendButtons[(size_t) b].setBounds (grid.getX() + col * (cellW + 3),
                                               grid.getY() + row * (25 + 3), cellW, 25);
            ++placed;
        }
    }
    sends.removeFromTop (gap);
    fxLegend = sends.removeFromTop (legendH);
    for (int f = 0; f < omx::kNumFxSends; ++f)
    {
        auto row = sends.removeFromTop (18);
        fxEditButtons[(size_t) f].setBounds (row.removeFromLeft (34));
        row.removeFromLeft (3);
        fxSendSliders[(size_t) f].setBounds (row);
        if (f + 1 < omx::kNumFxSends)
            sends.removeFromTop (2);
    }
}
