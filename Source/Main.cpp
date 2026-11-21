#include <JuceHeader.h>
#include "MainComponent.h"
#include "Theme.h"
#include "OpenMixLookAndFeel.h"
#include "PluginScanner.h"
#include "AppSettings.h"
#include "ui/TrayIcon.h"

static bool isScanChild()
{
    return juce::JUCEApplicationBase::getCommandLineParameters().contains (PluginScanner::childFlag);
}

class OpenMixApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }

    // The plugin-scan child runs next to the main app, so it must not be single-instance.
    bool moreThanOneInstanceAllowed() override          { return isScanChild(); }

    void initialise (const juce::String& commandLine) override
    {
        if (isScanChild())
        {
            // OpenMix.exe --scan-plugin "<plugin file>" "<output xml>"
            const auto args = juce::StringArray::fromTokens (commandLine, true);
            const int i = args.indexOf (PluginScanner::childFlag);
            const auto file = args[i + 1].unquoted();
            const auto output = juce::File (args[i + 2].unquoted());
            setApplicationReturnValue (PluginScanner::runChildScan (file, output));
            quit();
            return;
        }

        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

        tray = std::make_unique<TrayIcon>();
        tray->onShow = [this] { showWindow(); };
        tray->onQuit = [this] { systemRequestedQuit(); };

        // Started by Windows at login (--minimized) or set to start in the tray: stay hidden.
        const bool startHidden = commandLine.contains (AppSettings::minimizedFlag) || AppSettings::get().startMinimized();
        mainWindow = std::make_unique<MainWindow> (getApplicationName(), ! startHidden, [this] { windowClosed(); });
    }

    void shutdown() override
    {
        mainWindow.reset();
        tray.reset();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override { quit(); }

    // A second launch (e.g. from the Start menu while OpenMix sits in the tray) brings it up.
    void anotherInstanceStarted (const juce::String&) override { showWindow(); }

    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow (const juce::String& name, bool visible, std::function<void()> onCloseFn)
            : DocumentWindow (name, Theme::p().window, DocumentWindow::allButtons),
              onClose (std::move (onCloseFn))
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent(), true);
            setResizable (true, true);

            // Smallest size that still fits a 1366x768 laptop (taskbar and title bar included).
            setResizeLimits (960, 600, 8000, 5000);

            const auto display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
            const auto area = display != nullptr ? display->userBounds.toNearestInt() : juce::Rectangle<int> (1366, 728);
            centreWithSize (juce::jmin (1320, area.getWidth() - 40), juce::jmin (900, area.getHeight() - 40));
            setVisible (visible);
        }

        void closeButtonPressed() override
        {
            if (onClose) onClose();
        }

    private:
        std::function<void()> onClose;
    };

private:
    void windowClosed()
    {
        if (AppSettings::get().closeToTray() && tray != nullptr)
        {
            mainWindow->setVisible (false);
            tray->tellStillRunning();
        }
        else
        {
            systemRequestedQuit();
        }
    }

    void showWindow()
    {
        if (mainWindow == nullptr)
            return;

        mainWindow->setVisible (true);
        mainWindow->setMinimised (false);
        mainWindow->toFront (true);

        // Started hidden: the window never took focus, so give it focus now (for Tab).
        if (auto* content = mainWindow->getContentComponent(); content != nullptr && ! content->hasKeyboardFocus (true))
            content->grabKeyboardFocus();
    }

    OpenMixLookAndFeel lookAndFeel;
    std::unique_ptr<TrayIcon> tray;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (OpenMixApplication)
