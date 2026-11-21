#include "ui/StripView.h"
#include "ui/Theme.h"
#include "engine/AppCapture.h"

//==============================================================================
// PluginWindow

PluginWindow::PluginWindow (juce::AudioPluginInstance& plugin, std::function<void()> onCloseFn)
    : DocumentWindow (plugin.getName(), Theme::p().window, DocumentWindow::closeButton),
      onClose (std::move (onCloseFn))
{
    setUsingNativeTitleBar (true);

    if (auto* editor = plugin.createEditorAndMakeActive())
    {
        setContentOwned (editor, true);
        setResizable (editor->isResizable(), false);
    }
    else
    {
        auto* generic = new juce::GenericAudioProcessorEditor (plugin);
        generic->setSize (420, 360);
        setContentOwned (generic, true);
        setResizable (true, false);
    }

    centreWithSize (getWidth(), getHeight());
    setVisible (true);
}

PluginWindow::~PluginWindow()
{
    clearContentComponent();
}

void PluginWindow::closeButtonPressed()
{
    // Defer so the window isn't deleted from inside its own callback.
    juce::MessageManager::callAsync ([fn = onClose] { if (fn) fn(); });
}

//==============================================================================
// Insert list: the slot rows plus a drop indicator while dragging.

class StripView::InsertList : public juce::Component
{
public:
    std::array<PluginSlotRow, kNumSlots> rows;
    int dropIndex = -1;

    InsertList()
    {
        for (auto& r : rows)
            addChildComponent (r);
    }

    void layoutRows (int visible, int rowH, int gap)
    {
        for (int i = 0; i < kNumSlots; ++i)
        {
            rows[(size_t) i].setVisible (i < visible);
            rows[(size_t) i].setBounds (0, i * (rowH + gap), getWidth(), rowH);
        }
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        if (dropIndex < 0)
            return;

        const int y = dropIndex < kNumSlots && rows[(size_t) dropIndex].isVisible()
                        ? rows[(size_t) dropIndex].getY() - 2
                        : rows[0].getBottom();
        g.setColour (Theme::p().focus);
        g.fillRect (0, juce::jmax (0, y - 1), getWidth(), 2);
    }
};

//==============================================================================

static constexpr int rowHeight = 26, rowGap = 3;

StripView::StripView (MixerEngine& e, int i)
    : engine (e), strip (e.getStrip (i)), index (i),
      insertList (std::make_unique<InsertList>()),
      pad (strip.pad, "Input " + juce::String (i + 1)),
      faderSection (strip.peakL, strip.peakR, "Input " + juce::String (i + 1))
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

    for (int s = 0; s < kNumSlots; ++s)
    {
        auto& row = insertList->rows[(size_t) s];
        row.onMain   = [this, s] { slotMain (s); };
        row.onMenu   = [this, s] { showSlotMenu (s); };
        row.onBypass = [this, s] { toggleBypass (s); };
        row.onRemove = [this, s] { removeSlot (s); };
        row.onMove   = [this, s] (int delta) { moveSlot (s, s + delta); };
        row.onDragTo = [this, s] (int screenY)
        {
            dragFrom = s;
            const int localY = screenY - insertList->getScreenY();
            dropIndex = juce::jlimit (0, juce::jmax (0, strip.getNumPlugins() - 1), localY / (rowHeight + rowGap));
            insertList->dropIndex = dropIndex == s ? -1 : dropIndex;
            insertList->repaint();
        };
        row.onDragEnd = [this]
        {
            if (dragFrom >= 0 && dropIndex >= 0)
                moveSlot (dragFrom, dropIndex);
            dragFrom = dropIndex = -1;
            insertList->dropIndex = -1;
            insertList->repaint();
        };
    }
    insertsView.setViewedComponent (insertList.get(), false);
    insertsView.setScrollBarsShown (true, false);
    insertsView.setScrollBarThickness (6);
    addAndMakeVisible (insertsView);

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

StripView::~StripView()
{
    editorWindows.clear();
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
    updateSlotRows();
    updateSourceButton();
}

void StripView::refreshDevice()
{
    updateSourceButton();
}

void StripView::tick()
{
    faderSection.tick();

    // Plugins auto-stopped by the audio thread, and latency changes, show up here.
    const int faults = strip.faultCount.load();
    const int latency = strip.latencySamples.load();
    if (faults != lastFaultCount || (++tickCount % 15 == 0 && latency != lastLatency))
    {
        lastFaultCount = faults;
        lastLatency = latency;
        updateSlotRows();
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
    auto panel = std::make_unique<ProcessorPanel> (engine, index, (Processor) k);
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

            safe->editorWindows.clear();
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
// Inserts

int StripView::visibleRowCount() const
{
    const int n = strip.getNumPlugins();
    const bool loadingNew = loadingSlot >= n;
    return juce::jmin (kNumSlots, n + (loadingNew ? 1 : 0) + 1);
}

void StripView::updateSlotRows()
{
    const int n = strip.getNumPlugins();
    const double sr = engine.getSampleRate();

    for (int s = 0; s < kNumSlots; ++s)
    {
        auto& row = insertList->rows[(size_t) s];
        if (s == loadingSlot)
            row.setState (PluginSlotRow::State::Loading, loadingName, false, false, 0, sr);
        else if (s < n)
        {
            auto* p = strip.getPlugin (s);
            const auto ab = abStates.find (p);
            row.setState (PluginSlotRow::State::Loaded, p->getName(), strip.slotBypass[(size_t) s].load(),
                          strip.slotFault[(size_t) s].load(), p->getLatencySamples(), sr,
                          juce::roundToInt (strip.slotMix[(size_t) s].load() * 100.0f),
                          ab == abStates.end() ? juce::String() : juce::String (ab->second.onB ? "B" : "A"));
        }
        else
            row.setState (PluginSlotRow::State::Empty, {}, false, false, 0, sr);
    }

    const int visible = visibleRowCount();

    // The number of rows changed: re-run the strip layout so the list grows or shrinks.
    const int maxRows = getHeight() < 760 ? 2 : 4;   // same rule as resized()
    const int wantedHeight = juce::jmax (juce::jmin (visible, maxRows), 1) * (rowHeight + rowGap) - rowGap;
    if (! inLayout && insertsView.getHeight() != wantedHeight && getHeight() > 0)
    {
        resized();
        return;
    }

    insertList->setSize (insertsView.getMaximumVisibleWidth() > 0 ? insertsView.getMaximumVisibleWidth() : insertsView.getWidth(),
                         visible * (rowHeight + rowGap) - rowGap);
    insertList->layoutRows (visible, rowHeight, rowGap);
}

void StripView::slotMain (int slot)
{
    if (slot == loadingSlot)
        return;

    if (auto* p = strip.getPlugin (slot))
        openEditor (p);
    else
        showPluginMenu (slot);
}

void StripView::toggleBypass (int slot)
{
    strip.slotFault[(size_t) slot].store (false);
    strip.slotBypass[(size_t) slot].store (! strip.slotBypass[(size_t) slot].load());
    updateSlotRows();
}

void StripView::removeSlot (int slot)
{
    if (auto* p = strip.getPlugin (slot))
    {
        closeEditor (p);
        abStates.erase (p);
    }
    strip.removePlugin (slot);
    updateSlotRows();

    // Keep keyboard focus in the list.
    const int n = strip.getNumPlugins();
    insertList->rows[(size_t) juce::jmin (slot, n)].grabKeyboardFocus();
}

void StripView::moveSlot (int from, int to)
{
    const int n = strip.getNumPlugins();
    if (! juce::isPositiveAndBelow (from, n) || ! juce::isPositiveAndBelow (to, n) || from == to)
        return;

    strip.movePlugin (from, to);
    updateSlotRows();
    insertList->rows[(size_t) to].grabKeyboardFocus();
}

void StripView::showSlotMenu (int slot)
{
    auto* p = strip.getPlugin (slot);
    if (p == nullptr)
        return;

    const int n = strip.getNumPlugins();
    const bool faulted = strip.slotFault[(size_t) slot].load();

    juce::PopupMenu menu;
    menu.addItem ("Open window", [this, p] { openEditor (p); });
    menu.addItem (faulted ? "Try again" : strip.slotBypass[(size_t) slot].load() ? "Turn on" : "Bypass",
                  [this, slot] { toggleBypass (slot); });
    menu.addSeparator();
    menu.addItem ("Move up", slot > 0, false, [this, slot] { moveSlot (slot, slot - 1); });
    menu.addItem ("Move down", slot < n - 1, false, [this, slot] { moveSlot (slot, slot + 1); });
    menu.addSeparator();
    menu.addItem ("Wet/dry mix...", [this, slot] { openMixPanel (slot); });

    juce::PopupMenu ab;
    if (auto it = abStates.find (p); it == abStates.end())
    {
        ab.addItem ("Start comparing (A and B = current settings)", [this, p]
        {
            juce::MemoryBlock state;
            p->getStateInformation (state);
            abStates[p] = { state, state, false };
            updateSlotRows();
        });
    }
    else
    {
        const bool onB = it->second.onB;
        ab.addItem ("Listen to A", true, ! onB, [this, p] { abSwitch (p, false); });
        ab.addItem ("Listen to B", true, onB, [this, p] { abSwitch (p, true); });
        ab.addSeparator();
        ab.addItem (onB ? "Copy B to A" : "Copy A to B", [this, p]
        {
            auto& st = abStates[p];
            juce::MemoryBlock current;
            p->getStateInformation (current);
            st.a = current;
            st.b = current;
        });
        ab.addItem ("Stop comparing (keep what you hear)", [this, p]
        {
            abStates.erase (p);
            updateSlotRows();
        });
    }
    menu.addSubMenu ("A/B compare", ab);
    menu.addSeparator();
    menu.addItem ("Save preset...", [this, slot] { savePreset (slot); });
    menu.addItem ("Load preset...", [this, slot] { loadPreset (slot); });
    menu.addItem ("Replace with...", [this, slot] { showPluginMenu (slot); });
    menu.addSeparator();
    menu.addItem ("Remove", [this, slot] { removeSlot (slot); });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (insertList->rows[(size_t) slot]));
}

void StripView::showPluginMenu (int slot)
{
    auto& scanner = engine.getScanner();
    const auto plugins = scanner.getPlugins();

    juce::PopupMenu menu;

    if (scanner.isScanning())
    {
        const auto current = scanner.getCurrentPluginName();
        menu.addItem ("Scanning plugins: " + juce::String (scanner.getNumDone()) + " of " + juce::String (scanner.getNumToScan())
                        + (current.isNotEmpty() ? " (" + current + ")" : juce::String()), false, false, nullptr);
        menu.addSeparator();
    }

    if (plugins.isEmpty() && ! scanner.isScanning())
        menu.addItem ("No VST3 plugins found in C:\\Program Files\\Common Files\\VST3", false, false, nullptr);

    auto addPlugin = [this, slot] (juce::PopupMenu& m, const juce::PluginDescription& d, const juce::String& label)
    {
        m.addItem (label, [this, slot, d] { loadPluginAsync (slot, d); });
    };

    if (plugins.size() > 14)
    {
        // Group by maker once the list gets long.
        juce::String maker;
        juce::PopupMenu sub;
        for (const auto& d : plugins)
        {
            const auto m = d.manufacturerName.isNotEmpty() ? d.manufacturerName : juce::String ("Other");
            if (m != maker && maker.isNotEmpty())
            {
                menu.addSubMenu (maker, sub);
                sub = {};
            }
            maker = m;
            addPlugin (sub, d, d.name);
        }
        if (maker.isNotEmpty())
            menu.addSubMenu (maker, sub);
    }
    else
    {
        for (const auto& d : plugins)
            addPlugin (menu, d, d.name + (d.manufacturerName.isNotEmpty() ? "  (" + d.manufacturerName + ")" : juce::String()));
    }

    menu.addSeparator();
    menu.addItem ("Scan for new plugins", ! scanner.isScanning(), false, [this] { engine.getScanner().startScan (false); });

    const auto skipped = scanner.getSkippedFiles();
    if (! skipped.isEmpty())
    {
        juce::PopupMenu skippedMenu;
        for (const auto& f : skipped)
            skippedMenu.addItem (juce::File (f).getFileNameWithoutExtension(), false, false, nullptr);
        menu.addSubMenu ("Skipped: crashed or hung while scanning (" + juce::String (skipped.size()) + ")", skippedMenu);
        menu.addItem ("Retry skipped plugins", ! scanner.isScanning(), false, [this] { engine.getScanner().startScan (true); });
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (insertList->rows[(size_t) juce::jmin (slot, kNumSlots - 1)])
                                                  .withMinimumWidth (insertList->getWidth()));
}

void StripView::loadPluginAsync (int slot, const juce::PluginDescription& desc,
                                 std::function<void (juce::AudioPluginInstance&)> then)
{
    if (loadingSlot >= 0)
        return;   // one load at a time per strip

    loadingSlot = juce::jmin (slot, strip.getNumPlugins());
    loadingName = desc.name;
    updateSlotRows();

    // Let the "Loading..." row paint before the plugin's own (message-thread) start-up runs.
    juce::Component::SafePointer<StripView> safe (this);
    juce::Timer::callAfterDelay (30, [safe, desc, then]
    {
        if (safe == nullptr)
            return;

        safe->engine.getFormatManager().createPluginInstanceAsync (desc, safe->engine.getSampleRate(), 512,
            [safe, desc, then] (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error)
            {
                if (safe == nullptr)
                    return;

                const int slot = safe->loadingSlot;
                safe->loadingSlot = -1;

                if (instance == nullptr)
                {
                    safe->updateSlotRows();
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                            "Couldn't load " + desc.name,
                                                            error.isNotEmpty() ? error : juce::String ("The plugin didn't start."));
                    return;
                }

                if (auto* old = safe->strip.getPlugin (slot))
                {
                    safe->closeEditor (old);
                    safe->abStates.erase (old);
                }

                auto* raw = instance.get();
                safe->strip.installPlugin (slot, std::move (instance));
                safe->updateSlotRows();

                if (then)
                    then (*raw);
                else
                    safe->openEditor (raw);
            });
    });
}

static juce::File presetFolder()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("OpenMix Presets");
    dir.createDirectory();
    return dir;
}

void StripView::savePreset (int slot)
{
    auto* p = strip.getPlugin (slot);
    if (p == nullptr)
        return;

    chooser = std::make_unique<juce::FileChooser> ("Save " + p->getName() + " preset",
                                                   presetFolder().getChildFile (p->getName() + ".omxpreset"), "*.omxpreset");
    juce::Component::SafePointer<StripView> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                            | juce::FileBrowserComponent::warnAboutOverwriting,
        [safe, slot] (const juce::FileChooser& fc)
        {
            if (safe == nullptr || fc.getResult() == juce::File())
                return;

            auto* plugin = safe->strip.getPlugin (slot);
            if (plugin == nullptr)
                return;

            juce::XmlElement root ("OpenMixPreset");
            root.setAttribute ("version", 1);
            root.addChildElement (plugin->getPluginDescription().createXml().release());
            juce::MemoryBlock state;
            plugin->getStateInformation (state);
            root.setAttribute ("state", state.toBase64Encoding());

            if (! root.writeTo (fc.getResult().withFileExtension ("omxpreset")))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Preset not saved",
                                                        "Couldn't write " + fc.getResult().getFullPathName());
        });
}

void StripView::loadPreset (int slot)
{
    chooser = std::make_unique<juce::FileChooser> ("Load preset", presetFolder(), "*.omxpreset");
    juce::Component::SafePointer<StripView> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe, slot] (const juce::FileChooser& fc)
        {
            if (safe == nullptr || fc.getResult() == juce::File())
                return;

            auto xml = juce::parseXML (fc.getResult());
            juce::PluginDescription desc;
            juce::MemoryBlock state;
            if (xml == nullptr || ! xml->hasTagName ("OpenMixPreset") || xml->getChildByName ("PLUGIN") == nullptr
                || ! desc.loadFromXml (*xml->getChildByName ("PLUGIN"))
                || ! state.fromBase64Encoding (xml->getStringAttribute ("state")))
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Not an OpenMix preset",
                                                        fc.getResult().getFileName() + " couldn't be read.");
                return;
            }

            auto applyState = [state] (juce::AudioPluginInstance& p) { p.setStateInformation (state.getData(), (int) state.getSize()); };

            if (auto* current = safe->strip.getPlugin (slot);
                current != nullptr && current->getPluginDescription().createIdentifierString() == desc.createIdentifierString())
            {
                applyState (*current);   // same plugin: just load the settings
                return;
            }

            safe->loadPluginAsync (slot, desc, applyState);
        });
}

void StripView::openEditor (juce::AudioPluginInstance* plugin)
{
    if (plugin == nullptr)
        return;

    if (auto it = editorWindows.find (plugin); it != editorWindows.end())
    {
        it->second->toFront (true);
        return;
    }

    juce::Component::SafePointer<StripView> safe (this);
    editorWindows[plugin] = std::make_unique<PluginWindow> (*plugin, [safe, plugin]
    {
        if (safe != nullptr)
            safe->closeEditor (plugin);
    });
}

void StripView::closeEditor (juce::AudioPluginInstance* plugin)
{
    editorWindows.erase (plugin);
}

void StripView::abSwitch (juce::AudioPluginInstance* plugin, bool toB)
{
    auto it = abStates.find (plugin);
    if (it == abStates.end() || it->second.onB == toB)
        return;

    // Remember what's playing now in its own slot, then load the other one.
    auto& st = it->second;
    juce::MemoryBlock current;
    plugin->getStateInformation (current);
    (st.onB ? st.b : st.a) = current;

    const auto& next = toB ? st.b : st.a;
    plugin->setStateInformation (next.getData(), (int) next.getSize());
    st.onB = toB;
    updateSlotRows();
}

void StripView::openMixPanel (int slot)
{
    auto* p = strip.getPlugin (slot);
    if (p == nullptr)
        return;

    auto panel = std::make_unique<SlotMixPanel> (p->getName(), strip.slotMix[(size_t) slot]);
    juce::Component::SafePointer<StripView> safe (this);
    panel->onChange = [safe] { if (safe != nullptr) safe->updateSlotRows(); };

    auto& row = insertList->rows[(size_t) slot];
    auto* top = popupParent();
    juce::CallOutBox::launchAsynchronously (std::move (panel), top->getLocalArea (&row, row.getLocalBounds()), top);
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
    Theme::drawLegend (g, "Send to", sendsLegend);
}

void StripView::resized()
{
    const juce::ScopedValueSetter<bool> layoutGuard (inLayout, true);
    auto r = getLocalBounds().reduced (Theme::s3, Theme::s3);
    const bool compact = getHeight() < 760;

    auto titleRow = r.removeFromTop (30);
    menuButton.setBounds (titleRow.removeFromRight (30));
    titleRow.removeFromRight (Theme::s1);
    tape.setBounds (titleRow);
    r.removeFromTop (Theme::s2);

    // Source + gain. Roomy layout: their own rows. Compact: side by side.
    if (compact)
    {
        auto row = r.removeFromTop (40);
        gainKnob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        gainKnob.setPopupDisplayEnabled (true, true, this);
        gainKnob.setBounds (row.removeFromRight (40));
        row.removeFromRight (Theme::s1);
        sourceButton.setBounds (row.withSizeKeepingCentre (row.getWidth(), 30));
        gainLegend = {};
    }
    else
    {
        sourceButton.setBounds (r.removeFromTop (30));
        r.removeFromTop (Theme::s2);
        auto row = r.removeFromTop (44);
        gainLegend = row.removeFromLeft (48);
        gainKnob.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
        gainKnob.setPopupDisplayEnabled (false, false, nullptr);
        gainKnob.setBounds (row.withSizeKeepingCentre (44 + 64, 44).withX (row.getX()));
    }
    r.removeFromTop (Theme::s3);

    // Bottom section first (fixed), so the pad and the fader share what's left.
    int activeBuses = 0;
    for (int b = 0; b < kNumBuses; ++b)
        activeBuses += engine.getBus (b).active.load() ? 1 : 0;
    const int sendRows = juce::jmax (1, (activeBuses + 2) / 3);

    auto bottom = r.removeFromBottom (18 + sendRows * 30 + (sendRows - 1) * Theme::s1 + Theme::s2 + 30);
    sendsLegend = bottom.removeFromTop (18);
    {
        const int bw = (bottom.getWidth() - 2 * Theme::s1) / 3;
        int col = 0, row = 0;
        for (int b = 0; b < kNumBuses; ++b)
        {
            if (! engine.getBus (b).active.load())
                continue;
            sendButtons[(size_t) b].setBounds (bottom.getX() + col * (bw + Theme::s1),
                                               bottom.getY() + row * (30 + Theme::s1), bw, 30);
            if (++col == 3) { col = 0; ++row; }
        }
        bottom.removeFromTop (sendRows * 30 + (sendRows - 1) * Theme::s1 + Theme::s2);
    }
    auto muteRow = bottom.removeFromTop (30);
    muteButton.setBounds (muteRow.removeFromLeft ((muteRow.getWidth() - Theme::s1) / 2));
    muteRow.removeFromLeft (Theme::s1);
    soloButton.setBounds (muteRow);
    r.removeFromBottom (Theme::s3);

    // Inserts: as many rows as fit, the list scrolls beyond that.
    insertsLegend = r.removeFromTop (18);
    const int maxRows = compact ? 2 : 4;
    const int rowsShown = juce::jmin (visibleRowCount(), maxRows);
    insertsView.setBounds (r.removeFromTop (juce::jmax (rowsShown, 1) * (rowHeight + rowGap) - rowGap));
    updateSlotRows();
    r.removeFromTop (Theme::s3);

    // Processing keys: one row of six, in signal-flow order.
    processLegend = r.removeFromTop (18);
    {
        auto row = r.removeFromTop (26);
        const int n = (int) processKeys.size();
        const int kw = (row.getWidth() - (n - 1) * 3) / n;
        for (auto& key : processKeys)
        {
            key.setBounds (row.removeFromLeft (kw));
            row.removeFromLeft (3);
        }
    }
    r.removeFromTop (Theme::s3);

    // Pad and fader share the rest; the fader keeps at least 140 px.
    padLegend = r.removeFromTop (18);
    const int padSize = juce::jlimit (64, juce::jmin (r.getWidth(), 180), r.getHeight() - VoicePadComponent::headerHeight - Theme::s3 - 140);
    pad.setBounds (r.removeFromTop (padSize + VoicePadComponent::headerHeight));
    r.removeFromTop (Theme::s3);

    faderSection.setBounds (r);
}
