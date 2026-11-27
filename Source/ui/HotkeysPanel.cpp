#include "ui/HotkeysPanel.h"
#include "ui/Widgets.h"
#include "ui/Theme.h"
#include "creator/Soundboard.h"

//==============================================================================
// One binding: what it does, its keys (click to change), Remove.

class HotkeysPanel::Row : public juce::Component
{
public:
    Row (const juce::String& action, const juce::String& keyText)
    {
        label.setText (action, juce::dontSendNotification);
        label.setFont (Theme::ui (13.5f));
        label.setColour (juce::Label::textColourId, Theme::p().text);
        addAndMakeVisible (label);

        keys.setButtonText (keyText);
        keys.setTitle ("Keys for " + action + ": " + keyText + ". Press to change.");
        keys.setTooltip ("Click, then press the new key combination.");
        makeClickable (keys);
        addAndMakeVisible (keys);

        remove.setTitle ("Remove the hotkey for " + action);
        makeClickable (remove);
        addAndMakeVisible (remove);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (0, 2);
        remove.setBounds (r.removeFromRight (80));
        r.removeFromRight (Theme::s2);
        keys.setBounds (r.removeFromRight (160));
        r.removeFromRight (Theme::s2);
        label.setBounds (r);
    }

    juce::Label label;
    juce::TextButton keys, remove { "Remove" };
};

//==============================================================================

HotkeysPanel::HotkeysPanel (GlobalHotkeys& h, juce::StringArray strips, juce::Array<int> active, juce::StringArray scenes)
    : hotkeys (h), stripNames (std::move (strips)), sceneNames (std::move (scenes)), activeStrips (std::move (active))
{
    setFocusContainerType (FocusContainerType::keyboardFocusContainer);

    view.setViewedComponent (&list, false);
    view.setScrollBarsShown (true, false);
    view.setScrollBarThickness (8);
    addAndMakeVisible (view);

    addButton.setTooltip ("Pick what the hotkey does, then press its keys.");
    addButton.onClick = [this] { showAddMenu(); };
    makeClickable (addButton);
    addAndMakeVisible (addButton);

    rebuild();
    setSize (600, 440);
}

HotkeysPanel::~HotkeysPanel()
{
    if (capturing)
        hotkeys.cancelCapture();
}

juce::String HotkeysPanel::actionText (const GlobalHotkeys::Binding& b) const
{
    return GlobalHotkeys::describeAction (b.action, b.index, stripNames, sceneNames);
}

void HotkeysPanel::rebuild()
{
    rows.clear();
    const auto& bindings = hotkeys.getBindings();
    for (int i = 0; i < (int) bindings.size(); ++i)
    {
        const auto b = bindings[(size_t) i];
        auto row = std::make_unique<Row> (actionText (b), GlobalHotkeys::describe (b.keys));
        row->keys.onClick = [this, b, i] { capture (b, i); };
        row->remove.onClick = [this, i]
        {
            auto all = hotkeys.getBindings();
            if (juce::isPositiveAndBelow (i, (int) all.size()))
                all.erase (all.begin() + i);
            hotkeys.setBindings (std::move (all));
            hotkeys.save();
            setNote ("Hotkey removed.");
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<HotkeysPanel> (this), i]
            {
                if (safe == nullptr)
                    return;
                safe->rebuild();
                if (! safe->isShowing())
                    return;
                if (! safe->rows.empty())
                {
                    const int nextIdx = juce::jmin (i, (int) safe->rows.size() - 1);
                    safe->rows[(size_t) nextIdx]->remove.grabKeyboardFocus();
                }
                else
                {
                    safe->addButton.grabKeyboardFocus();
                }
            });
        };
        list.addAndMakeVisible (*row);
        rows.push_back (std::move (row));
    }
    resized();
    repaint();
}

void HotkeysPanel::showAddMenu()
{
    using A = GlobalHotkeys::Action;
    auto add = [this] (A action, int index) { capture ({ {}, action, index }, -1); };

    juce::PopupMenu menu, mute, ptt, pads, scenes;
    for (int s : activeStrips)
    {
        mute.addItem (stripNames[s], [add, s] { add (A::ToggleMute, s); });
        ptt.addItem (stripNames[s], [add, s] { add (A::PushToTalk, s); });
    }
    menu.addSubMenu ("Mute or unmute", mute);
    menu.addSubMenu ("Push to talk (muted when you let go)", ptt);
    menu.addSeparator();
    for (int p = 0; p < Soundboard::numPads; ++p)
    {
        if (p > 0 && p % Soundboard::padsPerPage == 0)
            pads.addColumnBreak();
        pads.addItem ("Pad " + juce::String (p + 1), [add, p] { add (A::PlayPad, p); });
    }
    menu.addSubMenu ("Play soundboard pad", pads);
    menu.addItem ("Stop all sounds", [add] { add (A::StopSounds, 0); });
    menu.addSeparator();
    menu.addItem ("Start or stop recording", [add] { add (A::ToggleRecord, 0); });
    menu.addItem ("Save replay", [add] { add (A::SaveReplay, 0); });
    menu.addSeparator();
    if (sceneNames.isEmpty())
        scenes.addItem ("No scenes yet (save one from the Scene menu in the header)", false, false, nullptr);
    for (int i = 0; i < sceneNames.size(); ++i)
        scenes.addItem (sceneNames[i], [add, i] { add (A::RecallScene, i); });
    menu.addSubMenu ("Recall scene", scenes);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (addButton));
}

void HotkeysPanel::capture (GlobalHotkeys::Binding binding, int replaceIndex)
{
    if (capturing)
        hotkeys.cancelCapture();
    capturing = true;
    setNote ("Press the keys for \"" + actionText (binding) + "\" now. Escape cancels.");
    addButton.setEnabled (false);

    juce::Component::SafePointer<HotkeysPanel> safe (this);
    hotkeys.captureNext ([safe, binding, replaceIndex] (GlobalHotkeys::Keys keys) mutable
    {
        if (safe == nullptr)
            return;
        safe->capturing = false;
        safe->addButton.setEnabled (true);

        if (! keys.isValid())
        {
            safe->setNote ("Cancelled.");
            return;
        }

        auto all = safe->hotkeys.getBindings();
        for (int i = 0; i < (int) all.size(); ++i)
            if (i != replaceIndex && all[(size_t) i].keys == keys)
            {
                safe->setNote (GlobalHotkeys::describe (keys) + " is already used for \"" + safe->actionText (all[(size_t) i])
                               + "\". Pick other keys.", true);
                return;
            }

        binding.keys = keys;
        if (juce::isPositiveAndBelow (replaceIndex, (int) all.size()))
            all[(size_t) replaceIndex] = binding;
        else
            all.push_back (binding);

        safe->hotkeys.setBindings (std::move (all));
        safe->hotkeys.save();
        safe->setNote (GlobalHotkeys::describe (keys) + ": " + safe->actionText (binding) + ".");
        safe->rebuild();
    });
}

void HotkeysPanel::setNote (const juce::String& text, bool problem)
{
    note = text;
    noteIsProblem = problem;
    repaint (noteArea);
}

void HotkeysPanel::paint (juce::Graphics& g)
{
    const auto& t = Theme::p();
    g.fillAll (t.panel);

    g.setColour (t.textDim);
    g.setFont (Theme::ui (13.0f));
    g.drawFittedText ("These work in any app, also while a game is in front. The keys still reach that app, "
                      "so pick combinations it doesn't use (for example with Ctrl+Alt).",
                      helpArea, juce::Justification::topLeft, 3);

    const auto wellBounds = view.getBounds().toFloat().expanded (4.0f);
    g.setColour (t.well);
    g.fillRoundedRectangle (wellBounds, Theme::controlRadius);
    g.setColour (t.line);
    g.drawRoundedRectangle (wellBounds.reduced (0.5f), Theme::controlRadius, 1.0f);

    if (rows.empty())
    {
        g.setColour (t.textDim);
        g.drawFittedText ("No hotkeys yet. Add one for mute, push to talk, the soundboard, recording or a scene.",
                          emptyArea, juce::Justification::centred, 2);
    }

    if (note.isNotEmpty())
    {
        if (noteIsProblem)
        {
            g.setColour (t.mute);
            g.fillRect (noteArea.withWidth (3));
        }
        g.setColour (t.text);
        g.drawFittedText (note, noteArea.withTrimmedLeft (noteIsProblem ? 10 : 0), juce::Justification::centredLeft, 2);
    }
}

void HotkeysPanel::resized()
{
    auto r = getLocalBounds().reduced (Theme::s4);
    helpArea = r.removeFromTop (40);
    r.removeFromTop (Theme::s2);

    auto bottom = r.removeFromBottom (32);
    addButton.setBounds (bottom.removeFromLeft (140));
    bottom.removeFromLeft (Theme::s3);
    noteArea = bottom;
    r.removeFromBottom (Theme::s3);

    view.setBounds (r.reduced (4));
    emptyArea = view.getBounds();

    const int rowH = 34;
    const int w = view.getMaximumVisibleWidth();
    list.setSize (w, juce::jmax (1, (int) rows.size() * rowH));
    for (int i = 0; i < (int) rows.size(); ++i)
        rows[(size_t) i]->setBounds (Theme::s2, i * rowH, w - 2 * Theme::s2, rowH);
}
