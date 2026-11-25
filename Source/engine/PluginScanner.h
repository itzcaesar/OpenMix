#pragma once

#include <JuceHeader.h>

// Finds VST3 plugins (and, when the build enables it with OPENMIX_ENABLE_VST2,
// VST2 plugins too) without risking the mixer: every plugin file is opened in a separate
// OpenMix process ("OpenMix.exe --scan-plugin <file> <out.xml>"). If that process crashes or
// hangs, the file is put on a skip list and the mixer keeps running.
//
// Results are cached in %APPDATA%\OpenMix\plugin-cache.xml, so only new or changed plugins
// are scanned on the next start. Broadcasts a change message whenever progress changes.
class PluginScanner : public juce::ChangeBroadcaster,
                      private juce::Thread
{
public:
    explicit PluginScanner (juce::AudioPluginFormatManager&);
    ~PluginScanner() override;

    // Scans in the background. retrySkipped = also try plugins that failed before.
    void startScan (bool retrySkipped = false);

    bool isScanning() const noexcept               { return isThreadRunning(); }
    int getNumDone() const noexcept                { return numDone.load(); }
    int getNumToScan() const noexcept              { return numToScan.load(); }
    juce::String getCurrentPluginName() const;

    // Message thread
    juce::Array<juce::PluginDescription> getPlugins() const;   // sorted by maker, then name
    juce::StringArray getSkippedFiles() const;
    bool isBlacklisted (const juce::String& file) const;
    void blacklistFile (const juce::String& file);   // skip this file until "Retry skipped plugins"

    // Crash-safe loading probe (see below): runs the plugin's own start-up in
    // a child process and returns true only if it survived. A plugin that
    // crashes while loading is caught here, in the child, instead of killing
    // the mixer. Call from a background thread: it blocks for up to 30 s.
    enum class ProbeResult
    {
        ok,            // started fine in the test process
        crashed,       // crashed or failed to start: don't load it into the mixer
        timedOut,      // still busy after 30 s (a slow first start, a licence dialog...)
        unavailable    // the test process itself couldn't run: no verdict on the plugin
    };
    static ProbeResult probePluginFile (const juce::String& file, juce::String& error);

    // Entry point for the child process. Returns the process exit code.
    static int runChildScan (const juce::String& fileOrIdentifier, const juce::File& output);
    static constexpr const char* childFlag = "--scan-plugin";

    // Entry point for the probe child process: creates the plugin, prepares it
    // and runs one silent block. Returns 0 if it survived, 1 if it failed.
    static int runChildProbe (const juce::String& fileOrIdentifier);
    static constexpr const char* probeFlag = "--probe-plugin";

private:
    void run() override;
    bool scanOneInChildProcess (const juce::String& file, juce::String& error);
    void saveCache();
    // Formats to scan: always VST3, plus VST (VST2) when JUCE_PLUGINHOST_VST is on.
    // With the default build the VST format is not registered, so this returns VST3 only.
    juce::Array<juce::AudioPluginFormat*> formatsToScan() const;

    juce::AudioPluginFormatManager& formats;
    juce::KnownPluginList known;
    const juce::File cacheFile;

    std::atomic<bool> retry { false };
    std::atomic<int> numDone { 0 }, numToScan { 0 };
    mutable juce::CriticalSection nameLock;
    juce::CriticalSection cacheLock;   // saveCache() from two threads
    juce::String currentName;
};
