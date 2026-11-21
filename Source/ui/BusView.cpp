#include "ui/BusView.h"
#include "ui/Theme.h"
#include "ui/OpenMixLookAndFeel.h"

BusView::BusView (MixerEngine& e, int i)
    : engine (e), bus (e.getBus (i)), index (i),
      faderSection (bus.peakL, bus.peakR, "Bus " + juce::String (kBusNames[i]))
{
    setFocusContainerType (FocusContainerType::keyboardFocusContainer);
    setTitle ("Bus " + juce::String (kBusNames[i]));

    tape.setText (kBusNames[i]);
    addAndMakeVisible (tape);

    menuButton.setTitle ("Bus " + juce::String (kBusNames[i]) + " options");
    menuButton.setTooltip ("Monitor bus, remove bus.");
    menuButton.onClick = [this] { showBusMenu(); };
    addAndMakeVisible (menuButton);

    targetButton.onClick = [this] { showTargetMenu(); };
    addAndMakeVisible (targetButton);

    makeLamp (muteButton, "mute");
    muteButton.setTitle ("Mute bus " + juce::String (kBusNames[i]));
    muteButton.setTooltip ("Mute everything going out of " + juce::String (kBusNames[i]) + ".");
    muteButton.onClick = [this] { bus.mute.store (muteButton.getToggleState()); };
    addAndMakeVisible (muteButton);

    faderSection.fader.onValueChange = [this] { bus.faderDb.store ((float) faderSection.fader.getValue()); };
    addAndMakeVisible (faderSection);

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

void BusView::openAnalyser()
{
    auto panel = std::make_unique<AnalyserPanel> ("Bus " + juce::String (kBusNames[index]), analysis);
    auto* top = getTopLevelComponent();
    juce::CallOutBox::launchAsynchronously (std::move (panel), top->getLocalArea (&menuButton, menuButton.getLocalBounds()), top);
}

void BusView::tick()
{
    faderSection.tick();

    drainAnalysis();
    if (++analysisTicks % 10 == 0)
        repaint (loudnessArea);

    // The safety limiter lamp: lit while it reduces gain by more than 0.5 dB (held 1 s).
    const float g = bus.limiterGain.exchange (1.0f);
    if (g < 0.944f)
        limitHoldFrames = 30;
    else if (limitHoldFrames > 0)
        --limitHoldFrames;

    if (const bool now = limitHoldFrames > 0; now != limiting)
    {
        limiting = now;
        repaint (limiterArea);
    }
}

void BusView::showBusMenu()
{
    juce::PopupMenu menu;
    menu.addItem ("Analyser and loudness...", [this] { openAnalyser(); });
    menu.addSeparator();
    menu.addItem ("Monitor bus (solo plays here)", true, bus.monitor.load(), [this]
    {
        engine.setMonitorBus (bus.monitor.load() ? -1 : index);
        if (onLayoutChanged) onLayoutChanged();
    });
    menu.addSeparator();
    menu.addItem ("Remove bus...", engine.getNumActiveBuses() > 1, false, [this]
    {
        const juce::String name (kBusNames[index]);
        juce::Component::SafePointer<BusView> safe (this);
        juce::AlertWindow::showAsync (
            juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::QuestionIcon).withTitle ("Remove bus " + name + "?")
                .withMessage ("Its output is disconnected and every strip stops sending to it.").withButton ("Remove", 1).withButton ("Cancel", 0).withAssociatedComponent (this),
            [safe] (int result)
            {
                if (safe == nullptr || result != 1)
                    return;
                safe->engine.removeBus (safe->index);
                if (safe->onLayoutChanged)
                    safe->onLayoutChanged();
            });
    });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (menuButton));
}

void BusView::refreshFromBus()
{
    faderSection.fader.setValue (bus.faderDb.load(), juce::dontSendNotification);
    muteButton.setToggleState (bus.mute.load(), juce::dontSendNotification);
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
    targetButton.setTitle (juce::String (kBusNames[index]) + " output: " + text);

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
        targetButton.setTooltip (juce::String (kBusNames[index]) + " output: " + text + ". " + hint);
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

    Theme::drawLegend (g, isVirtual() ? "Virtual out" : "Hardware out", kindLegend);
    Theme::drawLegend (g, "Output", outputLegend);

    // Limiter lamp: a real state (the -0.3 dBFS safety limiter is reducing gain right now).
    auto la = limiterArea;
    OpenMixLookAndFeel::drawLamp (g, la.removeFromLeft (10).withSizeKeepingCentre (8, 8).toFloat(), limiting, t.mute);
    la.removeFromLeft (6);
    g.setFont (Theme::ui (11.5f, true));
    g.setColour (limiting ? t.text : t.textFaint);
    g.drawText (limiting ? "LIMITING" : "LIMITER", la, juce::Justification::centredLeft);

    // Monitor lamp: this is the bus that plays soloed strips.
    auto ma = monitorArea;
    const bool isMonitor = bus.monitor.load();
    OpenMixLookAndFeel::drawLamp (g, ma.removeFromLeft (10).withSizeKeepingCentre (8, 8).toFloat(), isMonitor, t.accent);
    ma.removeFromLeft (6);
    g.setColour (isMonitor ? t.text : t.textFaint);
    g.drawText (isMonitor ? "MONITOR (SOLO)" : "MONITOR", ma, juce::Justification::centredLeft);

    // Short-term loudness (3 s), the number streaming platforms normalise to.
    auto la2 = loudnessArea;
    Theme::drawLegend (g, "Loudness", la2.removeFromLeft (70));
    const double st = analysis->loudness.shortTerm();
    g.setFont (Theme::mono (11.5f));
    g.setColour (std::isfinite (st) ? t.text : t.textFaint);
    g.drawText (std::isfinite (st) ? juce::String (st, 1) + " LUFS" : juce::String ("no signal"), la2, juce::Justification::centredRight);
}

void BusView::resized()
{
    auto r = getLocalBounds().reduced (Theme::s3, Theme::s3);

    auto titleRow = r.removeFromTop (30);
    menuButton.setBounds (titleRow.removeFromRight (30));
    titleRow.removeFromRight (Theme::s1);
    tape.setBounds (titleRow);
    r.removeFromTop (Theme::s1);
    kindLegend = r.removeFromTop (18);
    r.removeFromTop (Theme::s2);

    outputLegend = r.removeFromTop (18);
    targetButton.setBounds (r.removeFromTop (30));
    r.removeFromTop (Theme::s3);
    limiterArea = r.removeFromTop (18);
    monitorArea = r.removeFromTop (18);
    loudnessArea = r.removeFromTop (18);

    muteButton.setBounds (r.removeFromBottom (30));
    r.removeFromBottom (Theme::s3);
    r.removeFromTop (Theme::s3);
    faderSection.setBounds (r);
}
