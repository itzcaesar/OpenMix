#include "engine/PluginScanner.h"

static constexpr int childTimeoutMs = 30000;   // a plugin that takes longer than this to open is skipped

PluginScanner::PluginScanner (juce::AudioPluginFormatManager& f)
    : juce::Thread ("OpenMix plugin scanner"),
      formats (f),
      cacheFile (juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                     .getChildFile ("OpenMix").getChildFile ("plugin-cache.xml"))
{
    if (auto xml = juce::parseXML (cacheFile))
        known.recreateFromXml (*xml);
}

PluginScanner::~PluginScanner()
{
    stopThread (childTimeoutMs + 2000);
}

juce::Array<juce::AudioPluginFormat*> PluginScanner::formatsToScan() const
{
    juce::Array<juce::AudioPluginFormat*> result;
    for (auto* f : formats.getFormats())
        if (f->getName() == "VST3" || f->getName() == "VST")   // "VST" is JUCE's VST2 format
            result.add (f);
    return result;
}

void PluginScanner::startScan (bool retrySkipped)
{
    if (isThreadRunning())
        return;

    retry = retrySkipped;
    numDone = 0;
    numToScan = 0;
    startThread (juce::Thread::Priority::low);
    sendChangeMessage();
}

juce::String PluginScanner::getCurrentPluginName() const
{
    const juce::ScopedLock sl (nameLock);
    return currentName;
}

juce::Array<juce::PluginDescription> PluginScanner::getPlugins() const
{
    auto types = known.getTypes();
    std::sort (types.begin(), types.end(), [] (const juce::PluginDescription& a, const juce::PluginDescription& b)
    {
        const int m = a.manufacturerName.compareIgnoreCase (b.manufacturerName);
        return m != 0 ? m < 0 : a.name.compareIgnoreCase (b.name) < 0;
    });
    return types;
}

juce::StringArray PluginScanner::getSkippedFiles() const
{
    return known.getBlacklistedFiles();
}

void PluginScanner::run()
{
    const auto scanFormats = formatsToScan();
    if (scanFormats.isEmpty())
        return;

    if (retry.load())
        known.clearBlacklistedFiles();

    // Collect candidate files from every enabled format (VST3 .vst3 files,
    // VST2 .dll files when enabled). A file is only scanned once even if two
    // formats list it.
    juce::StringArray allFiles;
    for (auto* format : scanFormats)
        for (const auto& file : format->searchPathsForPlugins (format->getDefaultLocationsToSearch(), true))
            allFiles.addIfNotAlreadyThere (file);

    // Forget plugins whose files are gone.
    for (const auto& type : known.getTypes())
        if (! allFiles.contains (type.fileOrIdentifier))
            known.removeType (type);

    juce::StringArray toScan;
    const auto skipped = known.getBlacklistedFiles();
    for (const auto& file : allFiles)
    {
        if (skipped.contains (file))
            continue;
        // Only scan if some format thinks this file is out of date. A VST2 .dll
        // is not a VST3 file (and vice versa), so check each format in turn.
        for (auto* format : scanFormats)
        {
            if (! known.isListingUpToDate (file, *format))
            {
                toScan.add (file);
                break;
            }
        }
    }

    numToScan = toScan.size();
    sendChangeMessage();

    for (const auto& file : toScan)
    {
        if (threadShouldExit())
            break;

        {
            const juce::ScopedLock sl (nameLock);
            currentName = juce::File (file).getFileNameWithoutExtension();
        }
        sendChangeMessage();

        juce::String error;
        if (! scanOneInChildProcess (file, error))
            known.addToBlacklist (file);

        ++numDone;
        sendChangeMessage();
    }

    {
        const juce::ScopedLock sl (nameLock);
        currentName.clear();
    }

    saveCache();
    sendChangeMessage();
}

bool PluginScanner::scanOneInChildProcess (const juce::String& file, juce::String& error)
{
    const auto output = juce::File::createTempFile (".xml");
    const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);

    juce::ChildProcess child;
    if (! child.start (juce::StringArray { exe.getFullPathName(), childFlag, file, output.getFullPathName() }, 0))
    {
        error = "couldn't start the scanner process";
        return false;
    }

    // Poll so the app can shut down quickly mid-scan.
    for (int waited = 0; child.isRunning(); waited += 100)
    {
        if (threadShouldExit() || waited > childTimeoutMs)
        {
            child.kill();
            output.deleteFile();
            error = "timed out";
            return false;
        }
        juce::Thread::sleep (100);
    }

    const auto exitCode = child.getExitCode();
    auto xml = juce::parseXML (output);
    output.deleteFile();

    if (exitCode != 0 || xml == nullptr)
    {
        error = "crashed or isn't a valid plugin (exit code " + juce::String (exitCode) + ")";
        return false;
    }

    int found = 0;
    for (auto* e : xml->getChildIterator())
    {
        juce::PluginDescription desc;
        if (desc.loadFromXml (*e))
        {
            known.addType (desc);
            ++found;
        }
    }

    return found > 0;
}

void PluginScanner::saveCache()
{
    if (auto xml = known.createXml())
    {
        cacheFile.getParentDirectory().createDirectory();
        xml->writeTo (cacheFile);
    }
}

int PluginScanner::runChildScan (const juce::String& fileOrIdentifier, const juce::File& output)
{
    juce::AudioPluginFormatManager fm;
    juce::addDefaultFormatsToManager (fm);

    // Try every registered format in turn. A .vst3 file yields nothing from the
    // VST2 format (and a .dll nothing from VST3), so the first format that
    // recognises the file wins. With the default build only VST3 is registered.
    for (auto* format : fm.getFormats())
    {
        if (format->getName() != "VST3" && format->getName() != "VST")
            continue;

        juce::OwnedArray<juce::PluginDescription> results;
        format->findAllTypesForFile (results, fileOrIdentifier);
        if (results.isEmpty())
            continue;

        juce::XmlElement root ("SCAN");
        for (auto* desc : results)
            root.addChildElement (desc->createXml().release());

        return root.writeTo (output) ? 0 : 3;
    }

    return 2;   // no enabled format recognised this file
}
