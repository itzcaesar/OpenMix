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

ProcessorPanel::ProcessorPanel (MixerEngine& e, int stripIndex, Processor p)
    : PanelBase (processorName (p), &processorOn (e.getStrip (stripIndex).fx, p)),
      engine (e), strip (e.getStrip (stripIndex)), kind (p)
{
    auto& fx = strip.fx;
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
            setHelp ("Low shelf at 120 Hz, a sweepable middle band, high shelf at 8 kHz.");
            height = 200;
            break;

        case Processor::Comp:
            addKnob ({ "Threshold", -60, 0, 0.5, " dB", fx.compThresholdDb });
            addKnob ({ "Ratio", 1, 20, 0.1, ":1", fx.compRatio, 4.0 });
            setMeter (&fx.compGain, "Reduction");
            setHelp ("Evens out loud and quiet parts. Make-up gain is added automatically.");
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
                if (other.active.load() && &other != &strip)
                    duckSource.addItem (other.name.isNotEmpty() ? other.name : "Input " + juce::String (s + 1), s + 2);
            }
            duckSource.setSelectedId (fx.duckSource.load() + 2, juce::dontSendNotification);
            if (duckSource.getSelectedId() == 0)
                duckSource.setSelectedId (1, juce::dontSendNotification);
            duckSource.setTitle ("Duck under which strip");
            duckSource.onChange = [this]
            {
                strip.fx.duckSource.store (duckSource.getSelectedId() - 2);
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
    if (kind != Processor::Duck)
        return;

    auto row = r.removeFromTop (30);
    duckLabel.setBounds (row.removeFromLeft (90));
    duckSource.setBounds (row);
    r.removeFromTop (Theme::s3);
}

void ProcessorPanel::tick()
{
    if (kind == Processor::Denoise && strip.fx.denoiseOn.load() && ! strip.fx.denoiseUnavailable.load())
    {
        const auto voice = juce::roundToInt (strip.fx.voiceProbability.load() * 100.0f);
        setHelp ("Removes steady background noise (fans, hum, keyboard) from voice. Adds "
                 + juce::String (strip.fx.denoiseLatencyMs.load()) + " ms of latency, which the other strips are matched to. "
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
