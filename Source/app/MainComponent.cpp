#include "app/MainComponent.h"
#include "app/Session.h"
#include "app/AppSettings.h"
#include "ui/Theme.h"
#include "ui/OpenMixLookAndFeel.h"
#include "ui/SettingsPanel.h"
#include "ui/TrayIcon.h"
#include "ui/HotkeysPanel.h"

static constexpr int stripGap = Theme::s2;
static constexpr int sectionGap = 32;   // space between inputs and the master section
static constexpr int consolePad = Theme::s4;

//==============================================================================
// ConsoleContent

ConsoleContent::ConsoleContent (MixerEngine& e) : soundboard (e), engine (e)
{
    addChildComponent (soundboard);

    for (int s = 0; s < kNumStrips; ++s)
    {
        strips[(size_t) s] = std::make_unique<StripView> (engine, s);
        strips[(size_t) s]->onLayoutChanged = [this] { relayout(); };
        addChildComponent (*strips[(size_t) s]);
    }

    for (int b = 0; b < kNumBuses; ++b)
    {
        buses[(size_t) b] = std::make_unique<BusView> (engine, b);
        buses[(size_t) b]->onLayoutChanged = [this] { relayout(); };
        addChildComponent (*buses[(size_t) b]);
    }

    addStripButton.setButtonText ("+ Strip");
    addBusButton.setButtonText ("+ Bus");
    addStripButton.setTitle ("Add input strip");
    addStripButton.setTooltip ("Add an input strip (up to " + juce::String (kNumStrips) + ").");
    addStripButton.onClick = [this]
    {
        if (engine.addStrip() >= 0)
            relayout();
    };
    makeClickable (addStripButton);
    addChildComponent (addStripButton);

    addBusButton.setTitle ("Add bus");
    addBusButton.setTooltip ("Add an output bus: A for hardware outputs, B for virtual ones.");
    addBusButton.onClick = [this] { showAddBusMenu(); };
    makeClickable (addBusButton);
    addChildComponent (addBusButton);

    relayout();
}

void ConsoleContent::showAddBusMenu()
{
    juce::PopupMenu menu;
    for (int b = 0; b < kNumBuses; ++b)
    {
        if (engine.getBus (b).active.load())
            continue;
        const juce::String name (kBusNames[b]);
        menu.addItem (name + (name.startsWith ("B") ? "  (virtual out)" : "  (hardware out)"), [this, b]
        {
            engine.addBus (b);
            relayout();
        });
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (addBusButton));
}

void ConsoleContent::relayout()
{
    for (int s = 0; s < kNumStrips; ++s)
    {
        const bool on = engine.getStrip (s).active.load();
        strips[(size_t) s]->setVisible (on);
        if (on)
            strips[(size_t) s]->refreshFromStrip();   // send keys follow the active buses
    }

    for (int b = 0; b < kNumBuses; ++b)
    {
        buses[(size_t) b]->setVisible (engine.getBus (b).active.load());
        buses[(size_t) b]->refreshFromBus();
        buses[(size_t) b]->repaint();
    }

    soundboard.refresh();   // its send keys follow the active buses
    addStripButton.setVisible (engine.getNumActiveStrips() < kNumStrips);
    addBusButton.setVisible (engine.getNumActiveBuses() < kNumBuses);

    if (onSizeChanged)
        onSizeChanged();
    resized();
    repaint();
}

int ConsoleContent::minimumWidth() const
{
    const int ns = juce::jmax (1, engine.getNumActiveStrips());
    const int nb = juce::jmax (1, engine.getNumActiveBuses());
    const int desk = ns * StripView::minWidth + (ns - 1) * stripGap
                   + sectionGap
                   + juce::jmax (nb * BusView::minWidth + (nb - 1) * stripGap, 150);
    return consolePad * 2 + (soundboard.isVisible() ? juce::jmax (desk, SoundboardPanel::minWidth) : desk);
}

void ConsoleContent::paint (juce::Graphics& g)
{
    Theme::drawLegend (g, "Inputs", inputsLegend);
    Theme::drawLegend (g, "Outputs", outputsLegend);

    // Divider between the input channels and the master section, as on a desk.
    g.setColour (Theme::p().line);
    g.fillRect (dividerX, inputsLegend.getBottom(), 1, deskBottom - inputsLegend.getBottom());
}

void ConsoleContent::resized()
{
    auto r = getLocalBounds().reduced (consolePad, 0).withTrimmedBottom (consolePad);

    const int ns = juce::jmax (1, engine.getNumActiveStrips());
    const int nb = juce::jmax (1, engine.getNumActiveBuses());
    constexpr int soundW = 0;   // the soundboard is a bar below the desk, not a column

    // Fill the window: strips and buses share the width, a bus getting about 3/4 of a strip,
    // within each one's limits. Only past those limits is the desk centred with margins.
    const int avail = r.getWidth() - sectionGap - soundW - (ns - 1) * stripGap - (nb - 1) * stripGap;
    const int unit = (int) (avail / (ns + nb * 0.75f));
    const int stripW = juce::jlimit (StripView::minWidth, StripView::maxWidth, unit);
    const int busW = juce::jlimit (BusView::minWidth, BusView::maxWidth, (avail - ns * stripW) / nb);

    const int inputsW = ns * stripW + (ns - 1) * stripGap;
    const int outputsW = juce::jmax (nb * busW + (nb - 1) * stripGap, 150);
    const int deskW = inputsW + sectionGap + soundW + outputsW;
    r = r.withSizeKeepingCentre (juce::jmin (r.getWidth(), juce::jmax (deskW, soundboard.isVisible() ? SoundboardPanel::minWidth : 0)),
                                 r.getHeight());

    // Soundboard: a bar under the whole desk, so the strips and buses keep one fader line.
    if (soundboard.isVisible())
    {
        soundboard.setBounds (r.removeFromBottom (SoundboardPanel::barHeight));
        r.removeFromBottom (Theme::s3);
    }
    deskBottom = r.getBottom();
    r = r.withSizeKeepingCentre (juce::jmin (r.getWidth(), deskW), r.getHeight());

    // Section legends, with the add buttons at their right end.
    auto legends = r.removeFromTop (36);
    inputsLegend = legends.removeFromLeft (inputsW);
    legends.removeFromLeft (sectionGap);
    outputsLegend = legends;
    addStripButton.setBounds (inputsLegend.withTrimmedLeft (inputsLegend.getWidth() - 72).withSizeKeepingCentre (72, 24));
    addBusButton.setBounds (outputsLegend.withTrimmedLeft (outputsLegend.getWidth() - 64).withSizeKeepingCentre (64, 24));

    // One fader bay for every column: faders, meters and mute rows line up across the desk,
    // whatever sits above them. The bay takes about half the height.
    const int bayHeight = juce::jlimit (230, 440, (int) (r.getHeight() * 0.48f));
    const int bayTop = r.getHeight() - bayHeight;

    for (int s = 0; s < kNumStrips; ++s)
    {
        if (! strips[(size_t) s]->isVisible())
            continue;
        strips[(size_t) s]->setBayTop (bayTop);
        strips[(size_t) s]->setBounds (r.removeFromLeft (stripW));
        r.removeFromLeft (stripGap);
    }

    r.removeFromLeft (sectionGap - stripGap);
    dividerX = r.getX() - sectionGap / 2;

    for (int b = 0; b < kNumBuses; ++b)
    {
        if (! buses[(size_t) b]->isVisible())
            continue;
        buses[(size_t) b]->setBayTop (bayTop);
        buses[(size_t) b]->setBounds (r.removeFromLeft (busW));
        r.removeFromLeft (stripGap);
    }
}

//==============================================================================
// StatusDisplay

StatusDisplay::StatusDisplay() : juce::Button ({})
{
    makeClickable (*this);
    setWantsKeyboardFocus (true);
    setTooltip ("Audio settings: driver (ASIO or Windows Audio), device, channels, sample rate and buffer size.");
}

void StatusDisplay::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto& t = Theme::p();
    auto r = getLocalBounds().toFloat();

    // A recessed readout; hover and press lift it like any other control.
    g.setColour (down ? t.well.darker (0.2f) : highlighted ? t.well.brighter (0.06f) : t.well);
    g.fillRoundedRectangle (r, Theme::controlRadius);
    g.setColour (highlighted ? t.controlEdge : t.line);
    g.drawRoundedRectangle (r.reduced (0.5f), Theme::controlRadius, 1.0f);

    auto area = getLocalBounds().reduced (Theme::s3, 0);

    // Chevron: this readout opens Audio settings.
    auto chevron = area.removeFromRight (12).toFloat().withSizeKeepingCentre (9, 5);
    juce::Path p;
    p.startNewSubPath (chevron.getX(), chevron.getY());
    p.lineTo (chevron.getCentreX(), chevron.getBottom());
    p.lineTo (chevron.getRight(), chevron.getY());
    g.setColour (t.textDim);
    g.strokePath (p, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    area.removeFromRight (Theme::s2);

    const auto font = Theme::mono (12.0f);
    g.setFont (font);
    for (int i = 0; i < segments.size(); ++i)
    {
        const auto& seg = segments[i];
        const bool isDevice = i == 0;
        const int natural = juce::GlyphArrangement::getStringWidthInt (font, seg);
        const int w = isDevice ? juce::jmin (natural, area.getWidth()) : natural;
        if (w > area.getWidth() || w <= 0)
            break;

        g.setColour (isDevice ? t.text : t.textDim);
        g.drawText (seg, area.removeFromLeft (w), juce::Justification::centredLeft, true);

        if (i < segments.size() - 1 && area.getWidth() > 30)
        {
            area.removeFromLeft (Theme::s3);
            g.setColour (t.line);
            g.fillRect (area.getX(), area.getCentreY() - 7, 1, 14);
            area.removeFromLeft (Theme::s3);
        }
    }

    if (hasKeyboardFocus (false))
        Theme::drawFocusRing (g, getLocalBounds().toFloat());
}

//==============================================================================
// MainComponent

MainComponent::MainComponent()
{
    const auto session = Session::load();
    const bool firstRun = session == nullptr;

    if (session != nullptr)
        Theme::setDark (session->getStringAttribute ("theme", "dark") != "light");
    applyTheme();

    engine.start (session != nullptr ? session->getChildByName ("DEVICESETUP") : nullptr);

    // Crash guard: this marker exists only while plugins are being restored. If it's still
    // there at startup, a plugin crashed OpenMix last time, so start without plugins.
    const auto sessionFile = Session::getFile();
    const auto restoreMarker = sessionFile.getSiblingFile ("restoring-plugins.lock");
    bool safeMode = false;

    if (session != nullptr)
    {
        safeMode = restoreMarker.existsAsFile();
        if (safeMode)
            sessionFile.copyFileTo (sessionFile.getSiblingFile ("session-before-crash.xml"));

        sessionFile.getParentDirectory().createDirectory();
        restoreMarker.create();
        Session::apply (*session, engine, ! safeMode);
        restoreMarker.deleteFile();
    }

    if (safeMode)
        juce::MessageManager::callAsync ([backup = sessionFile.getSiblingFile ("session-before-crash.xml")]
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Started without plugins",
                "OpenMix closed unexpectedly while loading your plugins last time, so it started without them.\n\n"
                "Your previous setup, with plugins, is saved here:\n" + backup.getFullPathName()
                + "\n\nAdd plugins back one at a time to find the one that crashes.");
        });

    if (firstRun)
    {
        const int numInputs = engine.getActiveInputNames().size();
        for (int s = 0; s < kNumStrips; ++s)
            engine.getStrip (s).inputChannel.store (s < numInputs ? s : -1);
    }

    setFocusContainerType (FocusContainerType::keyboardFocusContainer);

    // The window itself holds focus at start (no ring), so the first Tab has somewhere to
    // start from; keyPressed() then moves it to the first control.
    setWantsKeyboardFocus (true);
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this)]
    {
        if (safe != nullptr && safe->isShowing())
            safe->grabKeyboardFocus();
    });

    statusDisplay.onClick = [this] { showAudioSettings(); };
    addAndMakeVisible (statusDisplay);

    appSettingsButton.setTooltip ("Light or dark theme, startup and tray, plugins, remote control, updates.");
    appSettingsButton.onClick = [this] { showSettings(); };
    makeClickable (appSettingsButton);
    addAndMakeVisible (appSettingsButton);

    fixButton.onClick = [this] { showAudioSettings(); };
    makeClickable (fixButton);
    addChildComponent (fixButton);

    console = std::make_unique<ConsoleContent> (engine);
    console->onSizeChanged = [this] { if (! getBounds().isEmpty()) resized(); };
    consoleView.setViewedComponent (console.get(), false);
    consoleView.setScrollBarsShown (true, true);
    consoleView.setScrollBarThickness (8);
    addAndMakeVisible (consoleView);

    engine.getDeviceManager().addChangeListener (this);
    engine.getScanner().addChangeListener (this);
    engine.getScanner().startScan();   // background, crash-safe; only new or changed plugins

    // Creator tools in the header
    makeLamp (recordButton, "mute");
    recordButton.setClickingTogglesState (false);
    recordButton.onClick = [this] { toggleRecording(); };
    recordButton.onSecondary = [this] { showRecordMenu(); };
    addAndMakeVisible (recordButton);

    recordMenuButton.setTitle ("Recording options");
    recordMenuButton.setTooltip ("What to record, the replay buffer, and the recordings folder.");
    recordMenuButton.onClick = [this] { showRecordMenu(); };
    addAndMakeVisible (recordMenuButton);

    replayButton.onClick = [this] { saveReplay(); };
    makeClickable (replayButton);
    addAndMakeVisible (replayButton);

    makeLamp (soundboardButton, "accent");
    soundboardButton.setClickingTogglesState (false);
    soundboardButton.setTitle ("Show the soundboard");
    soundboardButton.setTooltip ("Show or hide the soundboard column: eight pads you can play into any bus.");
    soundboardButton.onClick = [this] { setSoundboardShown (! console->soundboard.isVisible()); };
    addAndMakeVisible (soundboardButton);
    console->soundboard.onChanged = [this] { Session::save (engine); };
    setSoundboardShown (AppSettings::get().soundboardShown());

    updateButton.onClick = [this] { updatePage.launchInDefaultBrowser(); };
    makeClickable (updateButton);
    addChildComponent (updateButton);
    updater.onUpdateAvailable = [this] (juce::String version, juce::URL page)
    {
        updatePage = page;
        updateButton.setButtonText ("Update: " + version);
        updateButton.setTooltip ("OpenMix " + version + " is available. Opens the release page on GitHub.");
        updateButton.setVisible (true);
        resized();
    };
    if (AppSettings::get().checkForUpdates())
        updater.checkInBackground();

    osc.onRecord = [this] (bool on) { if (on != engine.getRecorder().isRecording()) toggleRecording(); };
    osc.onSaveReplay = [this] { saveReplay(); };
    osc.onRecallScene = [this] (int index) { recallScene (index); };
    applyOsc();

    // Scenes and global hotkeys
    sceneButton.setTooltip ("Scenes: saved mixes (levels, mutes, sends, processing) you can switch between.");
    sceneButton.onClick = [this] { showSceneMenu(); };
    addAndMakeVisible (sceneButton);
    updateSceneButton();

    hotkeys.onAction = [this] (const GlobalHotkeys::Binding& b, bool down) { handleHotkey (b, down); };
    hotkeys.load();
    applyReplaySettings();
    updateRecordButton();

    changeListenerCallback (&engine.getDeviceManager());
    startTimerHz (30);

    setSize (1320, 880);
}

MainComponent::~MainComponent()
{
    engine.getRecorder().stop();
    stopTimer();
    engine.getScanner().removeChangeListener (this);
    engine.getDeviceManager().removeChangeListener (this);
    Session::save (engine);

    // Close plugin editors before the engine (and the plugins) go away.
    consoleView.setViewedComponent (nullptr, false);
    console.reset();
}

//==============================================================================

void MainComponent::applyTheme()
{
    if (auto* laf = dynamic_cast<OpenMixLookAndFeel*> (&juce::LookAndFeel::getDefaultLookAndFeel()))
        laf->applyPalette();

    if (auto* window = dynamic_cast<juce::DocumentWindow*> (getTopLevelComponent()))
        window->setBackgroundColour (Theme::p().window);

    sendLookAndFeelChange();
    repaint();
}

void MainComponent::toggleTheme()
{
    Theme::setDark (! Theme::isDark());
    applyTheme();
    Session::save (engine);
}

void MainComponent::showAudioSettings()
{
    auto* selector = new juce::AudioDeviceSelectorComponent (engine.getDeviceManager(),
                                                             0, 16,   // inputs
                                                             2, 16,   // outputs
                                                             false, false, false, false);
    selector->setSize (560, 520);

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (selector);
    o.dialogTitle = "Audio settings";
    o.dialogBackgroundColour = Theme::p().panel;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.launchAsync();
}

void MainComponent::showSettings()
{
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (new SettingsPanel (engine, [this] { toggleTheme(); }, [this] { applyOsc(); }, [this] { showHotkeys(); }));
    o.dialogTitle = "Settings";
    o.dialogBackgroundColour = Theme::p().panel;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.launchAsync();
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &engine.getDeviceManager())
    {
        for (auto& s : console->strips) s->refreshDevice();
        for (auto& b : console->buses)  b->refreshDevice();
    }
    updateStatus();
}

void MainComponent::updateStatus()
{
    statusSegments.clearQuick();
    auto& dm = engine.getDeviceManager();

    if (auto* device = dm.getCurrentAudioDevice())
    {
        const double sr = device->getCurrentSampleRate();
        const int bs = device->getCurrentBufferSizeSamples();
        const double latencyMs = sr > 0 ? 1000.0 * (bs + device->getInputLatencyInSamples() + device->getOutputLatencyInSamples()) / sr : 0.0;
        const int dropouts = juce::jmax (0, device->getXRunCount()) + engine.getLinkDropouts();

        statusSegments.add (device->getTypeName() + "  " + device->getName());
        statusSegments.add (juce::String (sr / 1000.0, 1) + " kHz / " + juce::String (bs) + " smp");
        statusSegments.add (juce::String (latencyMs, 1) + " ms");
        statusSegments.add ("CPU " + juce::String (juce::roundToInt (dm.getCpuUsage() * 100.0)) + "%");
        statusSegments.add (juce::String (dropouts) + (dropouts == 1 ? " dropout" : " dropouts"));
    }
    else
    {
        statusSegments.add ("No audio device. Open Audio settings to choose one.");
    }

    if (note.isNotEmpty() && juce::Time::getMillisecondCounter() < noteUntilMs)
        statusSegments.insert (0, note);

    auto& scanner = engine.getScanner();
    if (scanner.isScanning() && scanner.getNumToScan() > 0)
        statusSegments.add ("Scanning plugins " + juce::String (scanner.getNumDone()) + "/" + juce::String (scanner.getNumToScan()));

    const bool split = engine.mainDeviceHasSplitClocks();
    if (split != showBanner)
    {
        showBanner = split;
        fixButton.setVisible (split);
        resized();
    }

    statusDisplay.setSegments (statusSegments);
    statusDisplay.setTitle ("Audio settings. " + statusSegments.joinIntoString (", "));
    repaint (bannerArea);
}

void MainComponent::timerCallback()
{
    for (auto& s : console->strips) if (s->isVisible()) s->tick();
    for (auto& b : console->buses)  if (b->isVisible()) b->tick();

    if (++ticks % 15 == 0)
    {
        updateStatus();
        updateRecordButton();
    }

    // Changes made over OSC: refresh what's on screen.
    if (const int ext = engine.externalChanges.load(); ext != lastExternalChanges)
    {
        lastExternalChanges = ext;
        for (auto& s : console->strips) if (s->isVisible()) s->refreshFromStrip();
        for (auto& b : console->buses)  if (b->isVisible()) b->refreshFromBus();
        console->soundboard.refresh();
        updateSceneButton();
    }

    if (ticks % 60 == 0)
    {
        engine.maintainSources();
        for (auto& s : console->strips) s->refreshDevice();
        for (auto& b : console->buses)  b->refreshDevice();
    }

    if (ticks % 300 == 0)
        Session::save (engine);
}

//==============================================================================

void MainComponent::paint (juce::Graphics& g)
{
    const auto& t = Theme::p();
    g.fillAll (t.window);

    // Header rail
    g.setColour (t.panel);
    g.fillRect (headerArea);
    g.setColour (t.line);
    g.fillRect (headerArea.withTop (headerArea.getBottom() - 1));

    // App icon + name
    auto brand = headerArea.reduced (consolePad, 0).removeFromLeft (110);
    static const auto icon = TrayIcon::makeIcon (48);
    g.drawImage (icon, brand.removeFromLeft (24).withSizeKeepingCentre (24, 24).toFloat(), juce::RectanglePlacement::centred);
    brand.removeFromLeft (Theme::s2);
    g.setColour (t.text);
    g.setFont (Theme::ui (16.0f, true));
    g.drawText ("OpenMix", brand, juce::Justification::centredLeft);

    // Divider between the transport group (record, replay) and the window tools.
    if (transportDividerX > 0)
    {
        g.setColour (t.line);
        g.fillRect (transportDividerX, headerArea.getCentreY() - 12, 1, 24);
    }

    // Clock-drift warning (text first, the edge is secondary)
    if (showBanner)
    {
        const auto b = bannerArea.toFloat();
        g.setColour (t.panel);
        g.fillRoundedRectangle (b, Theme::panelRadius);
        g.setColour (t.meterMid);
        g.drawRoundedRectangle (b.reduced (0.75f), Theme::panelRadius, 1.5f);
        g.setColour (t.text);
        g.setFont (Theme::ui (13.0f));
        g.drawFittedText ("Your mic and your output are two different devices in Audio settings. Their clocks drift apart, "
                          "which causes crackling. Set Input to <none> there, then choose the mic with a strip's "
                          "\"Windows input device\".",
                          bannerArea.reduced (Theme::s4, 0).withTrimmedRight (fixButton.getWidth() + Theme::s4),
                          juce::Justification::centredLeft, 2);
    }
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    // Shortcuts while OpenMix is focused. If a global hotkey just fired for this key press,
    // it has already been handled: don't act twice.
    if (juce::Time::getMillisecondCounter() - hotkeys.getLastFireMs() < 250)
        return true;

    const auto mods = key.getModifiers();
    if (mods.isCtrlDown() && mods.isShiftDown() && (key.getKeyCode() == 'R' || key.getKeyCode() == 'r'))
    {
        toggleRecording();
        return true;
    }
    if (mods.isCtrlDown() && mods.isShiftDown() && (key.getKeyCode() == 'S' || key.getKeyCode() == 's'))
    {
        saveReplay();
        return true;
    }

    // 1-8 play the soundboard pads while it's shown (text fields keep their digits).
    if (console->soundboard.isVisible() && ! mods.isAnyModifierKeyDown()
        && key.getKeyCode() >= '1' && key.getKeyCode() <= '0' + Soundboard::numPads)
    {
        engine.getSoundboard().trigger (key.getKeyCode() - '1');
        return true;
    }

    // Tab arrives here from children too (unhandled keys bubble up). Only step in when the
    // window itself has focus, otherwise let JUCE's normal focus order work.
    if (key.isKeyCode (juce::KeyPress::tabKey) && hasKeyboardFocus (false))
    {
        if (auto traverser = createKeyboardFocusTraverser())
        {
            auto all = traverser->getAllComponents (this);
            if (! all.empty())
                (key.getModifiers().isShiftDown() ? all.back() : all.front())->grabKeyboardFocus();
        }
        return true;
    }
    return false;
}

//==============================================================================
// Creator tools

void MainComponent::showNote (const juce::String& text)
{
    note = text;
    noteUntilMs = juce::Time::getMillisecondCounter() + 8000;
    updateStatus();
}

void MainComponent::updateRecordButton()
{
    auto& rec = engine.getRecorder();
    const bool on = rec.isRecording();
    recordButton.setToggleState (on, juce::dontSendNotification);

    if (on)
    {
        const int secs = (int) rec.getElapsedSeconds();
        recordButton.setButtonText ("Rec " + juce::String (secs / 60).paddedLeft ('0', 2) + ":" + juce::String (secs % 60).paddedLeft ('0', 2));
        recordButton.setTitle ("Stop recording");
        recordButton.setTooltip ("Recording. Click (or Ctrl+Shift+R) to stop.");
    }
    else
    {
        static const char* what[] { "every bus", "every strip", "buses and strips" };
        recordButton.setButtonText ("Record");
        recordButton.setTitle ("Start recording");
        recordButton.setTooltip (juce::String ("Record ") + what[juce::jlimit (0, 2, AppSettings::get().recordWhat())]
                                 + " to WAV files (Ctrl+Shift+R). Right-click or the dots for options.");
    }

    auto& replay = engine.getReplay();
    replayButton.setEnabled (replay.isEnabled());
    replayButton.setButtonText (replay.isEnabled() ? "Replay " + juce::String ((int) replay.getSeconds()) + " s" : "Replay off");
    replayButton.setTitle (replay.isEnabled() ? "Save the last " + juce::String ((int) replay.getSeconds()) + " seconds" : "Replay buffer is off");
    replayButton.setTooltip (replay.isEnabled()
        ? "Save the last " + juce::String ((int) replay.getSeconds()) + " seconds of " + juce::String (kBusNames[replay.sourceBus.load()]) + " (Ctrl+Shift+S)."
        : juce::String ("Turn on the replay buffer in the recording options (the dots next to Record)."));
}

void MainComponent::toggleRecording()
{
    auto& rec = engine.getRecorder();
    if (rec.isRecording())
    {
        rec.stop();
        showNote ("Recording saved to " + rec.getLastFolder().getFullPathName());
        juce::Logger::writeToLog ("Recording saved to " + rec.getLastFolder().getFullPathName());
    }
    else
    {
        startRecording();
    }
    updateRecordButton();
}

void MainComponent::startRecording()
{
    const int what = AppSettings::get().recordWhat();
    std::vector<Recorder::Target> targets;

    if (what == 0 || what == 2)
        for (int b = 0; b < kNumBuses; ++b)
            if (engine.getBus (b).active.load())
                targets.push_back ({ juce::String ("Bus ") + kBusNames[b], true, b });

    if (what == 1 || what == 2)
        for (int s = 0; s < kNumStrips; ++s)
        {
            auto& strip = engine.getStrip (s);
            if (strip.active.load())
                targets.push_back ({ juce::String (s + 1) + " " + (strip.name.isNotEmpty() ? strip.name : "Input " + juce::String (s + 1)), false, s });
        }

    juce::String error;
    if (! engine.getRecorder().start (targets, engine.getSampleRate(), error))
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Couldn't start recording", error);
    else if (error.isNotEmpty())
        showNote ("Recording, but: " + error);
}

void MainComponent::showRecordMenu()
{
    auto& settings = AppSettings::get();
    juce::PopupMenu menu;

    menu.addSectionHeader ("Record (one WAV file each)");
    const char* choices[] { "Every bus", "Every strip", "Buses and strips" };
    for (int i = 0; i < 3; ++i)
        menu.addItem (choices[i], ! engine.getRecorder().isRecording(), settings.recordWhat() == i, [this, i]
        {
            AppSettings::get().setRecordWhat (i);
            updateRecordButton();
        });

    menu.addSectionHeader ("Replay buffer (save the last seconds on demand)");
    for (int secs : { 0, 30, 60, 120 })
        menu.addItem (secs == 0 ? juce::String ("Off") : "Keep the last " + juce::String (secs) + " seconds", true,
                      settings.replaySeconds() == secs, [this, secs]
        {
            AppSettings::get().setReplaySeconds (secs);
            applyReplaySettings();
        });

    juce::PopupMenu source;
    for (int b = 0; b < kNumBuses; ++b)
        if (engine.getBus (b).active.load())
            source.addItem (kBusNames[b], true, settings.replayBus() == b, [this, b]
            {
                AppSettings::get().setReplayBus (b);
                applyReplaySettings();
            });
    menu.addSubMenu ("Replay listens to", source);

    menu.addSeparator();
    menu.addItem ("Open recordings folder", []
    {
        auto dir = Recorder::getRecordingsFolder();
        dir.createDirectory();
        dir.startAsProcess();
    });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (recordMenuButton));
}

void MainComponent::applyReplaySettings()
{
    auto& replay = engine.getReplay();
    replay.sourceBus.store (juce::jlimit (0, kNumBuses - 1, AppSettings::get().replayBus()));
    replay.configure (engine.getSampleRate(), AppSettings::get().replaySeconds());
    updateRecordButton();
}

void MainComponent::saveReplay()
{
    if (! engine.getReplay().isEnabled())
    {
        showNote ("The replay buffer is off. Turn it on in the recording options.");
        return;
    }

    juce::Component::SafePointer<MainComponent> safe (this);
    engine.getReplay().save ([safe] (juce::File file, juce::String error)
    {
        if (safe == nullptr)
            return;
        if (file == juce::File())
            safe->showNote ("Replay not saved: " + error);
        else
            safe->showNote ("Replay saved: " + file.getFileName());
    });
}

void MainComponent::setSoundboardShown (bool shown)
{
    AppSettings::get().setSoundboardShown (shown);
    soundboardButton.setToggleState (shown, juce::dontSendNotification);
    console->soundboard.setVisible (shown);
    console->relayout();
}

void MainComponent::applyOsc()
{
    auto& settings = AppSettings::get();
    if (! settings.oscEnabled())
    {
        osc.stop();
        return;
    }

    juce::String error;
    if (! osc.start (settings.oscPort(), error))
        showNote ("OSC remote control is off: " + error);
}

//==============================================================================
// Scenes

juce::StringArray MainComponent::stripNames() const
{
    juce::StringArray names;
    for (int s = 0; s < kNumStrips; ++s)
    {
        const auto& n = const_cast<MixerEngine&> (engine).getStrip (s).name;
        names.add (n.isNotEmpty() ? n : "Input " + juce::String (s + 1));
    }
    return names;
}

juce::StringArray MainComponent::sceneNames() const
{
    juce::StringArray names;
    for (const auto& s : engine.scenes)
        names.add (s.name);
    return names;
}

void MainComponent::updateSceneButton()
{
    const bool has = juce::isPositiveAndBelow (engine.currentScene, (int) engine.scenes.size());
    const auto text = has ? engine.scenes[(size_t) engine.currentScene].name
                          : engine.scenes.empty() ? juce::String ("Save a scene") : juce::String ("None");
    sceneButton.setContent ("SCENE", text, false);
    sceneButton.setTitle ("Scene: " + text + ". Opens the scene menu.");
}

void MainComponent::recallScene (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) engine.scenes.size()))
        return;

    Session::applyScene (*engine.scenes[(size_t) index].state, engine);
    engine.currentScene = index;
    updateSceneButton();
    showNote ("Scene: " + engine.scenes[(size_t) index].name);
}

void MainComponent::saveScene (int index)
{
    if (juce::isPositiveAndBelow (index, (int) engine.scenes.size()))
    {
        engine.scenes[(size_t) index].state = Session::captureScene (engine);
        engine.currentScene = index;
        updateSceneButton();
        showNote ("Scene \"" + engine.scenes[(size_t) index].name + "\" updated");
        Session::save (engine);
        return;
    }

    auto* w = new juce::AlertWindow ("Save the current mix as a scene",
                                     "Levels, mutes, sends, processing and plugin bypass are saved. "
                                     "Sources, devices and plugins stay as they are when you switch.",
                                     juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", "Scene " + juce::String ((int) engine.scenes.size() + 1), "Name");
    w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<MainComponent> safe (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int result)
    {
        if (safe == nullptr || result != 1)
            return;
        const auto name = w->getTextEditorContents ("name").trim();
        auto& engine = safe->engine;
        engine.scenes.push_back ({ name.isNotEmpty() ? name : "Scene " + juce::String ((int) engine.scenes.size() + 1),
                                   Session::captureScene (engine) });
        engine.currentScene = (int) engine.scenes.size() - 1;
        safe->updateSceneButton();
        Session::save (engine);
    }), true);
}

void MainComponent::renameScene (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) engine.scenes.size()))
        return;

    auto* w = new juce::AlertWindow ("Rename scene", {}, juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", engine.scenes[(size_t) index].name, "Name");
    w->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<MainComponent> safe (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w, index] (int result)
    {
        if (safe == nullptr || result != 1 || ! juce::isPositiveAndBelow (index, (int) safe->engine.scenes.size()))
            return;
        const auto name = w->getTextEditorContents ("name").trim();
        if (name.isNotEmpty())
            safe->engine.scenes[(size_t) index].name = name;
        safe->updateSceneButton();
        Session::save (safe->engine);
    }), true);
}

void MainComponent::deleteScene (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) engine.scenes.size()))
        return;

    const auto name = engine.scenes[(size_t) index].name;
    juce::Component::SafePointer<MainComponent> safe (this);
    juce::AlertWindow::showAsync (
        juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::QuestionIcon).withTitle ("Delete scene \"" + name + "\"?")
            .withMessage ("The mix as it is now doesn't change. Hotkeys for later scenes move up with them.")
            .withButton ("Delete", 1).withButton ("Cancel", 0).withAssociatedComponent (this),
        [safe, index] (int result)
        {
            if (safe == nullptr || result != 1 || ! juce::isPositiveAndBelow (index, (int) safe->engine.scenes.size()))
                return;
            auto& engine = safe->engine;
            engine.scenes.erase (engine.scenes.begin() + index);
            if (engine.currentScene == index)     engine.currentScene = -1;
            else if (engine.currentScene > index) --engine.currentScene;

            // Hotkeys that recall scenes keep pointing at the same scenes.
            auto bindings = safe->hotkeys.getBindings();
            std::vector<GlobalHotkeys::Binding> kept;
            for (auto b : bindings)
            {
                if (b.action == GlobalHotkeys::Action::RecallScene)
                {
                    if (b.index == index) continue;
                    if (b.index > index)  --b.index;
                }
                kept.push_back (b);
            }
            safe->hotkeys.setBindings (std::move (kept));
            safe->hotkeys.save();

            safe->updateSceneButton();
            Session::save (engine);
        });
}

void MainComponent::showSceneMenu()
{
    juce::PopupMenu menu;
    const int n = (int) engine.scenes.size();

    if (n == 0)
        menu.addItem ("No scenes yet. Save the current mix to make one.", false, false, nullptr);
    for (int i = 0; i < n; ++i)
        menu.addItem (engine.scenes[(size_t) i].name, true, i == engine.currentScene, [this, i] { recallScene (i); });

    menu.addSeparator();
    menu.addItem ("Save current mix as a new scene...", [this] { saveScene (-1); });

    if (juce::isPositiveAndBelow (engine.currentScene, n))
    {
        const int c = engine.currentScene;
        const auto name = engine.scenes[(size_t) c].name;
        menu.addItem ("Update \"" + name + "\" with the current mix", [this, c] { saveScene (c); });
        menu.addItem ("Rename \"" + name + "\"...", [this, c] { renameScene (c); });
        menu.addItem ("Delete \"" + name + "\"...", [this, c] { deleteScene (c); });
    }

    menu.addSeparator();
    menu.addItem ("Hotkeys...", [this] { showHotkeys(); });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (sceneButton).withMinimumWidth (sceneButton.getWidth()));
}

//==============================================================================
// Global hotkeys

void MainComponent::handleHotkey (const GlobalHotkeys::Binding& b, bool down)
{
    using A = GlobalHotkeys::Action;
    auto strip = [&]() -> ChannelStrip* { return juce::isPositiveAndBelow (b.index, kNumStrips) ? &engine.getStrip (b.index) : nullptr; };

    switch (b.action)
    {
        case A::ToggleMute:
            if (! down) return;
            if (auto* s = strip()) s->mute.store (! s->mute.load());
            break;

        case A::PushToTalk:
            // Open while the key is held, muted again when it's released.
            if (auto* s = strip()) s->mute.store (! down);
            break;

        case A::PlayPad:
            if (down) engine.getSoundboard().trigger (b.index);
            return;

        case A::StopSounds:
            if (down) engine.getSoundboard().stopAll();
            return;

        case A::ToggleRecord:
            if (down) toggleRecording();
            return;

        case A::SaveReplay:
            if (down) saveReplay();
            return;

        case A::RecallScene:
            if (down) recallScene (b.index);
            return;
    }

    engine.externalChanges.fetch_add (1);   // mute changes show on the strip
}

void MainComponent::showHotkeys()
{
    juce::Array<int> active;
    for (int s = 0; s < kNumStrips; ++s)
        if (engine.getStrip (s).active.load())
            active.add (s);
    auto* panel = new HotkeysPanel (hotkeys, stripNames(), active, sceneNames());

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (panel);
    o.dialogTitle = "Hotkeys";
    o.dialogBackgroundColour = Theme::p().panel;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.componentToCentreAround = this;
    o.launchAsync();
}

void MainComponent::resized()
{
    auto r = getLocalBounds();

    // Header, left to right: name | status readout (opens Audio settings) | scene picker |
    // transport (record, its options, replay) | window tools (soundboard, update, settings).
    headerArea = r.removeFromTop (48);
    auto header = headerArea.reduced (consolePad, 0);
    constexpr int h = 28;
    header.removeFromLeft (120);

    appSettingsButton.setBounds (header.removeFromRight (84).withSizeKeepingCentre (84, h));
    header.removeFromRight (Theme::s1);
    if (updateButton.isVisible())
    {
        updateButton.setBounds (header.removeFromRight (124).withSizeKeepingCentre (124, h));
        header.removeFromRight (Theme::s1);
    }
    soundboardButton.setBounds (header.removeFromRight (108).withSizeKeepingCentre (108, h));

    header.removeFromRight (Theme::s3);
    transportDividerX = header.getRight();
    header.removeFromRight (Theme::s3);

    replayButton.setBounds (header.removeFromRight (104).withSizeKeepingCentre (104, h));
    header.removeFromRight (Theme::s1);
    recordMenuButton.setBounds (header.removeFromRight (h).withSizeKeepingCentre (h, h));
    header.removeFromRight (2);
    recordButton.setBounds (header.removeFromRight (100).withSizeKeepingCentre (100, h));
    header.removeFromRight (Theme::s4);

    sceneButton.setBounds (header.removeFromRight (170).withSizeKeepingCentre (170, h));
    header.removeFromRight (Theme::s3);

    // The readout takes what's left, up to a comfortable width.
    const int statusW = juce::jmin (header.getWidth(), 640);
    statusDisplay.setVisible (statusW > 120);
    statusDisplay.setBounds (header.removeFromLeft (statusW).withSizeKeepingCentre (statusW, h));

    r.removeFromTop (Theme::s3);

    if (showBanner)
    {
        bannerArea = r.removeFromTop (52).reduced (consolePad, 0);
        fixButton.setBounds (bannerArea.withTrimmedLeft (bannerArea.getWidth() - 176).reduced (Theme::s3, 11));
        r.removeFromTop (Theme::s3);
    }
    else
    {
        bannerArea = {};
    }

    consoleView.setBounds (r);
    const int minW = console->minimumWidth();
    console->setSize (juce::jmax (r.getWidth() - (r.getHeight() < console->minimumHeight() ? consoleView.getScrollBarThickness() : 0), minW),
                      juce::jmax (r.getHeight() - (r.getWidth() < minW ? consoleView.getScrollBarThickness() : 0),
                                  console->minimumHeight()));
}
