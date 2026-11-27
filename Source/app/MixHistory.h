#pragma once

#include <JuceHeader.h>

// Undo / redo for the mix. The main window offers a snapshot of the mix (Session::captureScene
// as text) a few times a second, once no mouse button is held, so a whole fader drag is one
// step. Undo hands back the snapshot to apply.
class MixHistory
{
public:
    static constexpr size_t maxSteps = 100;

    // Records a new state. Returns true if it differed from the last one.
    bool offer (const juce::String& state)
    {
        if (state == current)
            return false;
        if (current.isNotEmpty())
        {
            undoStack.push_back (current);
            if (undoStack.size() > maxSteps)
                undoStack.erase (undoStack.begin());
        }
        redoStack.clear();
        current = state;
        return true;
    }

    bool canUndo() const noexcept { return ! undoStack.empty(); }
    bool canRedo() const noexcept { return ! redoStack.empty(); }

    // The state to apply, or empty when there is nothing to undo / redo.
    juce::String undo()
    {
        if (undoStack.empty())
            return {};
        redoStack.push_back (current);
        current = undoStack.back();
        undoStack.pop_back();
        return current;
    }

    juce::String redo()
    {
        if (redoStack.empty())
            return {};
        undoStack.push_back (current);
        current = redoStack.back();
        redoStack.pop_back();
        return current;
    }

    // After applying an undo / redo: the state as captured again, so a tiny difference from
    // the round trip through text doesn't count as a new step (which would drop the redo steps).
    void settle (const juce::String& state) { current = state; }

    void clear() { undoStack.clear(); redoStack.clear(); current.clear(); }

private:
    std::vector<juce::String> undoStack, redoStack;
    juce::String current;
};
