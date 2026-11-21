#include "MainComponent.h"
#include "Session.h"
#include "Theme.h"
#include "OpenMixLookAndFeel.h"
#include "ui/SettingsPanel.h"

static constexpr int stripGap = Theme::s2;
static constexpr int sectionGap = 32;   // space between inputs and the master section
static constexpr int consolePad = Theme::s4;

//==============================================================================
// ConsoleContent

static constexpr int addColumnWidth = 40;   // the slim "+" columns

ConsoleContent::ConsoleContent (MixerEngine& e) : engine (e)
{
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
    return consolePad * 2
         + ns * StripView::minWidth + (ns - 1) * stripGap + (addStripButton.isVisible() ? stripGap + addColumnWidth : 0)
         + sectionGap
         + nb * BusView::minWidth + (nb - 1) * stripGap + (addBusButton.isVisible() ? stripGap + addColumnWidth : 0);
}

void ConsoleContent::paint (juce::Graphics& g)
{
    Theme::drawLegend (g, "Inputs", inputsLegend);
    Theme::drawLegend (g, "Outputs", outputsLegend);

    // Divider between the input channels and the master section, as on a desk.
    g.setColour (Theme::p().line);
    g.fillRect (dividerX, inputsLegend.getBottom() + Theme::s1, 1, getHeight() - inputsLegend.getBottom() - consolePad - Theme::s1);
}

void ConsoleContent::resized()
{
    auto r = getLocalBounds().reduced (consolePad, 0).withTrimmedBottom (consolePad);

    const int ns = juce::jmax (1, engine.getNumActiveStrips());
    const int nb = juce::jmax (1, engine.getNumActiveBuses());
    const int addS = addStripButton.isVisible() ? stripGap + addColumnWidth : 0;
    const int addB = addBusButton.isVisible() ? stripGap + addColumnWidth : 0;

    // Strip width: share the room, within limits. Leftover space is split around the desk.
    const int busesMax = nb * BusView::maxWidth + (nb - 1) * stripGap + addB;
    int stripW = (r.getWidth() - busesMax - sectionGap - addS - (ns - 1) * stripGap) / ns;
    stripW = juce::jlimit (StripView::minWidth, StripView::maxWidth, stripW);
    const int busW = juce::jlimit (BusView::minWidth, BusView::maxWidth,
                                   (r.getWidth() - ns * stripW - (ns - 1) * stripGap - addS - sectionGap
                                    - (nb - 1) * stripGap - addB) / nb);

    const int inputsW = ns * stripW + (ns - 1) * stripGap + addS;
    const int deskW = inputsW + sectionGap + nb * busW + (nb - 1) * stripGap + addB;
    r = r.withSizeKeepingCentre (juce::jmin (r.getWidth(), deskW), r.getHeight());

    auto legends = r.removeFromTop (28);
    inputsLegend = legends.removeFromLeft (inputsW);
    legends.removeFromLeft (sectionGap);
    outputsLegend = legends;

    for (int s = 0; s < kNumStrips; ++s)
    {
        if (! strips[(size_t) s]->isVisible())
            continue;
        strips[(size_t) s]->setBounds (r.removeFromLeft (stripW));
        r.removeFromLeft (stripGap);
    }
    if (addStripButton.isVisible())
    {
        addStripButton.setBounds (r.removeFromLeft (addColumnWidth).withHeight (juce::jmin (r.getHeight(), 120)));
        r.removeFromLeft (stripGap);
    }

    r.removeFromLeft (sectionGap - stripGap);
    dividerX = r.getX() - sectionGap / 2;

    for (int b = 0; b < kNumBuses; ++b)
    {
        if (! buses[(size_t) b]->isVisible())
            continue;
        buses[(size_t) b]->setBounds (r.removeFromLeft (busW));
        r.removeFromLeft (stripGap);
    }
    if (addBusButton.isVisible())
        addBusButton.setBounds (r.removeFromLeft (addColumnWidth).withHeight (juce::jmin (r.getHeight(), 120)));
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

    settingsButton.setTooltip ("Driver (ASIO or Windows Audio), device, channels, sample rate and buffer size.");
    settingsButton.onClick = [this] { showAudioSettings(); };
    makeClickable (settingsButton);
    addAndMakeVisible (settingsButton);

    appSettingsButton.setTooltip ("Startup, tray, theme and plugin options.");
    appSettingsButton.onClick = [this] { showSettings(); };
    makeClickable (appSettingsButton);
    addAndMakeVisible (appSettingsButton);

    themeButton.onClick = [this] { toggleTheme(); };
    makeClickable (themeButton);
    addAndMakeVisible (themeButton);

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

    changeListenerCallback (&engine.getDeviceManager());
    startTimerHz (30);

    setSize (1320, 880);
}

MainComponent::~MainComponent()
{
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

    themeButton.setButtonText (Theme::isDark() ? "Light mode" : "Dark mode");
    themeButton.setTitle (Theme::isDark() ? "Switch to light mode" : "Switch to dark mode");
    themeButton.setTooltip (themeButton.getTitle() + ".");

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
    o.content.setOwned (new SettingsPanel (engine, [this] { toggleTheme(); }));
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

    setDescription (statusSegments.joinIntoString (", "));
    repaint (headerArea.getUnion (bannerArea));
}

void MainComponent::timerCallback()
{
    for (auto& s : console->strips) if (s->isVisible()) s->tick();
    for (auto& b : console->buses)  if (b->isVisible()) b->tick();

    if (++ticks % 15 == 0)
        updateStatus();

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

    // Product name as text (no invented logo)
    g.setColour (t.text);
    g.setFont (Theme::ui (18.0f, true));
    g.drawText ("OpenMix", headerArea.reduced (consolePad, 0).removeFromLeft (110), juce::Justification::centredLeft);

    // Status display: a recessed readout, segments right to left by importance.
    if (! statusArea.isEmpty())
    {
        g.setColour (t.well);
        g.fillRoundedRectangle (statusArea.toFloat(), Theme::controlRadius);
        g.setColour (t.line);
        g.drawRoundedRectangle (statusArea.toFloat().reduced (0.5f), Theme::controlRadius, 1.0f);

        const auto font = Theme::mono (12.0f);
        g.setFont (font);
        auto area = statusArea.reduced (Theme::s3, 0);
        for (int i = 0; i < statusSegments.size(); ++i)
        {
            const auto& seg = statusSegments[i];
            const bool isDevice = i == 0;
            const int natural = juce::GlyphArrangement::getStringWidthInt (font, seg);
            const int w = isDevice ? juce::jmin (natural, area.getWidth()) : natural;
            if (w > area.getWidth() || w <= 0)
                break;

            g.setColour (isDevice ? t.text : t.textDim);
            g.drawText (seg, area.removeFromLeft (w), juce::Justification::centredLeft, true);

            if (i < statusSegments.size() - 1 && area.getWidth() > 30)
            {
                area.removeFromLeft (Theme::s3);
                g.setColour (t.line);
                g.fillRect (area.getX(), area.getCentreY() - 7, 1, 14);
                area.removeFromLeft (Theme::s3);
            }
        }
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
    // Only reached while the window itself has focus: Tab / Shift+Tab enter the controls.
    if (key.isKeyCode (juce::KeyPress::tabKey))
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

void MainComponent::resized()
{
    auto r = getLocalBounds();

    headerArea = r.removeFromTop (52);
    auto header = headerArea.reduced (consolePad, 0);
    header.removeFromLeft (110);
    settingsButton.setBounds (header.removeFromLeft (128).withSizeKeepingCentre (128, 30));
    themeButton.setBounds (header.removeFromRight (108).withSizeKeepingCentre (108, 30));
    header.removeFromRight (Theme::s2);
    appSettingsButton.setBounds (header.removeFromRight (92).withSizeKeepingCentre (92, 30));
    header.removeFromRight (Theme::s3);
    header.removeFromLeft (Theme::s3);
    statusArea = header.getWidth() > 160 ? header.withSizeKeepingCentre (header.getWidth(), 30) : juce::Rectangle<int>();

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
    console->setSize (juce::jmax (r.getWidth() - (r.getHeight() < ConsoleContent::minimumHeight ? consoleView.getScrollBarThickness() : 0), minW),
                      juce::jmax (r.getHeight() - (r.getWidth() < minW ? consoleView.getScrollBarThickness() : 0),
                                  ConsoleContent::minimumHeight));
}
