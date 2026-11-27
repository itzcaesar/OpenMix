#pragma once

#include <JuceHeader.h>

// Undo / redo for the mix. The main window offers a snapshot of the mix (Session::captureScene
// as text) a few times a second, once no mouse button is held, so a whole fader drag is one
// step. Undo hands back the snapshot to apply.
class MixHistory
{
public:
    static constexpr size_t maxSteps = 100;

    // A state can carry whole strip copies (with plugins) after this marker, so undoing a paste
    // brings the old plugins back. Only the part before it is compared as "the mix".
    static constexpr const char* fullMarker = "\n<!--strips-->\n";
    static juce::String mixPart (const juce::String& s)  { return s.upToFirstOccurrenceOf (fullMarker, false, false); }
    static juce::String fullPart (const juce::String& s) { return s.fromFirstOccurrenceOf (fullMarker, false, false); }

    // Records a new state. Returns true if it differed from the last one.
    bool offer (const juce::String& state)
    {
        if (state == current)
            return false;
        if (mixPart (state) == mixPart (current))
        {
            if (fullPart (state).isEmpty())
                return false;   // same mix: keep the strip copies this step already has
            if (fullPart (current).isEmpty())
            {
                current = state;   // add strip copies to this step
                return false;
            }
        }
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
    void settle (const juce::String& state)
    {
        const auto full = fullPart (current);
        current = full.isEmpty() ? state : state + fullMarker + full;
    }

    void clear() { undoStack.clear(); redoStack.clear(); current.clear(); }

private:
    std::vector<juce::String> undoStack, redoStack;
    juce::String current;
};
