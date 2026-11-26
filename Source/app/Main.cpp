#include <JuceHeader.h>
#include "app/MainComponent.h"
#include "ui/Theme.h"
#include "ui/OpenMixLookAndFeel.h"
#include "engine/PluginScanner.h"
#include "engine/PluginSandbox.h"
#include "app/AppSettings.h"
#include "ui/TrayIcon.h"

// Windows 11 rounded window corners (DWM). Declared here to keep <windows.h> out of this file.
#pragma comment (lib, "dwmapi.lib")
extern "C" __declspec (dllimport) long __stdcall DwmSetWindowAttribute (void* hwnd, unsigned long attribute, const void* value, unsigned long size);

static bool isScanChild()
{
    return juce::JUCEApplicationBase::getCommandLineParameters().contains (PluginScanner::childFlag);
}

static bool isProbeChild()
{
    return juce::JUCEApplicationBase::getCommandLineParameters().contains (PluginScanner::probeFlag);
}

static bool isPluginAudioWorker()
{
    return juce::JUCEApplicationBase::getCommandLineParameters().contains (PluginSandbox::workerFlag);
}

// If OpenMix crashes, leave a stack trace next to the session so the cause can be found.
static void writeCrashReport (void*)
{
    const auto file = AppSettings::getLogFolder()
                          .getChildFile ("crash-" + juce::Time::getCurrentTime().formatted ("%Y-%m-%d_%H-%M-%S") + ".txt");
    file.getParentDirectory().createDirectory();
    file.replaceWithText ("OpenMix " + juce::String (JUCE_APPLICATION_VERSION_STRING) + " crashed.\n\n"
                          + juce::SystemStats::getStackBacktrace());
}

class OpenMixApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }

    // Scanner, probe and audio worker children run next to the main app.
    bool moreThanOneInstanceAllowed() override          { return isScanChild() || isProbeChild() || isPluginAudioWorker(); }

    void initialise (const juce::String& commandLine) override
    {
        if (isPluginAudioWorker())
        {
            const auto args = juce::StringArray::fromTokens (commandLine, true);
            const int i = args.indexOf (PluginSandbox::workerFlag);
            if (i >= 0 && i + 4 < args.size())
            {
                sandboxWorker = std::make_unique<PluginSandboxWorkerThread> (
                    args[i + 1].unquoted(), args[i + 2].unquoted(), juce::File (args[i + 3].unquoted()),
                    args[i + 4].getIntValue());
                sandboxWorker->startThread (juce::Thread::Priority::high);
            }
            else
            {
                setApplicationReturnValue (2);
                quit();
            }
            return;
        }

        if (isProbeChild())
        {
            // OpenMix.exe --probe-plugin "<plugin file>"
            const auto args = juce::StringArray::fromTokens (commandLine, true);
            const int i = args.indexOf (PluginScanner::probeFlag);
            setApplicationReturnValue (PluginScanner::runChildProbe (args[i + 1].unquoted()));
            quit();
            return;
        }

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

        juce::SystemStats::setApplicationCrashHandler (writeCrashReport);
        logger.reset (juce::FileLogger::createDefaultAppLogger ("OpenMix", "openmix.log",
                                                                "OpenMix " + getApplicationVersion() + " started"));
        juce::Logger::setCurrentLogger (logger.get());

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
        if (sandboxWorker != nullptr)
        {
            sandboxWorker->signalThreadShouldExit();
            sandboxWorker->stopThread (2000);
            sandboxWorker.reset();
        }
        mainWindow.reset();
        tray.reset();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        juce::Logger::setCurrentLogger (nullptr);
        logger.reset();
    }

    void systemRequestedQuit() override { quit(); }

    // A second launch (e.g. from the Start menu while OpenMix sits in the tray) brings it up.
    void anotherInstanceStarted (const juce::String&) override { showWindow(); }

    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow (const juce::String& name, bool visible, std::function<void()> onCloseFn)
            : DocumentWindow (name, Theme::p().window, 0),
              onClose (std::move (onCloseFn))
        {
            // No title bar row: OpenMix's own header carries the name, dragging and the
            // window buttons (see MainComponent).
            setUsingNativeTitleBar (false);
            setTitleBarHeight (0);
            setIcon (TrayIcon::makeIcon (32));
            setContentOwned (new MainComponent(), true);
            setResizable (true, false);

            // Smallest size that still fits a 1366x768 laptop (taskbar and title bar included).
            setResizeLimits (960, 600, 8000, 5000);

            const auto display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
            const auto area = display != nullptr ? display->userBounds.toNearestInt() : juce::Rectangle<int> (1366, 728);
            centreWithSize (juce::jmin (1480, area.getWidth() - 40), juce::jmin (940, area.getHeight() - 40));
            setVisible (visible);

            if (auto* peer = getPeer())
            {
                // OpenMix draws its own title bar, so ask Windows 11 for its rounded corners
                // (older Windows ignores this).
                const int roundCorners = 2;   // DWMWCP_ROUND
                DwmSetWindowAttribute (peer->getNativeHandle(), 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &roundCorners, sizeof (roundCorners));

                const auto engines = peer->getAvailableRenderingEngines();
                juce::Logger::writeToLog ("Renderer: " + engines[peer->getCurrentRenderingEngine()] + " (of " + engines.joinIntoString (", ") + ")");
            }
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
    std::unique_ptr<juce::FileLogger> logger;
    std::unique_ptr<TrayIcon> tray;
    std::unique_ptr<MainWindow> mainWindow;
    std::unique_ptr<PluginSandboxWorkerThread> sandboxWorker;
};

START_JUCE_APPLICATION (OpenMixApplication)
