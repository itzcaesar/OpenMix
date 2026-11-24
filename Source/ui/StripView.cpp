#include "ui/StripView.h"
#include "ui/Theme.h"
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

    gainKnob.setRange (-24.0, 24.0, 0.1);
    gainKnob.setDoubleClickReturnValue (true, 0.0);
    gainKnob.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
    gainKnob.textFromValueFunction = [] (double v) { return (v > 0.05 ? "+" : "") + juce::String (v, 1) + " dB"; };
    gainKnob.valueFromTextFunction = [] (const juce::String& s) { return s.getDoubleValue(); };
    gainKnob.setTooltip ("Input gain. Drag, scroll or use the arrow keys. Double-click or Home for 0 dB.");
    gainKnob.setWantsKeyboardFocus (true);
    makeClickable (gainKnob);
    gainKnob.onValueChange = [this] { strip.gainDb.store ((float) gainKnob.getValue()); };
    addAndMakeVisible (gainKnob);

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

    gainKnob.setTitle (name + " gain");
    gainKnob.setValue (strip.gainDb.load(), juce::dontSendNotification);
    gainKnob.updateText();

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

    // The strip's total latency (noise removal + plugins) is printed next to "Inserts".
    if (const int latency = strip.latencySamples.load(); ++tickCount % 15 == 0 && latency != lastLatency)
    {
        lastLatency = latency;
        repaint (insertsLegend);
    }
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
        juce::String text (kBusNames[b]);
        if (std::abs (level) >= 0.05f)
            text << " " << (level <= -60.0f ? juce::String ("-inf") : (level > 0 ? "+" : "") + juce::String (level, 0));
        if (pre)
            text << " P";
        btn.setButtonText (text);
        btn.setToggleState (strip.busOn[b].load(), juce::dontSendNotification);
        btn.setTitle ("Send " + name + " to " + kBusNames[b]);
        btn.setTooltip ("Send " + name + " to " + juce::String (kBusNames[b]) + ". Right-click (or M) for level and pre-fader."
                        + (pre ? " Pre-fader." : ""));
        btn.setVisible (busActive);
    }
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

    if (! gainLegend.isEmpty())
        Theme::drawLegend (g, "Gain", gainLegend);

    Theme::drawLegend (g, "Inserts", insertsLegend);
    if (const int lat = strip.latencySamples.load(); lat > 0 && engine.getSampleRate() > 0)
    {
        g.setFont (Theme::mono (11.0f));
        g.setColour (Theme::p().textFaint);
        g.drawText ("latency " + juce::String (1000.0 * lat / engine.getSampleRate(), 1) + " ms",
                    insertsLegend, juce::Justification::centredRight);
    }

    Theme::drawLegend (g, "Processing", processLegend);
    Theme::drawLegend (g, "Voice pad", padLegend);
    Theme::drawLegend (g, "Sends", sendsLegend);

    // A hairline above the fader bay, the same height on every column.
    g.setColour (Theme::p().line);
    g.fillRect (Theme::s3, (bayTop > 0 ? bayTop : getHeight() / 2) - Theme::s3 / 2 - 1, getWidth() - 2 * Theme::s3, 1);
}

void StripView::resized()
{
    const juce::ScopedValueSetter<bool> layoutGuard (inLayout, true);
    auto r = getLocalBounds().reduced (Theme::s3, Theme::s3);
    const int split = bayTop > 0 ? bayTop : getHeight() / 2;
    auto top = r.withBottom (split - Theme::s3);
    auto bay = r.withTop (split);
    constexpr int legendH = 16, gap = 10;

    // Header: name, options. Then source + input gain on one row.
    auto titleRow = top.removeFromTop (28);
    menuButton.setBounds (titleRow.removeFromRight (28));
    titleRow.removeFromRight (Theme::s1);
    tape.setBounds (titleRow);
    top.removeFromTop (Theme::s2);

    auto sourceRow = top.removeFromTop (32);
    gainKnob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    gainKnob.setPopupDisplayEnabled (true, true, this);
    gainKnob.setBounds (sourceRow.removeFromRight (32));
    sourceRow.removeFromRight (Theme::s1);
    sourceButton.setBounds (sourceRow);
    gainLegend = {};
    top.removeFromTop (gap);

    // Signal-flow order: processing, then the inserts, then the pad.
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

    // Compact pad, pinned to the bottom of the top section: wide and short (it's a gesture
    // surface, it doesn't need to be square). The inserts get everything between.
    const int rackMin = InsertRack::rowHeight();
    const int padH = juce::jlimit (32, 88, juce::jmin ((int) (top.getWidth() * 0.42f),
                                                       top.getHeight() - legendH - rackMin - gap - legendH - VoicePadComponent::headerHeight));
    pad.setBounds (top.removeFromBottom (padH + VoicePadComponent::headerHeight));
    padLegend = top.removeFromBottom (legendH);
    top.removeFromBottom (gap);

    // Inserts: as many rows as fit; the list scrolls past that.
    insertsLegend = top.removeFromTop (legendH);
    const int fit = (top.getHeight() + InsertRack::rowGap()) / (InsertRack::rowHeight() + InsertRack::rowGap());
    inserts.setBounds (top.removeFromTop (InsertRack::heightForRows (juce::jlimit (1, kNumSlots, juce::jmin (fit, inserts.getVisibleRowCount())))));

    // Fader bay: mute and solo along the bottom, the fader with the send keys beside it.
    auto muteRow = bay.removeFromBottom (28);
    muteButton.setBounds (muteRow.removeFromLeft ((muteRow.getWidth() - Theme::s1) / 2));
    muteRow.removeFromLeft (Theme::s1);
    soloButton.setBounds (muteRow);
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
}
