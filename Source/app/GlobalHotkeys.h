#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <mutex>
#include <thread>

// System-wide hotkeys: they work while any app has focus (a game, OBS, Discord).
//
// Uses a low-level keyboard hook that only listens: the keys still reach the app in front, and
// key releases are seen too, which push-to-talk needs. The hook runs on its own thread with its
// own message loop, so a busy UI thread never delays typing in other apps (Windows waits for a
// low-level hook on every key press, and silently removes one that keeps timing out). It is
// installed only while there is something to listen for (bindings, or a key being captured).
class GlobalHotkeys
{
public:
    enum class Action : int
    {
        ToggleMute = 0,   // index = strip
        PushToTalk,       // index = strip: unmuted while held, muted when released
        PlayPad,          // index = soundboard pad
        StopSounds,
        ToggleRecord,
        SaveReplay,
        RecallScene       // index = scene
    };

    struct Keys
    {
        int vk = 0;   // Windows virtual-key code of the main key
        bool ctrl = false, shift = false, alt = false, win = false;
        bool operator== (const Keys& o) const { return vk == o.vk && ctrl == o.ctrl && shift == o.shift && alt == o.alt && win == o.win; }
        bool isValid() const { return vk != 0; }
    };

    struct Binding
    {
        Keys keys;
        Action action = Action::ToggleRecord;
        int index = 0;
    };

    GlobalHotkeys();
    ~GlobalHotkeys();

    // Bindings (message thread). Saved in AppSettings.
    std::vector<Binding> getBindings() const { const std::lock_guard<std::mutex> l (lock); return bindings; }
    void setBindings (std::vector<Binding>);
    void load();
    void save() const;

    // Called on the message thread. down = key pressed, false = released (only sent for
    // push-to-talk bindings).
    std::function<void (const Binding&, bool down)> onAction;

    // Capture the next key combination (for setting a binding). Escape alone cancels (keys.vk = 0).
    void captureNext (std::function<void (Keys)> callback);
    void cancelCapture();

    // When a binding fired last (ms counter): lets in-window shortcuts skip a duplicate.
    juce::uint32 getLastFireMs() const noexcept { return lastFireMs.load(); }

    static juce::String describe (const Keys&);
    static juce::String describeAction (Action, int index, const juce::StringArray& stripNames,
                                        const juce::StringArray& sceneNames);

    // Hook entry point (internal, hook thread).
    bool handleKey (int vk, bool down);

private:
    void updateHook();       // message thread: starts or stops the hook thread
    void stopHookThread();

    mutable std::mutex lock;   // bindings, captureCallback and held: message thread vs hook thread
    std::vector<Binding> bindings;
    std::function<void (Keys)> captureCallback;
    std::vector<int> held;   // vks currently down that fired a binding (to ignore auto-repeat)
    std::atomic<juce::uint32> lastFireMs { 0 };
    std::thread hookThread;
    std::atomic<unsigned long> hookThreadId { 0 };
};
