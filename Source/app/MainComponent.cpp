#include "app/MainComponent.h"
#include "ui/RoutingPanel.h"
#include "ui/MediaPlayerPanel.h"
#include "app/Session.h"
#include "app/AppSettings.h"
#include "ui/Theme.h"
#include "ui/OpenMixLookAndFeel.h"
#include "ui/SettingsPanel.h"
#include "ui/TrayIcon.h"
#include "ui/HotkeysPanel.h"

// For restartApp; declared here to keep <windows.h> out of this file (as Main.cpp does).
extern "C" __declspec (dllimport) unsigned long __stdcall GetCurrentProcessId();

// Windows 11 window corners (DWM): rounded normally, square while maximised.
#pragma comment (lib, "dwmapi.lib")
extern "C" __declspec (dllimport) long __stdcall DwmSetWindowAttribute (void* hwnd, unsigned long attribute, const void* value, unsigned long size);

static void setRoundCorners (juce::Component& window, bool round)
{
    if (auto* peer = window.getPeer())
    {
        const int preference = round ? 2 : 1;   // DWMWCP_ROUND : DWMWCP_DONOTROUND
        DwmSetWindowAttribute (peer->getNativeHandle(), 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &preference, sizeof (preference));
    }
}

// Console gaps; compact mode tightens them (Theme::isCompact()).
static int stripGap()   { return Theme::isCompact() ? 5 : 6; }
static int sectionGap() { return Theme::isCompact() ? 12 : 16; }   // space between inputs and the master section
static int consolePad() { return Theme::isCompact() ? 12 : 16; }

//==============================================================================
// ConsoleContent

ConsoleContent::ConsoleContent (MixerEngine& e) : soundboard (e), media (e), engine (e)
{
    addChildComponent (soundboard);
    addChildComponent (media);

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
        {
            relayout();
            Session::save (engine);
        }
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
    // Pick the group first (Hardware = A slots, Virtual = B slots), then a name;
    // the bus takes the next free slot in that group.
    juce::PopupMenu menu;
    menu.addItem ("Hardware bus (A)...", engine.findNextFreeSlotInGroup (false) >= 0, false,
                  [this] { askBusName (false); });
    menu.addItem ("Virtual bus (B)...", engine.findNextFreeSlotInGroup (true) >= 0, false,
                  [this] { askBusName (true); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (addBusButton));
}

void ConsoleContent::askBusName (bool virtualGroup)
{
    const int slot = engine.findNextFreeSlotInGroup (virtualGroup);
    if (slot < 0)
        return;

    const auto groupName = virtualGroup ? juce::String ("virtual (B)") : juce::String ("hardware (A)");
    const auto slotName = virtualGroup && ! omx::isVirtualBusSlot (slot) ? engine.borrowedBusName (slot)
                                                                         : juce::String (kBusNames[slot]);
    auto* w = new juce::AlertWindow ("Add a " + groupName + " bus",
                                     "It takes the next free slot in the " + groupName + " group ("
                                     + slotName + "). Give it a custom name, or leave "
                                     "it empty for the default name.",
                                     juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", "", "Bus name (optional)");
    w->addButton ("Add bus", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<ConsoleContent> safe (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w, slot, virtualGroup] (int result)
    {
        if (safe == nullptr || result != 1)
            return;
        safe->engine.addBus (slot, virtualGroup);
        const auto name = w->getTextEditorContents ("name").trim();
        if (name.isNotEmpty())
            safe->engine.setBusName (slot, name);   // empty = default A1..A4 / B1..B4 (B5.. when borrowed)
        safe->relayout();
        Session::save (safe->engine);
    }), true);
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
    int nA = 0, nB = 0;
    for (int b = 0; b < kNumBuses; ++b)
        if (engine.getBus (b).active.load())
            (engine.isVirtualBus (b) ? nB : nA)++;
    const int gap = (nA > 0 && nB > 0) ? groupGap() : 0;
    const int desk = ns * StripView::minWidth() + (ns - 1) * stripGap()
                   + sectionGap()
                   + juce::jmax (nb * BusView::minWidth() + (nb - 1) * stripGap() + gap, 150);
    return consolePad() * 2 + (soundboard.isVisible() || media.isVisible() ? juce::jmax (desk, SoundboardPanel::minWidth) : desk);
}

void ConsoleContent::paint (juce::Graphics& g)
{
    Theme::drawLegend (g, "Inputs", inputsLegend);
    if (! hardwareLegend.isEmpty())
        Theme::drawLegend (g, "Hardware", hardwareLegend);
    if (! virtualLegend.isEmpty())
        Theme::drawLegend (g, "Virtual", virtualLegend);

    // Divider between the input channels and the master section, as on a desk.
    g.setColour (Theme::p().line);
    g.fillRect (dividerX, inputsLegend.getBottom(), 1, deskBottom - inputsLegend.getBottom());

    // Divider between the Hardware and Virtual bus groups.
    if (groupDividerX > 0)
    {
        g.setColour (Theme::p().line);
        g.fillRect (groupDividerX, groupDividerTop, 1, groupDividerBottom - groupDividerTop);
    }
}

void ConsoleContent::resized()
{
    auto r = getLocalBounds().reduced (consolePad(), 0).withTrimmedBottom (consolePad());

    const int ns = juce::jmax (1, engine.getNumActiveStrips());
    const int nb = juce::jmax (1, engine.getNumActiveBuses());
    constexpr int soundW = 0;   // the soundboard is a bar below the desk, not a column

    // Narrow console columns: strips and buses share the width within their (narrow) limits;
    // past those limits the desk is centred.
    // The bus groups get a gap + divider between them when both have active buses.
    int nA = 0, nB = 0;
    for (int b = 0; b < kNumBuses; ++b)
        if (engine.getBus (b).active.load())
            (engine.isVirtualBus (b) ? nB : nA)++;
    const int busGroupGap = (nA > 0 && nB > 0) ? groupGap() : 0;

    const int avail = r.getWidth() - sectionGap() - soundW - (ns - 1) * stripGap() - (nb - 1) * stripGap() - busGroupGap;
    const int unit = avail / (ns + nb);
    const int stripW = juce::jlimit (StripView::minWidth(), StripView::maxWidth(), unit);
    const int busW = juce::jlimit (BusView::minWidth(), BusView::maxWidth(), (avail - ns * stripW) / nb);

    const int inputsW = ns * stripW + (ns - 1) * stripGap();
    const int outputsW = juce::jmax (nb * busW + (nb - 1) * stripGap() + busGroupGap, 150);
    const int deskW = inputsW + sectionGap() + soundW + outputsW;
    r = r.withSizeKeepingCentre (juce::jmin (r.getWidth(), juce::jmax (deskW, soundboard.isVisible() || media.isVisible() ? SoundboardPanel::minWidth : 0)),
                                 r.getHeight());

    // Soundboard: a bar under the whole desk, so the strips and buses keep one fader line.
    if (soundboard.isVisible())
    {
        soundboard.setBounds (r.removeFromBottom (SoundboardPanel::barHeight()));
        r.removeFromBottom (Theme::s2);
    }
    if (media.isVisible())
    {
        media.setBounds (r.removeFromBottom (MediaPlayerPanel::barHeight()));
        r.removeFromBottom (Theme::s2);
    }
    deskBottom = r.getBottom();
    r = r.withSizeKeepingCentre (juce::jmin (r.getWidth(), deskW), r.getHeight());

    // Section legends, with the add buttons at their right end. The bus row's group
    // labels sit over their own group's columns (HARDWARE over A, VIRTUAL over B).
    auto legends = r.removeFromTop (30);
    inputsLegend = legends.removeFromLeft (inputsW);
    legends.removeFromLeft (sectionGap());
    outputsLegend = legends;
    addStripButton.setBounds (inputsLegend.withTrimmedLeft (inputsLegend.getWidth() - 72).withSizeKeepingCentre (72, 22));
    addBusButton.setBounds (outputsLegend.withTrimmedLeft (outputsLegend.getWidth() - 64).withSizeKeepingCentre (64, 22));

    const int aW = nA > 0 ? nA * busW + (nA - 1) * stripGap() : 0;
    const int bW = nB > 0 ? nB * busW + (nB - 1) * stripGap() : 0;
    hardwareLegend = nA > 0 ? outputsLegend.withWidth (aW) : juce::Rectangle<int>();
    virtualLegend = nB > 0 ? outputsLegend.withTrimmedLeft (aW + busGroupGap).withWidth (bW) : juce::Rectangle<int>();
    groupDividerTop = inputsLegend.getBottom();

    // One fader bay for every column: faders, meters and mute rows line up across the desk,
    // whatever sits above them.
    const int bayHeight = juce::jlimit (250, 380, (int) (r.getHeight() * 0.44f));
    const int bayTop = r.getHeight() - bayHeight;

    for (int s = 0; s < kNumStrips; ++s)
    {
        if (! strips[(size_t) s]->isVisible())
            continue;
        strips[(size_t) s]->setBayTop (bayTop);
        strips[(size_t) s]->setBounds (r.removeFromLeft (stripW));
        r.removeFromLeft (stripGap());
    }

    r.removeFromLeft (sectionGap() - stripGap());
    dividerX = r.getX() - sectionGap() / 2;
    groupDividerBottom = deskBottom;

    // Buses in display order, with the group gap + divider between the A and B groups.
    groupDividerX = 0;
    int placed = 0;
    for (int b : engine.getBusDisplayOrder())
    {
        if (! buses[(size_t) b]->isVisible())
            continue;
        if (engine.isVirtualBus (b) && placed == nA && busGroupGap > 0)
        {
            groupDividerX = r.getX() + busGroupGap / 2;
            r.removeFromLeft (busGroupGap);
        }
        buses[(size_t) b]->setBayTop (bayTop);
        buses[(size_t) b]->setBounds (r.removeFromLeft (busW));
        r.removeFromLeft (stripGap());
        ++placed;
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

    // Plain text like the rest of the status bar; hover shows that it opens Audio settings.
    if (highlighted || down)
    {
        g.setColour (down ? t.raised.darker (0.1f) : t.raised);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), Theme::controlRadius);
    }
    if (hasKeyboardFocus (false))
        Theme::drawFocusRing (g, getLocalBounds().toFloat());

    auto area = getLocalBounds().reduced (Theme::s2, 0);
    const auto nameFont = Theme::ui (11.5f, true), valueFont = Theme::mono (11.0f);

    // Right-aligned, last segment first, so the device name is what gets cut on a narrow window.
    for (int i = segments.size(); --i >= 0;)
    {
        const auto& seg = segments[i];
        const auto& font = i == 0 ? nameFont : valueFont;
        g.setFont (font);
        const int natural = juce::GlyphArrangement::getStringWidthInt (font, seg);
        const int w = juce::jmin (natural, area.getWidth());
        if (w <= 0)
            break;
        g.setColour (i == 0 ? t.text : t.textDim);
        g.drawText (seg, area.removeFromRight (w), juce::Justification::centredRight, true);

        if (i > 0 && area.getWidth() > 30)
        {
            area.removeFromRight (Theme::s2);
            g.setColour (t.line);
            g.fillRect (area.getRight(), area.getCentreY() - 6, 1, 12);
            area.removeFromRight (Theme::s2);
        }
    }
}

//==============================================================================
// StatusBar

void StatusBar::setServices (std::vector<Service> s)
{
    const bool same = s.size() == services.size()
        && std::equal (s.begin(), s.end(), services.begin(), [] (const Service& a, const Service& b)
                       { return a.name == b.name && a.detail == b.detail && a.running == b.running; });
    if (same)
        return;
    services = std::move (s);
    setTitle ("Status");
    repaint();
}

void StatusBar::paint (juce::Graphics& g)
{
    const auto& t = Theme::p();
    g.setColour (t.panel);
    g.fillRect (getLocalBounds());
    g.setColour (t.line);
    g.fillRect (getLocalBounds().withHeight (1));

    auto area = getLocalBounds().reduced (consolePad(), 0).withTrimmedRight (reservedRight);

    // LIVE indicator: the audio device is running.
    auto liveArea = area.removeFromLeft (74);
    g.setColour (isLive ? t.meterLow : t.textFaint);
    g.fillEllipse (liveArea.removeFromLeft (10).withSizeKeepingCentre (8, 8).toFloat());
    liveArea.removeFromLeft (5);
    g.setFont (Theme::ui (11.5f, true));
    g.setColour (isLive ? t.text : t.textFaint);
    g.drawText (isLive ? "LIVE" : "OFFLINE", liveArea, juce::Justification::centredLeft);

    area.removeFromLeft (Theme::s3);
    g.setColour (t.line);
    g.fillRect (area.getX(), area.getCentreY() - 7, 1, 14);
    area.removeFromLeft (Theme::s3);

    // Right: one lamp per switched-on remote service, lit while it is listening.
    {
        const auto nameFont = Theme::ui (11.5f, true), detailFont = Theme::mono (11.0f);
        for (int i = (int) services.size(); --i >= 0;)
        {
            const auto& s = services[(size_t) i];
            const int nameW = juce::GlyphArrangement::getStringWidthInt (nameFont, s.name);
            const int detailW = juce::jmin (160, juce::GlyphArrangement::getStringWidthInt (detailFont, s.detail));
            const int w = 10 + 5 + nameW + 6 + detailW;
            if (w > area.getWidth() - 120)
                break;   // the left side keeps room for the engine readout

            auto cell = area.removeFromRight (w);
            area.removeFromRight (Theme::s4);
            g.setColour (s.running ? t.meterLow : t.mute);
            g.fillEllipse (cell.removeFromLeft (10).withSizeKeepingCentre (7, 7).toFloat());
            cell.removeFromLeft (5);
            g.setFont (nameFont);
            g.setColour (t.text);
            g.drawText (s.name, cell.removeFromLeft (nameW), juce::Justification::centredLeft);
            cell.removeFromLeft (6);
            g.setFont (detailFont);
            g.setColour (t.textDim);
            g.drawText (s.detail, cell, juce::Justification::centredLeft, true);
        }
    }

    const auto font = Theme::mono (12.0f);
    g.setFont (font);
    for (int i = 0; i < segments.size(); ++i)
    {
        const auto& seg = segments[i];
        const bool isFirst = i == 0;
        const int natural = juce::GlyphArrangement::getStringWidthInt (font, seg);
        const int w = isFirst ? juce::jmin (natural, area.getWidth()) : natural;
        if (w > area.getWidth() || w <= 0)
            break;

        g.setColour (isFirst ? t.text : t.textDim);
        g.drawText (seg, area.removeFromLeft (w), juce::Justification::centredLeft, true);

        if (i < segments.size() - 1 && area.getWidth() > 30)
        {
            area.removeFromLeft (Theme::s3);
            g.setColour (t.line);
            g.fillRect (area.getX(), area.getCentreY() - 7, 1, 14);
            area.removeFromLeft (Theme::s3);
        }
    }
}

//==============================================================================
// MainComponent

MainComponent::MainComponent()
{
    const auto session = Session::load();
    const bool firstRun = session == nullptr;

    if (session != nullptr)
        Theme::setDark (session->getStringAttribute ("theme", "dark") != "light");

    // Appearance preferences (AppSettings): compact density.
    Theme::setCompact (AppSettings::get().compactMode());
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

    // Window buttons (the header is the window's title bar).
    minimiseDot.onClick = [this] { if (auto* w = window()) w->setMinimised (true); };
    maximiseDot.onClick = [this] { toggleMaximised(); };
    closeDot.onClick = [this] { if (auto* w = window()) w->closeButtonPressed(); };
    for (auto* d : { &minimiseDot, &maximiseDot, &closeDot })
        addAndMakeVisible (*d);

    // Menu bar: Mixer / View / Setup, flat text buttons that open menus.
    for (auto* b : { &mixerMenuButton, &viewMenuButton, &setupMenuButton })
    {
        b->getProperties().set ("flat", true);
        makeClickable (*b);
        addAndMakeVisible (*b);
    }
    mixerMenuButton.setTooltip ("Add strips and buses, scenes, hotkeys.");
    mixerMenuButton.onClick = [this] { showMixerMenu(); };
    viewMenuButton.setTooltip ("Soundboard, compact desk, light or dark theme.");
    viewMenuButton.onClick = [this] { showViewMenu(); };
    setupMenuButton.setTooltip ("Audio device, settings, hotkeys, recordings folder.");
    setupMenuButton.onClick = [this] { showSetupMenu(); };

    // Scene stepper: previous / picker / next, Store (update the current scene), New.
    scenePrevButton.setTitle ("Previous scene");
    scenePrevButton.setTooltip ("Switch to the previous scene.");
    scenePrevButton.onClick = [this] { stepScene (-1); };
    sceneNextButton.setTitle ("Next scene");
    sceneNextButton.setTooltip ("Switch to the next scene.");
    sceneNextButton.onClick = [this] { stepScene (1); };
    sceneStoreButton.setTooltip ("Store the current mix into the selected scene.");
    sceneStoreButton.onClick = [this] { saveScene (engine.currentScene); };
    sceneNewButton.setTooltip ("Save the current mix as a new scene.");
    sceneNewButton.onClick = [this] { saveScene (-1); };
    for (auto* b : { &scenePrevButton, &sceneNextButton, &sceneStoreButton, &sceneNewButton })
    {
        makeClickable (*b);
        addAndMakeVisible (*b);
    }

    // Bottom status bar: device / rate / buffer / engine CPU / LIVE.
    addAndMakeVisible (statusBar);


    fixButton.onClick = [this] { showAudioSettings(); };
    makeClickable (fixButton);
    addChildComponent (fixButton);

    console = std::make_unique<ConsoleContent> (engine);
    console->onSizeChanged = [this] { if (! getBounds().isEmpty()) resized(); };
    consoleView.setViewedComponent (console.get(), false);
    applyLock();
    juce::Desktop::getInstance().addFocusChangeListener (this);
    StripView::onPasteStep = [this] (const juce::Array<int>& strips, bool) { history.offer (captureMixText (strips)); };
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

    makeLamp (soundboardButton, "key");
    soundboardButton.setClickingTogglesState (false);
    soundboardButton.setTitle ("Show the soundboard");
    soundboardButton.setTooltip ("Show or hide the soundboard column: eight pads you can play into any bus.");
    soundboardButton.onClick = [this] { setSoundboardShown (! console->soundboard.isVisible()); };
    addAndMakeVisible (soundboardButton);
    makeLamp (mediaButton, "key");
    mediaButton.setClickingTogglesState (false);
    mediaButton.setTitle ("Show the media player");
    mediaButton.setTooltip ("Show or hide the media player bar: a playlist of audio and video files any strip can play.");
    mediaButton.onClick = [this] { setMediaShown (! console->media.isVisible()); };
    addAndMakeVisible (mediaButton);
    console->soundboard.onChanged = [this] { Session::save (engine); };
    for (auto& s : console->strips)
        s->onOpenMediaPlayer = [this] { setMediaShown (true); };
    setSoundboardShown (AppSettings::get().soundboardShown());
    console->media.onChanged = [this] { Session::save (engine); };
    setMediaShown (AppSettings::get().mediaShown());

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
    remoteServer.onRecord = osc.onRecord;
    remoteServer.onSaveReplay = osc.onSaveReplay;
    remoteServer.onRecallScene = osc.onRecallScene;
    midiControl.onRecord = osc.onRecord;
    midiControl.onSaveReplay = osc.onSaveReplay;
    midiControl.onRecallScene = osc.onRecallScene;
    midiControl.setMappings (MidiControl::mappingsFromJson (AppSettings::get().midiMappings()));
    applyRemote();

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
    juce::Desktop::getInstance().removeFocusChangeListener (this);
    StripView::onPasteStep = nullptr;
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
    {
        window->setBackgroundColour (Theme::p().window);
        window->repaint();
    }

    sendLookAndFeelChange();
    repaint();
}

void MainComponent::toggleTheme()
{
    Theme::setDark (! Theme::isDark());
    applyTheme();
    Session::save (engine);
}

namespace
{
class AudioSettingsPanel : public juce::Component, private juce::ChangeListener
{
public:
    explicit AudioSettingsPanel (MixerEngine& e)
        : dm (e.getDeviceManager())
    {
        setFocusContainerType (FocusContainerType::keyboardFocusContainer);
        content.setFocusContainerType (FocusContainerType::keyboardFocusContainer);
        viewport.setViewedComponent (&content, false);
        viewport.setScrollBarsShown (true, false);
        viewport.setScrollBarThickness (8);
        addAndMakeVisible (viewport);

        rescanButton.setTooltip ("Rescan for newly connected audio interfaces or USB devices.");
        rescanButton.onClick = [this]
        {
            if (auto* t = dm.getCurrentDeviceTypeObject())
                t->scanForDevices();
            rebuildFromDevice();
        };
        makeClickable (rescanButton);
        content.addAndMakeVisible (rescanButton);

        outputBox.setTitle ("Audio output device");
        outputBox.onChange = [this]
        {
            if (rebuilding) return;
            auto setup = dm.getAudioDeviceSetup();
            const int id = outputBox.getSelectedId();
            setup.outputDeviceName = (id <= 1 || ! juce::isPositiveAndBelow (id - 2, outputNames.size()))
                                       ? juce::String() : outputNames[id - 2];
            applySetup (setup);
        };
        makeClickable (outputBox);
        content.addAndMakeVisible (outputBox);

        inputBox.setTitle ("Audio input device");
        inputBox.onChange = [this]
        {
            if (rebuilding) return;
            auto setup = dm.getAudioDeviceSetup();
            const int id = inputBox.getSelectedId();
            setup.inputDeviceName = (id <= 1 || ! juce::isPositiveAndBelow (id - 2, inputNames.size()))
                                      ? juce::String() : inputNames[id - 2];
            applySetup (setup);
        };
        makeClickable (inputBox);
        content.addChildComponent (inputBox);

        rateBox.setTitle ("Sample rate");
        rateBox.onChange = [this]
        {
            if (rebuilding) return;
            const int idx = rateBox.getSelectedId() - 1;
            if (! juce::isPositiveAndBelow (idx, sampleRates.size())) return;
            auto setup = dm.getAudioDeviceSetup();
            setup.sampleRate = sampleRates[idx];
            applySetup (setup);
        };
        makeClickable (rateBox);
        content.addAndMakeVisible (rateBox);

        bufferBox.setTitle ("Audio buffer size");
        bufferBox.onChange = [this]
        {
            if (rebuilding) return;
            const int idx = bufferBox.getSelectedId() - 1;
            if (! juce::isPositiveAndBelow (idx, bufferSizes.size())) return;
            auto setup = dm.getAudioDeviceSetup();
            setup.bufferSize = bufferSizes[idx];
            applySetup (setup);
        };
        makeClickable (bufferBox);
        content.addAndMakeVisible (bufferBox);

        testButton.setTooltip ("Play a short test tone on the active output.");
        testButton.onClick = [this] { dm.playTestSound(); };
        makeClickable (testButton);
        content.addAndMakeVisible (testButton);

        controlPanelButton.setTooltip ("Open this driver's control panel (e.g. ASIO buffer / clock settings).");
        controlPanelButton.onClick = [this]
        {
            if (auto* dev = dm.getCurrentAudioDevice())
                dev->showControlPanel();
        };
        makeClickable (controlPanelButton);
        content.addChildComponent (controlPanelButton);

        dm.addChangeListener (this);
        rebuildFromDevice();
    }

    ~AudioSettingsPanel() override
    {
        dm.removeChangeListener (this);
    }

    void paint (juce::Graphics& g) override { g.fillAll (Theme::p().window); }

    void resized() override
    {
        viewport.setBounds (getLocalBounds());
        const int w = juce::jmax (500, viewport.getMaximumVisibleWidth());
        content.setSize (w, totalContentHeight());
        layoutContent();
    }

private:
    struct Content : public juce::Component
    {
        AudioSettingsPanel& owner;
        explicit Content (AudioSettingsPanel& o) : owner (o) {}
        void paint (juce::Graphics& g) override { owner.paintContent (g); }
        void resized() override { owner.layoutContent(); }
    };

    void changeListenerCallback (juce::ChangeBroadcaster*) override { rebuildFromDevice(); }

    void applySetup (juce::AudioDeviceManager::AudioDeviceSetup setup)
    {
        lastError = dm.setAudioDeviceSetup (setup, true);
        rebuildFromDevice();
    }

    void rebuildFromDevice()
    {
        const juce::ScopedValueSetter<bool> guard (rebuilding, true);

        // Driver selector buttons
        driverButtons.clear();
        const auto currentType = dm.getCurrentAudioDeviceType();
        for (auto* t : dm.getAvailableDeviceTypes())
        {
            auto btn = std::make_unique<juce::TextButton> (t->getTypeName());
            makeLamp (*btn, "key");
            btn->setToggleState (t->getTypeName() == currentType, juce::dontSendNotification);
            const auto typeName = t->getTypeName();
            btn->onClick = [this, typeName]
            {
                lastError.clear();
                dm.setCurrentAudioDeviceType (typeName, true);
                if (auto* dt = dm.getCurrentDeviceTypeObject())
                    dt->scanForDevices();
                rebuildFromDevice();
            };
            content.addAndMakeVisible (*btn);
            driverButtons.push_back (std::move (btn));
        }

        auto* type = dm.getCurrentDeviceTypeObject();
        const auto setup = dm.getAudioDeviceSetup();
        hasSeparateInput = (type != nullptr && type->hasSeparateInputsAndOutputs());

        outputNames.clear();
        outputBox.clear (juce::dontSendNotification);
        outputBox.addItem ("<none>", 1);
        if (type != nullptr)
        {
            outputNames = type->getDeviceNames (false);
            for (int i = 0; i < outputNames.size(); ++i)
                outputBox.addItem (outputNames[i], i + 2);
        }
        const int outIdx = setup.outputDeviceName.isEmpty() ? -1 : outputNames.indexOf (setup.outputDeviceName);
        outputBox.setSelectedId (outIdx >= 0 ? outIdx + 2 : 1, juce::dontSendNotification);

        inputNames.clear();
        inputBox.clear (juce::dontSendNotification);
        inputBox.setVisible (hasSeparateInput);
        if (hasSeparateInput && type != nullptr)
        {
            inputBox.addItem ("<none> (recommended for strip mics)", 1);
            inputNames = type->getDeviceNames (true);
            for (int i = 0; i < inputNames.size(); ++i)
                inputBox.addItem (inputNames[i], i + 2);
            const int inIdx = setup.inputDeviceName.isEmpty() ? -1 : inputNames.indexOf (setup.inputDeviceName);
            inputBox.setSelectedId (inIdx >= 0 ? inIdx + 2 : 1, juce::dontSendNotification);
        }

        auto* dev = dm.getCurrentAudioDevice();
        sampleRates.clear();
        rateBox.clear (juce::dontSendNotification);
        bufferSizes.clear();
        bufferBox.clear (juce::dontSendNotification);
        outputChannelButtons.clear();
        inputChannelButtons.clear();

        if (dev != nullptr)
        {
            sampleRates = dev->getAvailableSampleRates();
            const double curSr = dev->getCurrentSampleRate();
            int srSel = 1;
            for (int i = 0; i < sampleRates.size(); ++i)
            {
                const double r = sampleRates[i];
                rateBox.addItem (juce::String (r / 1000.0, std::fmod (r, 1000.0) == 0.0 ? 0 : 1) + " kHz", i + 1);
                if (std::abs (r - curSr) < 1.0)
                    srSel = i + 1;
            }
            if (! sampleRates.isEmpty())
                rateBox.setSelectedId (srSel, juce::dontSendNotification);
            rateBox.setEnabled (! sampleRates.isEmpty());

            bufferSizes = dev->getAvailableBufferSizes();
            const int curBuf = dev->getCurrentBufferSizeSamples();
            int bufSel = 1;
            for (int i = 0; i < bufferSizes.size(); ++i)
            {
                const int b = bufferSizes[i];
                const double ms = curSr > 0.0 ? (1000.0 * b / curSr) : 0.0;
                bufferBox.addItem (juce::String (b) + " smp (" + juce::String (ms, 1) + " ms)", i + 1);
                if (b == curBuf)
                    bufSel = i + 1;
            }
            if (! bufferSizes.isEmpty())
                bufferBox.setSelectedId (bufSel, juce::dontSendNotification);
            bufferBox.setEnabled (! bufferSizes.isEmpty());

            const double sr = curSr > 0.0 ? curSr : 48000.0;
            const double inMs = 1000.0 * dev->getInputLatencyInSamples() / sr;
            const double outMs = 1000.0 * dev->getOutputLatencyInSamples() / sr;
            statusSummary = "Active: " + juce::String (dev->getCurrentBitDepth()) + "-bit  |  In "
                          + juce::String (inMs, 1) + " ms / Out " + juce::String (outMs, 1) + " ms";

            // Stereo pair channel keys (up to 16 outputs / 16 inputs)
            const auto outNames = dev->getOutputChannelNames();
            const auto activeOut = dev->getActiveOutputChannels();
            for (int ch = 0; ch < juce::jmin (16, outNames.size()); ch += 2)
            {
                const bool hasPair = (ch + 1) < outNames.size();
                const juce::String lbl = hasPair
                    ? ("Out " + juce::String (ch + 1) + "-" + juce::String (ch + 2) + ": " + outNames[ch])
                    : ("Out " + juce::String (ch + 1) + ": " + outNames[ch]);
                auto btn = std::make_unique<juce::TextButton> (lbl);
                makeLamp (*btn, "accent");
                btn->setClickingTogglesState (false);
                const bool on = activeOut[ch] || (hasPair && activeOut[ch + 1]);
                btn->setToggleState (on, juce::dontSendNotification);
                btn->onClick = [this, ch, hasPair, on]
                {
                    auto s = dm.getAudioDeviceSetup();
                    if (auto* d = dm.getCurrentAudioDevice())
                        if (s.useDefaultOutputChannels)
                            s.outputChannels = d->getActiveOutputChannels();
                    s.useDefaultOutputChannels = false;
                    s.outputChannels.setBit (ch, ! on);
                    if (hasPair)
                        s.outputChannels.setBit (ch + 1, ! on);
                    applySetup (s);
                };
                content.addAndMakeVisible (*btn);
                outputChannelButtons.push_back (std::move (btn));
            }

            const auto inNames = dev->getInputChannelNames();
            const auto activeIn = dev->getActiveInputChannels();
            for (int ch = 0; ch < juce::jmin (16, inNames.size()); ch += 2)
            {
                const bool hasPair = (ch + 1) < inNames.size();
                const juce::String lbl = hasPair
                    ? ("In " + juce::String (ch + 1) + "-" + juce::String (ch + 2) + ": " + inNames[ch])
                    : ("In " + juce::String (ch + 1) + ": " + inNames[ch]);
                auto btn = std::make_unique<juce::TextButton> (lbl);
                makeLamp (*btn, "accent");
                btn->setClickingTogglesState (false);
                const bool on = activeIn[ch] || (hasPair && activeIn[ch + 1]);
                btn->setToggleState (on, juce::dontSendNotification);
                btn->onClick = [this, ch, hasPair, on]
                {
                    auto s = dm.getAudioDeviceSetup();
                    if (auto* d = dm.getCurrentAudioDevice())
                        if (s.useDefaultInputChannels)
                            s.inputChannels = d->getActiveInputChannels();
                    s.useDefaultInputChannels = false;
                    s.inputChannels.setBit (ch, ! on);
                    if (hasPair)
                        s.inputChannels.setBit (ch + 1, ! on);
                    applySetup (s);
                };
                content.addAndMakeVisible (*btn);
                inputChannelButtons.push_back (std::move (btn));
            }

            testButton.setEnabled (true);
            controlPanelButton.setVisible (dev->hasControlPanel());
            controlPanelButton.setEnabled (dev->hasControlPanel());
        }
        else
        {
            rateBox.setEnabled (false);
            bufferBox.setEnabled (false);
            testButton.setEnabled (false);
            controlPanelButton.setVisible (false);
            statusSummary = "No audio device is currently active.";
        }

        if (getWidth() > 0 && getHeight() > 0)
            resized();
        content.repaint();
    }

    int totalContentHeight() const
    {
        int h = Theme::s3 * 2 + 52 + 10;   // outer pad + tip card
        if (lastError.isNotEmpty())
            h += 44 + 10;
        h += 78 + 10;                      // Driver card
        const int devRows = hasSeparateInput ? 5 : 4;
        h += (24 + 18 + devRows * 34 + 34) + 10;   // Device & format card
        if (! outputChannelButtons.empty() || ! inputChannelButtons.empty())
        {
            const int outRows = ((int) outputChannelButtons.size() + 1) / 2;
            const int inRows  = ((int) inputChannelButtons.size() + 1) / 2;
            h += 24 + 18
               + (outRows > 0 ? 18 + outRows * 30 : 0)
               + (inRows  > 0 ? 8 + 18 + inRows * 30 : 0);
        }
        return juce::jmax (440, h);
    }

    void paintContent (juce::Graphics& g)
    {
        const auto& t = Theme::p();
        g.fillAll (t.window);

        // Tip card
        g.setColour (t.selBg);
        g.fillRoundedRectangle (tipCard.toFloat(), Theme::panelRadius);
        g.setColour (t.line);
        g.drawRoundedRectangle (tipCard.toFloat().reduced (0.5f), Theme::panelRadius, 1.0f);
        g.setColour (t.accent);
        g.fillRect (tipCard.getX() + 1, tipCard.getY() + 6, 3, tipCard.getHeight() - 12);
        g.setFont (Theme::ui (12.5f));
        g.setColour (t.textDim);
        g.drawFittedText ("Select your ASIO or Windows output device below. Pick mics per strip under "
                          "Input -> Windows input device so each audio device keeps its own clock.",
                          tipCard.reduced (14, 8), juce::Justification::centredLeft, 3);

        if (! errorCard.isEmpty())
        {
            g.setColour (t.selBg);
            g.fillRoundedRectangle (errorCard.toFloat(), Theme::panelRadius);
            g.setColour (t.mute);
            g.drawRoundedRectangle (errorCard.toFloat().reduced (0.5f), Theme::panelRadius, 1.0f);
            g.fillRect (errorCard.getX() + 1, errorCard.getY() + 6, 3, errorCard.getHeight() - 12);
            g.setFont (Theme::ui (12.5f));
            g.setColour (t.text);
            g.drawFittedText (lastError, errorCard.reduced (14, 6), juce::Justification::centredLeft, 2);
        }

        Theme::drawFaceplate (g, driverCard.toFloat());
        Theme::drawLegend (g, "Driver", driverLegend);

        Theme::drawFaceplate (g, deviceCard.toFloat());
        Theme::drawLegend (g, "Device and format", deviceLegend);
        g.setFont (Theme::ui (13.0f));
        g.setColour (t.textDim);
        g.drawText ("Output",      outputLabelArea, juce::Justification::centredLeft);
        if (hasSeparateInput)
            g.drawText ("Input",   inputLabelArea,  juce::Justification::centredLeft);
        g.drawText ("Sample rate", rateLabelArea,   juce::Justification::centredLeft);
        g.drawText ("Buffer size", bufferLabelArea, juce::Justification::centredLeft);

        g.setFont (Theme::mono (11.5f));
        g.setColour (t.textFaint);
        g.drawText (statusSummary, statusArea, juce::Justification::centredRight, true);

        if (! channelsCard.isEmpty())
        {
            Theme::drawFaceplate (g, channelsCard.toFloat());
            Theme::drawLegend (g, "Active channels", channelsLegend);
            g.setFont (Theme::ui (12.0f));
            g.setColour (t.textDim);
            if (! outChLabelArea.isEmpty())
                g.drawText ("Output channels", outChLabelArea, juce::Justification::centredLeft);
            if (! inChLabelArea.isEmpty())
                g.drawText ("Input channels",  inChLabelArea,  juce::Justification::centredLeft);
        }
    }

    void layoutContent()
    {
        auto r = content.getLocalBounds().reduced (Theme::s3);
        constexpr int pad = Theme::s3;

        tipCard = r.removeFromTop (52);
        r.removeFromTop (10);

        if (lastError.isNotEmpty())
        {
            errorCard = r.removeFromTop (44);
            r.removeFromTop (10);
        }
        else
        {
            errorCard = {};
        }

        driverCard = r.removeFromTop (78);
        {
            auto c = driverCard.reduced (pad);
            auto head = c.removeFromTop (22);
            rescanButton.setBounds (head.removeFromRight (84));
            driverLegend = head;
            c.removeFromTop (4);
            const int n = (int) driverButtons.size();
            if (n > 0)
            {
                const int bw = (c.getWidth() - (n - 1) * Theme::s1) / n;
                for (auto& b : driverButtons)
                {
                    b->setBounds (c.removeFromLeft (bw));
                    c.removeFromLeft (Theme::s1);
                }
            }
        }
        r.removeFromTop (10);

        const int devRows = hasSeparateInput ? 5 : 4;
        deviceCard = r.removeFromTop (24 + 18 + devRows * 34 + 34);
        {
            auto c = deviceCard.reduced (pad);
            deviceLegend = c.removeFromTop (18);
            c.removeFromTop (6);
            constexpr int labelW = 104;

            auto row = c.removeFromTop (28);
            outputLabelArea = row.removeFromLeft (labelW);
            outputBox.setBounds (row);
            c.removeFromTop (6);

            if (hasSeparateInput)
            {
                row = c.removeFromTop (28);
                inputLabelArea = row.removeFromLeft (labelW);
                inputBox.setBounds (row);
                c.removeFromTop (6);
            }
            else
            {
                inputLabelArea = {};
            }

            row = c.removeFromTop (28);
            rateLabelArea = row.removeFromLeft (labelW);
            rateBox.setBounds (row);
            c.removeFromTop (6);

            row = c.removeFromTop (28);
            bufferLabelArea = row.removeFromLeft (labelW);
            bufferBox.setBounds (row);
            c.removeFromTop (10);

            auto footer = c.removeFromTop (28);
            testButton.setBounds (footer.removeFromLeft (92));
            if (controlPanelButton.isVisible())
            {
                footer.removeFromLeft (Theme::s2);
                controlPanelButton.setBounds (footer.removeFromLeft (120));
            }
            footer.removeFromLeft (Theme::s2);
            statusArea = footer;
        }
        r.removeFromTop (10);

        if (! outputChannelButtons.empty() || ! inputChannelButtons.empty())
        {
            channelsCard = r;
            auto c = channelsCard.reduced (pad);
            channelsLegend = c.removeFromTop (18);
            c.removeFromTop (4);

            auto placeGrid = [] (juce::Rectangle<int>& area, std::vector<std::unique_ptr<juce::TextButton>>& btns)
            {
                const int rows = ((int) btns.size() + 1) / 2;
                const int colW = (area.getWidth() - Theme::s1) / 2;
                for (int idx = 0; idx < (int) btns.size(); ++idx)
                {
                    const int col = idx % 2, rowIdx = idx / 2;
                    btns[(size_t) idx]->setBounds (area.getX() + col * (colW + Theme::s1),
                                                   area.getY() + rowIdx * 30, colW, 26);
                }
                area.removeFromTop (rows * 30);
            };

            if (! outputChannelButtons.empty())
            {
                outChLabelArea = c.removeFromTop (18);
                placeGrid (c, outputChannelButtons);
            }
            else
            {
                outChLabelArea = {};
            }

            if (! inputChannelButtons.empty())
            {
                if (! outputChannelButtons.empty())
                    c.removeFromTop (8);
                inChLabelArea = c.removeFromTop (18);
                placeGrid (c, inputChannelButtons);
            }
            else
            {
                inChLabelArea = {};
            }
        }
        else
        {
            channelsCard = {};
            outChLabelArea = {};
            inChLabelArea = {};
        }
    }

    juce::AudioDeviceManager& dm;
    juce::Viewport viewport;
    Content content { *this };

    std::vector<std::unique_ptr<juce::TextButton>> driverButtons, outputChannelButtons, inputChannelButtons;
    juce::TextButton rescanButton { "Rescan" }, testButton { "Test tone" }, controlPanelButton { "Control panel..." };
    juce::ComboBox outputBox, inputBox, rateBox, bufferBox;
    juce::StringArray outputNames, inputNames;
    juce::Array<double> sampleRates;
    juce::Array<int> bufferSizes;
    juce::String lastError, statusSummary;
    bool hasSeparateInput = false, rebuilding = false;

    juce::Rectangle<int> tipCard, errorCard, driverCard, driverLegend, deviceCard, deviceLegend,
                         outputLabelArea, inputLabelArea, rateLabelArea, bufferLabelArea, statusArea,
                         channelsCard, channelsLegend, outChLabelArea, inChLabelArea;
};
}

void MainComponent::showAudioSettings()
{
    const auto display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
    const int maxH = display != nullptr ? juce::jmax (460, (int) display->userBounds.getHeight() - 60) : 640;
    auto* panel = new AudioSettingsPanel (engine);
    panel->setSize (580, juce::jmin (620, maxH));

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (panel);
    o.dialogTitle = "Audio settings";
    o.dialogBackgroundColour = Theme::p().window;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = false;
    o.resizable = true;
    o.componentToCentreAround = this;
    o.launchAsync();
}

void MainComponent::showSettings()
{
    auto* panel = new SettingsPanel (engine, [this] { toggleTheme(); }, [this] { applyRemote(); }, [this] { showHotkeys(); }, &midiControl,
                                     // Accent or compact changed: repaint everything and re-run the layout.
                                     [this] { applyTheme(); resized(); });
    // Fits inside the main window (it scrolls only when the window is smaller than the panel).
    const auto inside = getScreenBounds().reduced (24, 40);
    panel->setSize (juce::jmin (SettingsPanel::preferredWidth, inside.getWidth()),
                    juce::jmin (panel->getHeight(), inside.getHeight() - 30));

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (panel);
    o.dialogTitle = "Settings";
    o.dialogBackgroundColour = Theme::p().window;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = false;
    o.resizable = true;
    o.componentToCentreAround = this;
    o.launchAsync();
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &engine.getDeviceManager())
    {
        // The WAV headers carry the old rate: carrying on would play back at the wrong speed.
        auto& rec = engine.getRecorder();
        if (rec.isRecording() && std::abs (rec.getSampleRate() - engine.getSampleRate()) > 0.5)
        {
            toggleRecording();
            showNote ("Recording stopped: the sample rate changed. Saved to " + rec.getLastFolder().getFullPathName());
        }

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

        // The header readout is the audio device's own numbers; CPU lives in the bottom bar
        // (the engine's measurement), so the two never show different CPU figures.
        statusSegments.add (device->getTypeName() + "  " + device->getName());
        statusSegments.add (juce::String (sr / 1000.0, 1) + " kHz / " + juce::String (bs) + " smp");
        statusSegments.add (juce::String (latencyMs, 1) + " ms");
        statusSegments.add (juce::String (dropouts) + (dropouts == 1 ? " dropout" : " dropouts"));
    }
    else
    {
        statusSegments.add ("No audio device. Open Audio settings to choose one.");
    }

    if (note.isNotEmpty() && juce::Time::getMillisecondCounter() < noteUntilMs)
        statusSegments.insert (0, note);

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

    // Bottom status bar: LIVE, the engine's CPU, background work, and which remote-control
    // services are listening (the device itself is in the header readout).
    juce::StringArray bottom;
    const bool live = dm.getCurrentAudioDevice() != nullptr;
    statusBar.setLive (live);
    bottom.add (live ? "Engine CPU " + juce::String (juce::roundToInt (engine.getCpuUsagePercent())) + "%"
                     : juce::String ("No audio device. Click the readout in the header to choose one."));

    auto& scanner = engine.getScanner();
    if (scanner.isScanning() && scanner.getNumToScan() > 0)
        bottom.add ("Scanning plugins " + juce::String (scanner.getNumDone()) + " of " + juce::String (scanner.getNumToScan()));
    if (engine.getRecorder().isRecording())
        bottom.add ("Recording " + juce::String ((int) engine.getRecorder().getElapsedSeconds() / 60) + ":"
                    + juce::String ((int) engine.getRecorder().getElapsedSeconds() % 60).paddedLeft ('0', 2));
    statusBar.setSegments (bottom);

    auto& settings = AppSettings::get();
    std::vector<StatusBar::Service> services;
    if (settings.oscEnabled())
        services.push_back ({ "OSC", osc.isRunning() ? "UDP " + juce::String (osc.getPort()) : juce::String ("port in use"), osc.isRunning() });
    if (settings.wsEnabled())
        services.push_back ({ "Phone", remoteServer.isRunning() ? "TCP " + juce::String (remoteServer.getPort()) : juce::String ("port in use"),
                              remoteServer.isRunning() });
    if (settings.midiEnabled())
        services.push_back ({ "MIDI", midiControl.isRunning() ? midiControl.getDeviceName() : juce::String ("not connected"),
                              midiControl.isRunning() });
    statusBar.setServices (std::move (services));
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
        console->media.refresh();
        updateSceneButton();
    }

    // A mic or device input that gives digital silence for 10 s is unplugged, muted at the
    // device or blocked by Windows privacy. Quiet rooms still have a noise floor above this.
    for (int s = 0; s < kNumStrips; ++s)
    {
        auto& strip = engine.getStrip (s);
        const int src = strip.sourceType.load();
        const float peak = strip.inputPeak.exchange (-1.0f);
        auto& silent = silentTicks[(size_t) s];
        const bool hardware = src == (int) SourceType::Main || src == (int) SourceType::Device;
        if (! strip.active.load() || strip.mute.load() || ! hardware || peak < 0.0f || peak > 1.0e-5f)
        {
            silent = 0;
            continue;
        }
        if (++silent == 300)
            showNote ((strip.name.isNotEmpty() ? strip.name : "Input " + juce::String (s + 1))
                      + " has had no signal for 10 seconds. Check the cable, the device's mute switch"
                        " or Windows microphone privacy.");
    }

    // Undo history: a new step whenever the mix changed, but not mid-drag.
    if (ticks % 10 == 0 && ! juce::ModifierKeys::getCurrentModifiersRealtime().isAnyMouseButtonDown())
        history.offer (captureMixText());

    // Push the new state to phone / WebSocket clients.
    remoteServer.broadcastStateIfChanged();

    if (ticks % 60 == 0)
    {
        midiControl.maintain();   // reopens the MIDI controller after it was unplugged
        engine.maintainSources();

        if (const int mode = AppSettings::get().autoRestart(); mode > 0)
        {
            const auto setup = engine.getDeviceManager().getAudioDeviceSetup();
            auto* device = engine.getDeviceManager().getCurrentAudioDevice();
            bool restart = (setup.outputDeviceName.isNotEmpty() || setup.inputDeviceName.isNotEmpty())
                        && (device == nullptr || ! device->isPlaying());
            if (mode == 2)
            {
                const auto list = engine.getWindowsInputNames().joinIntoString ("|") + "#"
                                + engine.getWindowsOutputNames().joinIntoString ("|");
                restart = restart || (lastDeviceList.isNotEmpty() && list != lastDeviceList);
                lastDeviceList = list;
            }
            if (restart)
                restartAudioEngine (false);
        }
        for (auto& s : console->strips) s->refreshDevice();
        for (auto& b : console->buses)  b->refreshDevice();
    }

    if (ticks % 300 == 0)
        Session::save (engine, true);   // plugin state refreshed at most once a minute here
}

//==============================================================================

void MainComponent::paint (juce::Graphics& g)
{
    const auto& t = Theme::p();
    g.fillAll (t.window);

    // Header rail with subtle vertical surface gradient (mockup menu-bar style)
    const auto headerTop = t.dark ? juce::Colour (0xff303035) : juce::Colour (0xffe6e6e8);
    g.setGradientFill (juce::ColourGradient::vertical (headerTop, (float) headerArea.getY(), t.panel, (float) headerArea.getBottom()));
    g.fillRect (headerArea);
    g.setColour (t.line);
    g.fillRect (headerArea.withTop (headerArea.getBottom() - 1));

    // App icon + name
    auto brand = headerArea.reduced (consolePad(), 0).removeFromLeft (110);
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

bool MainComponent::keyStateChanged (bool isKeyDown)
{
    if (! isKeyDown)
        console->soundboard.releaseKeys();   // Hold pads stop when their key is let go
    return false;
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    // Shortcuts while OpenMix is focused. If a global hotkey just fired for this key press,
    // it has already been handled: don't act twice.
    if (juce::Time::getMillisecondCounter() - hotkeys.getLastFireMs() < 250)
        return true;

    const auto mods = key.getModifiers();
    const int code = juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) key.getKeyCode());
    if (mods.isCtrlDown() && ! mods.isAltDown() && (code == 'Z' || code == 'Y'))
    {
        undoMix (code == 'Y' || mods.isShiftDown());   // Ctrl+Y or Ctrl+Shift+Z redo
        return true;
    }
    if (mods.isCtrlDown() && ! mods.isShiftDown() && ! mods.isAltDown() && code == 'R')
    {
        restartAudioEngine (true);
        return true;
    }
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

    // 1-8 play the soundboard page's pads while it's shown (text fields keep their digits).
    if (console->soundboard.isVisible() && console->soundboard.pressKey (key))
        return true;

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
        ? "Save the last " + juce::String ((int) replay.getSeconds()) + " seconds of " + engine.getBusName (replay.sourceBus.load()) + " (Ctrl+Shift+S)."
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
        for (int b : engine.getBusDisplayOrder())
            if (engine.getBus (b).active.load())
            {
                const auto custom = engine.getBus (b).customName;
                const auto label = juce::String ("Bus ") + kBusNames[b] + (custom.isNotEmpty() ? " " + custom : juce::String());
                targets.push_back ({ label, true, b });
            }

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
    for (int b : engine.getBusDisplayOrder())
        if (engine.getBus (b).active.load())
            source.addItem (engine.getBusName (b), true, settings.replayBus() == b, [this, b]
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

void MainComponent::setMediaShown (bool shown)
{
    AppSettings::get().setMediaShown (shown);
    mediaButton.setToggleState (shown, juce::dontSendNotification);
    console->media.setVisible (shown);
    if (shown)
        console->media.refresh();
    console->relayout();
}

void MainComponent::applyOsc()
{
    applyRemote();
}

void MainComponent::applyRemote()
{
    // Each start() is a no-op when that service already runs with the same setting, so
    // changing one setting doesn't drop the others (phones stay connected, for example).
    auto& settings = AppSettings::get();

    if (! settings.oscEnabled())
    {
        osc.stop();
    }
    else
    {
        juce::String error;
        if (! osc.start (settings.oscPort(), error))
            showNote ("OSC remote control is off: " + error);
    }

    if (! settings.wsEnabled())
    {
        remoteServer.stop();
    }
    else
    {
        juce::String error;
        if (! remoteServer.start (settings.wsPort(), error))
            showNote ("Phone / WebSocket remote is off: " + error);
    }

    if (! settings.midiEnabled() || settings.midiDevice().isEmpty())
    {
        midiControl.stop();
    }
    else
    {
        juce::String error;
        if (! midiControl.start (settings.midiDevice(), error))
            showNote ("MIDI remote is off: " + error);
    }
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
            .withMessage ("The mix as it is now doesn't change. Hotkeys and MIDI buttons for later scenes move up with them.")
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

            // MIDI scene buttons too ("scene/N" is 1-based).
            std::vector<MidiControl::Mapping> midiKept;
            for (auto m : safe->midiControl.getMappings())
            {
                if (m.target.startsWith ("scene/"))
                {
                    const int scene = m.target.fromFirstOccurrenceOf ("/", false, false).getIntValue() - 1;
                    if (scene == index) continue;
                    if (scene > index)  m.target = "scene/" + juce::String (scene);
                }
                midiKept.push_back (m);
            }
            safe->midiControl.setMappings (midiKept);
            AppSettings::get().setMidiMappings (MidiControl::mappingsToJson (midiKept));

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
            else      engine.getSoundboard().release (b.index);   // ends a Hold pad
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

// The header doubles as the title bar: drag it to move the window, double-click to maximise.
void MainComponent::mouseDown (const juce::MouseEvent& e)
{
    draggingWindow = headerArea.contains (e.getPosition()) && window() != nullptr && restoreBounds.isEmpty();
    if (draggingWindow)
        windowDragger.startDraggingComponent (window(), e.getEventRelativeTo (window()));
}

void MainComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (draggingWindow)
        windowDragger.dragComponent (window(), e.getEventRelativeTo (window()), nullptr);
}

void MainComponent::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (headerArea.contains (e.getPosition()))
        toggleMaximised();
}

void MainComponent::toggleMaximised()
{
    if (auto* w = window())
    {
        // Maximise to the screen's work area, so the Windows taskbar stays visible
        // (JUCE's full-screen would cover it, and the status bar under it).
        if (! restoreBounds.isEmpty())
        {
            w->setBounds (restoreBounds);
            restoreBounds = {};
        }
        else if (const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect (w->getScreenBounds()))
        {
            restoreBounds = w->getBounds();
            w->setBounds (display->userBounds.toNearestInt());
        }
        const bool maximised = ! restoreBounds.isEmpty();
        setRoundCorners (*w, ! maximised);
        maximiseDot.setMaximised (maximised);
        maximiseDot.setTooltip (maximised ? "Restore" : "Maximise");
    }
}

//==============================================================================
// WindowDot

WindowDot::WindowDot (Kind k) : juce::Button ({}), kind (k)
{
    setTitle (k == close ? "Close" : k == minimise ? "Minimise" : "Maximise");
    setTooltip (k == close ? "Close (OpenMix keeps running in the tray if that's switched on in Settings)"
                           : k == minimise ? "Minimise" : "Maximise or restore");
    makeClickable (*this);
    setWantsKeyboardFocus (true);
}

void WindowDot::paintButton (juce::Graphics& g, bool isHighlighted, bool isDown)
{
    OpenMixLookAndFeel::drawWindowButton (g, getLocalBounds().toFloat(), kind == close ? 2 : kind == minimise ? 0 : 1,
                                          maximised, isHighlighted, isDown, hasKeyboardFocus (false));
}

juce::String MainComponent::captureMixText (const juce::Array<int>& fullStrips)
{
    // A scene plus each strip's name plate, so undoing a paste or a rename restores it too
    // (scenes themselves leave names alone).
    auto xml = Session::captureScene (engine);
    for (auto* x : xml->getChildWithTagNameIterator ("Strip"))
    {
        const auto& strip = engine.getStrip (x->getIntAttribute ("index"));
        x->setAttribute ("name", strip.name);
        x->setAttribute ("colour", strip.colour);
        x->setAttribute ("icon", strip.icon);
    }
    auto text = xml->toString (juce::XmlElement::TextFormat().singleLine().withoutHeader());
    if (! fullStrips.isEmpty())
    {
        juce::XmlElement copies ("Strips");
        for (int s : fullStrips)
            if (auto c = Session::copyStrip (engine, s))
            {
                c->setAttribute ("index", s);
                copies.addChildElement (c.release());
            }
        text << MixHistory::fullMarker << copies.toString (juce::XmlElement::TextFormat().singleLine().withoutHeader());
    }
    return text;
}

// Each slot's plugin and state, to tell whether undo has to reload a strip's plugins.
static juce::String chainKey (const juce::XmlElement& stripCopy)
{
    juce::String key;
    for (auto* xp : stripCopy.getChildWithTagNameIterator ("Slot"))
    {
        juce::PluginDescription d;
        if (auto* pd = xp->getChildElement (0); pd != nullptr && d.loadFromXml (*pd))
            key << xp->getIntAttribute ("index") << ':' << d.createIdentifierString() << ':' << xp->getStringAttribute ("state") << ';';
    }
    return key;
}

static juce::String chainKey (PluginChain& chain)
{
    juce::String key;
    for (int s = 0; s < kNumSlots; ++s)
        if (auto* p = chain.getPlugin (s))
        {
            juce::MemoryBlock state;
            p->getStateInformation (state);
            key << s << ':' << p->getPluginDescription().createIdentifierString() << ':' << state.toBase64Encoding() << ';';
        }
    return key;
}

void MainComponent::undoMix (bool redo)
{
    history.offer (captureMixText());   // a change from the last few hundred ms is a step too
    const auto state = redo ? history.redo() : history.undo();
    auto xml = juce::parseXML (MixHistory::mixPart (state));
    if (xml == nullptr)
    {
        showNote (redo ? "Nothing to redo" : "Nothing to undo");
        return;
    }
    // A paste step: put back each strip as it was then, plugins and their state included.
    if (auto copies = juce::parseXML (MixHistory::fullPart (state)))
    {
        for (auto* c : copies->getChildIterator())
        {
            const int s = c->getIntAttribute ("index", -1);
            if (! juce::isPositiveAndBelow (s, kNumStrips))
                continue;
            if (chainKey (*c) == chainKey (engine.getStrip (s).inserts))
                continue;   // same plugins in the same state: nothing to reload
            console->strips[(size_t) s]->closePluginEditors();
            Session::pasteStrip (*c, engine, s);
        }
        Session::save (engine);
    }
    Session::applyScene (*xml, engine);
    for (auto* x : xml->getChildWithTagNameIterator ("Strip"))
    {
        auto& strip = engine.getStrip (juce::jlimit (0, kNumStrips - 1, x->getIntAttribute ("index")));
        strip.name = x->getStringAttribute ("name", strip.name);
        strip.colour = x->getIntAttribute ("colour", strip.colour);
        strip.icon = juce::jlimit (0, StripIcons::count - 1, x->getIntAttribute ("icon", strip.icon));
    }
    for (auto& s : console->strips) if (s->isVisible()) s->refreshFromStrip();
    history.settle (captureMixText());
    showNote (redo ? "Redo" : "Undo");
}

void MainComponent::showRouting()
{
    auto* panel = new RoutingPanel (engine);
    const auto inside = getScreenBounds().reduced (24, 40);
    panel->setSize (juce::jmin (juce::jmax (420, panel->preferredWidth()), inside.getWidth()),
                    juce::jmin (panel->preferredHeight(), inside.getHeight() - 30));

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (panel);
    o.dialogTitle = "Routing matrix";
    o.dialogBackgroundColour = Theme::p().window;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = false;
    o.resizable = true;
    o.componentToCentreAround = this;
    o.launchAsync();
}

void MainComponent::showMixerMenu()
{
    juce::PopupMenu menu;
    menu.addItem ("Add strip", engine.getNumActiveStrips() < kNumStrips, false, [this] { console->addStrip(); });
    menu.addItem ("Add bus...", engine.getNumActiveBuses() < kNumBuses, false, [this] { console->addBus(); });
    menu.addItem ("Routing matrix...", [this] { showRouting(); });
    menu.addItem ("Solo before the fader (PFL)", true, engine.soloPreFader.load(), [this]
    {
        engine.soloPreFader.store (! engine.soloPreFader.load());
        showNote (engine.soloPreFader.load() ? "Solo: pre-fader listen (PFL)" : "Solo: after-fader listen (AFL)");
        Session::save (engine);
    });
    menu.addSeparator();
    history.offer (captureMixText());   // so a change made just now can be undone from the menu
    menu.addItem ("Undo (Ctrl+Z)", history.canUndo(), false, [this] { undoMix (false); });
    menu.addItem ("Redo (Ctrl+Y)", history.canRedo(), false, [this] { undoMix (true); });
    menu.addSeparator();
    menu.addItem ("Save mix as a new scene...", [this] { saveScene (-1); });
    menu.addItem ("Scenes...", [this] { showSceneMenu(); });
    menu.addItem ("Hotkeys...", [this] { showHotkeys(); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (mixerMenuButton));
}

void MainComponent::showViewMenu()
{
    juce::PopupMenu menu;
    menu.addItem ("Soundboard", true, console->soundboard.isVisible(), [this] { setSoundboardShown (! console->soundboard.isVisible()); });
    menu.addItem ("Media player", true, console->media.isVisible(), [this] { setMediaShown (! console->media.isVisible()); });
    menu.addItem ("Compact desk", true, Theme::isCompact(), [this]
    {
        AppSettings::get().setCompactMode (! Theme::isCompact());
        Theme::setCompact (! Theme::isCompact());
        applyTheme();
        resized();
    });
    menu.addItem ("Dark theme", true, Theme::isDark(), [this] { toggleTheme(); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (viewMenuButton));
}

void MainComponent::showSetupMenu()
{
    auto& settings = AppSettings::get();
    juce::PopupMenu menu;
    menu.addItem ("Audio device...", [this] { showAudioSettings(); });
    menu.addItem ("Restart audio engine\tCtrl+R", [this] { restartAudioEngine (true); });
    juce::PopupMenu autoRestart;
    const char* const modes[] { "Off", "When the main device stops", "When any device changes" };
    for (int m = 0; m < 3; ++m)
        autoRestart.addItem (modes[m], true, settings.autoRestart() == m, [m] { AppSettings::get().setAutoRestart (m); });
    menu.addSubMenu ("Auto restart audio engine", autoRestart);
    menu.addItem ("Release media file", engine.getMediaPlayer().getCurrentIndex() >= 0, false, [this]
    {
        engine.getMediaPlayer().eject();
        console->media.refresh();
        showNote ("Media file released");
    });
    menu.addSeparator();
    menu.addItem ("Load settings...", [this] { loadSettings(); });
    menu.addItem ("Save settings...", [this] { saveSettings(); });
    juce::PopupMenu onStartup;
    const auto startup = settings.startupSessionFile();
    onStartup.addItem ("Off (start with the last session)", true, startup == juce::File(), []
    {
        AppSettings::get().setStartupSessionFile ({});
    });
    if (startup != juce::File())
        onStartup.addItem (startup.getFileName(), false, true, nullptr);
    onStartup.addItem ("Choose file...", [this] { chooseStartupSettings(); });
    menu.addSubMenu ("Load settings on startup", onStartup);
    menu.addItem ("Reset settings...", [this] { resetSettings(); });
    menu.addSeparator();
    menu.addItem ("Settings...", [this] { showSettings(); });
    menu.addItem ("Hotkeys...", [this] { showHotkeys(); });
    menu.addSeparator();
    menu.addItem ("Recording options...", [this] { showRecordMenu(); });
    menu.addItem ("Open recordings folder", []
    {
        auto dir = Recorder::getRecordingsFolder();
        dir.createDirectory();
        dir.startAsProcess();
    });
    menu.addSeparator();
    menu.addItem ("Close to system tray", true, settings.closeToTray(), [] { AppSettings::get().setCloseToTray (! AppSettings::get().closeToTray()); });
    menu.addItem ("Run on Windows startup", true, settings.startWithWindows(), [] { AppSettings::get().setStartWithWindows (! AppSettings::get().startWithWindows()); });
    menu.addItem ("Show window on startup", true, ! settings.startMinimized(), [] { AppSettings::get().setStartMinimized (! AppSettings::get().startMinimized()); });
    menu.addItem ("Always on top", true, settings.alwaysOnTop(), [this]
    {
        AppSettings::get().setAlwaysOnTop (! AppSettings::get().alwaysOnTop());
        if (auto* w = window())
            w->setAlwaysOnTop (AppSettings::get().alwaysOnTop());
    });
    menu.addItem ("Lock controls", true, settings.lockControls(), [this]
    {
        AppSettings::get().setLockControls (! AppSettings::get().lockControls());
        applyLock();
        showNote (AppSettings::get().lockControls() ? "Controls locked (Setup > Lock controls to unlock)" : "Controls unlocked");
    });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (setupMenuButton));
}

void MainComponent::restartAudioEngine (bool fromUser)
{
    auto& dm = engine.getDeviceManager();
    const auto setup = dm.getAudioDeviceSetup();
    dm.closeAudioDevice();
    const auto error = dm.setAudioDeviceSetup (setup, true);
    engine.reopenExternalDevices();
    if (fromUser)
        showNote (error.isEmpty() ? "Audio engine restarted" : "Couldn't reopen the audio device: " + error);
    else if (error.isEmpty())
        showNote ("Audio engine restarted after a device change");
}

void MainComponent::globalFocusChanged (juce::Component* focused)
{
    if (focused != nullptr && AppSettings::get().lockControls() && console->isParentOf (focused))
        grabKeyboardFocus();
}

void MainComponent::applyLock()
{
    const bool locked = AppSettings::get().lockControls();
    console->setInterceptsMouseClicks (! locked, ! locked);   // the desk still scrolls
    if (locked && console->hasKeyboardFocus (true))
        grabKeyboardFocus();
}

// Load / Reset settings restart OpenMix: the file is applied at the next start (Session::load),
// so a half-loaded mix is never possible. A hidden PowerShell waits for this process to exit.
void MainComponent::restartApp()
{
    const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName();
    juce::StringArray cmd { "powershell.exe", "-NoProfile", "-WindowStyle", "Hidden", "-Command",
                            "Wait-Process -Id " + juce::String ((juce::int64) GetCurrentProcessId())
                                + " -Timeout 60 -ErrorAction SilentlyContinue; Start-Process -FilePath '"
                                + exe.replace ("'", "''") + "'" };
    juce::ChildProcess relauncher;   // not killed when this object goes away
    if (! relauncher.start (cmd, 0))
        showNote ("Couldn't restart OpenMix by itself. Close it and open it again.");
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}

void MainComponent::loadSettings()
{
    settingsChooser = std::make_unique<juce::FileChooser> ("Load OpenMix settings", juce::File(), "*.xml");
    settingsChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (file == juce::File())
            return;
        const auto xml = juce::parseXML (file);
        if (xml == nullptr || ! xml->hasTagName ("OpenMixSession"))
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Not an OpenMix settings file",
                                                    file.getFileName() + " isn't a file saved by OpenMix.");
            return;
        }
        juce::Component::SafePointer<MainComponent> safe (this);
        juce::AlertWindow::showAsync (
            juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::QuestionIcon).withTitle ("Load " + file.getFileName() + "?")
                .withMessage ("OpenMix restarts to load it. Your current settings are kept as session-backup.xml next to the session file.")
                .withButton ("Restart and load", 1).withButton ("Cancel", 0).withAssociatedComponent (this),
            [safe, copy = std::make_shared<juce::XmlElement> (*xml)] (int result)
            {
                if (safe == nullptr || result != 1)
                    return;
                if (Session::queueForNextStart (copy.get()))
                    safe->restartApp();
                else
                    safe->showNote ("Couldn't prepare the settings file");
            });
    });
}

void MainComponent::saveSettings()
{
    settingsChooser = std::make_unique<juce::FileChooser> ("Save OpenMix settings",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("OpenMix settings.xml"), "*.xml");
    settingsChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                      | juce::FileBrowserComponent::warnAboutOverwriting,
                                  [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (file == juce::File())
            return;
        Session::save (engine);
        showNote (Session::getFile().copyFileTo (file.withFileExtension ("xml")) ? "Settings saved to " + file.getFileName()
                                                                                 : "Couldn't save " + file.getFileName());
    });
}

void MainComponent::chooseStartupSettings()
{
    settingsChooser = std::make_unique<juce::FileChooser> ("Settings to load on startup", juce::File(), "*.xml");
    settingsChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (file == juce::File())
            return;
        if (! Session::isSessionFile (file))
        {
            showNote (file.getFileName() + " isn't an OpenMix settings file");
            return;
        }
        AppSettings::get().setStartupSessionFile (file);
        showNote ("OpenMix will start with " + file.getFileName());
    });
}

void MainComponent::resetSettings()
{
    juce::Component::SafePointer<MainComponent> safe (this);
    juce::AlertWindow::showAsync (
        juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::WarningIcon).withTitle ("Reset settings?")
            .withMessage ("OpenMix restarts with a fresh desk: default strips and buses, no plugins or scenes. "
                          "Your current settings are kept as session-backup.xml, so you can load them again.")
            .withButton ("Restart and reset", 1).withButton ("Cancel", 0).withAssociatedComponent (this),
        [safe] (int result)
        {
            if (safe == nullptr || result != 1)
                return;
            if (Session::queueForNextStart (nullptr))
                safe->restartApp();
            else
                safe->showNote ("Couldn't reset the settings");
        });
}

void MainComponent::stepScene (int delta)
{
    const int n = (int) engine.scenes.size();
    if (n == 0)
    {
        showNote ("No scenes yet. Press New to save the current mix as one.");
        return;
    }
    const int from = juce::isPositiveAndBelow (engine.currentScene, n) ? engine.currentScene : (delta > 0 ? -1 : n);
    recallScene ((from + delta + n) % n);
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
    o.dialogBackgroundColour = Theme::p().window;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = false;
    o.resizable = false;
    o.componentToCentreAround = this;
    o.launchAsync();
}

void MainComponent::resized()
{
    auto r = getLocalBounds();

    // Menu bar, left to right: name | Mixer View Setup | scene stepper (centre) |
    // Record, its options, Replay, Soundboard, update (right).
    headerArea = r.removeFromTop (38);
    auto header = headerArea.reduced (consolePad(), 0);
    constexpr int h = 24;
    header.removeFromLeft (104);

    for (auto* b : { &mixerMenuButton, &viewMenuButton, &setupMenuButton })
    {
        const int w = juce::GlyphArrangement::getStringWidthInt (Theme::ui (13.0f, true), b->getButtonText()) + 20;
        b->setBounds (header.removeFromLeft (w).withSizeKeepingCentre (w, h));
    }
    header.removeFromLeft (Theme::s3);

    const bool narrowHeader = getWidth() < 1140;
    const int updateW     = narrowHeader ? 104 : 124;
    const int soundboardW = narrowHeader ? 92  : 104;
    const int mediaW      = narrowHeader ? 64  : 76;
    const int replayW     = narrowHeader ? 88  : 100;
    const int recordW     = narrowHeader ? 80  : 92;
    for (auto* d : { &closeDot, &maximiseDot, &minimiseDot })
    {
        const int w = OpenMixLookAndFeel::windowButtonW + 2;
        d->setBounds (header.removeFromRight (w).withSizeKeepingCentre (w, OpenMixLookAndFeel::windowButtonH));
        header.removeFromRight (d == &minimiseDot ? Theme::s3 : 0);
    }
    if (updateButton.isVisible())
    {
        updateButton.setBounds (header.removeFromRight (updateW).withSizeKeepingCentre (updateW, h));
        header.removeFromRight (Theme::s1);
    }
    soundboardButton.setBounds (header.removeFromRight (soundboardW).withSizeKeepingCentre (soundboardW, h));
    header.removeFromRight (Theme::s1);
    mediaButton.setBounds (header.removeFromRight (mediaW).withSizeKeepingCentre (mediaW, h));
    header.removeFromRight (Theme::s3);
    transportDividerX = header.getRight() + Theme::s3 / 2;
    replayButton.setBounds (header.removeFromRight (replayW).withSizeKeepingCentre (replayW, h));
    header.removeFromRight (Theme::s1);
    recordMenuButton.setBounds (header.removeFromRight (h).withSizeKeepingCentre (h, h));
    header.removeFromRight (2);
    recordButton.setBounds (header.removeFromRight (recordW).withSizeKeepingCentre (recordW, h));
    header.removeFromRight (Theme::s3);

    // Scene stepper in the middle of what's left.
    const int sceneW = juce::jlimit (120, 220, header.getWidth() - 2 * h - 2 * 56 - 4 * Theme::s1);
    const int stepperW = h + Theme::s1 + sceneW + Theme::s1 + h + Theme::s2 + 56 + Theme::s1 + 56;
    auto stepper = header.withSizeKeepingCentre (juce::jmin (stepperW, header.getWidth()), h);
    scenePrevButton.setBounds (stepper.removeFromLeft (h));
    stepper.removeFromLeft (Theme::s1);
    sceneButton.setBounds (stepper.removeFromLeft (sceneW));
    stepper.removeFromLeft (Theme::s1);
    sceneNextButton.setBounds (stepper.removeFromLeft (h));
    stepper.removeFromLeft (Theme::s2);
    sceneStoreButton.setBounds (stepper.removeFromLeft (56));
    stepper.removeFromLeft (Theme::s1);
    sceneNewButton.setBounds (stepper.removeFromLeft (56));
    r.removeFromTop (Theme::s2);

    if (showBanner)
    {
        bannerArea = r.removeFromTop (52).reduced (consolePad(), 0);
        fixButton.setBounds (bannerArea.withTrimmedLeft (bannerArea.getWidth() - 176).reduced (Theme::s3, 11));
        r.removeFromTop (Theme::s2);
    }
    else
    {
        bannerArea = {};
    }

    // Bottom status bar: LIVE, engine CPU, background work and remote services on the left,
    // the audio device readout (opens Audio settings) on the right.
    {
        auto bar = r.removeFromBottom (28);
        statusBar.setBounds (bar);
        const int statusW = juce::jmin (560, bar.getWidth() / 2);
        statusDisplay.setVisible (statusW > 160);
        statusDisplay.setBounds (bar.removeFromRight (statusW).reduced (consolePad() - Theme::s2, 4));
        statusDisplay.toFront (false);
        statusBar.setReservedRight (statusW);
    }
    r.removeFromBottom (Theme::s2);

    consoleView.setBounds (r);
    const int minW = console->minimumWidth();
    console->setSize (juce::jmax (r.getWidth() - (r.getHeight() < console->minimumHeight() ? consoleView.getScrollBarThickness() : 0), minW),
                      juce::jmax (r.getHeight() - (r.getWidth() < minW ? consoleView.getScrollBarThickness() : 0),
                                  console->minimumHeight()));
}
