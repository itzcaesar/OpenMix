#include "MainComponent.h"
#include "Session.h"
#include "Theme.h"

//==============================================================================
// Helpers

static void setupFader (juce::Slider& s)
{
    s.setRange (-60.0, 12.0, 0.1);
    s.setSkewFactorFromMidPoint (-12.0);
    s.setDoubleClickReturnValue (true, 0.0);
    s.setTextBoxStyle (juce::Slider::TextBoxAbove, false, 64, 20);
    s.textFromValueFunction = [] (double v) { return v <= -60.0 ? juce::String ("-inf") : (v > 0 ? "+" : "") + juce::String (v, 1); };
    s.valueFromTextFunction = [] (const juce::String& t) { return t.trim().startsWithIgnoreCase ("-inf") ? -60.0 : t.getDoubleValue(); };
    s.setTooltip ("Level in dB. Double-click to reset to 0 dB.");
}

static void styleToggle (juce::TextButton& b, juce::Colour onColour)
{
    b.setClickingTogglesState (true);
    b.setColour (juce::TextButton::buttonColourId, Theme::raised);
    b.setColour (juce::TextButton::buttonOnColourId, onColour);
    b.setColour (juce::TextButton::textColourOffId, Theme::textDim);
    b.setColour (juce::TextButton::textColourOnId, Theme::background);
}

static juce::AudioPluginFormat* findVst3Format (juce::AudioPluginFormatManager& fm)
{
    for (auto* f : fm.getFormats())
        if (f->getName() == "VST3")
            return f;
    return nullptr;
}

// Cached list of VST3 bundles in the standard folders.
static juce::StringArray& vst3Files (juce::AudioPluginFormatManager& fm, bool rescan = false)
{
    static juce::StringArray files;
    static bool scanned = false;

    if (! scanned || rescan)
        if (auto* f = findVst3Format (fm))
        {
            files = f->searchPathsForPlugins (f->getDefaultLocationsToSearch(), true);
            files.sortNatural();
            scanned = true;
        }

    return files;
}

static void drawCard (juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto r = area.toFloat();
    g.setColour (Theme::panel);
    g.fillRoundedRectangle (r, Theme::radius);
    g.setColour (Theme::outline);
    g.drawRoundedRectangle (r.reduced (0.5f), Theme::radius, 1.0f);
}

//==============================================================================
// LevelMeter

float LevelMeter::dbToProportion (float db) noexcept
{
    // Fader-like curve: more room near the top where it matters.
    return std::pow (juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f), 1.6f);
}

void LevelMeter::update (Channel& c, float peak)
{
    c.level = juce::jmax (peak, c.level * 0.82f);
    if (peak >= c.hold)
    {
        c.hold = peak;
        c.holdFrames = 45;   // ~1.5 s at 30 Hz
    }
    else if (--c.holdFrames <= 0)
    {
        c.hold *= 0.9f;
    }

    if (peak >= 1.0f)
        clipped = true;
}

void LevelMeter::tick()
{
    update (left, peakL.exchange (0.0f));
    update (right, peakR.exchange (0.0f));
    repaint();
}

void LevelMeter::drawChannel (juce::Graphics& g, juce::Rectangle<float> r, const Channel& c)
{
    g.setColour (Theme::panelDark);
    g.fillRoundedRectangle (r, 2.0f);

    const float db = juce::Decibels::gainToDecibels (c.level, -60.0f);
    const float top = r.getBottom() - r.getHeight() * dbToProportion (db);

    // Gradient bar, then 1 px gaps every 3 px for an LED look.
    juce::ColourGradient grad (Theme::meterLow, 0.0f, r.getBottom(), Theme::meterClip, 0.0f, r.getY(), false);
    grad.addColour (1.0 - dbToProportion (-12.0f), Theme::meterLow);
    grad.addColour (1.0 - dbToProportion (-3.0f), Theme::meterHigh);
    g.setGradientFill (grad);
    g.fillRect (r.withTop (top));

    g.setColour (Theme::panelDark);
    for (float y = r.getBottom() - 3.0f; y > top; y -= 3.0f)
        g.fillRect (r.getX(), y, r.getWidth(), 1.0f);

    // Peak hold line
    const float holdDb = juce::Decibels::gainToDecibels (c.hold, -60.0f);
    if (holdDb > -59.0f)
    {
        const float y = r.getBottom() - r.getHeight() * dbToProportion (holdDb);
        g.setColour (holdDb > -0.1f ? Theme::meterClip : Theme::text.withAlpha (0.8f));
        g.fillRect (r.getX(), y - 1.0f, r.getWidth(), 2.0f);
    }
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();

    // Clip LED (click to reset)
    auto led = area.removeFromTop (6.0f);
    g.setColour (clipped ? Theme::meterClip : Theme::panelDark);
    g.fillRoundedRectangle (led, 2.0f);
    area.removeFromTop (3.0f);

    const float w = (area.getWidth() - 2.0f) / 2.0f;
    drawChannel (g, area.removeFromLeft (w), left);
    area.removeFromLeft (2.0f);
    drawChannel (g, area, right);
}

//==============================================================================
// FaderSection

FaderSection::FaderSection (std::atomic<float>& peakL, std::atomic<float>& peakR) : meter (peakL, peakR)
{
    setupFader (fader);
    addAndMakeVisible (fader);
    addAndMakeVisible (meter);
    meter.setTooltip ("Peak meter. The red LED latches on clipping; click to reset.");
}

void FaderSection::resized()
{
    fader.setColour (juce::Slider::trackColourId, accent);

    auto r = getLocalBounds();
    const int scaleW = 30, meterW = 14;
    auto content = r.withSizeKeepingCentre (juce::jmin (r.getWidth(), scaleW + 44 + 6 + meterW), r.getHeight());

    content.removeFromLeft (scaleW);
    auto meterArea = content.removeFromRight (meterW);
    content.removeFromRight (6);
    fader.setBounds (content);

    // Meter spans the fader's travel (below the value readout).
    meterArea.removeFromTop (fader.getTextBoxHeight() + 4);
    meter.setBounds (meterArea);
}

void FaderSection::paint (juce::Graphics& g)
{
    const int scaleRight = fader.getX() - 2;
    g.setFont (Theme::mono (11.0f));
    int lastLabelY = -100;

    // 0 dB first so it always gets a label; the rest only where there's room.
    for (double db : { 0.0, 12.0, 6.0, -6.0, -12.0, -24.0, -40.0, -60.0 })
    {
        const int y = fader.getY() + (int) fader.getPositionOfValue (db);
        g.setColour (Theme::grid);
        g.fillRect (scaleRight - 5, y, 5, 1);

        const int zeroY = fader.getY() + (int) fader.getPositionOfValue (0.0);
        if (db != 0.0 && (std::abs (y - lastLabelY) < 14 || std::abs (y - zeroY) < 14))
            continue;
        lastLabelY = db == 0.0 ? lastLabelY : y;

        g.setColour (db == 0.0 ? Theme::textDim : Theme::textFaint);
        const auto label = db <= -60.0 ? juce::String ("-inf") : (db > 0 ? "+" : "") + juce::String ((int) db);
        g.drawText (label, scaleRight - 36, y - 7, 29, 14, juce::Justification::centredRight);
    }
}

//==============================================================================
// ChipButton

void ChipButton::setContent (const juce::String& newCaption, const juce::String& newText,
                             juce::Colour newCaptionColour, bool isProblem)
{
    caption = newCaption;
    text = newText;
    captionColour = newCaptionColour;
    problem = isProblem;
    setTooltip (caption + ": " + text);
    repaint();
}

void ChipButton::paintButton (juce::Graphics& g, bool isHighlighted, bool isDown)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (isDown ? Theme::raised.darker (0.2f) : isHighlighted ? Theme::raised.brighter (0.12f) : Theme::raised);
    g.fillRoundedRectangle (r, Theme::smallRadius);
    g.setColour (problem ? Theme::warning.withAlpha (0.8f) : Theme::outline);
    g.drawRoundedRectangle (r, Theme::smallRadius, 1.0f);

    auto content = getLocalBounds().reduced (8, 0);

    // Chevron
    auto chev = content.removeFromRight (10).toFloat().withSizeKeepingCentre (8.0f, 5.0f);
    juce::Path p;
    p.startNewSubPath (chev.getX(), chev.getY());
    p.lineTo (chev.getCentreX(), chev.getBottom());
    p.lineTo (chev.getRight(), chev.getY());
    g.setColour (Theme::textDim);
    g.strokePath (p, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    content.removeFromRight (6);

    // Caption badge
    if (caption.isNotEmpty())
    {
        const auto badgeFont = Theme::ui (11.0f, true);
        const int bw = juce::GlyphArrangement::getStringWidthInt (badgeFont, caption) + 10;
        auto badge = content.removeFromLeft (bw).withSizeKeepingCentre (bw, 18).toFloat();
        g.setColour (captionColour.withAlpha (0.18f));
        g.fillRoundedRectangle (badge, 3.0f);
        g.setColour (captionColour);
        g.setFont (badgeFont);
        g.drawText (caption, badge, juce::Justification::centred);
        content.removeFromLeft (7);
    }

    g.setColour (problem ? Theme::warning : Theme::text);
    g.setFont (Theme::ui (13.0f));
    g.drawText (text, content, juce::Justification::centredLeft, true);
}

//==============================================================================
// PluginSlotRow

void PluginSlotRow::setState (const juce::String& pluginName, bool isLoaded, bool isBypassed, int slotNumber, bool isFaulted)
{
    name = pluginName;
    loaded = isLoaded;
    bypassed = isBypassed;
    faulted = isFaulted && isBypassed;
    number = slotNumber;
    setTooltip (faulted ? name + " produced invalid audio (NaN/Inf) and was bypassed automatically. "
                                 "Click the power button to try it again."
              : loaded ? "Click to open " + name + ". Power button: bypass. Dots: more options."
                       : "Click to add a VST3 plugin to insert slot " + juce::String (number) + ".");
    repaint();
}

PluginSlotRow::Zone PluginSlotRow::zoneAt (juce::Point<int> p) const
{
    if (! loaded)          return Zone::Main;
    if (p.x < 26)          return Zone::Power;
    if (p.x > getWidth() - 26) return Zone::Menu;
    return Zone::Main;
}

void PluginSlotRow::mouseMove (const juce::MouseEvent& e)
{
    const auto z = zoneAt (e.getPosition());
    if (z != hover) { hover = z; repaint(); }
}

void PluginSlotRow::mouseExit (const juce::MouseEvent&)
{
    hover = Zone::None;
    repaint();
}

void PluginSlotRow::mouseUp (const juce::MouseEvent& e)
{
    if (! getLocalBounds().contains (e.getPosition()))
        return;

    if (e.mods.isPopupMenu() && loaded) { if (onMenu) onMenu(); return; }

    switch (zoneAt (e.getPosition()))
    {
        case Zone::Power: if (onBypass) onBypass(); break;
        case Zone::Menu:  if (onMenu) onMenu(); break;
        case Zone::Main:  if (onMain) onMain(); break;
        case Zone::None:  break;
    }
}

void PluginSlotRow::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);

    if (! loaded)
    {
        // Empty slot: dashed outline with "+ Add plugin"
        g.setColour (hover != Zone::None ? Theme::raised : Theme::panelDark);
        g.fillRoundedRectangle (r, Theme::smallRadius);
        juce::Path outline;
        outline.addRoundedRectangle (r, Theme::smallRadius);
        juce::Path dashed;
        const float dashes[] { 4.0f, 3.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dashes, 2);
        g.setColour (hover != Zone::None ? Theme::accent.withAlpha (0.7f) : Theme::outline);
        g.fillPath (dashed);

        g.setColour (hover != Zone::None ? Theme::text : Theme::textFaint);
        g.setFont (Theme::ui (13.0f));
        g.drawText ("+  Add plugin", r, juce::Justification::centred);
        return;
    }

    g.setColour (hover == Zone::Main ? Theme::raised.brighter (0.1f) : Theme::raised);
    g.fillRoundedRectangle (r, Theme::smallRadius);
    g.setColour (Theme::outline);
    g.drawRoundedRectangle (r, Theme::smallRadius, 1.0f);

    // Power (bypass) icon
    const auto power = juce::Rectangle<float> (14.0f, 14.0f).withCentre ({ 13.0f, r.getCentreY() });
    const auto powerColour = faulted ? Theme::mute : bypassed ? Theme::textFaint : Theme::success;
    juce::Path ring;
    ring.addCentredArc (power.getCentreX(), power.getCentreY(), 5.5f, 5.5f, 0.0f, 0.75f, juce::MathConstants<float>::twoPi - 0.75f, true);
    g.setColour (hover == Zone::Power ? powerColour.brighter (0.3f) : powerColour);
    g.strokePath (ring, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.drawLine (power.getCentreX(), power.getY(), power.getCentreX(), power.getCentreY(), 1.6f);

    // Name
    auto nameArea = r.withTrimmedLeft (26.0f).withTrimmedRight (26.0f);
    g.setColour (bypassed ? Theme::textFaint : Theme::text);
    g.setFont (Theme::ui (13.0f));
    g.drawText (name, nameArea, juce::Justification::centredLeft, true);

    // Menu dots
    const float mx = r.getRight() - 13.0f;
    g.setColour (hover == Zone::Menu ? Theme::text : Theme::textDim);
    for (int i = -1; i <= 1; ++i)
        g.fillEllipse (mx - 1.5f, r.getCentreY() + (float) i * 4.5f - 1.5f, 3.0f, 3.0f);
}

//==============================================================================
// PluginWindow

PluginWindow::PluginWindow (juce::AudioPluginInstance& plugin, std::function<void()> onCloseFn)
    : DocumentWindow (plugin.getName(), Theme::background, DocumentWindow::closeButton),
      onClose (std::move (onCloseFn))
{
    setUsingNativeTitleBar (true);

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
// StripComponent

StripComponent::StripComponent (MixerEngine& e, int i)
    : engine (e), strip (e.getStrip (i)), index (i),
      colour (Theme::stripColours[(size_t) i % std::size (Theme::stripColours)]),
      pad (strip.pad), faderSection (strip.peakL, strip.peakR)
{
    nameLabel.setFont (Theme::ui (16.0f, true));
    nameLabel.setJustificationType (juce::Justification::centredLeft);
    nameLabel.setEditable (false, true);
    nameLabel.setTooltip ("Double-click to rename");
    nameLabel.onTextChange = [this]
    {
        auto t = nameLabel.getText().trim();
        strip.name = t == defaultName() ? juce::String() : t;
        if (t.isEmpty())
            nameLabel.setText (defaultName(), juce::dontSendNotification);
    };
    addAndMakeVisible (nameLabel);

    sourceButton.onClick = [this] { showSourceMenu(); };
    addAndMakeVisible (sourceButton);

    gainKnob.setRange (-24.0, 24.0, 0.1);
    gainKnob.setDoubleClickReturnValue (true, 0.0);
    gainKnob.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 62, 20);
    gainKnob.textFromValueFunction = [] (double v) { return (v > 0 ? "+" : "") + juce::String (v, 1) + " dB"; };
    gainKnob.setColour (juce::Slider::rotarySliderFillColourId, colour);
    gainKnob.setTooltip ("Input gain. Double-click to reset.");
    gainKnob.onValueChange = [this] { strip.gainDb.store ((float) gainKnob.getValue()); };
    addAndMakeVisible (gainKnob);

    for (int s = 0; s < kNumSlots; ++s)
    {
        auto& row = slotRows[(size_t) s];
        row.onMain   = [this, s] { slotClicked (s); };
        row.onMenu   = [this, s] { showSlotMenu (s); };
        row.onBypass = [this, s]
        {
            strip.slotFault[(size_t) s].store (false);
            strip.slotBypass[(size_t) s].store (! strip.slotBypass[(size_t) s].load());
            updateSlotRows();
        };
        addAndMakeVisible (row);
    }

    addAndMakeVisible (pad);

    faderSection.accent = colour;
    faderSection.fader.onValueChange = [this] { strip.faderDb.store ((float) faderSection.fader.getValue()); };
    addAndMakeVisible (faderSection);

    styleToggle (muteButton, Theme::mute);
    muteButton.setTooltip ("Mute this input on every bus");
    muteButton.onClick = [this] { strip.mute.store (muteButton.getToggleState()); };
    addAndMakeVisible (muteButton);

    for (int b = 0; b < kNumBuses; ++b)
    {
        auto& btn = busButtons[(size_t) b];
        const juce::String busName (kBusNames[b]);
        btn.setButtonText (busName);
        styleToggle (btn, busName.startsWith ("B") ? Theme::busB : Theme::busA);
        btn.setTooltip ("Send this input to bus " + busName);
        btn.onClick = [this, b] { strip.busOn[b].store (busButtons[(size_t) b].getToggleState()); };
        addAndMakeVisible (btn);
    }

    refreshFromStrip();
}

StripComponent::~StripComponent()
{
    for (auto& w : editorWindows)
        w.reset();
}

void StripComponent::refreshFromStrip()
{
    nameLabel.setText (strip.name.isNotEmpty() ? strip.name : defaultName(), juce::dontSendNotification);
    gainKnob.setValue (strip.gainDb.load(), juce::dontSendNotification);
    gainKnob.updateText();
    faderSection.fader.setValue (strip.faderDb.load(), juce::dontSendNotification);
    muteButton.setToggleState (strip.mute.load(), juce::dontSendNotification);
    for (int b = 0; b < kNumBuses; ++b)
        busButtons[(size_t) b].setToggleState (strip.busOn[b].load(), juce::dontSendNotification);
    pad.refreshFromParams();
    updateSlotRows();
    updateSourceButton();
}

void StripComponent::refreshDevice()
{
    updateSourceButton();
}

void StripComponent::updateSourceButton()
{
    juce::String caption, text;
    bool problem = false;

    switch ((SourceType) strip.sourceType.load())
    {
        case SourceType::None:
            caption = "OFF";
            text = "No input";
            break;

        case SourceType::Main:
        {
            caption = "MAIN";
            const auto names = engine.getActiveInputNames();
            const int ch = strip.inputChannel.load();
            problem = ! juce::isPositiveAndBelow (ch, names.size());
            text = ! problem ? names[ch]
                 : names.isEmpty() ? juce::String ("Pick a source")
                                   : juce::String ("Channel missing");
            break;
        }

        case SourceType::Device:
            caption = "DEVICE";
            problem = strip.getExternalSource() == nullptr;
            text = strip.sourceDeviceName + (problem ? " (offline)" : "");
            break;

        case SourceType::App:
            caption = "APP";
            problem = strip.getExternalSource() == nullptr;
            text = strip.sourceAppExe.upToLastOccurrenceOf (".", false, true) + (problem ? " (waiting)" : "");
            break;
    }

    sourceKind = caption;
    sourceButton.setContent (caption, text, colour, problem);
}

void StripComponent::showSourceMenu()
{
    juce::PopupMenu menu;
    const auto current = (SourceType) strip.sourceType.load();

    menu.addItem ("No input", true, current == SourceType::None, [this]
    {
        engine.setStripSourceNone (index);
        updateSourceButton();
    });

    menu.addSectionHeader ("Main device");
    const auto mainInputs = engine.getActiveInputNames();
    if (mainInputs.isEmpty())
        menu.addItem ("No active inputs (see Audio Settings)", false, false, nullptr);

    for (int i = 0; i < mainInputs.size(); ++i)
        menu.addItem (mainInputs[i], true, current == SourceType::Main && strip.inputChannel.load() == i, [this, i]
        {
            engine.setStripSourceMain (index, i);
            updateSourceButton();
        });

    menu.addSectionHeader ("Other sources");

    juce::PopupMenu devices;
    for (const auto& name : engine.getWindowsInputNames())
        devices.addItem (name, true, current == SourceType::Device && strip.sourceDeviceName == name, [this, name]
        {
            juce::String error;
            if (! engine.setStripSourceDevice (index, name, error))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Input device", error);
            updateSourceButton();
        });
    menu.addSubMenu ("Windows input device", devices);

    juce::PopupMenu apps;
    const auto running = listAudioApps();
    if (running.empty())
        apps.addItem ("No apps with audio right now", false, false, nullptr);

    for (const auto& app : running)
    {
        const juce::String exe (app.exeName.c_str());
        const auto label = exe.upToLastOccurrenceOf (".", false, true) + (app.active ? "  (playing)" : "");
        apps.addItem (label, true, current == SourceType::App && strip.sourceAppExe.equalsIgnoreCase (exe), [this, exe]
        {
            juce::String error;
            if (! engine.setStripSourceApp (index, exe, error))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "App capture", error);
            updateSourceButton();
        });
    }
    menu.addSubMenu ("Application audio", apps);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (sourceButton).withMinimumWidth (sourceButton.getWidth()));
}

void StripComponent::updateSlotRows()
{
    for (int s = 0; s < kNumSlots; ++s)
    {
        auto* p = strip.getPlugin (s);
        slotRows[(size_t) s].setState (p != nullptr ? p->getName() : juce::String(), p != nullptr,
                                       strip.slotBypass[(size_t) s].load(), s + 1,
                                       strip.slotFault[(size_t) s].load());
    }
}

void StripComponent::tick()
{
    faderSection.tick();

    // The audio thread auto-bypasses plugins that output NaN/Inf; reflect that here.
    if (const int faults = strip.faultCount.load(); faults != lastFaultCount)
    {
        lastFaultCount = faults;
        updateSlotRows();
    }
}

void StripComponent::slotClicked (int slot)
{
    if (strip.getPlugin (slot) == nullptr)
        showPluginMenu (slot);
    else
        openEditor (slot);
}

void StripComponent::showSlotMenu (int slot)
{
    if (strip.getPlugin (slot) == nullptr)
        return;

    juce::PopupMenu menu;
    menu.addItem ("Open editor", [this, slot] { openEditor (slot); });
    menu.addItem (strip.slotBypass[(size_t) slot].load() ? "Enable" : "Bypass", [this, slot]
    {
        strip.slotBypass[(size_t) slot].store (! strip.slotBypass[(size_t) slot].load());
        updateSlotRows();
    });
    menu.addItem ("Replace...", [this, slot] { showPluginMenu (slot); });
    menu.addSeparator();
    menu.addItem ("Remove", [this, slot]
    {
        closeEditor (slot);
        strip.removePlugin (slot);
        updateSlotRows();
    });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (slotRows[(size_t) slot]));
}

void StripComponent::showPluginMenu (int slot)
{
    auto& fm = engine.getFormatManager();
    const auto& files = vst3Files (fm);

    juce::PopupMenu menu;
    menu.addSectionHeader ("VST3 plugins");
    if (files.isEmpty())
        menu.addItem ("No VST3 plugins in C:\\Program Files\\Common Files\\VST3", false, false, nullptr);

    for (const auto& f : files)
        menu.addItem (juce::File (f).getFileNameWithoutExtension(), [this, slot, f] { loadPluginFile (slot, f); });

    menu.addSeparator();
    menu.addItem ("Rescan plugin folder", [this, slot]
    {
        vst3Files (engine.getFormatManager(), true);
        showPluginMenu (slot);
    });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (slotRows[(size_t) slot])
                                                  .withMinimumWidth (slotRows[(size_t) slot].getWidth()));
}

void StripComponent::loadPluginFile (int slot, const juce::String& fileOrId)
{
    auto* format = findVst3Format (engine.getFormatManager());
    if (format == nullptr)
        return;

    juce::OwnedArray<juce::PluginDescription> types;
    format->findAllTypesForFile (types, fileOrId);

    if (types.isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Plugin",
                                                "Couldn't read a plugin from:\n" + fileOrId);
        return;
    }

    closeEditor (slot);

    juce::String error;
    if (! strip.loadPlugin (slot, *types[0], engine.getFormatManager(), error))
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Plugin",
                                                "Couldn't load " + types[0]->name + ":\n" + error);
        return;
    }

    strip.slotBypass[(size_t) slot].store (false);
    updateSlotRows();
    openEditor (slot);
}

void StripComponent::openEditor (int slot)
{
    auto* plugin = strip.getPlugin (slot);
    if (plugin == nullptr)
        return;

    if (auto& w = editorWindows[(size_t) slot]; w != nullptr)
    {
        w->toFront (true);
        return;
    }

    juce::Component::SafePointer<StripComponent> safe (this);
    editorWindows[(size_t) slot] = std::make_unique<PluginWindow> (*plugin, [safe, slot]
    {
        if (safe != nullptr)
            safe->closeEditor (slot);
    });
}

void StripComponent::closeEditor (int slot)
{
    editorWindows[(size_t) slot].reset();
}

void StripComponent::paint (juce::Graphics& g)
{
    drawCard (g, getLocalBounds());

    // Identity stripe
    g.setColour (colour);
    g.fillRoundedRectangle (getLocalBounds().toFloat().removeFromTop (4.0f).reduced (Theme::radius, 0.0f), 2.0f);

    Theme::drawSectionLabel (g, "Inserts", insertsCaptionArea);
    Theme::drawSectionLabel (g, "Voice / pan pad", padCaptionArea);
    Theme::drawSectionLabel (g, "Send to", routeCaptionArea);
}

void StripComponent::resized()
{
    auto r = getLocalBounds().reduced (12, 10);
    r.removeFromTop (4);

    // Name on the left, gain knob (with its value) on the right
    auto titleRow = r.removeFromTop (40);
    gainKnob.setBounds (titleRow.removeFromRight (62 + 40));
    nameLabel.setBounds (titleRow.withTrimmedLeft (-4));
    r.removeFromTop (6);
    sourceButton.setBounds (r.removeFromTop (30));
    r.removeFromTop (12);

    insertsCaptionArea = r.removeFromTop (18);
    for (auto& row : slotRows)
    {
        row.setBounds (r.removeFromTop (28));
        r.removeFromTop (4);
    }
    r.removeFromTop (6);

    // Bottom controls first, so the pad and the fader share what's left.
    auto bottom = r.removeFromBottom (30 + 6 + 18 + 30 + 8);
    padCaptionArea = r.removeFromTop (18);
    const int remaining = r.getHeight() - VoicePadComponent::headerHeight - 10;
    const int padSize = juce::jlimit (110, juce::jmin (r.getWidth(), 220), remaining - 230);   // fader keeps >= 230 px
    pad.setBounds (r.removeFromTop (padSize + VoicePadComponent::headerHeight));
    r.removeFromTop (10);
    r = r.getUnion (bottom);

    auto busRow = r.removeFromBottom (30);
    const int bw = (busRow.getWidth() - (kNumBuses - 1) * 6) / kNumBuses;
    for (auto& b : busButtons)
    {
        b.setBounds (busRow.removeFromLeft (bw));
        busRow.removeFromLeft (6);
    }
    routeCaptionArea = r.removeFromBottom (18);
    r.removeFromBottom (6);
    muteButton.setBounds (r.removeFromBottom (30));
    r.removeFromBottom (8);

    faderSection.setBounds (r);
}

//==============================================================================
// BusComponent

BusComponent::BusComponent (MixerEngine& e, int i)
    : engine (e), bus (e.getBus (i)), index (i), faderSection (bus.peakL, bus.peakR)
{
    targetButton.onClick = [this] { showTargetMenu(); };
    addAndMakeVisible (targetButton);

    faderSection.accent = isVirtual() ? Theme::busB : Theme::busA;
    faderSection.fader.onValueChange = [this] { bus.faderDb.store ((float) faderSection.fader.getValue()); };
    addAndMakeVisible (faderSection);

    styleToggle (muteButton, Theme::mute);
    muteButton.onClick = [this] { bus.mute.store (muteButton.getToggleState()); };
    addAndMakeVisible (muteButton);

    refreshFromBus();
}

void BusComponent::tick()
{
    faderSection.tick();

    // "LIMIT" badge while the safety limiter is reducing gain by more than 0.5 dB (held 1 s).
    const float g = bus.limiterGain.exchange (1.0f);
    if (g < 0.944f)
        limitHoldFrames = 30;
    else if (limitHoldFrames > 0)
        --limitHoldFrames;

    const bool nowLimiting = limitHoldFrames > 0;
    if (nowLimiting != limiting)
    {
        limiting = nowLimiting;
        repaint (getLocalBounds().removeFromTop (44));
    }
}

void BusComponent::refreshFromBus()
{
    faderSection.fader.setValue (bus.faderDb.load(), juce::dontSendNotification);
    muteButton.setToggleState (bus.mute.load(), juce::dontSendNotification);
    refreshDevice();
}

void BusComponent::refreshDevice()
{
    juce::String caption, text;
    bool problem = false;

    switch ((BusTarget) bus.target.load())
    {
        case BusTarget::Off:
            caption = "OFF";
            text = "Not routed";
            break;

        case BusTarget::MainPair:
        {
            caption = "MAIN";
            const auto pairs = engine.getActiveOutputPairNames();
            const int pair = bus.outputPair.load();
            problem = ! juce::isPositiveAndBelow (pair, pairs.size());
            text = problem ? juce::String ("Pair missing") : pairs[pair];
            break;
        }

        case BusTarget::Device:
            caption = "DEVICE";
            problem = bus.sink == nullptr;
            text = bus.deviceName + (problem ? " (offline)" : "");
            break;
    }

    targetButton.setContent (caption, text, isVirtual() ? Theme::busB : Theme::busA, problem);
}

void BusComponent::showTargetMenu()
{
    juce::PopupMenu menu;
    const auto current = (BusTarget) bus.target.load();

    menu.addItem ("Off", true, current == BusTarget::Off, [this]
    {
        engine.setBusOff (index);
        refreshDevice();
    });

    menu.addSectionHeader ("Main device");
    const auto pairs = engine.getActiveOutputPairNames();
    for (int i = 0; i < pairs.size(); ++i)
        menu.addItem (pairs[i], true, current == BusTarget::MainPair && bus.outputPair.load() == i, [this, i]
        {
            engine.setBusMainPair (index, i);
            refreshDevice();
        });

    menu.addSectionHeader ("Windows output device");
    const auto devices = engine.getWindowsOutputNames();
    if (devices.isEmpty())
        menu.addItem ("No Windows output devices found", false, false, nullptr);

    for (const auto& name : devices)
        menu.addItem (name, true, current == BusTarget::Device && bus.deviceName == name, [this, name]
        {
            juce::String error;
            if (! engine.setBusDevice (index, name, error))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Output device", error);
            refreshDevice();
        });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (targetButton).withMinimumWidth (220));
}

void BusComponent::paint (juce::Graphics& g)
{
    drawCard (g, getLocalBounds());

    const auto accent = isVirtual() ? Theme::busB : Theme::busA;
    g.setColour (accent);
    g.fillRoundedRectangle (getLocalBounds().toFloat().removeFromTop (4.0f).reduced (Theme::radius, 0.0f), 2.0f);

    auto r = getLocalBounds().reduced (12, 10).withTrimmedTop (4);
    auto title = r.removeFromTop (28);
    g.setColour (Theme::text);
    g.setFont (Theme::ui (16.0f, true));
    g.drawText (kBusNames[index], title, juce::Justification::centredLeft);
    g.setFont (Theme::ui (11.0f, true).withExtraKerningFactor (0.1f));
    if (limiting)
    {
        auto badge = title.removeFromRight (48).withSizeKeepingCentre (48, 18).toFloat();
        g.setColour (Theme::mute.withAlpha (0.2f));
        g.fillRoundedRectangle (badge, 3.0f);
        g.setColour (Theme::mute);
        g.drawText ("LIMIT", badge, juce::Justification::centred);
    }
    else
    {
        g.setColour (accent);
        g.drawText (isVirtual() ? "VIRTUAL" : "HARDWARE", title, juce::Justification::centredRight);
    }
}

void BusComponent::resized()
{
    auto r = getLocalBounds().reduced (12, 10);
    r.removeFromTop (4 + 28 + 6);
    targetButton.setBounds (r.removeFromTop (30));
    r.removeFromTop (12);
    muteButton.setBounds (r.removeFromBottom (30));
    r.removeFromBottom (8);
    faderSection.setBounds (r);
}

//==============================================================================
// MainComponent

MainComponent::MainComponent()
{
    const auto session = Session::load();
    const bool firstRun = session == nullptr;

    engine.start (session != nullptr ? session->getChildByName ("DEVICESETUP") : nullptr);

    // Crash guard: this marker exists only while plugins are being restored. If it's still
    // there at startup, a plugin crashed OpenMix last time, so start without plugins.
    const auto sessionFile = Session::getFile();
    const auto restoreMarker = sessionFile.getSiblingFile ("restoring-plugins.lock");
    bool safeMode = false;

    if (session != nullptr)
    {
        safeMode = restoreMarker.existsAsFile();
        if (safeMode)
            sessionFile.copyFileTo (sessionFile.getSiblingFile ("session-before-crash.xml"));

        sessionFile.getParentDirectory().createDirectory();
        restoreMarker.create();
        Session::apply (*session, engine, ! safeMode);
        restoreMarker.deleteFile();
    }

    if (safeMode)
        juce::MessageManager::callAsync ([backup = sessionFile.getSiblingFile ("session-before-crash.xml")]
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Safe mode",
                "OpenMix closed unexpectedly while loading your plugins last time, so it started "
                "without them.\n\nYour previous setup (with plugins) was backed up to:\n"
                + backup.getFullPathName() + "\n\nAdd plugins back one at a time to find the one causing the crash.");
        });

    if (firstRun)
    {
        // Strip N listens to input N, if the device has it.
        const int numInputs = engine.getActiveInputNames().size();
        for (int s = 0; s < kNumStrips; ++s)
            engine.getStrip (s).inputChannel.store (s < numInputs ? s : -1);
    }

    settingsButton.onClick = [this] { showAudioSettings(); };
    settingsButton.setTooltip ("Driver (ASIO / Windows Audio), device, channels, sample rate, buffer size");
    addAndMakeVisible (settingsButton);

    fixButton.setColour (juce::TextButton::buttonColourId, Theme::warning);
    fixButton.setColour (juce::TextButton::textColourOffId, Theme::background);
    fixButton.onClick = [this] { showAudioSettings(); };
    addChildComponent (fixButton);

    for (int s = 0; s < kNumStrips; ++s)
    {
        strips[(size_t) s] = std::make_unique<StripComponent> (engine, s);
        addAndMakeVisible (*strips[(size_t) s]);
    }

    for (int b = 0; b < kNumBuses; ++b)
    {
        buses[(size_t) b] = std::make_unique<BusComponent> (engine, b);
        addAndMakeVisible (*buses[(size_t) b]);
    }

    engine.getDeviceManager().addChangeListener (this);
    changeListenerCallback (nullptr);
    updateStatus();
    startTimerHz (30);

    setSize (1240, 920);
}

MainComponent::~MainComponent()
{
    stopTimer();
    engine.getDeviceManager().removeChangeListener (this);
    Session::save (engine);

    // Close plugin editors before the engine (and the plugins) go away.
    for (auto& s : strips) s.reset();
    for (auto& b : buses)  b.reset();
}

void MainComponent::showAudioSettings()
{
    auto* selector = new juce::AudioDeviceSelectorComponent (engine.getDeviceManager(),
                                                             0, 16,   // inputs
                                                             2, 16,   // outputs
                                                             false, false, false, false);
    selector->setSize (560, 520);

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (selector);
    o.dialogTitle = "Audio Settings";
    o.dialogBackgroundColour = Theme::panel;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.launchAsync();
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    for (auto& s : strips) s->refreshDevice();
    for (auto& b : buses)  b->refreshDevice();
    updateStatus();
}

void MainComponent::updateStatus()
{
    chips.clearQuick();
    auto& dm = engine.getDeviceManager();

    if (auto* device = dm.getCurrentAudioDevice())
    {
        const double sr = device->getCurrentSampleRate();
        const int bs = device->getCurrentBufferSizeSamples();
        const double latencyMs = sr > 0 ? 1000.0 * (bs + device->getInputLatencyInSamples() + device->getOutputLatencyInSamples()) / sr : 0.0;
        const double cpu = dm.getCpuUsage();
        const int dropouts = juce::jmax (0, device->getXRunCount()) + engine.getLinkDropouts();

        chips.add ({ device->getTypeName() + "  " + device->getName(), Theme::textDim });
        chips.add ({ juce::String (sr / 1000.0, 1) + " kHz  /  " + juce::String (bs) + " smp", Theme::textDim });
        chips.add ({ juce::String (latencyMs, 1) + " ms", latencyMs > 30.0 ? Theme::warning : Theme::textDim });
        chips.add ({ "CPU " + juce::String (juce::roundToInt (cpu * 100.0)) + "%",
                     cpu > 0.8 ? Theme::mute : cpu > 0.5 ? Theme::warning : Theme::textDim, (float) cpu });
        chips.add ({ juce::String (dropouts) + (dropouts == 1 ? " dropout" : " dropouts"),
                     dropouts > 0 ? Theme::warning : Theme::success });
    }
    else
    {
        chips.add ({ "No audio device", Theme::warning });
    }

    const bool split = engine.mainDeviceHasSplitClocks();
    if (split != showBanner)
    {
        showBanner = split;
        fixButton.setVisible (split);
        resized();
    }

    repaint (headerArea.getUnion (bannerArea));
}

void MainComponent::timerCallback()
{
    for (auto& s : strips) s->tick();
    for (auto& b : buses)  b->tick();

    if (++ticks % 15 == 0)
        updateStatus();

    // Every 2 s: reconnect app strips (apps start, quit and restart with new process IDs).
    if (ticks % 60 == 0)
    {
        engine.maintainSources();
        for (auto& s : strips) s->refreshDevice();
        for (auto& b : buses)  b->refreshDevice();
    }

    // Autosave every 10 s (only writes if something changed).
    if (ticks % 300 == 0)
        Session::save (engine);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);

    // Header bar
    g.setColour (Theme::panel.darker (0.25f));
    g.fillRect (headerArea);
    g.setColour (Theme::outline);
    g.fillRect (headerArea.withTop (headerArea.getBottom() - 1));

    // Logo: three bars of different height + name
    auto logo = headerArea.reduced (16, 0).removeFromLeft (150);
    const float baseY = (float) logo.getCentreY() + 9.0f;
    const float heights[] { 10.0f, 18.0f, 13.0f };
    for (int i = 0; i < 3; ++i)
    {
        g.setColour (i == 1 ? Theme::accent : Theme::accent.withAlpha (0.55f));
        g.fillRoundedRectangle ((float) logo.getX() + (float) i * 6.0f, baseY - heights[i], 4.0f, heights[i], 1.5f);
    }
    g.setColour (Theme::text);
    g.setFont (Theme::ui (19.0f, true));
    g.drawText ("OpenMix", logo.withTrimmedLeft (26), juce::Justification::centredLeft);

    // Status chips, right-aligned
    auto area = chipsArea;
    const auto font = Theme::mono (12.0f);
    g.setFont (font);
    for (int i = chips.size(); --i >= 0;)
    {
        const auto& c = chips.getReference (i);
        const int natural = juce::GlyphArrangement::getStringWidthInt (font, c.text) + 22;
        const bool isLast = i == 0;   // the device name may shrink (with "..."), others must fit
        if ((! isLast && natural > area.getWidth()) || area.getWidth() < 80)
            break;
        const int w = juce::jmin (area.getWidth(), natural);

        auto chip = area.removeFromRight (w).withSizeKeepingCentre (w, 26).toFloat();
        area.removeFromRight (6);

        g.setColour (Theme::raised);
        g.fillRoundedRectangle (chip, 13.0f);
        if (c.bar >= 0.0f)
        {
            g.setColour (c.colour.withAlpha (0.25f));
            g.fillRoundedRectangle (chip.withWidth (juce::jmax (26.0f, chip.getWidth() * juce::jlimit (0.0f, 1.0f, c.bar))), 13.0f);
        }
        g.setColour (Theme::outline);
        g.drawRoundedRectangle (chip.reduced (0.5f), 13.0f, 1.0f);
        g.setColour (c.colour == Theme::textDim ? Theme::text.withAlpha (0.85f) : c.colour);
        g.drawText (c.text, chip.reduced (10.0f, 0.0f), juce::Justification::centred, true);
    }

    // Clock-drift warning banner
    if (showBanner)
    {
        auto b = bannerArea.toFloat();
        g.setColour (Theme::warning.withAlpha (0.14f));
        g.fillRoundedRectangle (b, Theme::radius);
        g.setColour (Theme::warning.withAlpha (0.6f));
        g.drawRoundedRectangle (b.reduced (0.5f), Theme::radius, 1.0f);
        g.setColour (Theme::text);
        g.setFont (Theme::ui (13.0f));
        g.drawFittedText ("Your mic and output are different devices in Audio Settings. Their clocks drift, which crackles. "
                          "Set Input to <none> there, then pick the mic with a strip's \"Windows input device\".",
                          bannerArea.reduced (14, 0).withTrimmedRight (fixButton.getWidth() + 16),
                          juce::Justification::centredLeft, 2);
    }

    Theme::drawSectionLabel (g, "Inputs", inputsLabelArea);
    Theme::drawSectionLabel (g, "Outputs", outputsLabelArea);
}

void MainComponent::resized()
{
    auto r = getLocalBounds();

    headerArea = r.removeFromTop (56);
    auto header = headerArea.reduced (16, 0);
    header.removeFromLeft (150);
    settingsButton.setBounds (header.removeFromLeft (130).withSizeKeepingCentre (130, 30));
    header.removeFromLeft (16);
    chipsArea = header;

    r.reduce (16, 0);
    r.removeFromTop (12);

    if (showBanner)
    {
        bannerArea = r.removeFromTop (48);
        fixButton.setBounds (bannerArea.removeFromRight (180).reduced (10, 9));
        bannerArea = bannerArea.withRight (fixButton.getRight() + 10);
        r.removeFromTop (12);
    }
    else
    {
        bannerArea = {};
    }

    r.removeFromBottom (16);

    const int busWidth = 168;
    auto busArea = r.removeFromRight (busWidth * kNumBuses + Theme::gap * (kNumBuses - 1));
    outputsLabelArea = busArea.removeFromTop (22);
    for (auto& b : buses)
    {
        b->setBounds (busArea.removeFromLeft (busWidth));
        busArea.removeFromLeft (Theme::gap);
    }

    r.removeFromRight (20);
    inputsLabelArea = r.removeFromTop (22);
    const int stripWidth = juce::jmin (300, (r.getWidth() - Theme::gap * (kNumStrips - 1)) / kNumStrips);
    for (auto& s : strips)
    {
        s->setBounds (r.removeFromLeft (stripWidth));
        r.removeFromLeft (Theme::gap);
    }
}
