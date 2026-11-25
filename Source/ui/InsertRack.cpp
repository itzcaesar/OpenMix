#include "ui/InsertRack.h"
#include "ui/Panels.h"
#include "ui/Theme.h"
#include "app/AppSettings.h"
#include <thread>

//==============================================================================
// PluginWindow

PluginWindow::PluginWindow (juce::AudioPluginInstance& plugin, std::function<void()> onCloseFn)
    : DocumentWindow (plugin.getName(), Theme::p().window, DocumentWindow::closeButton),
      onClose (std::move (onCloseFn))
{
    setUsingNativeTitleBar (false);
    setTitleBarHeight (26);
    setTitleBarTextCentred (false);

    if (auto* editor = plugin.createEditorAndMakeActive())
    {
        setContentOwned (editor, true);
        setResizable (editor->isResizable(), false);
    }
    else
    {
        auto* generic = new juce::GenericAudioProcessorEditor (plugin);
        generic->setSize (420, 360);
        setContentOwned (generic, true);
        setResizable (true, false);
    }

    centreWithSize (getWidth(), getHeight());
    setVisible (true);
}

PluginWindow::~PluginWindow()
{
    clearContentComponent();
}

void PluginWindow::closeButtonPressed()
{
    // Defer so the window isn't deleted from inside its own callback.
    juce::MessageManager::callAsync ([fn = onClose] { if (fn) fn(); });
}

//==============================================================================
// The slot rows plus a drop indicator while dragging.

class InsertRack::List : public juce::Component
{
public:
    std::array<PluginSlotRow, kNumSlots> rows;
    int dropIndex = -1;

    List()
    {
        for (auto& r : rows)
            addChildComponent (r);
    }

    void layoutRows (int visible)
    {
        for (int i = 0; i < kNumSlots; ++i)
        {
            rows[(size_t) i].setVisible (i < visible);
            rows[(size_t) i].setBounds (0, i * (rowHeight() + rowGap()), getWidth(), rowHeight());
        }
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        if (dropIndex < 0)
            return;

        const int y = dropIndex < kNumSlots && rows[(size_t) dropIndex].isVisible()
                        ? rows[(size_t) dropIndex].getY() - 2
                        : rows[0].getBottom();
        g.setColour (Theme::p().focus);
        g.fillRect (0, juce::jmax (0, y - 1), getWidth(), 2);
    }
};

//==============================================================================

InsertRack::InsertRack (MixerEngine& e, PluginChain& c)
    : engine (e), chain (c), list (std::make_unique<List>())
{
    for (int s = 0; s < kNumSlots; ++s)
    {
        auto& row = list->rows[(size_t) s];
        row.onMain   = [this, s] { slotMain (s); };
        row.onMenu   = [this, s] { showSlotMenu (s); };
        row.onBypass = [this, s] { toggleBypass (s); };
        row.onRemove = [this, s] { removeSlot (s); };
        row.onMove   = [this, s] (int delta) { moveSlot (s, s + delta); };
        row.onDragTo = [this, s] (int screenY)
        {
            dragFrom = s;
            const int localY = screenY - list->getScreenY();
            dropIndex = juce::jlimit (0, juce::jmax (0, chain.getNumPlugins() - 1), localY / (rowHeight() + rowGap()));
            list->dropIndex = dropIndex == s ? -1 : dropIndex;
            list->repaint();
        };
        row.onDragEnd = [this]
        {
            if (dragFrom >= 0 && dropIndex >= 0)
                moveSlot (dragFrom, dropIndex);
            dragFrom = dropIndex = -1;
            list->dropIndex = -1;
            list->repaint();
        };
    }

    view.setViewedComponent (list.get(), false);
    view.setScrollBarsShown (true, false);
    view.setScrollBarThickness (6);
    addAndMakeVisible (view);

    updateRows();
}

InsertRack::~InsertRack()
{
    editorWindows.clear();
}

void InsertRack::resized()
{
    view.setBounds (getLocalBounds());
    updateRows();
}

int InsertRack::getVisibleRowCount() const
{
    const int n = chain.getNumPlugins();
    const bool loadingNew = loadingSlot >= n;
    return juce::jmin (kNumSlots, n + (loadingNew ? 1 : 0) + 1);
}

void InsertRack::refresh()
{
    updateRows();
}

void InsertRack::tick()
{
    // Plugins auto-stopped by the audio thread, and latency changes, show up here.
    const int faults = chain.faultCount.load();
    const int latency = chain.latencySamples.load();
    if (faults != lastFaultCount || (++tickCount % 15 == 0 && latency != lastLatency))
    {
        lastFaultCount = faults;
        lastLatency = latency;
        updateRows();
    }
}

void InsertRack::updateRows()
{
    const int n = chain.getNumPlugins();
    const double sr = engine.getSampleRate();

    for (int s = 0; s < kNumSlots; ++s)
    {
        auto& row = list->rows[(size_t) s];
        if (s == loadingSlot)
            row.setState (PluginSlotRow::State::Loading, loadingName, false, false, 0, sr);
        else if (s < n)
        {
            if (auto* p = chain.getPlugin (s))
            {
                const auto ab = abStates.find (p);
                row.setState (PluginSlotRow::State::Loaded, p->getName(), chain.slotBypass[(size_t) s].load(),
                              chain.slotFault[(size_t) s].load(), p->getLatencySamples(), sr,
                              juce::roundToInt (chain.slotMix[(size_t) s].load() * 100.0f),
                              ab == abStates.end() ? juce::String() : juce::String (ab->second.onB ? "B" : "A"));
            }
        }
        else
            row.setState (PluginSlotRow::State::Empty, {}, false, false, 0, sr);
    }

    const int visible = getVisibleRowCount();
    const int listH = heightForRows (visible);
    const bool needsScroll = listH > getHeight() && getHeight() > 0;
    const int listW = juce::jmax (1, getWidth() - (needsScroll ? view.getScrollBarThickness() : 0));
    list->setSize (listW, listH);
    list->layoutRows (visible);

    // The owner sizes the rack: tell it when the number of rows changed.
    if (visible != lastRowCount)
    {
        lastRowCount = visible;
        if (onRowCountChanged)
            onRowCountChanged();
    }
}

void InsertRack::slotMain (int slot)
{
    if (slot == loadingSlot)
        return;

    if (auto* p = chain.getPlugin (slot))
        openEditor (p);
    else
        showPluginMenu (slot);
}

void InsertRack::toggleBypass (int slot)
{
    chain.slotFault[(size_t) slot].store (false);
    chain.slotBypass[(size_t) slot].store (! chain.slotBypass[(size_t) slot].load());
    updateRows();
}

void InsertRack::removeSlot (int slot)
{
    if (auto* p = chain.getPlugin (slot))
    {
        closeEditor (p);
        abStates.erase (p);
    }
    chain.removePlugin (slot);
    updateRows();

    // Keep keyboard focus in the list.
    list->rows[(size_t) juce::jmin (slot, chain.getNumPlugins())].grabKeyboardFocus();
}

void InsertRack::moveSlot (int from, int to)
{
    const int n = chain.getNumPlugins();
    if (! juce::isPositiveAndBelow (from, n) || ! juce::isPositiveAndBelow (to, n) || from == to)
        return;

    chain.movePlugin (from, to);
    updateRows();
    list->rows[(size_t) to].grabKeyboardFocus();
}

void InsertRack::showSlotMenu (int slot)
{
    const int n = chain.getNumPlugins();
    auto* p = chain.getPlugin (slot);
    if (p == nullptr)
        return;

    const bool faulted = chain.slotFault[(size_t) slot].load();

    juce::PopupMenu menu;
    menu.addItem ("Open window", [this, p] { openEditor (p); });
    menu.addItem (faulted ? "Try again" : chain.slotBypass[(size_t) slot].load() ? "Turn on" : "Bypass",
                  [this, slot] { toggleBypass (slot); });
    menu.addSeparator();
    menu.addItem ("Move up", slot > 0, false, [this, slot] { moveSlot (slot, slot - 1); });
    menu.addItem ("Move down", slot < n - 1, false, [this, slot] { moveSlot (slot, slot + 1); });
    menu.addSeparator();
    menu.addItem ("Wet/dry mix...", [this, slot] { openMixPanel (slot); });

    juce::PopupMenu ab;
    if (auto it = abStates.find (p); it == abStates.end())
    {
        ab.addItem ("Start comparing (A and B = current settings)", [this, p]
        {
            juce::MemoryBlock state;
            p->getStateInformation (state);
            abStates[p] = { state, state, false };
            updateRows();
        });
    }
    else
    {
        const bool onB = it->second.onB;
        ab.addItem ("Listen to A", true, ! onB, [this, p] { abSwitch (p, false); });
        ab.addItem ("Listen to B", true, onB, [this, p] { abSwitch (p, true); });
        ab.addSeparator();
        ab.addItem (onB ? "Copy B to A" : "Copy A to B", [this, p]
        {
            auto& st = abStates[p];
            juce::MemoryBlock current;
            p->getStateInformation (current);
            st.a = current;
            st.b = current;
        });
        ab.addItem ("Stop comparing (keep what you hear)", [this, p]
        {
            abStates.erase (p);
            updateRows();
        });
    }
    menu.addSubMenu ("A/B compare", ab);
    menu.addSeparator();
    menu.addItem ("Save preset...", [this, slot] { savePreset (slot); });
    menu.addItem ("Load preset...", [this, slot] { loadPreset (slot); });
    menu.addItem ("Replace with...", [this, slot] { showPluginMenu (slot); });
    menu.addSeparator();
    menu.addItem ("Remove", [this, slot] { removeSlot (slot); });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (list->rows[(size_t) slot]));
}

void InsertRack::showPluginMenu (int slot)
{
    auto& scanner = engine.getScanner();
    const auto plugins = scanner.getPlugins();

    juce::PopupMenu menu;

    if (scanner.isScanning())
    {
        const auto current = scanner.getCurrentPluginName();
        menu.addItem ("Scanning plugins: " + juce::String (scanner.getNumDone()) + " of " + juce::String (scanner.getNumToScan())
                        + (current.isNotEmpty() ? " (" + current + ")" : juce::String()), false, false, nullptr);
        menu.addSeparator();
    }

    if (plugins.isEmpty() && ! scanner.isScanning())
#if OPENMIX_VST2_ENABLED
        menu.addItem ("No plugins found in the VST3 folder or the VST2 folders", false, false, nullptr);
#else
        menu.addItem ("No VST3 plugins found in C:\\Program Files\\Common Files\\VST3", false, false, nullptr);
#endif

    auto addPlugin = [this, slot] (juce::PopupMenu& m, const juce::PluginDescription& d, const juce::String& label)
    {
        m.addItem (label, [this, slot, d] { loadPluginAsync (slot, d); });
    };

    if (plugins.size() > 14)
    {
        // Group by maker once the list gets long.
        juce::String maker;
        juce::PopupMenu sub;
        for (const auto& d : plugins)
        {
            const auto m = d.manufacturerName.isNotEmpty() ? d.manufacturerName : juce::String ("Other");
            if (m != maker && maker.isNotEmpty())
            {
                menu.addSubMenu (maker, sub);
                sub = {};
            }
            maker = m;
            addPlugin (sub, d, d.name);
        }
        if (maker.isNotEmpty())
            menu.addSubMenu (maker, sub);
    }
    else
    {
        for (const auto& d : plugins)
            addPlugin (menu, d, d.name + (d.manufacturerName.isNotEmpty() ? "  (" + d.manufacturerName + ")" : juce::String()));
    }

    menu.addSeparator();
    menu.addItem ("Scan for new plugins", ! scanner.isScanning(), false, [this] { engine.getScanner().startScan (false); });

    const auto skipped = scanner.getSkippedFiles();
    if (! skipped.isEmpty())
    {
        juce::PopupMenu skippedMenu;
        for (const auto& f : skipped)
            skippedMenu.addItem (juce::File (f).getFileNameWithoutExtension(), false, false, nullptr);
        menu.addSubMenu ("Skipped: crashed or hung while scanning (" + juce::String (skipped.size()) + ")", skippedMenu);
        menu.addItem ("Retry skipped plugins", ! scanner.isScanning(), false, [this] { engine.getScanner().startScan (true); });
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (list->rows[(size_t) juce::jmin (slot, kNumSlots - 1)])
                                                  .withMinimumWidth (list->getWidth()));
}

void InsertRack::loadPluginAsync (int slot, const juce::PluginDescription& desc,
                                  std::function<void (juce::AudioPluginInstance&)> then)
{
    if (loadingSlot >= 0)
        return;   // one load at a time per rack

    loadingSlot = juce::jmin (slot, chain.getNumPlugins());
    loadingName = desc.name;
    updateRows();

    // Crash-safe loading (on by default): a plugin that crashes while starting
    // is caught in a child process first, in a background thread so the UI
    // stays responsive. Only if the child survived does the mixer load it
    // in-process. A plugin that fails the probe is blacklisted, like a plugin
    // that crashed while scanning, until "Retry skipped plugins".
    // This protects loading, not the audio thread: a plugin that crashes
    // while processing audio can still take the mixer down. Full
    // out-of-process audio hosting is still open (ROADMAP Phase 5).
    if (AppSettings::get().pluginProbeEnabled())
    {
        juce::Component::SafePointer<InsertRack> safe (this);
        std::thread ([safe, desc, then]
        {
            juce::String error;
            const bool ok = PluginScanner::probePluginFile (desc.fileOrIdentifier, error);
            juce::MessageManager::callAsync ([safe, desc, then, ok, error]
            {
                if (safe == nullptr)
                    return;
                if (! ok)
                {
                    safe->loadingSlot = -1;
                    safe->updateRows();
                    safe->engine.getScanner().blacklistFile (desc.fileOrIdentifier);
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                            "Couldn't load " + desc.name,
                                                            "The plugin " + error + " in a test process, so it was "
                                                            "skipped instead of loading it into the mixer. "
                                                            "Use Settings > Retry skipped plugins to try it again.");
                    return;
                }
                safe->startAsyncLoad (desc, then);
            });
        }).detach();
        return;
    }

    startAsyncLoad (desc, then);
}

void InsertRack::startAsyncLoad (const juce::PluginDescription& desc,
                                 std::function<void (juce::AudioPluginInstance&)> then)
{
    // Let the "Loading..." row paint before the plugin's own (message-thread) start-up runs.
    juce::Component::SafePointer<InsertRack> safe (this);
    juce::Timer::callAfterDelay (30, [safe, desc, then]
    {
        if (safe == nullptr)
            return;

        safe->engine.getFormatManager().createPluginInstanceAsync (desc, safe->engine.getSampleRate(), 512,
            [safe, desc, then] (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error)
            {
                if (safe == nullptr)
                    return;

                const int slot = safe->loadingSlot;
                safe->loadingSlot = -1;

                if (instance == nullptr)
                {
                    safe->updateRows();
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                            "Couldn't load " + desc.name,
                                                            error.isNotEmpty() ? error : juce::String ("The plugin didn't start."));
                    return;
                }

                if (auto* old = safe->chain.getPlugin (slot))
                {
                    safe->closeEditor (old);
                    safe->abStates.erase (old);
                }

                auto* raw = instance.get();
                safe->chain.installPlugin (slot, std::move (instance));
                safe->updateRows();

                if (then)
                    then (*raw);
                else
                    safe->openEditor (raw);
            });
    });
}

static juce::File presetFolder()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("OpenMix Presets");
    dir.createDirectory();
    return dir;
}

void InsertRack::savePreset (int slot)
{
    auto* p = chain.getPlugin (slot);
    if (p == nullptr)
        return;

    chooser = std::make_unique<juce::FileChooser> ("Save " + p->getName() + " preset",
                                                   presetFolder().getChildFile (p->getName() + ".omxpreset"), "*.omxpreset");
    juce::Component::SafePointer<InsertRack> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                            | juce::FileBrowserComponent::warnAboutOverwriting,
        [safe, slot] (const juce::FileChooser& fc)
        {
            if (safe == nullptr || fc.getResult() == juce::File())
                return;

            auto* plugin = safe->chain.getPlugin (slot);
            if (plugin == nullptr)
                return;

            juce::XmlElement root ("OpenMixPreset");
            root.setAttribute ("version", 1);
            root.addChildElement (plugin->getPluginDescription().createXml().release());
            juce::MemoryBlock state;
            plugin->getStateInformation (state);
            root.setAttribute ("state", state.toBase64Encoding());

            if (! root.writeTo (fc.getResult().withFileExtension ("omxpreset")))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Preset not saved",
                                                        "Couldn't write " + fc.getResult().getFullPathName());
        });
}

void InsertRack::loadPreset (int slot)
{
    chooser = std::make_unique<juce::FileChooser> ("Load preset", presetFolder(), "*.omxpreset");
    juce::Component::SafePointer<InsertRack> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe, slot] (const juce::FileChooser& fc)
        {
            if (safe == nullptr || fc.getResult() == juce::File())
                return;

            auto xml = juce::parseXML (fc.getResult());
            juce::PluginDescription desc;
            juce::MemoryBlock state;
            if (xml == nullptr || ! xml->hasTagName ("OpenMixPreset") || xml->getChildByName ("PLUGIN") == nullptr
                || ! desc.loadFromXml (*xml->getChildByName ("PLUGIN"))
                || ! state.fromBase64Encoding (xml->getStringAttribute ("state")))
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Not an OpenMix preset",
                                                        fc.getResult().getFileName() + " couldn't be read.");
                return;
            }

            auto applyState = [state] (juce::AudioPluginInstance& p) { p.setStateInformation (state.getData(), (int) state.getSize()); };

            if (auto* current = safe->chain.getPlugin (slot);
                current != nullptr && current->getPluginDescription().createIdentifierString() == desc.createIdentifierString())
            {
                applyState (*current);   // same plugin: just load the settings
                return;
            }

            safe->loadPluginAsync (slot, desc, applyState);
        });
}

void InsertRack::openEditor (juce::AudioPluginInstance* plugin)
{
    if (plugin == nullptr)
        return;

    if (auto it = editorWindows.find (plugin); it != editorWindows.end())
    {
        it->second->toFront (true);
        return;
    }

    juce::Component::SafePointer<InsertRack> safe (this);
    editorWindows[plugin] = std::make_unique<PluginWindow> (*plugin, [safe, plugin]
    {
        if (safe != nullptr)
            safe->closeEditor (plugin);
    });
}

void InsertRack::closeEditor (juce::AudioPluginInstance* plugin)
{
    editorWindows.erase (plugin);
}

void InsertRack::abSwitch (juce::AudioPluginInstance* plugin, bool toB)
{
    auto it = abStates.find (plugin);
    if (it == abStates.end() || it->second.onB == toB)
        return;

    // Remember what's playing now in its own slot, then load the other one.
    auto& st = it->second;
    juce::MemoryBlock current;
    plugin->getStateInformation (current);
    (st.onB ? st.b : st.a) = current;

    const auto& next = toB ? st.b : st.a;
    plugin->setStateInformation (next.getData(), (int) next.getSize());
    st.onB = toB;
    updateRows();
}

void InsertRack::openMixPanel (int slot)
{
    auto* p = chain.getPlugin (slot);
    if (p == nullptr)
        return;

    auto panel = std::make_unique<SlotMixPanel> (p->getName(), chain.slotMix[(size_t) slot]);
    juce::Component::SafePointer<InsertRack> safe (this);
    panel->onChange = [safe] { if (safe != nullptr) safe->updateRows(); };

    auto& row = list->rows[(size_t) slot];
    auto* top = getTopLevelComponent();
    juce::CallOutBox::launchAsynchronously (std::move (panel), top->getLocalArea (&row, row.getLocalBounds()), top);
}
