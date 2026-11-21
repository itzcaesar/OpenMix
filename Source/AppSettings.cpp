#include "AppSettings.h"

static const juce::String runKey = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run\\OpenMix";

AppSettings& AppSettings::get()
{
    static AppSettings instance;
    return instance;
}

AppSettings::AppSettings()
{
    juce::PropertiesFile::Options o;
    o.applicationName = "settings";
    o.folderName = "OpenMix";
    o.filenameSuffix = ".xml";
    o.osxLibrarySubFolder = "Application Support";
    o.storageFormat = juce::PropertiesFile::storeAsXML;
    props = std::make_unique<juce::PropertiesFile> (o);
}

bool AppSettings::startWithWindows() const
{
   #if JUCE_WINDOWS
    return juce::WindowsRegistry::valueExists (runKey);
   #else
    return false;
   #endif
}

void AppSettings::setStartWithWindows (bool shouldStart)
{
   #if JUCE_WINDOWS
    if (shouldStart)
    {
        const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName();
        juce::WindowsRegistry::setValue (runKey, exe.quoted() + " " + minimizedFlag);
    }
    else
    {
        juce::WindowsRegistry::deleteValue (runKey);
    }
   #else
    juce::ignoreUnused (shouldStart);
   #endif
}
