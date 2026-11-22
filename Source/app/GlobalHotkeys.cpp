#ifndef NOMINMAX
 #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "app/GlobalHotkeys.h"
#include "app/AppSettings.h"

namespace
{
    GlobalHotkeys* instance = nullptr;
    std::shared_ptr<bool> alive;

    LRESULT CALLBACK keyboardHook (int code, WPARAM wParam, LPARAM lParam)
    {
        if (code == HC_ACTION && instance != nullptr)
        {
            const auto* k = reinterpret_cast<const KBDLLHOOKSTRUCT*> (lParam);
            const bool down = wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN;
            const bool up   = wParam == WM_KEYUP   || wParam == WM_SYSKEYUP;
            // Keys sent by other software (macros, remote tools) are ignored on purpose.
            if ((down || up) && (k->flags & LLKHF_INJECTED) == 0)
                if (instance->handleKey ((int) k->vkCode, down))
                    return 1;   // only while capturing a new binding
        }
        return CallNextHookEx (nullptr, code, wParam, lParam);
    }

    bool isModifier (int vk)
    {
        switch (vk)
        {
            case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT:
            case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
            case VK_MENU: case VK_LMENU: case VK_RMENU:
            case VK_LWIN: case VK_RWIN:
                return true;
            default:
                return false;
        }
    }

    bool isDown (int vk) { return (GetAsyncKeyState (vk) & 0x8000) != 0; }

    GlobalHotkeys::Keys currentKeys (int vk)
    {
        GlobalHotkeys::Keys k;
        k.vk = vk;
        k.ctrl = isDown (VK_CONTROL);
        k.shift = isDown (VK_SHIFT);
        k.alt = isDown (VK_MENU);
        k.win = isDown (VK_LWIN) || isDown (VK_RWIN);
        return k;
    }
}

GlobalHotkeys::GlobalHotkeys()
{
    instance = this;
    alive = std::make_shared<bool> (true);
}

GlobalHotkeys::~GlobalHotkeys()
{
    if (hook != nullptr)
        UnhookWindowsHookEx ((HHOOK) hook);
    hook = nullptr;
    *alive = false;
    instance = nullptr;
}

void GlobalHotkeys::updateHook()
{
    const bool wanted = ! bindings.empty() || captureCallback != nullptr;
    if (wanted && hook == nullptr)
        hook = SetWindowsHookExW (WH_KEYBOARD_LL, keyboardHook, GetModuleHandleW (nullptr), 0);
    else if (! wanted && hook != nullptr)
    {
        UnhookWindowsHookEx ((HHOOK) hook);
        hook = nullptr;
    }
}

void GlobalHotkeys::setBindings (std::vector<Binding> b)
{
    bindings = std::move (b);
    held.clear();
    updateHook();
}

void GlobalHotkeys::captureNext (std::function<void (Keys)> callback)
{
    captureCallback = std::move (callback);
    updateHook();
}

void GlobalHotkeys::cancelCapture()
{
    captureCallback = nullptr;
    updateHook();
}

bool GlobalHotkeys::handleKey (int vk, bool down)
{
    // Capturing a new binding: the next non-modifier key, with the modifiers held right now.
    if (captureCallback != nullptr)
    {
        if (isModifier (vk))
            return false;
        if (! down)
            return true;

        auto keys = currentKeys (vk);
        if (vk == VK_ESCAPE && ! keys.ctrl && ! keys.shift && ! keys.alt && ! keys.win)
            keys = {};   // cancelled

        auto callback = std::move (captureCallback);
        captureCallback = nullptr;
        juce::MessageManager::callAsync ([this, callback, keys, a = alive]
        {
            if (*a)
            {
                callback (keys);
                updateHook();
            }
        });
        return true;
    }

    if (isModifier (vk))
        return false;

    if (down)
    {
        if (std::find (held.begin(), held.end(), vk) != held.end())
            return false;   // auto-repeat

        const auto keys = currentKeys (vk);
        bool fired = false;
        for (const auto& b : bindings)
        {
            if (! (b.keys == keys))
                continue;
            fired = true;
            juce::MessageManager::callAsync ([this, b, a = alive] { if (*a && onAction) onAction (b, true); });
        }
        if (fired)
        {
            held.push_back (vk);
            lastFireMs = juce::Time::getMillisecondCounter();
        }
    }
    else if (auto it = std::find (held.begin(), held.end(), vk); it != held.end())
    {
        held.erase (it);
        // Releases matter for push-to-talk only. Matched by key, since the modifiers may already
        // be up.
        for (const auto& b : bindings)
            if (b.keys.vk == vk && b.action == Action::PushToTalk)
                juce::MessageManager::callAsync ([this, b, a = alive] { if (*a && onAction) onAction (b, false); });
    }
    return false;
}

//==============================================================================

juce::String GlobalHotkeys::describe (const Keys& k)
{
    if (! k.isValid())
        return "Not set";

    juce::String s;
    if (k.ctrl)  s << "Ctrl+";
    if (k.alt)   s << "Alt+";
    if (k.shift) s << "Shift+";
    if (k.win)   s << "Win+";

    // Keys whose scan code needs the "extended" bit to get the right name.
    LONG scan = (LONG) MapVirtualKeyW ((UINT) k.vk, MAPVK_VK_TO_VSC) << 16;
    switch (k.vk)
    {
        case VK_INSERT: case VK_DELETE: case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT:
        case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN: case VK_DIVIDE: case VK_NUMLOCK:
        case VK_RCONTROL: case VK_RMENU: case VK_SNAPSHOT:
            scan |= 1 << 24;
            break;
        default:
            break;
    }

    wchar_t name[64] {};
    if (GetKeyNameTextW (scan, name, 64) > 0)
        s << juce::String (name);
    else
        s << "Key " << k.vk;
    return s;
}

juce::String GlobalHotkeys::describeAction (Action a, int index, const juce::StringArray& stripNames,
                                            const juce::StringArray& sceneNames)
{
    auto strip = [&] { return juce::isPositiveAndBelow (index, stripNames.size()) ? stripNames[index] : "Input " + juce::String (index + 1); };
    switch (a)
    {
        case Action::ToggleMute:   return "Mute or unmute " + strip();
        case Action::PushToTalk:   return "Push to talk on " + strip();
        case Action::PlayPad:      return "Play soundboard pad " + juce::String (index + 1);
        case Action::StopSounds:   return "Stop all sounds";
        case Action::ToggleRecord: return "Start or stop recording";
        case Action::SaveReplay:   return "Save replay";
        case Action::RecallScene:
            return juce::isPositiveAndBelow (index, sceneNames.size()) ? "Recall scene \"" + sceneNames[index] + "\""
                                                                       : juce::String ("Recall scene (deleted)");
    }
    return {};
}

//==============================================================================

void GlobalHotkeys::load()
{
    std::vector<Binding> loaded;
    if (auto xml = juce::parseXML (AppSettings::get().hotkeysXml()))
    {
        for (auto* e : xml->getChildWithTagNameIterator ("Hotkey"))
        {
            Binding b;
            b.keys.vk = e->getIntAttribute ("vk");
            b.keys.ctrl = e->getBoolAttribute ("ctrl");
            b.keys.shift = e->getBoolAttribute ("shift");
            b.keys.alt = e->getBoolAttribute ("alt");
            b.keys.win = e->getBoolAttribute ("win");
            b.action = (Action) juce::jlimit (0, (int) Action::RecallScene, e->getIntAttribute ("action"));
            b.index = e->getIntAttribute ("index");
            if (b.keys.isValid())
                loaded.push_back (b);
        }
    }
    setBindings (std::move (loaded));
}

void GlobalHotkeys::save() const
{
    juce::XmlElement root ("Hotkeys");
    for (const auto& b : bindings)
    {
        auto* e = root.createNewChildElement ("Hotkey");
        e->setAttribute ("vk", b.keys.vk);
        e->setAttribute ("ctrl", b.keys.ctrl);
        e->setAttribute ("shift", b.keys.shift);
        e->setAttribute ("alt", b.keys.alt);
        e->setAttribute ("win", b.keys.win);
        e->setAttribute ("action", (int) b.action);
        e->setAttribute ("index", b.index);
    }
    AppSettings::get().setHotkeysXml (root.toString (juce::XmlElement::TextFormat().singleLine()));
}
