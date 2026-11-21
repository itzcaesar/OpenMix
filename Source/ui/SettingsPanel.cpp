#include "SettingsPanel.h"
#include "Widgets.h"
#include "../AppSettings.h"
#include "../Theme.h"

SettingsPanel::SettingsPanel (MixerEngine& e, std::function<void()> themeFn)
    : engine (e), toggleTheme (std::move (themeFn))
{
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

    engine.getScanner().addChangeListener (this);
    updateScanText();
    setSize (520, 470);
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
    Theme::drawLegend (g, "Look", themeLegend);
    Theme::drawLegend (g, "Plugins", pluginsLegend);
    Theme::drawLegend (g, "About", aboutLegend);

    g.setFont (Theme::ui (13.0f));
    g.setColour (t.textDim);
    g.drawFittedText (scanText, scanTextArea, juce::Justification::centredLeft, 2);

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
    themeButton.setBounds (r.removeFromTop (30).withWidth (190));
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

    aboutLegend = r.removeFromTop (20);
    aboutArea = r;
}
