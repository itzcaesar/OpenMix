#pragma once

#include <JuceHeader.h>

// Asks GitHub for the latest OpenMix release (only when the user turned this on in Settings).
// Sends no personal data: a plain GET with a generic User-Agent. Calls onUpdateAvailable on the
// message thread when the release is newer than this build.
class UpdateChecker : private juce::Thread
{
public:
    UpdateChecker() : juce::Thread ("OpenMix update check") {}
    ~UpdateChecker() override { stopThread (10000); }

    std::function<void (juce::String version, juce::URL page)> onUpdateAvailable;

    void checkInBackground()
    {
        if (! isThreadRunning())
            startThread (juce::Thread::Priority::low);
    }

    // "v0.3.0" vs "0.2.1" -> true. Missing parts count as 0.
    static bool isNewer (const juce::String& candidate, const juce::String& current)
    {
        auto parts = [] (const juce::String& v)
        {
            juce::StringArray a;
            a.addTokens (v.trimCharactersAtStart ("vV").upToFirstOccurrenceOf ("-", false, false), ".", {});
            return a;
        };
        const auto a = parts (candidate), b = parts (current);
        for (int i = 0; i < juce::jmax (a.size(), b.size()); ++i)
        {
            const int x = a[i].getIntValue(), y = b[i].getIntValue();
            if (x != y)
                return x > y;
        }
        return false;
    }

private:
    void run() override
    {
        const juce::URL url ("https://api.github.com/repos/itzcaesar/OpenMix/releases/latest");
        auto stream = url.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                                 .withConnectionTimeoutMs (8000)
                                                 .withExtraHeaders ("User-Agent: OpenMix-update-check\r\nAccept: application/vnd.github+json"));
        if (stream == nullptr || threadShouldExit())
            return;

        const auto json = juce::JSON::parse (stream->readEntireStreamAsString());
        const auto tag = json["tag_name"].toString();
        const auto page = json["html_url"].toString();

        if (tag.isNotEmpty() && page.startsWith ("https://github.com/")
            && isNewer (tag, JUCE_APPLICATION_VERSION_STRING))
        {
            juce::MessageManager::callAsync ([this, tag, page]
            {
                if (onUpdateAvailable)
                    onUpdateAvailable (tag, juce::URL (page));
            });
        }
    }
};
