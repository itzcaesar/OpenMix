#include "PluginScanner.h"

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

juce::AudioPluginFormat* PluginScanner::vst3() const
{
    for (auto* f : formats.getFormats())
        if (f->getName() == "VST3")
            return f;
    return nullptr;
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
    auto* format = vst3();
    if (format == nullptr)
        return;

    if (retry.load())
        known.clearBlacklistedFiles();

    const auto files = format->searchPathsForPlugins (format->getDefaultLocationsToSearch(), true);

    // Forget plugins whose files are gone.
    for (const auto& type : known.getTypes())
        if (! files.contains (type.fileOrIdentifier))
            known.removeType (type);

    juce::StringArray toScan;
    const auto skipped = known.getBlacklistedFiles();
    for (const auto& file : files)
        if (! skipped.contains (file) && ! known.isListingUpToDate (file, *format))
            toScan.add (file);

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

    for (auto* format : fm.getFormats())
    {
        if (format->getName() != "VST3")
            continue;

        juce::OwnedArray<juce::PluginDescription> results;
        format->findAllTypesForFile (results, fileOrIdentifier);
        if (results.isEmpty())
            return 2;

        juce::XmlElement root ("SCAN");
        for (auto* desc : results)
            root.addChildElement (desc->createXml().release());

        return root.writeTo (output) ? 0 : 3;
    }

    return 4;
}
