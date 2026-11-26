# Getting started

## Requirements

- Windows 10 2004 or later.
- To build it yourself: Visual Studio 2026 with the **Desktop development
  with C++** workload (it includes CMake).

## Install

Download the release from the
[releases page](https://github.com/itzcaesar/OpenMix/releases):

- `OpenMix-windows-x64.zip`: the app, README and licence, in a zip.
- `OpenMix-0.1.0-setup.exe`: the installer (first release, v0.1.0).

Or, for the latest `main` build, open the latest run under **Actions** in
the repository and download the `OpenMix-windows-x64` artifact.

## Build it yourself

Clone with submodules (JUCE and RNNoise):

```powershell
git clone --recursive https://github.com/itzcaesar/OpenMix.git
```

**In Visual Studio:** File > Open > Folder, pick the `vs2026` preset, set
the startup item to `OpenMix.exe`, press F5.

**From a Developer PowerShell for VS 2026:**

```powershell
cmake --preset vs2026
cmake --build --preset release
ctest --test-dir build -C Release
.\build\OpenMix_artefacts\Release\OpenMix.exe
```

## The window

- **Header:** app name, the status readout (device, sample rate, latency,
  CPU, dropouts; click it for Audio settings), the **Scene** picker,
  **Record** and its options, **Replay**, **Soundboard** (shows or hides
  the soundboard bar) and **Settings**.
- **Inputs** on the left, **outputs** (buses) on the right. **+ Strip**
  and **+ Bus** sit next to the section names. The bus row is split into
  **Hardware** (A1–A3) and **Virtual** (B1–B3) groups with a divider
  between them; **+ Bus** asks for the group first, then an optional
  custom name, and takes the next free slot in that group.
- The **status bar** along the bottom shows LIVE, the engine's own CPU%, plugin scanning and recording progress, and a lamp for each remote you switched on (OSC, phone, MIDI), lit while it is listening.
- Every strip and bus ends in the same **fader bay**, so faders, meters and
  mute rows line up across the desk. The number above each meter is the held
  peak; a red bar under it marks 0 dBFS or more.
- The **soundboard** is a bar along the bottom of the window.
- A warning banner appears if the main device's input and output are two
  different Windows devices, because their clocks drift and that crackles.
  The banner explains the fix.

## First mix

See [Quick start](index.md#quick-start-mic-to-discord-music-to-your-headphones)
on the home page: mic on Input 1, an app on Input 2, A1 to your headphones,
B1 to a virtual cable for Discord or OBS.

## Settings

Settings holds: light or dark theme, compact mode, hotkeys, start with Windows, start minimized, keep running
in the tray when the window closes, plugin scanning and crash-safe loading,
presets and VST3 folders, the update check, OSC, phone / WebSocket and MIDI
remote control, and the log folder. In the tray, double-click opens the
mixer; right-click to quit.

The **update check** is off by default. When on, OpenMix asks GitHub once
at startup whether a newer release exists and, if so, shows an Update
button that opens the release page. It doesn't download or install anything.

## Where things are saved

| What | Where |
|---|---|
| Session (devices, theme, strips, plugins and their settings, buses, soundboard) | `%APPDATA%\OpenMix\session.xml`, every 10 seconds (plugin state at most once a minute) and on exit |
| Scenes | inside the session |
| App settings (startup, tray, OSC, phone/WebSocket, MIDI, hotkeys, recording options) | `%APPDATA%\OpenMix\settings.xml` |
| Log and crash reports | `%APPDATA%\OpenMix\openmix.log`, `crash-<time>.txt` |
| Recordings and replays | `Documents\OpenMix Recordings` |
| Plugin presets | `Documents\OpenMix Presets` |

## Keyboard

Tab and Shift+Tab move through the header, then each strip top to bottom,
then the buses.

| Control | Keys |
|---|---|
| Faders, gain knob | Up/Down 1 dB (Shift: 0.1 dB), Page Up/Down 6 dB, Home 0 dB |
| Voice pad | Arrow keys move the puck (Shift: fine), Home resets |
| Plugin row | Enter open/add, Space bypass, Delete remove, Alt+Up/Down move, M menu |
| Processing key | Enter opens settings, Space switches on/off |
| Send key | Enter or Space switches, M or Shift+Enter opens level and pre-fader |
| Name plate | Enter or F2 rename |
| Meter | Enter resets the clip lamp |
| Spectrum | Enter or Space opens the analyser |
| Buttons and pickers | Enter or Space |
| Record, save replay | Ctrl+Shift+R, Ctrl+Shift+S |
| Soundboard pads | 1–8 while the soundboard is shown; M opens a focused pad's menu |

These shortcuts work while OpenMix has focus. For keys that work in any
app, set up [global hotkeys](creator.md#global-hotkeys).
