#include "ui/RoutingPanel.h"
#include "ui/Theme.h"

static juce::String levelText (double v)
{
    return v <= -60.0 ? juce::String ("-inf") : (v > 0.05 ? "+" : "") + juce::String (v, 1);
}

RoutingPanel::RoutingPanel (MixerEngine& e) : engine (e)
{
    setTitle ("Routing matrix");
    rebuild();
    startTimerHz (5);
}

RoutingPanel::~RoutingPanel() = default;

std::atomic<bool>& RoutingPanel::onFlag (const Cell& c)
{
    return isStripRow (c.row) ? engine.getStrip (c.row).busOn[c.bus]
                              : engine.getBus (c.row - kNumStrips).feedOn[(size_t) c.bus];
}

std::atomic<float>& RoutingPanel::levelDb (const Cell& c)
{
    return isStripRow (c.row) ? engine.getStrip (c.row).sendDb[c.bus]
                              : engine.getBus (c.row - kNumStrips).feedDb[(size_t) c.bus];
}

void RoutingPanel::rebuild()
{
    rows.clear(); columns.clear(); rowNames.clear(); cells.clear();
    layoutKey.clear();

    for (int s = 0; s < kNumStrips; ++s)
        if (engine.getStrip (s).active.load())
        {
            rows.push_back (s);
            const auto& name = engine.getStrip (s).name;
            rowNames.push_back (name.isNotEmpty() ? name : "Input " + juce::String (s + 1));
            layoutKey << "s" << s << name;
        }
    for (int b : kBusOrder)
        if (engine.getBus (b).active.load())
        {
            rows.push_back (kNumStrips + b);
            columns.push_back (b);
            rowNames.push_back (engine.getBusName (b));
            layoutKey << "b" << b << engine.getBusName (b);
        }

    for (const int row : rows)
        for (const int bus : columns)
        {
            if (! isStripRow (row) && row - kNumStrips == bus)
                continue;   // a bus never feeds itself

            auto cell = std::make_unique<Cell>();
            cell->row = row;
            cell->bus = bus;
            auto* c = cell.get();
            const juce::String from = rowNames[(size_t) (std::find (rows.begin(), rows.end(), row) - rows.begin())];

            cell->key = std::make_unique<StripKey> (engine.getBusName (bus));
            makeLamp (*cell->key, "key");
            cell->key->setClickingTogglesState (true);
            cell->key->setTitle (from + " to " + engine.getBusName (bus));
            cell->key->onClick = [this, c]
            {
                const bool on = c->key->getToggleState();
                if (isStripRow (c->row))
                    engine.getStrip (c->row).busOn[c->bus].store (on);
                else if (! engine.setBusFeed (c->row - kNumStrips, c->bus, on))
                    c->key->setToggleState (false, juce::dontSendNotification);   // would loop
                engine.externalChanges.fetch_add (1);   // strips and buses redraw their keys
                sync();
            };
            addAndMakeVisible (*cell->key);

            cell->knob = std::make_unique<DbSlider>();
            auto& k = *cell->knob;
            k.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            k.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
            k.setRange (-60.0, 12.0, 0.1);
            k.setSkewFactorFromMidPoint (-12.0);
            k.setDoubleClickReturnValue (true, 0.0);
            k.setMouseDragSensitivity (160);
            k.setTitle (from + " to " + engine.getBusName (bus) + " level");
            makeClickable (k);
            k.onValueChange = [this, c]
            {
                levelDb (*c).store ((float) c->knob->getValue());
                c->knob->setTooltip (levelText (c->knob->getValue()) + " dB. Double-click or Home for 0 dB.");
            };
            addAndMakeVisible (k);

            cells.push_back (std::move (cell));
        }

    sync();
    resized();
}

void RoutingPanel::sync()
{
    // Strips or buses added, removed or renamed while the window is open: rebuild the grid.
    juce::String key;
    for (int s = 0; s < kNumStrips; ++s)
        if (engine.getStrip (s).active.load())
            key << "s" << s << engine.getStrip (s).name;
    for (int b = 0; b < kNumBuses; ++b)
        if (engine.getBus (b).active.load())
            key << "b" << b << engine.getBusName (b);
    if (key != layoutKey)
    {
        rebuild();
        repaint();
        return;
    }

    for (auto& c : cells)
    {
        const bool on = onFlag (*c).load();
        c->key->setToggleState (on, juce::dontSendNotification);

        const bool loops = ! isStripRow (c->row) && ! on && engine.busFeedWouldLoop (c->row - kNumStrips, c->bus);
        c->key->setEnabled (! loops);
        c->key->setTooltip (loops ? "Not available: " + engine.getBusName (c->bus) + " already feeds "
                                        + engine.getBusName (c->row - kNumStrips) + ", so this would make a loop."
                                  : juce::String ("Send on or off"));
        c->knob->setEnabled (! loops);

        if (! c->knob->isMouseButtonDown())
            c->knob->setValue (levelDb (*c).load(), juce::dontSendNotification);
        c->knob->setTooltip (levelText (c->knob->getValue()) + " dB. Double-click or Home for 0 dB.");
    }
}

int RoutingPanel::preferredWidth() const
{
    return pad * 2 + nameWidth + (int) columns.size() * cellWidth;
}

int RoutingPanel::preferredHeight() const
{
    const int strips = (int) std::count_if (rows.begin(), rows.end(), [this] (int r) { return isStripRow (r); });
    const int gap = strips > 0 && strips < (int) rows.size() ? rowHeight / 2 : 0;
    return pad * 2 + headerHeight + (int) rows.size() * rowHeight + gap + 40;
}

void RoutingPanel::resized()
{
    int y = pad + headerHeight;
    bool inBuses = false;
    for (const int row : rows)
    {
        if (! isStripRow (row) && ! inBuses)
        {
            inBuses = true;
            if (row != rows.front())
                y += rowHeight / 2;   // gap before the bus rows
        }
        for (size_t col = 0; col < columns.size(); ++col)
            for (auto& c : cells)
                if (c->row == row && c->bus == columns[col])
                {
                    auto area = juce::Rectangle<int> (pad + nameWidth + (int) col * cellWidth, y, cellWidth, rowHeight).reduced (3, 4);
                    c->knob->setBounds (area.removeFromRight (area.getHeight()));
                    area.removeFromRight (2);
                    c->key->setBounds (area);
                }
        y += rowHeight;
    }
}

void RoutingPanel::paint (juce::Graphics& g)
{
    const auto& t = Theme::p();
    g.fillAll (t.window);

    // Column headers: the destination bus.
    g.setFont (Theme::ui (12.0f, true));
    g.setColour (t.textDim);
    g.drawText ("FROM", pad, pad, nameWidth, headerHeight, juce::Justification::centredLeft);
    for (size_t col = 0; col < columns.size(); ++col)
        g.drawText ("TO " + engine.getBusName (columns[col]).toUpperCase(),
                    pad + nameWidth + (int) col * cellWidth, pad, cellWidth, headerHeight, juce::Justification::centred);

    int y = pad + headerHeight;
    bool inBuses = false;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        const int row = rows[i];
        if (! isStripRow (row) && ! inBuses)
        {
            inBuses = true;
            if (i > 0)
                y += rowHeight / 2;
        }
        g.setColour (t.line);
        g.fillRect (pad, y, getWidth() - pad * 2, 1);
        g.setColour (t.text);
        g.setFont (Theme::ui (13.0f, ! isStripRow (row)));
        g.drawText (rowNames[i], pad, y, nameWidth - 8, rowHeight, juce::Justification::centredLeft, true);
        y += rowHeight;
    }

    g.setColour (t.textDim);
    g.setFont (Theme::ui (12.0f));
    g.drawFittedText ("Strip rows are the strips' own sends. A bus row feeds that bus, after its fader, into another bus.",
                      pad, y + 8, getWidth() - pad * 2, 30, juce::Justification::topLeft, 2);
}
