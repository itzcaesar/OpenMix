#pragma once

#include <JuceHeader.h>
#include "app/GlobalHotkeys.h"

// The Hotkeys window: the list of global hotkeys, with "+ Add hotkey" (pick an action, then
// press the keys), change keys, and remove.
class HotkeysPanel : public juce::Component
{
public:
    HotkeysPanel (GlobalHotkeys&, juce::StringArray stripNames, juce::Array<int> activeStrips, juce::StringArray sceneNames);
    ~HotkeysPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Row;
    void rebuild();
    void showAddMenu();
    void capture (GlobalHotkeys::Binding, int replaceIndex);   // -1 = add
    void setNote (const juce::String&, bool problem = false);
    juce::String actionText (const GlobalHotkeys::Binding&) const;

    GlobalHotkeys& hotkeys;
    juce::StringArray stripNames, sceneNames;
    juce::Array<int> activeStrips;

    juce::Viewport view;
    juce::Component list;
    std::vector<std::unique_ptr<Row>> rows;
    juce::TextButton addButton { "+ Add hotkey" };
    juce::String note;
    bool noteIsProblem = false;
    bool capturing = false;
    juce::Rectangle<int> helpArea, noteArea, emptyArea;
};
