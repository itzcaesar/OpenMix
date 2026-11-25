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

    // Accent colour: the lamp colour for "on" sends, the voice-pad puck and toggle lamps.
    // Amber is the default; the choice is saved and applied on startup.
    for (int i = 0; i < Theme::numAccentColours; ++i)
    {
        auto sw = std::make_unique<AccentSwatch> (juce::Colour (Theme::accentColours[i].argb), [this, i]
        {
            AppSettings::get().setAccentIndex (i);
            Theme::setAccent (i);
            updateAccentSwatches();
            if (appearanceChanged) appearanceChanged();
        });
        sw->setTitle (juce::String ("Accent colour: ") + Theme::accentColours[i].name);
        content.addAndMakeVisible (*sw);
        accentSwatches.push_back (std::move (sw));
    }
    updateAccentSwatches();

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

    auto addLearnTarget = [this] (const juce::String& label, const juce::String& targetPath)
    {
        midiLearnTarget.addItem (label, midiLearnTarget.getNumItems() + 1);
        midiLearnPaths.push_back (targetPath);
    };
    for (int sIdx = 0; sIdx < kNumStrips; ++sIdx)
    {
        const auto n = juce::String (sIdx + 1);
        addLearnTarget ("Strip " + n + " fader", "strip/" + n + "/fader");
        addLearnTarget ("Strip " + n + " mute",  "strip/" + n + "/mute");
        addLearnTarget ("Strip " + n + " solo",  "strip/" + n + "/solo");
    }
    for (int b = 0; b < kNumBuses; ++b)
    {
        const juce::String bn (kBusNames[b]);
        addLearnTarget ("Bus " + bn + " fader", "bus/" + bn + "/fader");
        addLearnTarget ("Bus " + bn + " mute",  "bus/" + bn + "/mute");
    }
    for (int p = 0; p < Soundboard::numPads; ++p)
        addLearnTarget ("Soundboard pad " + juce::String (p + 1), "soundboard/" + juce::String (p + 1) + "/play");
    addLearnTarget ("Record", "record");
    addLearnTarget ("Save replay", "replay/save");
    midiLearnTarget.setSelectedItemIndex (0);
    midiLearnButton.onClick = [this]
    {
        if (midiControl == nullptr)
            return;
        if (midiControl->isLearning())
        {
            midiControl->cancelLearn();
            midiLearnButton.setButtonText ("Learn...");
            return;
        }
        const int index = midiLearnTarget.getSelectedItemIndex();
        if (! juce::isPositiveAndBelow (index, (int) midiLearnPaths.size()))
            return;
        const auto target = midiLearnPaths[(size_t) index];
        if (midiControl->startLearn (target))
        {
            midiLearnButton.setButtonText ("Listening... (click to cancel)");
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
    setSize (556, 890);
}

SettingsPanel::~SettingsPanel()
{
    stopTimer();
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
    content.repaint (midiTextArea);
}

SettingsPanel::AccentSwatch::AccentSwatch (juce::Colour c, std::function<void()> onClickFn)
    : colour (c), onClick (std::move (onClickFn))
{
}

void SettingsPanel::AccentSwatch::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    g.setColour (colour);
    g.fillRoundedRectangle (area, Theme::controlRadius);
    if (selected)
    {
        g.setColour (Theme::p().text);
        g.drawRoundedRectangle (area.reduced (1.0f), Theme::controlRadius, 2.0f);
    }
}

void SettingsPanel::AccentSwatch::mouseUp (const juce::MouseEvent& e)
{
    if (e.mouseWasClicked() && onClick)
        onClick();
}

void SettingsPanel::updateAccentSwatches()
{
    for (int i = 0; i < (int) accentSwatches.size(); ++i)
        accentSwatches[(size_t) i]->setSelected (i == Theme::accentIndex());
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

void SettingsPanel::paint (juce::Graphics& g)
{
    g.fillAll (Theme::p().panel);
}

void SettingsPanel::paintContent (juce::Graphics& g)
{
    const auto& t = Theme::p();
    g.fillAll (t.panel);

    Theme::drawLegend (g, "Startup", startupLegend);
    Theme::drawLegend (g, "Look and hotkeys", themeLegend);
    Theme::drawLegend (g, "Plugins", pluginsLegend);
    Theme::drawLegend (g, "Network and logs", networkLegend);
    Theme::drawLegend (g, "About", aboutLegend);

    g.setFont (Theme::ui (13.0f));
    g.setColour (t.textDim);
    g.drawText ("Accent colour", accentLabelArea, juce::Justification::centredLeft);

    g.setFont (Theme::ui (13.0f));
    g.setColour (t.textDim);
    g.drawFittedText (scanText, scanTextArea, juce::Justification::centredLeft, 2);

    g.drawFittedText ("While OSC is on, any device on your network can move faders and start recordings. "
                      "Addresses start with /openmix (see the README).", oscNoteArea, juce::Justification::topLeft, 2);

    g.drawFittedText ("Phone remote: open http://<this PC>:" + juce::String (AppSettings::get().wsPort())
                        + "/ on your phone while the WebSocket remote is on. Stream Deck: use its OSC plugin "
                          "with the /openmix addresses, or a WebSocket plugin against this port.",
                      wsNoteArea, juce::Justification::topLeft, 3);

    g.setFont (Theme::ui (13.0f));
    g.setColour (t.textDim);
    g.drawFittedText (midiText, midiTextArea, juce::Justification::topLeft, 4);

    g.drawFittedText ("OpenMix " + juce::JUCEApplication::getInstance()->getApplicationVersion()
                        + ". Licensed under the GNU AGPLv3. Built with JUCE (AGPLv3) and RNNoise (BSD).",
                      aboutArea, juce::Justification::topLeft, 3);
}

void SettingsPanel::resized()
{
    viewport.setBounds (getLocalBounds());
    const int w = juce::jmax (500, viewport.getMaximumVisibleWidth());
    content.setSize (w, 890);
    layoutContent();
}

void SettingsPanel::layoutContent()
{
    auto r = content.getLocalBounds().reduced (Theme::s5, Theme::s4);

    startupLegend = r.removeFromTop (20);
    for (auto* t : { &startWithWindows, &startMinimized, &closeToTray })
        t->setBounds (r.removeFromTop (28));
    r.removeFromTop (Theme::s4);

    themeLegend = r.removeFromTop (20);
    {
        auto row = r.removeFromTop (30);
        themeButton.setBounds (row.removeFromLeft (190));
        row.removeFromLeft (Theme::s2);
        hotkeysButton.setBounds (row.removeFromLeft (190));
    }
    r.removeFromTop (Theme::s1);
    {
        auto row = r.removeFromTop (28);
        accentLabelArea = row.removeFromLeft (110);
        for (auto& sw : accentSwatches)
        {
            sw->setBounds (row.removeFromLeft (28));
            row.removeFromLeft (Theme::s1);
        }
    }
    compactMode.setBounds (r.removeFromTop (28));
    r.removeFromTop (Theme::s4);

    pluginsLegend = r.removeFromTop (20);
    auto row = r.removeFromTop (30);
    scanButton.setBounds (row.removeFromLeft (190));
    row.removeFromLeft (Theme::s2);
    retryButton.setBounds (row.removeFromLeft (190));
    r.removeFromTop (Theme::s1);
    scanTextArea = r.removeFromTop (36);
    row = r.removeFromTop (30);
    presetsButton.setBounds (row.removeFromLeft (190));
    row.removeFromLeft (Theme::s2);
    pluginsButton.setBounds (row.removeFromLeft (190));
    pluginProbe.setBounds (r.removeFromTop (28));
    r.removeFromTop (Theme::s4);

    networkLegend = r.removeFromTop (20);
    checkUpdates.setBounds (r.removeFromTop (28));
    row = r.removeFromTop (28);
    oscEnabled.setBounds (row.removeFromLeft (270));
    oscPort.setBounds (row.removeFromLeft (72).reduced (0, 2));
    oscNoteArea = r.removeFromTop (36);
    row = r.removeFromTop (28);
    wsEnabled.setBounds (row.removeFromLeft (270));
    wsPort.setBounds (row.removeFromLeft (72).reduced (0, 2));
    wsNoteArea = r.removeFromTop (48);
    midiEnabled.setBounds (r.removeFromTop (28));
    midiDeviceBox.setBounds (r.removeFromTop (30));
    r.removeFromTop (Theme::s1);
    row = r.removeFromTop (30);
    midiLearnTarget.setBounds (row.removeFromLeft (240));
    row.removeFromLeft (Theme::s2);
    midiLearnButton.setBounds (row.removeFromLeft (110));
    row.removeFromLeft (Theme::s2);
    midiClearButton.setBounds (row);
    midiTextArea = r.removeFromTop (64);
    logsButton.setBounds (r.removeFromTop (30).withWidth (190));
    r.removeFromTop (Theme::s4);

    aboutLegend = r.removeFromTop (20);
    aboutArea = r;
}
