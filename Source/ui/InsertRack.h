#pragma once

#include <JuceHeader.h>
#include "engine/MixerEngine.h"
#include "ui/Widgets.h"

// A plugin's own editor in its own window.
class PluginWindow : public juce::DocumentWindow
{
public:
    PluginWindow (juce::AudioPluginInstance&, std::function<void()> onCloseFn);
    ~PluginWindow() override;
    void closeButtonPressed() override;

private:
    std::function<void()> onClose;
};

// The insert list of a strip or bus: one row per plugin plus an "+ Add plugin" row, with
// loading, bypass, reorder (drag or Alt+arrows), wet/dry, A/B compare, presets and editors.
// Scrolls when it has more rows than the owner gives it room for.
class InsertRack : public juce::Component
{
public:
    InsertRack (MixerEngine&, PluginChain&);
    ~InsertRack() override;

    void resized() override;

    // Rows to show: the plugins, a "Loading..." row, and one empty "+ Add plugin" row.
    int getVisibleRowCount() const;
    static int heightForRows (int rows) { return juce::jmax (rows, 1) * (rowHeight + rowGap) - rowGap; }

    void refresh();     // re-read the chain (after a session load, OSC, etc.)
    void tick();        // ~30 Hz: notices auto-stopped plugins and latency changes
    void closeAllEditors() { editorWindows.clear(); abStates.clear(); }

    // The number of visible rows changed: the owner should re-run its layout.
    std::function<void()> onRowCountChanged;

    static constexpr int rowHeight = 26, rowGap = 3;

private:
    class List;

    void updateRows();
    void slotMain (int slot);
    void showSlotMenu (int slot);
    void showPluginMenu (int slot);
    void toggleBypass (int slot);
    void removeSlot (int slot);
    void moveSlot (int from, int to);
    void loadPluginAsync (int slot, const juce::PluginDescription&, std::function<void (juce::AudioPluginInstance&)> then = {});
    void startAsyncLoad (const juce::PluginDescription&, std::function<void (juce::AudioPluginInstance&)> then);
    void savePreset (int slot);
    void loadPreset (int slot);
    void openEditor (juce::AudioPluginInstance*);
    void closeEditor (juce::AudioPluginInstance*);
    void openMixPanel (int slot);

    MixerEngine& engine;
    PluginChain& chain;

    juce::Viewport view;
    std::unique_ptr<List> list;

    int loadingSlot = -1;
    juce::String loadingName;
    int lastFaultCount = 0, lastLatency = -1, lastRowCount = -1, tickCount = 0;
    int dragFrom = -1, dropIndex = -1;

    std::map<juce::AudioPluginInstance*, std::unique_ptr<PluginWindow>> editorWindows;

    // A/B compare: two snapshots of a plugin's settings (kept while OpenMix runs).
    struct AbState { juce::MemoryBlock a, b; bool onB = false; };
    std::map<juce::AudioPluginInstance*, AbState> abStates;
    void abSwitch (juce::AudioPluginInstance*, bool toB);

    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InsertRack)
};
