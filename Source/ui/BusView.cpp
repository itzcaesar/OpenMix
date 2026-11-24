#include "ui/BusView.h"
#include "ui/Theme.h"
#include "ui/OpenMixLookAndFeel.h"
#include "app/Session.h"

BusView::BusView (MixerEngine& e, int i)
    : engine (e), bus (e.getBus (i)), index (i),
      inserts (e, e.getBus (i).inserts),
      faderSection (bus.peakL, bus.peakR, bus.rmsL, bus.rmsR, "Bus " + engine.getBusName (i))
{
    setFocusContainerType (FocusContainerType::keyboardFocusContainer);
    setTitle ("Bus " + engine.getBusName (i));

    tape.setText (engine.getBusName (i));
    addAndMakeVisible (tape);

    menuButton.setTitle ("Bus " + engine.getBusName (i) + " options");
    menuButton.setTooltip ("Rename, analyser and loudness, monitor, remove bus.");
    menuButton.onClick = [this] { showBusMenu(); };
    addAndMakeVisible (menuButton);

    targetButton.onClick = [this] { showTargetMenu(); };
    addAndMakeVisible (targetButton);

    makeLamp (muteButton, "mute");
    muteButton.setTitle ("Mute bus " + engine.getBusName (i));
    muteButton.setTooltip ("Mute everything going out of " + engine.getBusName (i) + ".");
    muteButton.onClick = [this] { bus.mute.store (muteButton.getToggleState()); };
    addAndMakeVisible (muteButton);

    faderSection.fader.onValueChange = [this] { bus.faderDb.store ((float) faderSection.fader.getValue()); };
    addAndMakeVisible (faderSection);

    // Processing keys: click opens the settings, Space switches on/off (same as on a strip).
    for (auto [key, proc] : { std::pair { &eqKey, Processor::Eq }, std::pair { &compKey, Processor::Comp } })
    {
        makeLamp (*key, "key");
        key->onClick = [this, p = proc] { openProcessorPanel (p); };
        key->onSpace = [this, p = proc]
        {
            auto& flag = processorOn (bus.fx, p);
            flag.store (! flag.load());
            updateKeys();
        };
        addAndMakeVisible (*key);
    }

    inserts.onRowCountChanged = [this] { if (! inLayout && getHeight() > 0) resized(); };
    addAndMakeVisible (inserts);

    spectrum.setTitle ("Bus " + engine.getBusName (i) + " spectrum. Opens the analyser.");
    spectrum.onClick = [this] { openAnalyser(); };
    addChildComponent (spectrum);

    refreshFromBus();
}

void BusView::drainAnalysis()
{
    const double sr = engine.getSampleRate();
    if (sr != analysis->sampleRate)
    {
        analysis->sampleRate = sr;
        analysis->loudness.prepare (sr);
    }

    auto& fifo = bus.analysisFifo;
    for (int ready = fifo.getNumReady(); ready > 0; ready = fifo.getNumReady())
    {
        const auto scope = fifo.read (juce::jmin (ready, drainBuffer.getNumSamples()));
        int done = 0;
        for (const auto [start, size] : { std::pair { scope.startIndex1, scope.blockSize1 }, std::pair { scope.startIndex2, scope.blockSize2 } })
        {
            if (size <= 0)
                continue;
            drainBuffer.copyFrom (0, done, bus.analysisBuffer, 0, start, size);
            drainBuffer.copyFrom (1, done, bus.analysisBuffer, 1, start, size);
            done += size;
        }
        analysis->push (drainBuffer.getReadPointer (0), drainBuffer.getReadPointer (1), done);
    }
}

void BusView::updateKeys()
{
    const auto name = busName();
    eqKey.setToggleState (bus.fx.eqOn.load(), juce::dontSendNotification);
    compKey.setToggleState (bus.fx.compOn.load(), juce::dontSendNotification);
    eqKey.setTitle ("Bus " + name + " EQ, " + (bus.fx.eqOn.load() ? "on" : "off"));
    compKey.setTitle ("Bus " + name + " compressor, " + (bus.fx.compOn.load() ? "on" : "off"));
    eqKey.setTooltip ("EQ on " + name + " only. Click for settings, Space switches it on or off.");
    compKey.setTooltip ("Compressor on " + name + " only. Click for settings, Space switches it on or off.");
}

void BusView::openProcessorPanel (Processor p)
{
    auto panel = std::make_unique<ProcessorPanel> (engine, bus.fx, p);
    juce::Component::SafePointer<BusView> safe (this);
    panel->onChange = [safe] { if (safe != nullptr) safe->updateKeys(); };

    auto& key = p == Processor::Eq ? eqKey : compKey;
    auto* top = getTopLevelComponent();
    juce::CallOutBox::launchAsynchronously (std::move (panel), top->getLocalArea (&key, key.getLocalBounds()), top);
}

void BusView::openAnalyser()
{
    auto panel = std::make_unique<AnalyserPanel> ("Bus " + engine.getBusName (index), analysis);
    auto* top = getTopLevelComponent();
    juce::CallOutBox::launchAsynchronously (std::move (panel), top->getLocalArea (&menuButton, menuButton.getLocalBounds()), top);
}

void BusView::tick()
{
    faderSection.tick();
    inserts.tick();

    drainAnalysis();
    if (++analysisTicks % 10 == 0)
        repaint (loudnessArea);
    if (spectrum.isVisible() && analysisTicks % 3 == 0)
        spectrum.update();

    // The safety limiter lamp: lit while it reduces gain by more than 0.5 dB (held 1 s).
    // limiterReductionDb is the smoothed reduction for the gain-reduction meter.
    const float g = bus.limiterGain.exchange (1.0f);
    const float reduction = g >= 1.0f ? 0.0f : -juce::Decibels::gainToDecibels (g, -60.0f);
    limiterReductionDb = juce::jmax (reduction, limiterReductionDb * 0.85f);
    if (g < 0.944f)
        limitHoldFrames = 30;
    else if (limitHoldFrames > 0)
        --limitHoldFrames;

    if (const bool now = limitHoldFrames > 0; now != limiting)
    {
        limiting = now;
        repaint (limiterArea);
    }
    else if (limiterReductionDb > 0.05f)
    {
        repaint (limiterArea);
    }
}

void BusView::showBusMenu()
{
    juce::PopupMenu menu;
    menu.addItem ("Rename bus...", [this] { renameBus(); });
    menu.addItem ("Analyser and loudness...", [this] { openAnalyser(); });

    // Loudness normalisation: a slow gain that pulls this bus towards the target,
    // so a quiet mic and a loud game end up at the same streaming level.
    juce::PopupMenu normMenu;
    normMenu.addItem ("Off", true, ! bus.normOn.load(), [this] { bus.normOn.store (false); });
    for (const float target : { -23.0f, -16.0f, -14.0f })
        normMenu.addItem (juce::String ((int) target) + " LUFS", true,
                          bus.normOn.load() && bus.normTargetLufs.load() == target, [this, target]
                          {
                              bus.normTargetLufs.store (target);
                              bus.normOn.store (true);
                          });
    menu.addSubMenu ("Loudness normalisation", normMenu);
    menu.addSeparator();
    // The monitor bus is shown on the strip only by its accent outline and lit name
    // bar -- there is no "monitor" text on the strip itself.
    menu.addItem ("Monitor bus", true, bus.monitor.load(), [this]
    {
        engine.setMonitorBus (bus.monitor.load() ? -1 : index);
        if (onLayoutChanged) onLayoutChanged();
    });
    menu.addSeparator();
    menu.addItem ("Remove bus...", engine.getNumActiveBuses() > 1, false, [this]
    {
        const auto name = busName();
        juce::Component::SafePointer<BusView> safe (this);
        juce::AlertWindow::showAsync (
            juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::QuestionIcon).withTitle ("Remove bus " + name + "?")
                .withMessage ("Its output is disconnected and every strip stops sending to it.").withButton ("Remove", 1).withButton ("Cancel", 0).withAssociatedComponent (this),
            [safe] (int result)
            {
                if (safe == nullptr || result != 1)
                    return;
                safe->inserts.closeAllEditors();
                safe->engine.removeBus (safe->index);
                if (safe->onLayoutChanged)
                    safe->onLayoutChanged();
            });
    });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (menuButton));
}

void BusView::renameBus()
{
    auto* w = new juce::AlertWindow ("Rename bus",
                                     "A custom name is shown on the strip and on the strips' send keys. "
                                     "Leave it empty for the default name (" + juce::String (kBusNames[index]) + "). "
                                     "The bus stays in its group -- renaming can never move it between "
                                     "Hardware and Virtual.",
                                     juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", busName(), "Bus name");
    w->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<BusView> safe (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int result)
    {
        if (safe == nullptr || result != 1)
            return;
        safe->engine.setBusName (safe->index, w->getTextEditorContents ("name"));
        safe->refreshFromBus();
        Session::save (safe->engine);
        if (safe->onLayoutChanged)
            safe->onLayoutChanged();   // the strips' send keys show the new name
    }), true);
}

void BusView::refreshFromBus()
{
    tape.setText (busName());
    setTitle ("Bus " + busName());
    menuButton.setTitle ("Bus " + busName() + " options");
    muteButton.setTitle ("Mute bus " + busName());
    muteButton.setTooltip ("Mute everything going out of " + busName() + ".");
    spectrum.setTitle ("Bus " + busName() + " spectrum. Opens the analyser.");
    faderSection.fader.setTitle ("Bus " + busName() + " fader");
    faderSection.fader.setValue (bus.faderDb.load(), juce::dontSendNotification);
    muteButton.setToggleState (bus.mute.load(), juce::dontSendNotification);
    updateKeys();
    inserts.refresh();
    refreshDevice();
}

void BusView::refreshDevice()
{
    juce::String caption, text;
    bool problem = false;

    switch ((BusTarget) bus.target.load())
    {
        case BusTarget::Off:
            caption = "OFF";
            text = "Not connected";
            break;

        case BusTarget::MainPair:
        {
            caption = "MAIN";
            const auto pairs = engine.getActiveOutputPairNames();
            const int pair = bus.outputPair.load();
            problem = ! juce::isPositiveAndBelow (pair, pairs.size());
            text = problem ? juce::String ("Output missing") : pairs[pair];
            break;
        }

        case BusTarget::Device:
            caption = "DEVICE";
            problem = bus.sink == nullptr;
            text = bus.deviceName + (problem ? " (unplugged)" : "");
            break;
    }

    targetButton.setContent (caption, text, problem);
    targetButton.setTitle (engine.getBusName (index) + " output: " + text);

    // B buses: say what to pick in Discord/OBS, or what B buses are for.
    if (isVirtual())
    {
        const bool onCable = (BusTarget) bus.target.load() == BusTarget::Device && isVirtualCable (bus.deviceName);
        juce::String hint;
        if (onCable)
        {
            // Virtual cables pair an "Input" (what OpenMix plays into) with an "Output" (the mic side).
            const auto otherEnd = bus.deviceName.containsIgnoreCase ("Input")
                                    ? bus.deviceName.replaceFirstOccurrenceOf ("Input", "Output", true)
                                    : juce::String ("this cable's other end");
            hint = "In Discord or OBS, choose " + otherEnd + " as the microphone to hear this bus there.";
        }
        else
        {
            hint = "B buses are meant for a virtual cable: pick one here, then choose its other end "
                   "(for example CABLE Output) as the microphone in Discord or OBS.";
        }
        targetButton.setTooltip (engine.getBusName (index) + " output: " + text + ". " + hint);
        targetButton.setDescription (hint);
    }
}

void BusView::showTargetMenu()
{
    juce::PopupMenu menu;
    const auto current = (BusTarget) bus.target.load();

    menu.addItem ("Not connected", true, current == BusTarget::Off, [this]
    {
        engine.setBusOff (index);
        refreshDevice();
    });

    menu.addSectionHeader ("Main device");
    const auto pairs = engine.getActiveOutputPairNames();
    if (pairs.isEmpty())
        menu.addItem ("No outputs enabled. Turn some on in Audio settings.", false, false, nullptr);
    for (int i = 0; i < pairs.size(); ++i)
        menu.addItem (pairs[i], true, current == BusTarget::MainPair && bus.outputPair.load() == i, [this, i]
        {
            engine.setBusMainPair (index, i);
            refreshDevice();
        });

    // Split Windows outputs into virtual cables and real devices. B buses are meant for virtual
    // cables (their other end is what Discord/OBS use as a microphone), A buses for real devices.
    juce::StringArray cables, physical;
    for (const auto& name : engine.getWindowsOutputNames())
        (isVirtualCable (name) ? cables : physical).add (name);

    auto addDevice = [this, current] (juce::PopupMenu& m, const juce::String& name)
    {
        m.addItem (name, true, current == BusTarget::Device && bus.deviceName == name, [this, name]
        {
            juce::String error;
            if (! engine.setBusDevice (index, name, error))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Couldn't open " + name, error);
            refreshDevice();
        });
    };

    if (isVirtual())
    {
        menu.addSectionHeader ("Virtual cable (recommended): then pick its other end as the mic in Discord or OBS");
        if (cables.isEmpty())
            menu.addItem ("No virtual cable found. Install one (for example VB-Audio Virtual Cable), then open this menu again.",
                          false, false, nullptr);
        for (const auto& name : cables)
            addDevice (menu, name);

        menu.addSectionHeader ("Other Windows outputs");
        if (physical.isEmpty())
            menu.addItem ("No other output devices found", false, false, nullptr);
        for (const auto& name : physical)
            addDevice (menu, name);
    }
    else
    {
        menu.addSectionHeader ("Windows output device");
        if (physical.isEmpty())
            menu.addItem ("No output devices found", false, false, nullptr);
        for (const auto& name : physical)
            addDevice (menu, name);

        if (! cables.isEmpty())
        {
            menu.addSectionHeader ("Virtual cables (usually for B buses)");
            for (const auto& name : cables)
                addDevice (menu, name);
        }
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (targetButton).withMinimumWidth (260));
}

bool BusView::isVirtualCable (const juce::String& deviceName)
{
    // Names used by the common virtual-cable drivers: VB-Audio Virtual Cable ("CABLE Input"),
    // VoiceMeeter ("Voicemeeter Input"), VAC ("Line 1 (Virtual Audio Cable)") and similar.
    const auto n = deviceName.toLowerCase();
    return n.contains ("cable") || n.contains ("vb-audio") || n.contains ("voicemeeter") || n.contains ("virtual");
}

void BusView::paint (juce::Graphics& g)
{
    const auto& t = Theme::p();
    Theme::drawFaceplate (g, getLocalBounds().toFloat());

    // OUTPUT: the device this bus plays into.
    Theme::drawLegend (g, "Output", outputLegend);

    // LIMITER: the -0.3 dBFS safety limiter -- lamp, state, and a gain-reduction meter.
    Theme::drawLegend (g, "Limiter", limiterLegend);
    {
        auto la = limiterArea;
        g.setFont (Theme::ui (11.5f, true));
        OpenMixLookAndFeel::drawLamp (g, la.removeFromLeft (10).withSizeKeepingCentre (8, 8).toFloat(), limiting, t.mute);
        la.removeFromLeft (5);
        g.setColour (limiting ? t.text : t.textFaint);
        g.drawText (limiting ? "LIMITING" : "LIMIT", la.removeFromLeft (58), juce::Justification::centredLeft);

        // Gain-reduction bar (0..24 dB) with a numeric readout.
        auto readout = la.removeFromRight (44);
        auto bar = la.toFloat().withSizeKeepingCentre ((float) la.getWidth(), 8.0f);
        g.setColour (t.meterWell);
        g.fillRoundedRectangle (bar, 2.0f);
        g.setColour (t.meterMid);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, limiterReductionDb / 24.0f)), 2.0f);
        g.setFont (Theme::mono (11.0f));
        g.setColour (limiterReductionDb > 0.05f ? t.text : t.textFaint);
        g.drawText (limiterReductionDb < 0.05f ? juce::String ("0.0 dB") : "-" + juce::String (limiterReductionDb, 1) + " dB",
                    readout, juce::Justification::centredRight);
    }

    // LOUDNESS: short-term LUFS (3 s), the number streaming platforms normalise to.
    // With normalisation on, show the target and the gain being applied.
    // (Momentary / integrated LUFS are deliberately not shown here.)
    Theme::drawLegend (g, "Loudness", loudnessLegend);
    auto la2 = loudnessArea;
    const double st = analysis->loudness.shortTerm();
    juce::String loudText = std::isfinite (st) ? juce::String (st, 1) + " LUFS" : juce::String ("no signal");
    if (bus.normOn.load())
    {
        const float normDb = juce::Decibels::gainToDecibels (bus.normGain.load(), -100.0f);
        loudText += " · NORM " + juce::String ((int) bus.normTargetLufs.load())
                  + (normDb > 0.05f ? " +" : " ") + juce::String (normDb, 1) + " dB";
    }
    g.setFont (Theme::mono (12.0f));
    g.setColour (std::isfinite (st) ? t.text : t.textFaint);
    g.drawText (loudText, la2, juce::Justification::centredRight);

    g.setColour (t.line);
    g.fillRect (Theme::s3, (bayTop > 0 ? bayTop : getHeight() / 2) - Theme::s3 / 2 - 1, getWidth() - 2 * Theme::s3, 1);

    Theme::drawLegend (g, "Processing", processLegend);
    Theme::drawLegend (g, "Inserts", insertsLegend);
    if (! spectrumLegend.isEmpty())
        Theme::drawLegend (g, "Analyser", spectrumLegend);
    if (const int lat = bus.inserts.latencySamples.load(); lat > 0 && engine.getSampleRate() > 0)
    {
        g.setFont (Theme::mono (11.0f));
        g.setColour (t.textFaint);
        g.drawText (juce::String (1000.0 * lat / engine.getSampleRate(), 1) + " ms", insertsLegend, juce::Justification::centredRight);
    }
}

// The monitor bus is shown only here: a thin 1 px accent outline around the
// strip, plus the name bar lit in the accent colour. No text on the strip says
// "monitor" -- the outline and the lit name bar are the whole signal.
void BusView::paintOverChildren (juce::Graphics& g)
{
    if (! bus.monitor.load())
        return;

    const auto& t = Theme::p();
    g.setColour (t.accent);
    g.drawRect (getLocalBounds().toFloat(), 1.0f);
    g.fillRect (tape.getBounds().withHeight (3).translated (0, tape.getHeight() - 3).toFloat());
}

void BusView::resized()
{
    const juce::ScopedValueSetter<bool> layoutGuard (inLayout, true);
    auto r = getLocalBounds().reduced (Theme::s3, Theme::s3);
    const int split = bayTop > 0 ? bayTop : getHeight() / 2;
    auto top = r.withBottom (split - Theme::s3);
    auto bay = r.withTop (split);
    constexpr int legendH = 16, gap = 10;

    auto titleRow = top.removeFromTop (28);
    menuButton.setBounds (titleRow.removeFromRight (28));
    titleRow.removeFromRight (Theme::s1);
    tape.setBounds (titleRow);
    top.removeFromTop (Theme::s2);

    outputLegend = top.removeFromTop (legendH);
    targetButton.setBounds (top.removeFromTop (32));
    top.removeFromTop (Theme::s2);

    limiterLegend = top.removeFromTop (legendH);
    limiterArea = top.removeFromTop (18);
    loudnessLegend = top.removeFromTop (legendH);
    loudnessArea = top.removeFromTop (18);
    top.removeFromTop (gap);

    processLegend = top.removeFromTop (legendH);
    {
        auto row = top.removeFromTop (24);
        const int kw = (row.getWidth() - 2) / 2;
        eqKey.setBounds (row.removeFromLeft (kw));
        row.removeFromLeft (2);
        compKey.setBounds (row);
    }
    top.removeFromTop (gap);

    // Inserts take the rows they need (up to what fits, the list scrolls past that); a live
    // spectrum fills the rest above the bay when there is room for one.
    insertsLegend = top.removeFromTop (legendH);
    const int fit = (top.getHeight() + InsertRack::rowGap()) / (InsertRack::rowHeight() + InsertRack::rowGap());
    inserts.setBounds (top.removeFromTop (InsertRack::heightForRows (juce::jlimit (1, kNumSlots, juce::jmin (fit, inserts.getVisibleRowCount())))));
    top.removeFromTop (gap);

    const bool roomForSpectrum = top.getHeight() >= legendH + 36;
    spectrum.setVisible (roomForSpectrum);
    if (roomForSpectrum)
    {
        spectrumLegend = top.removeFromTop (legendH);
        spectrum.setBounds (top.withHeight (juce::jmin (top.getHeight(), 140)));
    }
    else
    {
        spectrumLegend = {};
    }

    // Fader bay, aligned with the strips: mute along the bottom, fader + meter above.
    muteButton.setBounds (bay.removeFromBottom (28));
    bay.removeFromBottom (Theme::s2);
    faderSection.setBounds (bay);
}
