#include "ui/SettingsPanel.h"
#include "ui/Widgets.h"
#include "app/AppSettings.h"
#include "ui/Theme.h"

SettingsPanel::SettingsPanel (MixerEngine& e, std::function<void()> themeFn, std::function<void()> oscFn,
                              std::function<void()> hotkeysFn, MidiControl* midi, std::function<void()> appearanceFn)
    : engine (e), midiControl (midi), toggleTheme (std::move (themeFn)), oscChanged (std::move (oscFn)), openHotkeys (std::move (hotkeysFn)), appearanceChanged (std::move (appearanceFn))
{
    hotkeysButton.setTooltip ("Global hotkeys for mute, push to talk, the soundboard, recording and scenes.");
    hotkeysButton.onClick = [this] { if (openHotkeys) openHotkeys(); };
    makeClickable (hotkeysButton);
    content.addAndMakeVisible (hotkeysButton);

    setFocusContainerType (FocusContainerType::keyboardFocusContainer);
    content.setFocusContainerType (FocusContainerType::keyboardFocusContainer);
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);

    auto& s = AppSettings::get();

    startWithWindows.setToggleState (s.startWithWindows(), juce::dontSendNotification);
    startWithWindows.onClick = [this] { AppSettings::get().setStartWithWindows (startWithWindows.getToggleState()); };

    startMinimized.setToggleState (s.startMinimized(), juce::dontSendNotification);
    startMinimized.onClick = [this] { AppSettings::get().setStartMinimized (startMinimized.getToggleState()); };

    closeToTray.setToggleState (s.closeToTray(), juce::dontSendNotification);
    closeToTray.onClick = [this] { AppSettings::get().setCloseToTray (closeToTray.getToggleState()); };

    for (auto* t : { &startWithWindows, &startMinimized, &closeToTray })
    {
        makeClickable (*t);
        content.addAndMakeVisible (t);
    }

    auto themeLabel = [] { return Theme::isDark() ? juce::String ("Switch to light mode") : juce::String ("Switch to dark mode"); };
    themeButton.setButtonText (themeLabel());
    themeButton.onClick = [this, themeLabel]
    {
        if (toggleTheme) toggleTheme();
        themeButton.setButtonText (themeLabel());
        content.repaint();
    };

    compactMode.setToggleState (s.compactMode(), juce::dontSendNotification);
    compactMode.onClick = [this]
    {
        AppSettings::get().setCompactMode (compactMode.getToggleState());
        Theme::setCompact (compactMode.getToggleState());
        if (appearanceChanged) appearanceChanged();
    };
    makeClickable (compactMode);
    content.addAndMakeVisible (compactMode);

    scanButton.onClick    = [this] { engine.getScanner().startScan (false); updateScanText(); };
    retryButton.onClick   = [this] { engine.getScanner().startScan (true);  updateScanText(); };
    presetsButton.onClick = []
    {
        auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("OpenMix Presets");
        dir.createDirectory();
        dir.startAsProcess();
    };
    pluginsButton.onClick = []
    {
        juce::File ("C:\\Program Files\\Common Files\\VST3").startAsProcess();
#if OPENMIX_VST2_ENABLED
        // JUCE's VST2 default locations on Windows.
        juce::File ("C:\\Program Files\\Steinberg\\VstPlugins").startAsProcess();
        juce::File ("C:\\Program Files\\VstPlugins").startAsProcess();
#endif
    };

    for (auto* b : { &themeButton, &scanButton, &retryButton, &presetsButton, &pluginsButton })
    {
        makeClickable (*b);
        content.addAndMakeVisible (b);
    }

    // Crash-safe plugin loading: a plugin that crashes while loading is caught
    // in a child process first, so the mixer keeps running. See PluginScanner.
    pluginProbe.setToggleState (s.pluginProbeEnabled(), juce::dontSendNotification);
    pluginProbe.onClick = [this] { AppSettings::get().setPluginProbeEnabled (pluginProbe.getToggleState()); };
    makeClickable (pluginProbe);
    content.addAndMakeVisible (pluginProbe);

    checkUpdates.setToggleState (s.checkForUpdates(), juce::dontSendNotification);
    checkUpdates.onClick = [this] { AppSettings::get().setCheckForUpdates (checkUpdates.getToggleState()); };

    oscEnabled.setToggleState (s.oscEnabled(), juce::dontSendNotification);
    oscEnabled.onClick = [this]
    {
        AppSettings::get().setOscEnabled (oscEnabled.getToggleState());
        if (oscChanged) oscChanged();
    };

    // Port fields read like the desk's numeric readouts.
    for (auto* port : { &oscPort, &wsPort })
    {
        port->setFont (Theme::mono (12.5f));
        port->setIndents (6, 4);
    }
    oscPort.setText (juce::String (s.oscPort()), false);
    oscPort.setInputRestrictions (5, "0123456789");
    oscPort.setTitle ("OSC port");
    oscPort.setJustification (juce::Justification::centredLeft);
    auto commitPort = [this]
    {
        const int port = oscPort.getText().getIntValue();
        if (port < 1024 || port > 65535)
        {
            oscPort.setText (juce::String (AppSettings::get().oscPort()), false);
            return;
        }
        if (port != AppSettings::get().oscPort())
        {
            AppSettings::get().setOscPort (port);
            if (oscChanged) oscChanged();
        }
    };
    oscPort.onReturnKey = commitPort;
    oscPort.onFocusLost = commitPort;

    // Phone / browser remote and WebSocket API: the same port serves the phone
    // page (GET /), the state (GET /api/state) and the WebSocket (ws://.../ws).
    wsEnabled.setToggleState (s.wsEnabled(), juce::dontSendNotification);
    wsEnabled.onClick = [this]
    {
        AppSettings::get().setWsEnabled (wsEnabled.getToggleState());
        if (oscChanged) oscChanged();
    };
    wsPort.setText (juce::String (s.wsPort()), false);
    wsPort.setInputRestrictions (5, "0123456789");
    wsPort.setTitle ("WebSocket / phone remote port");
    wsPort.setJustification (juce::Justification::centredLeft);
    auto commitWsPort = [this]
    {
        const int port = wsPort.getText().getIntValue();
        if (port < 1024 || port > 65535)
        {
            wsPort.setText (juce::String (AppSettings::get().wsPort()), false);
            return;
        }
        if (port != AppSettings::get().wsPort())
        {
            AppSettings::get().setWsPort (port);
            if (oscChanged) oscChanged();
        }
    };
    wsPort.onReturnKey = commitWsPort;
    wsPort.onFocusLost = commitWsPort;

    // MIDI: pick a controller, then learn one knob or key per target.
    midiEnabled.setToggleState (s.midiEnabled(), juce::dontSendNotification);
    midiEnabled.onClick = [this]
    {
        AppSettings::get().setMidiEnabled (midiEnabled.getToggleState());
        if (oscChanged) oscChanged();
    };
    midiDeviceBox.setTitle ("MIDI device");
    midiDeviceBox.onChange = [this]
    {
        const int id = midiDeviceBox.getSelectedId();
        if (id > 0 && juce::isPositiveAndBelow (id - 1, midiDevices.size()))
        {
            AppSettings::get().setMidiDevice (midiDevices[id - 1].identifier);
            if (oscChanged) oscChanged();
        }
    };
    refreshMidiDevices();

    // Item ids are 1-based positions in midiLearnPaths (section headings take no id).
    auto addLearnTarget = [this] (const juce::String& label, const juce::String& targetPath)
    {
        midiLearnPaths.push_back (targetPath);
        midiLearnTarget.addItem (label, (int) midiLearnPaths.size());
    };
    for (int sIdx = 0; sIdx < kNumStrips; ++sIdx)
    {
        const auto n = juce::String (sIdx + 1);
        midiLearnTarget.addSectionHeading ("Strip " + n);
        addLearnTarget ("Strip " + n + " fader", "strip/" + n + "/fader");
        addLearnTarget ("Strip " + n + " gain",  "strip/" + n + "/gain");
        addLearnTarget ("Strip " + n + " mute",  "strip/" + n + "/mute");
        addLearnTarget ("Strip " + n + " solo",  "strip/" + n + "/solo");
        for (int b = 0; b < kNumBuses; ++b)
        {
            const juce::String bn (kBusNames[b]);
            addLearnTarget ("Strip " + n + " send to " + bn + " (on/off)", "strip/" + n + "/send/" + bn);
            addLearnTarget ("Strip " + n + " send level to " + bn,         "strip/" + n + "/sendlevel/" + bn);
        }
    }
    midiLearnTarget.addSectionHeading ("Buses");
    for (int b = 0; b < kNumBuses; ++b)
    {
        const juce::String bn (kBusNames[b]);
        addLearnTarget ("Bus " + bn + " fader", "bus/" + bn + "/fader");
        addLearnTarget ("Bus " + bn + " mute",  "bus/" + bn + "/mute");
        addLearnTarget ("Bus " + bn + " loudness normalisation (on/off)", "bus/" + bn + "/norm");
    }
    midiLearnTarget.addSectionHeading ("Soundboard, recording and scenes");
    for (int p = 0; p < Soundboard::numPads; ++p)
        addLearnTarget ("Soundboard pad " + juce::String (p + 1), "soundboard/" + juce::String (p + 1) + "/play");
    addLearnTarget ("Record", "record");
    addLearnTarget ("Save replay", "replay/save");
    for (int sc = 0; sc < 8; ++sc)
        addLearnTarget ("Scene " + juce::String (sc + 1), "scene/" + juce::String (sc + 1));
    midiLearnTarget.setSelectedId (1, juce::dontSendNotification);
    midiLearnButton.getProperties().set ("lamp", "key");
    midiLearnButton.setClickingTogglesState (false);
    midiLearnButton.onClick = [this]
    {
        if (midiControl == nullptr)
            return;
        if (midiControl->isLearning())
        {
            midiControl->cancelLearn();
            midiLearnButton.setButtonText ("Learn...");
            midiLearnButton.setToggleState (false, juce::dontSendNotification);
            updateMidiText();
            return;
        }
        const int index = midiLearnTarget.getSelectedId() - 1;
        if (! juce::isPositiveAndBelow (index, (int) midiLearnPaths.size()))
            return;
        const auto target = midiLearnPaths[(size_t) index];
        if (midiControl->startLearn (target))
        {
            midiLearnButton.setButtonText ("Listening... (click to cancel)");
            midiLearnButton.setToggleState (true, juce::dontSendNotification);
            midiText = "Move a knob or press a key on the controller for " + target + ".";
            content.repaint (midiTextArea);
        }
    };
    midiClearButton.onClick = [this]
    {
        if (midiControl == nullptr)
            return;
        midiControl->clearMappings();
        AppSettings::get().setMidiMappings (juce::String());
        updateMidiText();
    };
    for (auto* b : { &midiLearnButton, &midiClearButton })
    {
        makeClickable (*b);
        content.addAndMakeVisible (*b);
    }
    content.addAndMakeVisible (midiDeviceBox);
    content.addAndMakeVisible (midiLearnTarget);
    updateMidiText();

    logsButton.onClick = [] { auto dir = AppSettings::getLogFolder(); dir.createDirectory(); dir.startAsProcess(); };

    for (auto* t : { &checkUpdates, &oscEnabled, &wsEnabled, &midiEnabled })
    {
        makeClickable (*t);
        content.addAndMakeVisible (t);
    }
    content.addAndMakeVisible (oscPort);
    content.addAndMakeVisible (wsPort);
    makeClickable (logsButton);
    content.addAndMakeVisible (logsButton);

    engine.getScanner().addChangeListener (this);
    updateScanText();
    startTimerHz (2);   // watches MIDI learn finishing (a knob/key on another thread)
    setSize (preferredWidth, contentHeight());
}

SettingsPanel::~SettingsPanel()
{
    stopTimer();
    // A learn left running would silently bind the next knob anyone touches.
    if (midiControl != nullptr && midiControl->isLearning())
        midiControl->cancelLearn();
    engine.getScanner().removeChangeListener (this);
}

void SettingsPanel::timerCallback()
{
    if (midiControl == nullptr)
        return;
    // A learn that finished (or was cancelled) puts the button back.
    if (! midiControl->isLearning() && midiLearnButton.getButtonText().startsWith ("Listening"))
    {
        midiLearnButton.setButtonText ("Learn...");
        midiLearnButton.setToggleState (false, juce::dontSendNotification);
        updateMidiText();
    }
}

void SettingsPanel::refreshMidiDevices()
{
    midiDeviceBox.clear();
    midiDevices = MidiControl::getDevices();
    const auto saved = AppSettings::get().midiDevice();
    for (int i = 0; i < midiDevices.size(); ++i)
    {
        midiDeviceBox.addItem (midiDevices[i].name, i + 1);
        if (midiDevices[i].identifier == saved || midiDevices[i].name == saved)
            midiDeviceBox.setSelectedId (i + 1, juce::dontSendNotification);
    }
    if (midiDevices.isEmpty())
        midiDeviceBox.addItem ("No MIDI devices found", -1);
    midiDeviceBox.setTextWhenNothingSelected ("Choose a MIDI controller");
}

void SettingsPanel::updateMidiText()
{
    if (midiControl == nullptr)
    {
        midiText = "MIDI is not available.";
    }
    else
    {
        const auto maps = midiControl->getMappings();
        if (maps.empty())
            midiText = "No MIDI mappings yet. Pick a target, press Learn, then move a knob or press a key.";
        else
        {
            juce::StringArray lines;
            for (auto& m : maps)
                lines.add ((m.isNote ? "Note " : "CC ") + juce::String (m.number) + " -> " + m.target);
            midiText = lines.joinIntoString ("\n");
        }
        AppSettings::get().setMidiMappings (MidiControl::mappingsToJson (maps));
    }
    if (content.getWidth() > 0)
        resized();
    content.repaint();
}

void SettingsPanel::updateScanText()
{
    auto& scanner = engine.getScanner();
    const int found = scanner.getPlugins().size();
    const int skipped = scanner.getSkippedFiles().size();

    if (scanner.isScanning())
        scanText = "Scanning: " + juce::String (scanner.getNumDone()) + " of " + juce::String (scanner.getNumToScan()) + " plugin files.";
    else
        scanText = juce::String (found) + (found == 1 ? " plugin" : " plugins") + " found"
                 + (skipped > 0 ? ", " + juce::String (skipped) + " skipped because they crashed or hung while scanning." : ".");

    retryButton.setEnabled (! scanner.isScanning() && skipped > 0);
    scanButton.setEnabled (! scanner.isScanning());
    content.repaint (scanTextArea);
}

int SettingsPanel::midiBoxHeight() const
{
    const int lines = juce::jmax (3, midiControl != nullptr ? (int) midiControl->getMappings().size() : 3);
    return juce::jmax (56, lines * 18 + 12);
}

void SettingsPanel::paint (juce::Graphics& g)
{
    g.fillAll (Theme::p().window);
}

void SettingsPanel::paintContent (juce::Graphics& g)
{
    const auto& t = Theme::p();
    g.fillAll (t.window);

    for (const auto* card : { &startupCard, &themeCard, &pluginsCard, &networkCard, &updatesCard, &aboutCard })
        Theme::drawFaceplate (g, card->toFloat());

    // Section legends and 1 px dividers, the same as the sections on a strip.
    Theme::drawLegend (g, "Startup", startupLegend);
    Theme::drawLegend (g, "Look and hotkeys", themeLegend);
    Theme::drawLegend (g, "Plugins", pluginsLegend);
    Theme::drawLegend (g, "Remote control", networkLegend);
    Theme::drawLegend (g, "Updates and logs", updatesLegend);
    Theme::drawLegend (g, "About", aboutLegend);

    g.setFont (Theme::ui (12.0f));
    g.setColour (t.textFaint);
    g.drawText ("Port", oscPortLabelArea, juce::Justification::centredRight);
    g.drawText ("Port", wsPortLabelArea, juce::Justification::centredRight);

    g.setFont (Theme::ui (12.5f));
    g.setColour (t.textDim);
    g.drawFittedText (scanText, scanTextArea, juce::Justification::centredLeft, 2);

    g.drawFittedText ("While this is on, any device on your network can move faders and start recordings. "
                      "Addresses start with /openmix.", oscNoteArea, juce::Justification::topLeft, 3, 1.0f);

    g.drawFittedText ("On your phone, open http://<this PC>:" + juce::String (AppSettings::get().wsPort())
                        + "/ while this is on. Stream Deck: its OSC plugin with the /openmix addresses, "
                          "or a WebSocket plugin on this port.",
                      wsNoteArea, juce::Justification::topLeft, 4, 1.0f);

    // Recessed MIDI mappings readout box matching the desk style.
    g.setColour (t.selBg);
    g.fillRoundedRectangle (midiTextArea.toFloat(), Theme::controlRadius);
    g.setColour (t.line);
    g.drawRoundedRectangle (midiTextArea.toFloat().reduced (0.5f), Theme::controlRadius, 1.0f);
    g.setFont (Theme::mono (11.5f));
    g.setColour (t.textDim);
    const int midiMaxLines = juce::jmax (3, juce::StringArray::fromLines (midiText).size());
    g.drawFittedText (midiText, midiTextArea.reduced (8, 6), juce::Justification::topLeft, midiMaxLines);

    g.setFont (Theme::ui (12.5f));
    g.setColour (t.textDim);
    g.drawFittedText ("OpenMix " + juce::JUCEApplication::getInstance()->getApplicationVersion()
                        + ". Licensed under the GNU AGPLv3. Built with JUCE (AGPLv3) and RNNoise (BSD).",
                      aboutArea, juce::Justification::topLeft, 3);
}

// Two columns, so everything fits without scrolling on a normal screen: setup on the left
// (startup, look, plugins, about), remote control and logs on the right. Rows are 24 px keys
// and toggles, the same sizes as on the desk.
namespace
{
    constexpr int pad = 12, cardPad = 10, cardGap = 8, legendH = 16, rowH = 24, colGap = 12;
}

int SettingsPanel::contentHeight() const
{
    const int left = (cardPad * 2 + legendH + 3 * rowH)
                   + cardGap + (cardPad * 2 + legendH + rowH + 6 + rowH)
                   + cardGap + (cardPad * 2 + legendH + rowH + 4 + 32 + rowH + 6 + rowH)
                   + cardGap + (cardPad * 2 + legendH + 36);
    const int right = (cardPad * 2 + legendH + rowH + 46 + rowH + 62 + rowH + rowH + 6 + rowH + 6 + midiBoxHeight())
                    + cardGap + (cardPad * 2 + legendH + rowH + 6 + rowH);
    return pad * 2 + juce::jmax (left, right);
}

void SettingsPanel::resized()
{
    viewport.setBounds (getLocalBounds());
    const int w = juce::jmax (620, viewport.getMaximumVisibleWidth());
    content.setSize (w, contentHeight());
    layoutContent();
}

void SettingsPanel::layoutContent()
{
    auto r = content.getLocalBounds().reduced (pad);
    auto left = r.removeFromLeft ((r.getWidth() - colGap) / 2);
    r.removeFromLeft (colGap);
    auto right = r;

    auto twoButtons = [] (juce::Rectangle<int> row, juce::Component& a, juce::Component& b)
    {
        const int bw = (row.getWidth() - Theme::s2) / 2;
        a.setBounds (row.removeFromLeft (bw));
        row.removeFromLeft (Theme::s2);
        b.setBounds (row);
    };

    // Left column
    startupCard = left.removeFromTop (cardPad * 2 + legendH + 3 * rowH);
    {
        auto c = startupCard.reduced (cardPad);
        startupLegend = c.removeFromTop (legendH);
        for (auto* t : { &startWithWindows, &startMinimized, &closeToTray })
            t->setBounds (c.removeFromTop (rowH));
    }
    left.removeFromTop (cardGap);

    themeCard = left.removeFromTop (cardPad * 2 + legendH + rowH + 6 + rowH);
    {
        auto c = themeCard.reduced (cardPad);
        themeLegend = c.removeFromTop (legendH);
        twoButtons (c.removeFromTop (rowH), themeButton, hotkeysButton);
        c.removeFromTop (6);
        compactMode.setBounds (c.removeFromTop (rowH));
    }
    left.removeFromTop (cardGap);

    pluginsCard = left.removeFromTop (cardPad * 2 + legendH + rowH + 4 + 32 + rowH + 6 + rowH);
    {
        auto c = pluginsCard.reduced (cardPad);
        pluginsLegend = c.removeFromTop (legendH);
        twoButtons (c.removeFromTop (rowH), scanButton, retryButton);
        c.removeFromTop (4);
        scanTextArea = c.removeFromTop (32);
        twoButtons (c.removeFromTop (rowH), presetsButton, pluginsButton);
        c.removeFromTop (6);
        pluginProbe.setBounds (c.removeFromTop (rowH));
    }
    left.removeFromTop (cardGap);

    aboutCard = left.removeFromTop (cardPad * 2 + legendH + 36);
    {
        auto c = aboutCard.reduced (cardPad);
        aboutLegend = c.removeFromTop (legendH);
        aboutArea = c;
    }

    // Right column
    const int midiH = midiBoxHeight();
    networkCard = right.removeFromTop (cardPad * 2 + legendH + rowH + 46 + rowH + 62 + rowH + rowH + 6 + rowH + 6 + midiH);
    {
        auto c = networkCard.reduced (cardPad);
        networkLegend = c.removeFromTop (legendH);

        auto portRow = [&] (juce::ToggleButton& toggle, juce::TextEditor& port, juce::Rectangle<int>& labelArea)
        {
            auto row = c.removeFromTop (rowH);
            port.setBounds (row.removeFromRight (64));
            row.removeFromRight (6);
            labelArea = row.removeFromRight (30);
            row.removeFromRight (Theme::s2);
            toggle.setBounds (row);
        };

        portRow (oscEnabled, oscPort, oscPortLabelArea);
        oscNoteArea = c.removeFromTop (46).withTrimmedLeft (28).reduced (0, 4);
        portRow (wsEnabled, wsPort, wsPortLabelArea);
        wsNoteArea = c.removeFromTop (62).withTrimmedLeft (28).reduced (0, 4);

        midiEnabled.setBounds (c.removeFromTop (rowH));
        midiDeviceBox.setBounds (c.removeFromTop (rowH));
        c.removeFromTop (6);
        auto row = c.removeFromTop (rowH);
        midiLearnTarget.setBounds (row.removeFromLeft (juce::jmax (120, row.getWidth() - 190)));
        row.removeFromLeft (Theme::s2);
        midiLearnButton.setBounds (row.removeFromLeft (84));
        row.removeFromLeft (Theme::s2);
        midiClearButton.setBounds (row);
        c.removeFromTop (6);
        midiTextArea = c.removeFromTop (midiH);
    }
    right.removeFromTop (cardGap);

    updatesCard = right.removeFromTop (cardPad * 2 + legendH + rowH + 6 + rowH);
    {
        auto c = updatesCard.reduced (cardPad);
        updatesLegend = c.removeFromTop (legendH);
        checkUpdates.setBounds (c.removeFromTop (rowH));
        c.removeFromTop (6);
        logsButton.setBounds (c.removeFromTop (rowH).withWidth (160));
    }
}
