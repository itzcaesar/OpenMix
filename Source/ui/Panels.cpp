#include "ui/Panels.h"
#include "ui/Theme.h"

juce::String processorShortName (Processor p)
{
    switch (p)
    {
        case Processor::Denoise: return "NR";
        case Processor::Gate:    return "Gate";
        case Processor::Eq:      return "EQ";
        case Processor::Comp:    return "Comp";
        case Processor::DeEss:   return "DS";
        case Processor::Duck:    return "Duck";
    }
    return {};
}

juce::String processorName (Processor p)
{
    switch (p)
    {
        case Processor::Denoise: return "Noise removal";
        case Processor::Gate:    return "Noise gate";
        case Processor::Eq:      return "EQ";
        case Processor::Comp:    return "Compressor";
        case Processor::DeEss:   return "De-esser";
        case Processor::Duck:    return "Ducking";
    }
    return {};
}

std::atomic<bool>& processorOn (ProcessingParams& fx, Processor p)
{
    switch (p)
    {
        case Processor::Denoise: return fx.denoiseOn;
        case Processor::Gate:    return fx.gateOn;
        case Processor::Eq:      return fx.eqOn;
        case Processor::Comp:    return fx.compOn;
        case Processor::DeEss:   return fx.deessOn;
        case Processor::Duck:    return fx.duckOn;
    }
    return fx.gateOn;
}

//==============================================================================
// PanelBase

PanelBase::PanelBase (const juce::String& t, std::atomic<bool>* flag) : title (t), onFlag (flag)
{
    setFocusContainerType (FocusContainerType::keyboardFocusContainer);
    setTitle (t);

    if (onFlag != nullptr)
    {
        makeLamp (onButton, "accent");
        onButton.setToggleState (onFlag->load(), juce::dontSendNotification);
        onButton.setTitle (t + " on");
        onButton.onClick = [this]
        {
            onFlag->store (onButton.getToggleState());
            if (onChange) onChange();
        };
        addAndMakeVisible (onButton);
    }

    startTimerHz (30);
}

void PanelBase::addKnob (const KnobSpec& spec)
{
    Knob k;
    k.label = spec.label;
    k.slider = std::make_unique<DbSlider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
    auto* s = k.slider.get();

    s->setRange (spec.min, spec.max, spec.step);
    if (spec.skewMid > 0.0)
        s->setSkewFactorFromMidPoint (spec.skewMid);
    s->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 18);
    const auto suffix = spec.suffix;
    s->textFromValueFunction = [suffix] (double v)
    {
        const bool whole = std::abs (v) >= 100.0 || suffix == " %";
        return juce::String (v, whole ? 0 : 1) + suffix;
    };
    // Keyboard: dB controls step 1 / 6 dB; others step 2% / 10% of their range.
    const double span = spec.max - spec.min;
    s->keyStep  = spec.suffix == " dB" ? 1.0 : juce::jmax (spec.step, span / 50.0);
    s->pageStep = spec.suffix == " dB" ? 6.0 : span / 10.0;
    s->homeValue = spec.min <= 0.0 && spec.max >= 0.0 && spec.suffix == " dB" ? 0.0
                 : spec.skewMid > 0.0 ? spec.skewMid : spec.max;
    s->setDoubleClickReturnValue (true, s->homeValue);

    s->setValue (spec.target.load() * spec.scale, juce::dontSendNotification);
    s->updateText();
    s->setTitle (title + " " + spec.label);
    s->setWantsKeyboardFocus (true);
    makeClickable (*s);

    auto* target = &spec.target;
    const double scale = spec.scale;
    s->onValueChange = [this, s, target, scale]
    {
        target->store ((float) (s->getValue() / scale));
        if (onChange) onChange();
    };

    addAndMakeVisible (s);
    knobs.push_back (std::move (k));
}

void PanelBase::setMeter (std::atomic<float>* gainMeter, const juce::String& label)
{
    meterSource = gainMeter;
    meterLabel = label;
}

void PanelBase::timerCallback()
{
    if (meterSource != nullptr)
    {
        const float reduction = -juce::Decibels::gainToDecibels (meterSource->exchange (1.0f), -60.0f);
        meterDb = juce::jmax (reduction, meterDb * 0.85f);
        repaint (meterArea);
    }
    tick();
}

void PanelBase::paint (juce::Graphics& g)
{
    const auto& t = Theme::p();

    g.setColour (t.text);
    g.setFont (Theme::ui (15.0f, true));
    g.drawText (title, titleArea, juce::Justification::centredLeft);

    // Knob labels
    g.setFont (Theme::ui (11.5f, true).withExtraKerningFactor (0.04f));
    g.setColour (t.textFaint);
    for (const auto& k : knobs)
        g.drawText (k.label.toUpperCase(), k.slider->getBounds().withHeight (16).translated (0, -16),
                    juce::Justification::centred);

    // Gain-reduction meter: a bar growing right with the amount of reduction (0..24 dB)
    if (meterSource != nullptr)
    {
        auto r = meterArea;
        g.setFont (Theme::ui (11.5f, true));
        g.setColour (t.textFaint);
        g.drawText (meterLabel.toUpperCase(), r.removeFromLeft (110), juce::Justification::centredLeft);

        auto readout = r.removeFromRight (60);
        auto bar = r.toFloat().withSizeKeepingCentre ((float) r.getWidth(), 8.0f);
        g.setColour (t.meterWell);
        g.fillRoundedRectangle (bar, 2.0f);
        g.setColour (t.meterMid);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, meterDb / 24.0f)), 2.0f);

        g.setFont (Theme::mono (12.0f));
        g.setColour (t.textDim);
        g.drawText (meterDb < 0.05f ? juce::String ("0.0 dB") : "-" + juce::String (meterDb, 1) + " dB",
                    readout, juce::Justification::centredRight);
    }

    if (help.isNotEmpty())
    {
        g.setFont (Theme::ui (12.5f));
        g.setColour (t.textDim);
        g.drawFittedText (help, helpArea, juce::Justification::topLeft, 3);
    }
}

void PanelBase::resized()
{
    auto r = getLocalBounds().reduced (Theme::s3);

    auto top = r.removeFromTop (30);
    if (onFlag != nullptr)
        onButton.setBounds (top.removeFromRight (64));
    titleArea = top;
    r.removeFromTop (Theme::s2);

    layoutExtra (r);

    if (! knobs.empty())
    {
        r.removeFromTop (16);   // knob labels
        auto row = r.removeFromTop (76);
        const int w = row.getWidth() / (int) knobs.size();
        for (auto& k : knobs)
            k.slider->setBounds (row.removeFromLeft (w).reduced (4, 0));
        r.removeFromTop (Theme::s2);
    }

    meterArea = meterSource != nullptr ? r.removeFromTop (20) : juce::Rectangle<int>();
    if (meterSource != nullptr)
        r.removeFromTop (Theme::s2);
    helpArea = r;
}

//==============================================================================
// ProcessorPanel

ProcessorPanel::ProcessorPanel (MixerEngine& e, ProcessingParams& params, Processor p, int strip)
    : PanelBase (processorName (p), &processorOn (params, p)),
      engine (e), fx (params), kind (p), stripIndex (strip)
{
    const bool onBus = stripIndex < 0;
    int height = 230;

    switch (p)
    {
        case Processor::Denoise:
            addKnob ({ "Amount", 0, 100, 1, " %", fx.denoiseAmount, 0.0, 100.0 });
            setMeter (nullptr, {});
            setHelp (fx.denoiseUnavailable.load()
                       ? "Noise removal couldn't start. Restart OpenMix and try again."
                       : "Removes steady background noise (fans, hum, keyboard) from voice. Adds "
                         + juce::String (fx.denoiseLatencyMs.load()) + " ms of latency, which the other strips are matched to.");
            break;

        case Processor::Gate:
            addKnob ({ "Threshold", -80, 0, 0.5, " dB", fx.gateThresholdDb });
            addKnob ({ "Range", -80, 0, 0.5, " dB", fx.gateRangeDb });
            setMeter (&fx.gateGain, "Reduction");
            setHelp ("Turns the strip down while it's quiet. Set the threshold just above the room noise.");
            break;

        case Processor::Eq:
            addKnob ({ "Low", -12, 12, 0.1, " dB", fx.eqLowDb });
            addKnob ({ "Mid", -12, 12, 0.1, " dB", fx.eqMidDb });
            addKnob ({ "Mid freq", 100, 10000, 1, " Hz", fx.eqMidHz, 1000.0 });
            addKnob ({ "High", -12, 12, 0.1, " dB", fx.eqHighDb });
            setHelp (onBus ? "Shapes this bus only (for example a brighter stream mix). Low shelf at 120 Hz, "
                             "a sweepable middle band, high shelf at 8 kHz."
                           : "Low shelf at 120 Hz, a sweepable middle band, high shelf at 8 kHz. "
                             "The high-pass and low-pass filters live here too: switch them on above "
                             "and drag the cutoff.");
            height = 200;
            if (! onBus)
            {
                // HPF/LPF live here, not in a strip section: an on/off key plus a
                // cutoff slider each, above the EQ knobs.
                makeLamp (hpfToggle, "key");
                hpfToggle.setToggleState (fx.hpfOn.load(), juce::dontSendNotification);
                hpfToggle.setTitle ("High-pass filter");
                hpfToggle.setTooltip ("High-pass filter: cut rumble below the cutoff.");
                hpfToggle.onClick = [this]
                {
                    fx.hpfOn.store (hpfToggle.getToggleState());
                    hpfCutoffSlider.setEnabled (hpfToggle.getToggleState());
                    if (onChange) onChange();
                };
                addAndMakeVisible (hpfToggle);

                hpfCutoffSlider.setRange (20.0, 1000.0, 1.0);
                hpfCutoffSlider.setSkewFactorFromMidPoint (80.0);
                hpfCutoffSlider.setDoubleClickReturnValue (true, 80.0);
                hpfCutoffSlider.textFromValueFunction = [] (double v) { return juce::String ((int) v) + " Hz"; };
                hpfCutoffSlider.setValue (fx.hpfHz.load(), juce::dontSendNotification);
                hpfCutoffSlider.setEnabled (fx.hpfOn.load());
                hpfCutoffSlider.setTitle ("High-pass filter cutoff");
                hpfCutoffSlider.setTooltip ("High-pass filter cutoff. Double-click for 80 Hz.");
                hpfCutoffSlider.setWantsKeyboardFocus (true);
                makeClickable (hpfCutoffSlider);
                hpfCutoffSlider.onValueChange = [this] { fx.hpfHz.store ((float) hpfCutoffSlider.getValue()); };
                addAndMakeVisible (hpfCutoffSlider);

                makeLamp (lpfToggle, "key");
                lpfToggle.setToggleState (fx.lpfOn.load(), juce::dontSendNotification);
                lpfToggle.setTitle ("Low-pass filter");
                lpfToggle.setTooltip ("Low-pass filter: cut hiss above the cutoff.");
                lpfToggle.onClick = [this]
                {
                    fx.lpfOn.store (lpfToggle.getToggleState());
                    lpfCutoffSlider.setEnabled (lpfToggle.getToggleState());
                    if (onChange) onChange();
                };
                addAndMakeVisible (lpfToggle);

                lpfCutoffSlider.setRange (1000.0, 20000.0, 10.0);
                lpfCutoffSlider.setSkewFactorFromMidPoint (8000.0);
                lpfCutoffSlider.setDoubleClickReturnValue (true, 18000.0);
                lpfCutoffSlider.textFromValueFunction = [] (double v) { return juce::String (v / 1000.0, 1) + " kHz"; };
                lpfCutoffSlider.setValue (fx.lpfHz.load(), juce::dontSendNotification);
                lpfCutoffSlider.setEnabled (fx.lpfOn.load());
                lpfCutoffSlider.setTitle ("Low-pass filter cutoff");
                lpfCutoffSlider.setTooltip ("Low-pass filter cutoff. Double-click for 18 kHz.");
                lpfCutoffSlider.setWantsKeyboardFocus (true);
                makeClickable (lpfCutoffSlider);
                lpfCutoffSlider.onValueChange = [this] { fx.lpfHz.store ((float) lpfCutoffSlider.getValue()); };
                addAndMakeVisible (lpfCutoffSlider);

                height = 280;
            }
            break;

        case Processor::Comp:
            addKnob ({ "Threshold", -60, 0, 0.5, " dB", fx.compThresholdDb });
            addKnob ({ "Ratio", 1, 20, 0.1, ":1", fx.compRatio, 4.0 });
            setMeter (&fx.compGain, "Reduction");
            setHelp (onBus ? "Evens out the whole bus mix (glue for a stream or recording). Make-up gain is added "
                             "automatically; the safety limiter still catches peaks after it."
                           : "Evens out loud and quiet parts. Make-up gain is added automatically.");
            break;

        case Processor::DeEss:
            addKnob ({ "Threshold", -60, 0, 0.5, " dB", fx.deessThresholdDb });
            addKnob ({ "Max cut", 0, 20, 0.5, " dB", fx.deessMaxDb });
            setMeter (&fx.deessGain, "Reduction");
            setHelp ("Softens harsh S and T sounds by turning down the top end only while they happen.");
            break;

        case Processor::Duck:
            addKnob ({ "Depth", -40, 0, 0.5, " dB", fx.duckDepthDb });
            addKnob ({ "Threshold", -80, 0, 0.5, " dB", fx.duckThresholdDb });
            setMeter (&fx.duckGain, "Ducking");
            setHelp ("Turns this strip down while the chosen strip has signal, e.g. music under your voice.");

            duckLabel.setText ("Duck under", juce::dontSendNotification);
            duckLabel.setFont (Theme::ui (13.0f));
            addAndMakeVisible (duckLabel);

            duckSource.addItem ("Nothing", 1);
            for (int s = 0; s < kNumStrips; ++s)
            {
                auto& other = engine.getStrip (s);
                if (other.active.load() && s != stripIndex)
                    duckSource.addItem (other.name.isNotEmpty() ? other.name : "Input " + juce::String (s + 1), s + 2);
            }
            duckSource.setSelectedId (fx.duckSource.load() + 2, juce::dontSendNotification);
            if (duckSource.getSelectedId() == 0)
                duckSource.setSelectedId (1, juce::dontSendNotification);
            duckSource.setTitle ("Duck under which strip");
            duckSource.onChange = [this]
            {
                fx.duckSource.store (duckSource.getSelectedId() - 2);
                if (onChange) onChange();
            };
            makeClickable (duckSource);
            addAndMakeVisible (duckSource);
            height = 270;
            break;
    }

    setSize (p == Processor::Eq ? 360 : 320, height);
}

void ProcessorPanel::layoutExtra (juce::Rectangle<int>& r)
{
    if (kind == Processor::Duck)
    {
        auto row = r.removeFromTop (30);
        duckLabel.setBounds (row.removeFromLeft (90));
        duckSource.setBounds (row);
        r.removeFromTop (Theme::s3);
        return;
    }

    // HPF/LPF rows in the EQ panel (strips only): on/off key + cutoff slider each.
    if (kind == Processor::Eq && stripIndex >= 0)
    {
        auto row = r.removeFromTop (28);
        hpfToggle.setBounds (row.removeFromLeft (56));
        row.removeFromLeft (6);
        hpfCutoffSlider.setBounds (row);
        r.removeFromTop (4);
        auto row2 = r.removeFromTop (28);
        lpfToggle.setBounds (row2.removeFromLeft (56));
        row2.removeFromLeft (6);
        lpfCutoffSlider.setBounds (row2);
        r.removeFromTop (Theme::s2);
    }
}

void ProcessorPanel::tick()
{
    if (kind == Processor::Denoise && fx.denoiseOn.load() && ! fx.denoiseUnavailable.load())
    {
        const auto voice = juce::roundToInt (fx.voiceProbability.load() * 100.0f);
        setHelp ("Removes steady background noise (fans, hum, keyboard) from voice. Adds "
                 + juce::String (fx.denoiseLatencyMs.load()) + " ms of latency, which the other strips are matched to. "
                 "Voice detected: " + juce::String (voice) + "%.");
    }
}

//==============================================================================
// SlotMixPanel

SlotMixPanel::SlotMixPanel (const juce::String& pluginName, std::atomic<float>& mix)
    : PanelBase (pluginName + ": wet/dry", nullptr)
{
    addKnob ({ "Mix", 0, 100, 1, " %", mix, 0.0, 100.0 });
    setHelp ("100% is the plugin only. Lower values blend in the dry signal, delayed to match the "
             "plugin's latency (parallel compression, subtle reverb).");
    setSize (300, 210);
}

//==============================================================================
// SendPanel

SendPanel::SendPanel (MixerEngine& e, int stripIndex, int busIndex)
    : PanelBase ("Send to " + juce::String (kBusNames[busIndex]), &e.getStrip (stripIndex).busOn[busIndex]),
      strip (e.getStrip (stripIndex)), bus (busIndex)
{
    addKnob ({ "Level", -60, 12, 0.1, " dB", strip.sendDb[busIndex], -12.0 });

    makeLamp (preButton, "accent");
    preButton.setToggleState (strip.sendPre[busIndex].load(), juce::dontSendNotification);
    preButton.setTitle ("Pre-fader send");
    preButton.onClick = [this]
    {
        strip.sendPre[bus].store (preButton.getToggleState());
        if (onChange) onChange();
    };
    addAndMakeVisible (preButton);

    setHelp ("Pre-fader: this send ignores the strip's fader, so you can give this bus "
             "(for example Discord) its own mix.");
    setSize (300, 230);
}

void SendPanel::layoutExtra (juce::Rectangle<int>& r)
{
    preButton.setBounds (r.removeFromTop (30).withWidth (120));
    r.removeFromTop (Theme::s3);
}

//==============================================================================
// BuiltInFxPanel

BuiltInFxPanel::BuiltInFxPanel (MixerEngine& e, PluginChain& c, int s)
    : engine (e), chain (c), slot (s)
{
    setFocusContainerType (FocusContainerType::keyboardFocusContainer);

    if (auto* fx = chain.getBuiltIn (slot))
        type = fx->getType();

    title = PluginChain::fxTypeDisplayName (type) + " (built-in)";
    setTitle (title);

    switch (type)
    {
        case omx::FxType::Reverb:
            defs = { { "Room size", 0, 100, 1, 0.0, " %", 0, 100.0 },
                     { "Damping",   0, 100, 1, 0.0, " %", 1, 100.0 },
                     { "Mix",       0, 100, 1, 0.0, " %", 2, 100.0 },
                     { "Pre-delay", 0, 100, 1, 0.0, " ms", 3, 1.0 } };
            help = "Algorithmic reverb (Freeverb-style). Mix is the effect's own wet/dry; "
                   "the slot's wet/dry mix applies on top of it, so leave that at 100%.";
            break;

        case omx::FxType::Delay:
            defs = { { "Time",     0, 2000, 1, 0.0, " ms", 0, 1.0 },
                     { "Feedback", 0, 95, 1, 0.0, " %", 1, 100.0 },
                     { "Mix",      0, 100, 1, 0.0, " %", 2, 100.0 },
                     { "Low-pass", 200, 20000, 1, 2000.0, " Hz", 3, 1.0 } };
            help = "Tempo-synced stereo delay: with tempo sync on, the time follows the tap-tempo "
                   "BPM and the note value below. The low-pass darkens each repeat.";
            break;

        case omx::FxType::Chorus:
            defs = { { "Rate",     0.05, 10, 0.01, 0.7, " Hz", 0, 1.0 },
                     { "Depth",    0, 10, 0.1, 0.0, " ms", 1, 1.0 },
                     { "Mix",      0, 100, 1, 0.0, " %", 2, 100.0 },
                     { "Feedback", 0, 50, 1, 0.0, " %", 3, 100.0 } };
            help = "Modulated delay chorus, left and right out of phase for stereo width. "
                   "Feedback turns it towards a flanger; keep it low for a plain chorus.";
            break;
    }

    for (size_t i = 0; i < defs.size(); ++i)
    {
        const auto& d = defs[i];
        auto slider = std::make_unique<DbSlider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
        auto* s = slider.get();

        s->setRange (d.min, d.max, d.step);
        if (d.skewMid > 0.0)
            s->setSkewFactorFromMidPoint (d.skewMid);
        s->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 18);
        const auto suffix = d.suffix;
        s->textFromValueFunction = [suffix] (double v)
        {
            const bool whole = std::abs (v) >= 100.0 || suffix == " %";
            return juce::String (v, whole ? 0 : 1) + suffix;
        };
        const double span = d.max - d.min;
        s->keyStep  = d.suffix == " ms" || d.suffix == " Hz" ? juce::jmax (d.step, span / 50.0) : d.step;
        s->pageStep = span / 10.0;
        s->homeValue = d.skewMid > 0.0 ? d.skewMid : d.max;
        s->setDoubleClickReturnValue (true, s->homeValue);
        s->setTitle (title + " " + d.label);
        s->setWantsKeyboardFocus (true);
        makeClickable (*s);

        const int paramIndex = d.paramIndex;
        const double scale = d.scale;
        s->onValueChange = [this, s, paramIndex, scale]
        {
            params[(size_t) paramIndex] = (float) (s->getValue() / scale);
            push();
        };

        addAndMakeVisible (s);
        sliders.push_back (std::move (slider));
    }

    if (type == omx::FxType::Delay)
    {
        makeLamp (tempoButton, "accent");
        tempoButton.setTitle ("Delay follows the tap tempo");
        tempoButton.onClick = [this]
        {
            tempoSync = tempoButton.getToggleState();
            if (tempoSync)
                params[5] = (float) engine.getBpm();
            push();
            if (tempoSync)
                engine.applyBpmToDelays();
            rebuildFromChain();
        };
        addAndMakeVisible (tempoButton);

        noteCombo.addItem ("1/4 note", 1);
        noteCombo.addItem ("Dotted 1/4", 2);
        noteCombo.addItem ("1/2 note", 3);
        noteCombo.addItem ("1/8 note", 4);
        noteCombo.addItem ("1/16 note", 5);
        noteCombo.setTitle ("Delay note value");
        noteCombo.setTooltip ("Note value for the tempo-synced delay time.");
        noteCombo.onChange = [this]
        {
            static constexpr float divisions[] { 1.0f, 1.5f, 2.0f, 0.5f, 0.25f };
            const int id = noteCombo.getSelectedId();
            if (juce::isPositiveAndBelow (id - 1, (int) std::size (divisions)))
            {
                params[4] = divisions[id - 1];
                push();
            }
        };
        makeClickable (noteCombo);
        addAndMakeVisible (noteCombo);
    }

    rebuildFromChain();
    setSize (340, type == omx::FxType::Delay ? 260 : 230);
}

void BuiltInFxPanel::rebuildFromChain()
{
    if (! chain.getBuiltInParams (slot, params, tempoSync))
        return;

    for (size_t i = 0; i < defs.size(); ++i)
    {
        auto* s = sliders[i].get();
        s->setValue (params[(size_t) defs[i].paramIndex] * defs[i].scale, juce::dontSendNotification);
        s->updateText();
    }

    if (type == omx::FxType::Delay)
    {
        tempoButton.setToggleState (tempoSync, juce::dontSendNotification);
        tempoButton.setTooltip (tempoSync ? juce::String ("Tempo sync on: the delay follows the tap tempo (")
                                            + juce::String (engine.getBpm(), 0) + " BPM)."
                                         : "Tempo sync off: the Time knob sets the delay directly.");
        // With tempo sync on the time is computed from the BPM, not set by hand.
        if (! sliders.empty())
            sliders[0]->setEnabled (! tempoSync);

        static constexpr float divisions[] { 1.0f, 1.5f, 2.0f, 0.5f, 0.25f };
        int id = 1;
        for (size_t i = 0; i < std::size (divisions); ++i)
            if (std::abs (params[4] - divisions[i]) < 0.001f)
                id = (int) i + 1;
        noteCombo.setSelectedId (id, juce::dontSendNotification);
        noteCombo.setEnabled (tempoSync);
    }
}

void BuiltInFxPanel::push()
{
    if (tempoSync && type == omx::FxType::Delay)
        params[5] = (float) engine.getBpm();
    chain.setBuiltInParams (slot, params, tempoSync);
}

void BuiltInFxPanel::paint (juce::Graphics& g)
{
    const auto& t = Theme::p();

    g.setColour (t.text);
    g.setFont (Theme::ui (15.0f, true));
    g.drawText (title, titleArea, juce::Justification::centredLeft);

    g.setFont (Theme::ui (11.5f, true).withExtraKerningFactor (0.04f));
    g.setColour (t.textFaint);
    for (size_t i = 0; i < sliders.size(); ++i)
        g.drawText (defs[i].label.toUpperCase(), sliders[i]->getBounds().withHeight (16).translated (0, -16),
                    juce::Justification::centred);

    if (help.isNotEmpty())
    {
        g.setFont (Theme::ui (12.5f));
        g.setColour (t.textDim);
        g.drawFittedText (help, helpArea, juce::Justification::topLeft, 3);
    }
}

void BuiltInFxPanel::resized()
{
    auto r = getLocalBounds().reduced (Theme::s3);

    auto top = r.removeFromTop (30);
    titleArea = top;
    r.removeFromTop (Theme::s2);

    if (type == omx::FxType::Delay)
    {
        auto row = r.removeFromTop (30);
        tempoButton.setBounds (row.removeFromLeft (120));
        row.removeFromLeft (Theme::s2);
        noteCombo.setBounds (row);
        r.removeFromTop (Theme::s3);
    }

    r.removeFromTop (16);   // knob labels
    auto row = r.removeFromTop (76);
    const int w = row.getWidth() / (int) sliders.size();
    for (auto& s : sliders)
        s->setBounds (row.removeFromLeft (w).reduced (4, 0));
    r.removeFromTop (Theme::s2);

    helpArea = r;
}
