# OpenMix

A virtual mixer for Windows: ASIO / WASAPI / DirectSound I/O, up to 8 input strips and
6 buses, built-in voice processing (AI noise removal, gate, EQ, compressor, de-esser,
ducking), VST3 plugin hosting on strips and buses with wet/dry and A/B compare, bus EQ and
compression, device loopback capture, a VoicePad (XY pad with voice,
pan, colour and space modes), send levels and a monitor bus, per-app audio capture, and
creator tools (multitrack recorder, replay buffer, soundboard, spectrum analyser and LUFS
meters, OSC remote control), in a studio-hardware style UI with dark and light themes.
Design direction: `docs/DESIGN.md`.

Built with C++20 + JUCE 9.0.5 (bundled in `external/JUCE`, includes JUCE's ASIO headers).

## Build

Every push to `main` is built by GitHub Actions; download `OpenMix-windows-x64` from the
run's artifacts. Pushing a tag like `v0.1.0` also attaches the build to a GitHub release.

To build it yourself, clone with `git clone --recursive` (JUCE is a submodule).

Requirements: Visual Studio 2026 with the **Desktop development with C++** workload
(it includes CMake).

**In Visual Studio:** File > Open > Folder... > `C:\vmix`, pick the `vs2026` preset,
set the startup item to `OpenMix.exe`, press F5.

**From a "Developer PowerShell for VS 2026":**

```powershell
cmake --preset vs2026
cmake --build --preset release
.\build\OpenMix_artefacts\Release\OpenMix.exe
```

## Using it

1. **Audio settings** (header): pick the driver type (ASIO for lowest latency, or Windows
   Audio), then the device, the active input/output channels, sample rate and buffer size.
   The status display next to it shows the device, sample rate, latency, CPU and dropouts.
   **Light mode / Dark mode** switches the theme; the choice is saved.
2. Each **strip** starts with its name on a tape label (double-click or Enter to rename).
   Under it, the source picker. Pick one of:
   - a channel of the main device (mono, spread to stereo)
   - **Windows input device**: any other mic or capture device, at the same time as the main device
   - **Application audio**: one app's sound (Spotify, a game, Discord...). This uses
     Windows' per-process loopback capture (Windows 10 2004+). The app keeps playing on its
     own output too; in Windows' Volume Mixer, point that app at a device you don't
     listen to if you only want to hear it through OpenMix. If the app quits, the strip
     shows "(not running)" and reconnects when it starts again.
   - **Windows output (loopback)**: everything one output device plays (for example your
     speakers while a game uses them), without a virtual cable. Devices OpenMix itself plays
     into are greyed out, because capturing them would feed the sound back into itself.

   A strip can also take a **stereo pair** of the main device (e.g. Input 1 + Input 2).
   For an app source, **App's own output** sends that app's direct sound to a device you
   don't listen to, so you only hear it through OpenMix (it goes back to normal when the
   strip's source changes or OpenMix quits).

   Signal path, top to bottom: gain -> polarity/mono -> noise removal -> gate -> EQ ->
   compressor -> de-esser -> up to 6 VST3 inserts -> VoicePad -> ducking -> sends ->
   mute -> fader.

   The three-dot button next to the name: rename, tape colour, mono, invert polarity,
   remove the strip. "+" after the last strip adds one (up to 8).
3. **Inserts**: "+ Add plugin" lists the VST3 plugins found in
   `C:\Program Files\Common Files\VST3`, grouped by maker when there are many. Plugins are
   scanned in the background, each in a separate process, so a plugin that crashes while
   being scanned is skipped instead of taking OpenMix down ("Retry skipped plugins" tries
   them again). Click a loaded plugin to open its window; the lamp bypasses it; the dots
   menu moves it, saves or loads a preset (`.omxpreset`, in Documents\OpenMix Presets),
   replaces or removes it. Drag a plugin up or down to reorder the chain.
   Put the denoiser before the autotune.
   **Wet/dry mix** (dots menu) blends the plugin with the dry signal, kept in time with the
   plugin's latency; the row shows the percentage. **A/B compare** stores two settings of a
   plugin so you can switch between them (the row shows "(B)" while you hear B).
4. **Processing** keys, lit when on (click for settings, Space switches on/off):
   - **NR** noise removal (RNNoise). Removes fans, hum and keyboard noise from voice.
     Adds 20 ms at 48 kHz (RNNoise's own rate); at other rates it resamples to 48 kHz and
     back, adding about 1 ms more. The other strips are delayed to match.
   - **Gate** turns the strip down while it's quiet. **EQ** low/mid/high.
   - **Comp** evens out loud and quiet parts (automatic make-up gain).
   - **DS** de-esser, softens harsh S sounds. **Duck** lowers this strip while another
     strip has signal (music under your voice).
   Gate, compressor, de-esser and ducking show a gain-reduction meter in their panel.
5. **VoicePad** (double-click or Home to reset):
   - *Voice*: X = tone tilt (Lo <-> Hi shelves, +/-6 dB), Y = presence (5 kHz) + echo.
   - *Pan*: X = constant-power pan, Y = stereo width (100-200%).
   - *Color*: the four corners are Warm, Bright, Radio and Close; the puck blends them.
   - *Space*: X = pan, Y = distance (quieter, darker, with early reflections further back).
6. **Sends**: click a send key (A1, B1...) to switch it; right-click it (or press M) for
   its level and **pre-fader** (the send ignores the strip fader: a separate mix for Discord).
   A level other than 0 dB and "P" for pre-fader are printed on the key.
   **Solo** plays the strip alone on the **monitor bus** (A1 by default, set in the bus
   menu); your stream and call buses are not affected.
7. **Buses**: the Output picker sends a bus to an output pair of the main device, or to
   **any Windows output device**. Each bus can use a different device, e.g. A1 headphones,
   A2 speakers, B1 a virtual cable that Discord/OBS use as their mic. Extra devices run on
   their own clocks; OpenMix resamples them and corrects drift, which adds about 10-25 ms of
   latency on those devices only. The **LIMITER** lamp lights while the bus safety limiter
   is working. "+" after the last bus adds one (A1-A3 hardware, B1-B3 virtual); the
   Each bus has its own **EQ** and **Comp** keys (click for settings, Space switches them on)
   and up to 6 **VST3 inserts**, with the same plugin handling as a strip. Bus signal path:
   sum -> EQ -> compressor -> inserts -> fader -> safety limiter.
   The three-dot menu sets the monitor bus, opens the **analyser** (spectrum, momentary,
   short-term and integrated LUFS) or removes the bus. The LOUDNESS readout under the fader
   shows short-term LUFS.
8. **Settings** (header): start with Windows, start minimized, keep running in the tray
   when the window is closed, theme, plugin scanning, the update check, OSC remote control
   and the log folder. In the tray, double-click opens the mixer; right-click to quit.
9. **Record** (header, Ctrl+Shift+R) writes every bus, every strip, or both, to one WAV
   file each in `Documents\OpenMix Recordings\<date and time>`. The dots next to it choose
   what to record and set the **replay buffer**: keep the last 30, 60 or 120 seconds of one
   bus in memory and save them with **Replay** (Ctrl+Shift+S).
10. **Soundboard**: a column in the console, between the inputs and the buses (the header
   button shows or hides it). Eight pads for WAV, MP3, FLAC or Ogg files up to 60 s. Click a
   pad or press 1-8 anywhere in OpenMix to play, again to stop; right-click to load,
   rename or clear. It has its own volume and plays on the buses you pick (A1 and B1 by
   default, so you and your stream both hear it).

### Remote control (OSC)

Off by default (Settings). While it's on, OpenMix listens on a UDP port (9000 unless you
change it) and anything on your network can control it, so only turn it on at home or on a
network you trust. Values are dB unless noted; strips and pads count from 1.

| Address | Argument |
|---|---|
| `/openmix/strip/<n>/fader`, `/gain` | dB |
| `/openmix/strip/<n>/mute`, `/solo` | 0 or 1 |
| `/openmix/strip/<n>/send/<bus>` | 0 or 1 (bus name, e.g. `B1`) |
| `/openmix/strip/<n>/sendlevel/<bus>` | dB |
| `/openmix/bus/<bus>/fader` | dB |
| `/openmix/bus/<bus>/mute` | 0 or 1 |
| `/openmix/soundboard/<n>/play` | none (plays or stops pad n) |
| `/openmix/record` | 1 starts, 0 stops |
| `/openmix/replay/save` | none |

### Keyboard

Tab and Shift+Tab move through the header, then each strip top to bottom, then the buses.

| Control | Keys |
|---|---|
| Faders, gain knob | Up/Down 1 dB (Shift: 0.1 dB), Page Up/Down 6 dB, Home 0 dB |
| VoicePad | Arrow keys move the puck (Shift: fine), Home resets |
| Plugin row | Enter open/add, Space bypass, Delete remove, Alt+Up/Down move, M menu |
| Processing key | Enter opens settings, Space switches on/off |
| Send key | Enter or Space switches, M or Shift+Enter opens level and pre-fader |
| Tape label | Enter or F2 rename |
| Meter | Enter resets the clip lamp |
| Buttons and pickers | Enter or Space |
| Record, save replay | Ctrl+Shift+R, Ctrl+Shift+S (while OpenMix has focus) |
| Soundboard pads | 1-8 while the soundboard is shown, M opens a focused pad's menu |

Everything (device, theme, strips, plugins and their settings, buses, soundboard) is saved to
`%APPDATA%\OpenMix\session.xml` every 10 seconds and when the app closes.
## Layout

`Source/` is split by job. Includes are written relative to `Source/`
(`#include "engine/MixerEngine.h"`).

| Folder | What's in it |
|---|---|
| `Source/app/` | `Main` (startup, crash report, log), `MainComponent` (window, header, status, console layout), `AppSettings`, `Session` (save/restore to XML), `OscControl`, `UpdateChecker` |
| `Source/engine/` | `MixerEngine` (device, real-time callback, strip -> bus -> output), `ChannelStrip` (per-strip DSP), `PluginChain` (VST3 inserts for strips and buses: bypass, wet/dry, NaN guard), `VoicePad` (XY pad DSP and its UI), `ClockBridge` (drift-correcting resampler between clocks), `ExternalIO` (extra devices and app sources), `AppCapture` (per-app and per-device loopback), `AppRouting` (per-app output device), `PluginScanner` (crash-safe scanning in child processes) |
| `Source/creator/` | `Recorder` and `ReplayBuffer`, `Soundboard` |
| `Source/dsp/` | Header-only real-time building blocks: `Biquad`, `DelayLine`, `AudioSafety` (limiter, NaN guard, DC blocker), `Dynamics` (gate, compressor, EQ, de-esser), `SincTable` (windowed-sinc resampling), `Loudness` (BS.1770 LUFS); plus `Denoiser` (RNNoise at any sample rate) |
| `Source/ui/` | `Theme` and `OpenMixLookAndFeel` (design tokens, control drawing), `Widgets`, `StripView`, `BusView`, `Panels` (processor, send and wet/dry pop-overs), `InsertRack` (insert list, plugin windows, presets, A/B), `AnalyserPanel`, `SoundboardPanel`, `SettingsPanel`, `TrayIcon` |
| `Source/third_party/rnnoise/` | RNNoise files patched for MSVC |
| `tests/CoreTests.cpp` | Core unit tests (no JUCE) |
| `resources/` | App icon (`make_icon.py` draws `icon.png` and `icon.ico`) |
| `installer/OpenMix.iss` | Inno Setup installer (built by CI for version tags) |

## Reliability

- **Audio thread:** no allocation, no blocking locks, no I/O; all UI changes are atomics,
  smoothed per sample. Denormals are flushed.
- **Safety limiter** on every bus (-0.3 dBFS ceiling, instant attack) protects ears,
  speakers and apps fed by virtual buses. The **LIMITER** lamp lights while it's working.
- **NaN/Inf guard:** a plugin that outputs invalid audio is bypassed automatically
  (red lamp, marked "stopped") instead of breaking the whole mix.
- **DC blocker** (5 Hz) on every input; **click-free** plugin bypass/enable (crossfade).
- **Plugin latency compensation:** every strip is delayed to match the slowest one, and a
  bypassed plugin keeps its latency, so strips that meet on a bus stay in time.
- **Crash-safe plugin scanning** in separate processes (see Inserts above).
- **Extra devices and app captures reconnect by themselves** after unplugging, stalls or
  Windows audio changes (checked every 2 s).
- **Crash-safe startup:** if a plugin crashes OpenMix while loading, the next start skips
  plugins and backs up the session to `session-before-crash.xml`.
- **Crash report and log:** if OpenMix crashes, it writes `crash-<time>.txt` with a stack
  trace to `%APPDATA%\OpenMix`; `openmix.log` there records device and plugin events
  (Settings > Open log folder).
- **Core tests** (`ctest`): clock drift/jitter, resampling pitch, limiter, NaN detection,
  DC blocker, filter stability, dynamics, denoiser latency at 44.1 and 48 kHz, and the EBU
  loudness reference. They run in CI on every push.

## License

OpenMix is licensed under the GNU AGPLv3 (see `LICENSE`). It's built on JUCE, which is
used here under its AGPLv3 option, and includes RNNoise v0.1.1 (BSD-3-Clause, see
`Source/third_party/rnnoise/COPYING`).

## Releases

Push a version tag (for example `git tag v0.2.0 && git push --tags`). CI builds the app,
runs the tests, and attaches both the zip and `OpenMix-<version>-setup.exe` to a GitHub
release.

## Known limits / next steps

- No own virtual audio driver yet; use a virtual cable meanwhile.
- VST3 plugins have to start on the UI thread (a VST3 rule), so a slow plugin can still pause
  the window briefly while it opens; the slot shows "Loading..." meanwhile.
- Hotkeys only work while OpenMix has focus; global hotkeys (while gaming) are planned.
- The update check (off by default) only tells you a new version exists and opens its
  release page; it doesn't download or install anything.
- "App's own output" uses the same Windows setting as Settings > Sound > Volume mixer; the
  interface behind it is undocumented, so on an unusual Windows build it can report that it's
  unavailable.
