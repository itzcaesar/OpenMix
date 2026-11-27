#pragma once

#include <JuceHeader.h>
#include "engine/MixerEngine.h"
#include "ui/Widgets.h"

// The routing matrix: one row per strip and per bus, one column per bus. A cell's key turns
// the send on, its knob sets the level. Strip rows are the strips' own sends; bus rows feed one
// bus into another (after its fader), and a cell that would make a loop is greyed out.
class RoutingPanel : public juce::Component, private juce::Timer
{
public:
    explicit RoutingPanel (MixerEngine&);
    ~RoutingPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    int preferredWidth() const;
    int preferredHeight() const;

private:
    struct Cell
    {
        int row = 0, bus = 0;                 // row: 0..kNumStrips-1 strips, then kNumStrips + bus index
        std::unique_ptr<StripKey> key;
        std::unique_ptr<DbSlider> knob;
    };

    void timerCallback() override { sync(); }
    void rebuild();
    void sync();
    bool isStripRow (int row) const noexcept { return row < kNumStrips; }
    std::atomic<bool>& onFlag (const Cell&);
    std::atomic<float>& levelDb (const Cell&);

    MixerEngine& engine;
    std::vector<int> rows, columns;           // active strips then active buses (row ids), active buses
    std::vector<std::unique_ptr<Cell>> cells;
    std::vector<juce::String> rowNames;
    juce::String layoutKey;                   // which strips/buses the cells were built for

    static constexpr int nameWidth = 124, cellWidth = 88, rowHeight = 34, headerHeight = 30, pad = 14;
};
