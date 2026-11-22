#include "ui/SettingsPanel.h"
#include "ui/Widgets.h"
#include "app/AppSettings.h"
#include "ui/Theme.h"

SettingsPanel::SettingsPanel (MixerEngine& e, std::function<void()> themeFn, std::function<void()> oscFn,
                              std::function<void()> hotkeysFn)
    : engine (e), toggleTheme (std::move (themeFn)), oscChanged (std::move (oscFn)), openHotkeys (std::move (hotkeysFn))
{
    hotkeysButton.setTooltip ("Global hotkeys for mute, push to talk, the soundboard, recording and scenes.");
    hotkeysButton.onClick = [this] { if (openHotkeys) openHotkeys(); };
    makeClickable (hotkeysButton);
    addAndMakeVisible (hotkeysButton);

    setFocusContainerType (FocusContainerType::keyboardFocusContainer);
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
        addAndMakeVisible (t);
    }

    auto themeLabel = [] { return Theme::isDark() ? juce::String ("Switch to light mode") : juce::String ("Switch to dark mode"); };
    themeButton.setButtonText (themeLabel());
    themeButton.onClick = [this, themeLabel]
    {
        if (toggleTheme) toggleTheme();
        themeButton.setButtonText (themeLabel());
        repaint();
    };

    scanButton.onClick    = [this] { engine.getScanner().startScan (false); updateScanText(); };
    retryButton.onClick   = [this] { engine.getScanner().startScan (true);  updateScanText(); };
    presetsButton.onClick = []
    {
        auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("OpenMix Presets");
        dir.createDirectory();
        dir.startAsProcess();
    };
    pluginsButton.onClick = [] { juce::File ("C:\\Program Files\\Common Files\\VST3").startAsProcess(); };

    for (auto* b : { &themeButton, &scanButton, &retryButton, &presetsButton, &pluginsButton })
    {
        makeClickable (*b);
        addAndMakeVisible (b);
    }

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

    logsButton.onClick = [] { auto dir = AppSettings::getLogFolder(); dir.createDirectory(); dir.startAsProcess(); };

    for (auto* t : { &checkUpdates, &oscEnabled })
    {
        makeClickable (*t);
        addAndMakeVisible (t);
    }
    addAndMakeVisible (oscPort);
    makeClickable (logsButton);
    addAndMakeVisible (logsButton);

    engine.getScanner().addChangeListener (this);
    updateScanText();
    setSize (540, 640);
}

SettingsPanel::~SettingsPanel()
{
    engine.getScanner().removeChangeListener (this);
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
    repaint (scanTextArea);
}

void SettingsPanel::paint (juce::Graphics& g)
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
    g.drawFittedText (scanText, scanTextArea, juce::Justification::centredLeft, 2);

    g.drawFittedText ("While OSC is on, any device on your network can move faders and start recordings. "
                      "Addresses start with /openmix (see the README).", oscNoteArea, juce::Justification::topLeft, 2);

    g.drawFittedText ("OpenMix " + juce::JUCEApplication::getInstance()->getApplicationVersion()
                        + ". Licensed under the GNU AGPLv3. Built with JUCE (AGPLv3) and RNNoise (BSD).",
                      aboutArea, juce::Justification::topLeft, 3);
}

void SettingsPanel::resized()
{
    auto r = getLocalBounds().reduced (Theme::s5, Theme::s4);

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
    r.removeFromTop (Theme::s4);

    networkLegend = r.removeFromTop (20);
    checkUpdates.setBounds (r.removeFromTop (28));
    row = r.removeFromTop (28);
    oscEnabled.setBounds (row.removeFromLeft (270));
    oscPort.setBounds (row.removeFromLeft (72).reduced (0, 2));
    oscNoteArea = r.removeFromTop (36);
    logsButton.setBounds (r.removeFromTop (30).withWidth (190));
    r.removeFromTop (Theme::s4);

    aboutLegend = r.removeFromTop (20);
    aboutArea = r;
}
