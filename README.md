# OpenMix

A virtual audio mixer for Windows. Bring in mics, other devices, single apps (Spotify, a
game, Discord) or everything an output device plays, process them, and send separate mixes
to your headphones, speakers, stream and calls.

- **Inputs:** up to 8 strips. Each takes a channel or stereo pair of the main device
  (ASIO, WASAPI or DirectSound), any other Windows input, one app's audio, or a loopback of
  a Windows output device.
- **Voice processing built in:** AI noise removal (RNNoise), gate, 3-band EQ, compressor,
  de-esser and ducking on every strip, plus a VoicePad (XY pad with voice, pan, colour and
  space modes).
- **VST3 plugins** on every strip and bus: 6 slots each, crash-safe scanning, bypass without
  clicks, wet/dry, A/B compare, presets, latency compensation. **Built-in effects** too:
  Reverb, tempo-synced Delay and Chorus as insert slots, with their own settings panels —
  no plugin to install. Every strip also has post-fader **FX sends** (Reverb/Delay/Chorus)
  into shared global effects, driven by the **tap tempo** in the status bar.
- **Outputs:** up to 6 buses (A1-A3 hardware, B1-B3 virtual), each to an output pair of the
  main device or to any Windows output device, with EQ, compressor, plugins and a safety
  limiter. Buses are grouped **Hardware** / **Virtual** on the desk, and each bus can have a
  custom name (its group never changes when you rename it).
- **Status bar** along the bottom: audio device, sample rate, buffer size, engine CPU%,
  a LIVE indicator, and tap tempo with the BPM next to it.
- **Creator tools:** multitrack recorder, replay buffer, soundboard, spectrum and LUFS
  loudness on every bus.
- **Scenes and global hotkeys:** save whole mixes and switch between them; mute, push to
  talk, soundboard, recording and scenes on hotkeys that work while a game has focus.
- **Control:** keyboard throughout, OSC remote control, tray icon, start with Windows.

Built with C++20 and JUCE 9.0.5. Licensed under the GNU AGPLv3.

## Download

Every push to `main` is built by GitHub Actions: open the latest run under **Actions** and
download `OpenMix-windows-x64`. Version tags (for example `v0.2.0`) also publish a GitHub
release with the zip and an installer, `OpenMix-<version>-setup.exe`.

## Build it yourself

Requirements: Windows 10 2004 or later, Visual Studio 2026 with the **Desktop development
with C++** workload (it includes CMake). Clone with submodules (JUCE and RNNoise):

```powershell
git clone --recursive https://github.com/itzcaesar/OpenMix.git
```

**In Visual Studio:** File > Open > Folder, pick the `vs2026` preset, set the startup item to
`OpenMix.exe`, press F5.

**From a Developer PowerShell for VS 2026:**

```powershell
cmake --preset vs2026
cmake --build --preset release
ctest --test-dir build -C Release
.\build\OpenMix_artefacts\Release\OpenMix.exe
```

## Quick start: mic to Discord, music to your headphones

1. Click the **status readout** in the header (it shows the device) and choose your audio
   device. ASIO gives the lowest latency; Windows Audio works with anything.
2. On **Input 1**, open the source picker and choose your mic. Switch on **NR** to remove
   background noise.
3. On **Input 2**, choose **Application audio** and pick Spotify (or a game).
4. Bus **A1** is what you hear: point its output at your headphones.
5. Bus **B1** is for apps that need a microphone: point it at a virtual cable (for example
   VB-Audio Virtual Cable's "CABLE Input"), then choose the cable's other end ("CABLE
   Output") as the microphone in Discord or OBS.
6. On each strip, the send keys beside the fader decide where it goes: mic on A1 and B1,
   music on A1 only (or on B1 too, if the call should hear it). Right-click a send key to set
   its level or make it pre-fader.

Everything is saved automatically.

## The window

- **Header:** app name, the status readout (device, sample rate, latency, CPU, dropouts;
  click it for Audio settings), the **Scene** picker, **Record** and its options,
  **Replay**, **Soundboard** (shows or hides the soundboard bar) and **Settings**.
- **Inputs** on the left, **outputs** (buses) on the right. **+ Strip** and **+ Bus** sit
  next to the section names. The bus row is split into **Hardware** (A1-A3) and **Virtual**
  (B1-B3) groups with a divider between them; **+ Bus** asks for the group first, then an
  optional custom name, and takes the next free slot in that group.
- The **status bar** along the bottom shows the audio device, sample rate, buffer size,
  engine CPU% and a LIVE indicator, plus **Tap** tempo with the BPM next to it (click the
  BPM to type one, 40-240). Tapping sets the tempo for every tempo-synced delay.
- Every strip and bus ends in the same **fader bay**, so faders, meters and mute rows line
  up across the desk. The number above each meter is the held peak; a red bar under it marks
  0 dBFS or more.
- The **soundboard** is a bar along the bottom.
- A warning banner appears if the main device's input and output are two different Windows
  devices, because their clocks drift and that crackles. The banner explains the fix.

## Strips

Top to bottom, in the order the audio flows:

1. **Name plate** (double-click, Enter or F2 to rename) and the three-dot menu: rename,
   colour, mono, invert polarity, remove the strip.
2. **Source**, then the **input gain slider** directly under the source name (-24 to
   +24 dB, value readout, double-click or Home for 0 dB), then **Ø** (invert polarity) and
   **MONO** below the slider. Sources:
   - **Main device:** one channel (spread to stereo) or a **stereo pair**.
   - **Windows input device:** any other mic or capture device, alongside the main device.
   - **Application audio:** one app's sound, using Windows' per-process loopback. If the
     app quits, the strip shows "(not running)" and reconnects when it starts again.
     **App's own output** (in the same menu) moves the app's direct sound to a device you
     don't listen to, so you only hear it through OpenMix; it goes back to normal when the
     strip's source changes or OpenMix quits.
   - **Windows output (loopback):** everything one output device plays, without a virtual
     cable. Devices OpenMix itself plays into are greyed out, because capturing them would
     feed the sound back into itself.
3. **Filter:** **HPF** and **LPF** keys with a cutoff knob each (HPF 20 Hz-1 kHz, LPF
   1-20 kHz). The filters sit before the EQ and switch on/off without clicks.
4. **Processing** keys, lit when on. Click for settings, Space switches on or off.
   - **NR** noise removal: fans, hum and keyboard noise. Adds 20 ms at 48 kHz (RNNoise's own
     rate); at other rates it resamples to 48 kHz and back, about 1 ms more. Other strips are
     delayed to match.
   - **Gate** turns the strip down while it's quiet. **EQ**: low shelf, sweepable mid, high
     shelf. **Comp** evens out loud and quiet parts. **DS** softens harsh S sounds.
     **Duck** lowers this strip while another strip has signal (music under your voice).
   - Gate, compressor, de-esser and ducking show a gain-reduction meter in their panel.
5. **Preset:** save the strip's sound under a name and recall it here (or on any other
   strip). A preset carries gain, fader, processing, inserts, sends and the VoicePad — never
   the name, source, mute or solo state. If a preset's plugins are missing on recall, those
   slots are skipped and you are told how many.
6. **Inserts:** up to 6 slots, VST3 plugins or built-in effects (Reverb, tempo-synced Delay,
   Chorus), see [Plugins](#plugins).
7. **Voice pad** (double-click or Home resets the puck):
   - *Voice*: X = tone tilt (dark to bright), Y = presence and echo.
   - *Pan*: X = constant-power pan, Y = stereo width (100-200%).
   - *Color*: corners Warm, Bright, Radio and Close; the puck blends them.
   - *Space*: X = pan, Y = distance (quieter, darker, with early reflections).
8. **Fader bay:** fader and meter, the **send keys** beside them, **Mute**, **Solo** and
   **SAFE**, a per-channel **latency** readout, and a solid-colour **name bar** along the
   bottom of the strip.
   - Click a send key to switch it. Right-click (or M) for its level and **pre-fader**
     (the send ignores the strip fader, for example a separate mix for a call). A level
     other than 0 dB and "P" for pre-fader are printed on the key. Send keys show the bus's
     own name, so a renamed bus reads correctly here.
   - Under the bus sends, three small **FX sends** (REV / DLY / CHO) feed the shared global
     reverb, delay and chorus; the effect return is mixed into every bus the strip is routed
     to. The Delay send follows the tap tempo.
   - **Solo** plays the strip alone on the **monitor bus** (A1 unless you choose another in a
     bus menu). **SAFE** (solo-safe) keeps the strip playing on the monitor bus while another
     strip is soloed. Stream and call buses are not affected.
   - **Latency** is the strip's total (noise removal + plugins) in milliseconds; the other
     strips are delayed to match it, so sends stay aligned.

Signal path: gain, polarity/mono, DC blocker, noise removal, gate, HPF/LPF, EQ, compressor,
de-esser, inserts, voice pad, ducking, latency alignment, mute, fader, sends.

## Plugins

"+ Add plugin" lists the VST3 plugins in `C:\Program Files\Common Files\VST3`, grouped by
maker when there are many. They are scanned in the background, each in its own process, so
a plugin that crashes while being scanned is skipped instead of taking OpenMix down
(Settings > Retry skipped plugins tries them again).

- Click a plugin to open its window; its lamp bypasses it (with a short crossfade, and its
  latency kept so timing doesn't jump).
- Drag a plugin up or down to reorder the chain (or Alt+Up/Down).
- The dots menu: **Wet/dry mix** (the dry signal is delayed to match the plugin's latency),
  **A/B compare** (two settings to switch between; the row shows "(B)" while you hear B),
  save or load a **preset** (`.omxpreset`), replace, remove.
- A plugin that outputs invalid audio (NaN/Inf) is stopped automatically and marked, instead
  of breaking the mix. "Try again" in its menu restarts it.
- **Built-in effects** (no plugin to install): **Reverb**, **Delay** and **Chorus** are in the
  "+ Add plugin" menu under **Built-in effect**, in any insert slot on a strip or a bus.
  Click one to open its settings panel; it has the same bypass, wet/dry mix, move and remove
  as a VST slot, and it is saved in sessions and strip presets. A Delay added this way is
  tempo-synced from the start: it follows the tap-tempo BPM (change the note value in its
  panel, or switch tempo sync off and set the time directly). Built-ins have zero latency.
- **Crash-safe loading** (on by default, Settings): before a plugin is loaded into the mixer,
  it is started in a separate test process — created, prepared and run through one silent
  block. A plugin that crashes or hangs while starting is skipped and blacklisted instead of
  taking OpenMix down (Settings > Retry skipped plugins tries it again). This protects
  *loading*; a plugin that crashes later, while processing audio, can still take the mixer
  down — full out-of-process audio hosting is not done yet (see ROADMAP Phase 5).

### VST2 plugins (opt-in, local build only)

VST2 hosting is **off by default** and is not in the released builds. JUCE 9 still
contains the VST2 host code, but it needs `aeffect.h` / `aeffectx.h` from the VST2 SDK,
which JUCE no longer bundles — and Steinberg stopped issuing new VST2 licences in
October 2018, so a new developer cannot legally distribute a VST2 host. Only enable
this if you already hold a VST2 licence and SDK. The SDK is never committed to this
repo, never fetched by CMake, and CI builds with VST2 off.

To build with it, from a Developer PowerShell for VS 2026:

```powershell
cmake --preset vs2026 -DOPENMIX_ENABLE_VST2=ON -DVST2_SDK_DIR="C:/SDKs/vstsdk2.4"
cmake --build --preset release
```

`VST2_SDK_DIR` must be the SDK folder containing `pluginterfaces/vst2.x/aeffect.h`
(from `vstsdk3610_11_06_2018_build_37` or older). With it on, OpenMix also scans
the usual VST2 locations (`C:\Program Files\Steinberg\VstPlugins`,
`C:\Program Files\VstPlugins`, and the `VST_PATH` environment variable) for `.dll`
plugins, in the same crash-safe child process as VST3. VST2 plugins then appear in
"+ Add plugin" alongside VST3 ones and use the same slots, bypass, wet/dry, presets
and latency compensation. Session files already store the plugin's format, so a
session saved with a VST2 plugin loads it back when built with VST2 on — and that
slot will be empty on a default (VST2-off) build.

## Buses

- **Output:** an output pair of the main device, or any Windows output device. Each bus can
  use a different device: A1 headphones, A2 speakers, B1 a virtual cable for Discord or OBS.
  Extra devices run on their own clocks; OpenMix resamples them and corrects the drift,
  which adds about 10-25 ms on those devices only.
- **Custom names:** rename a bus from its three-dot menu (empty = the default A1..A3 /
  B1..B3). The name is shown on the bus strip, on the strips' send keys, in the soundboard
  and in recording. A bus never changes group when renamed: slots A1-A3 are always
  **Hardware**, B1-B3 always **Virtual**.
- **LIMITER:** the lamp lights while the safety limiter (-0.3 dBFS ceiling) is reducing gain,
  and the gain-reduction meter next to it shows how much (in dB). The **monitor bus** is
  shown only by a thin accent-coloured outline around the strip and its name bar lit in the
  accent colour — there is no "monitor" text on the strip. **Loudness** is the short-term
  LUFS (the number streaming platforms normalise to), with the normalisation target and gain
  when normalisation is on. The bus strip shows the live spectrum only; the full analyser
  (see below) has the momentary and integrated numbers.
- **Meters** show peak (the LED ladder) and RMS (the solid bar inside it): peak catches
  transients, RMS shows how loud the bus actually sounds. The same RMS bar is on every
  strip and on the soundboard.
- **Loudness normalisation** (a bus's three-dot menu): a slow gain that pulls the bus towards
  -14 LUFS (streaming), -16 or -23 (broadcast), clamped to ±12 dB and smoothed so it never
  pumps on a drum hit. It holds its gain in silence instead of turning the noise floor up.
  It is saved with the session and with scenes.
- **EQ** and **Comp** keys and up to 6 **inserts**, the same as on a strip (VST3 or built-in
  effects). Bus path: sum, EQ, compressor, inserts, fader, loudness normalisation, safety limiter.
- **Analyser:** a live spectrum on the bus strip. Click it for the full **analyser**: spectrum plus
  momentary, short-term and integrated loudness.
- The three-dot menu: rename the bus, analyser, loudness normalisation, monitor bus (where
  solo plays), remove the bus.

## Recording and replay

- **Record** (Ctrl+Shift+R) writes every bus, every strip, or both, to one WAV file each in
  `Documents\OpenMix Recordings\<date and time>`. While recording, the button shows the
  elapsed time.
- The dots next to Record choose what to record, set the **replay buffer** and open the
  recordings folder.
- **Replay buffer:** keeps the last 30, 60 or 120 seconds of one bus in memory. **Replay**
  (Ctrl+Shift+S) saves them as a WAV file.

## Soundboard

A bar along the bottom of the window (the header's **Soundboard** button shows or hides it).

- Eight pads for WAV, MP3, FLAC or Ogg files up to 60 seconds. Click a pad, or press 1-8
  anywhere in OpenMix, to play; again to stop. Right-click a pad (or M) to load, rename or
  clear it.
- Its own volume and meter, and **Play on** keys for the buses it plays on (A1 and B1 by
  default, so you and your stream both hear it). **Stop all** stops every pad.

## Scenes

A scene is a saved mix: strip and bus levels, mutes, solos, sends (on, level, pre-fader),
processing settings, plugin bypass and wet/dry, the voice pad, bus EQ and compressor, and
the soundboard's level and buses. Sources, devices and the plugins themselves are not part
of a scene, so switching is instant, and every level glides to its new value without a
click.

The **Scene** picker in the header lists your scenes; click one to switch. The same menu
saves the current mix as a new scene, updates the current scene, renames or deletes it, and
opens Hotkeys. Scenes are saved with the session. Over OSC, `/openmix/scene/<n>` switches to
scene n (counting from 1).

## Global hotkeys

**Settings > Hotkeys** (or **Hotkeys...** in the Scene menu). These work in any app, also
while a game is in front, and the keys still reach that app, so pick combinations it doesn't
use (for example with Ctrl+Alt).

- **+ Add hotkey**, pick what it does, then press the keys (Escape cancels). Click a
  hotkey's keys to change them; **Remove** deletes it. A combination can only be used once.
- Actions: mute or unmute a strip, **push to talk** on a strip (open while you hold the keys,
  muted when you let go), play a soundboard pad, stop all sounds, start or stop recording,
  save the replay, switch to a scene.
- Keys sent by other software (macro tools, remote desktop) are ignored on purpose.

## Settings

Light or dark theme, an accent colour for the lamps, compact mode, hotkeys, start with Windows, start minimized, keep running in the tray when the
window closes, plugin scanning and crash-safe loading, presets and VST3 folders, the update check, OSC,
phone / WebSocket and MIDI remote control, and the log folder. In the tray, double-click opens the mixer; right-click to quit.

The **update check** is off by default. When on, OpenMix asks GitHub once at startup
whether a newer release exists and, if so, shows an Update button that opens the release
page. It doesn't download or install anything.

## Remote control (OSC, phone, MIDI, Stream Deck)

All three remotes speak the same paths, so a fader moved from a phone, a MIDI knob or a
Stream Deck does exactly what the same OSC address does. They are off by default (Settings);
while one is on, anyone on your network can control the mixer, so only turn them on at home
or on a network you trust. There is no password: the LAN is the trust boundary.

### Phone / browser remote and WebSocket API

Settings > "Phone / browser remote and WebSocket API" (TCP port 8080 unless you change it).
Open `http://<this PC>:8080/` on your phone: faders, mutes, soundboard pads and scenes, live
over the WebSocket, with HTTP polling as a fallback if the WebSocket can't connect.

The same port is the API:

- `GET /api/state` — the mixer's state as JSON (strips, buses, soundboard, scenes, recording).
- `POST /api/command` with `{"path": "/openmix/strip/1/fader", "value": -6}` — one command.
- `ws://<this PC>:8080/ws` — send `{"path": ..., "value": ...}`, receive
  `{"type": "state", "state": {...}}` whenever the mix changes, plus `{"type": "ok"}` or
  `{"type": "error", ...}` for each command.

### OSC

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
| `/openmix/bus/<bus>/norm` | 0 or 1 (loudness normalisation on/off) |
| `/openmix/soundboard/<n>/play` | none (plays or stops pad n) |
| `/openmix/record` | 1 starts, 0 stops |
| `/openmix/replay/save` | none |
| `/openmix/scene/<n>` | none (switches to scene n) |

### MIDI controllers

Settings > "MIDI controller": pick a device, then pick a target (a strip fader or mute, a bus
fader, a soundboard pad, Record), press **Learn...**, and move a knob or press a key on the
controller. A CC moves a fader across its range; a note toggles a mute, plays a pad or starts
recording. Mappings are saved in the app settings.

### Stream Deck

There is no native OpenMix Stream Deck plugin. Use the Stream Deck software's OSC plugin
with the addresses above, or a WebSocket plugin against the phone-remote port — both speak
the same paths, so a button that sends `/openmix/strip/1/mute` with 1 works either way.

## Keyboard

Tab and Shift+Tab move through the header, then each strip top to bottom, then the buses.

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
| Soundboard pads | 1-8 while the soundboard is shown; M opens a focused pad's menu |

These shortcuts work while OpenMix has focus. For keys that work in any app, set up
[global hotkeys](#global-hotkeys).

## Where things are saved

| What | Where |
|---|---|
| Session (devices, theme, strips, plugins and their settings, buses, soundboard) | `%APPDATA%\OpenMix\session.xml`, every 10 seconds and on exit |
| Scenes | inside the session |
| App settings (startup, tray, OSC, phone/WebSocket, MIDI, hotkeys, recording options) | `%APPDATA%\OpenMix\settings.xml` |
| Log and crash reports | `%APPDATA%\OpenMix\openmix.log`, `crash-<time>.txt` |
| Recordings and replays | `Documents\OpenMix Recordings` |
| Plugin presets | `Documents\OpenMix Presets` |

## Reliability

- **Audio thread:** no allocation, no blocking locks, no I/O. Every UI change is an atomic,
  smoothed per sample. Denormals are flushed.
- **Safety limiter** on every bus protects ears, speakers and apps fed by virtual buses.
- **NaN/Inf guard** on every plugin, and a last guard on every strip.
- **DC blocker** (5 Hz) on every input; click-free plugin bypass and enable.
- **Latency compensation:** every strip is delayed to match the slowest one (plugins and
  noise removal), and a bypassed plugin keeps its latency, so strips that meet on a bus stay
  in time.
- **Crash-safe plugin scanning** in separate processes, and **crash-safe plugin loading**:
  a plugin is started in a test process before it is loaded into the mixer.
- **Reconnects by itself:** extra devices, app captures and loopbacks come back after
  unplugging, stalls or Windows audio changes (checked every 2 seconds).
- **Safe start:** if a plugin crashes OpenMix while a session loads, the next start skips
  plugins and keeps a copy of the session in `session-before-crash.xml`.
- **Crash report and log** in `%APPDATA%\OpenMix` (Settings > Open log folder).
- **Core tests** (`ctest`, no JUCE needed): clock drift and jitter, resampling pitch,
  limiter, NaN detection, DC blocker, filter stability, dynamics, denoiser latency at 44.1
  and 48 kHz, the EBU loudness reference, RMS metering and loudness normalisation. They run in CI on every push.

## Project layout

`Source/` is split by job. Includes are written relative to `Source/`
(`#include "engine/MixerEngine.h"`).

| Folder | What's in it |
|---|---|
| `Source/app/` | `Main` (startup, crash report, log), `MainComponent` (window, header, console layout, scenes), `AppSettings`, `Session` (save and restore, scene capture and recall), `GlobalHotkeys` (system-wide keyboard hook), `OscControl`, `RemoteCommands` (one remote language for OSC, WebSocket and MIDI), `RemoteServer` (phone page, HTTP and WebSocket API), `MidiControl` (learnable MIDI mappings), `UpdateChecker` |
| `Source/engine/` | `MixerEngine` (device, real-time callback, strips to buses to outputs), `ChannelStrip` (per-strip DSP), `PluginChain` (VST3 inserts for strips and buses), `VoicePad` (XY pad DSP and its UI), `ClockBridge` (drift-correcting resampler between clocks), `ExternalIO` (extra devices, app and loopback sources), `AppCapture` (app and device loopback capture), `AppRouting` (per-app output device), `PluginScanner` (scanning and crash-safe loading probes in child processes) |
| `Source/creator/` | `Recorder` and `ReplayBuffer`, `Soundboard` |
| `Source/dsp/` | Header-only real-time blocks: `Biquad`, `DelayLine`, `AudioSafety` (limiter, NaN guard, DC blocker), `Dynamics` (gate, compressor, EQ, de-esser), `SincTable` (windowed-sinc resampling), `Loudness` (BS.1770 LUFS), `Metering` (RMS meter, loudness normaliser); plus `Denoiser` (RNNoise at any sample rate) |
| `Source/ui/` | `Theme` and `OpenMixLookAndFeel` (design tokens, control drawing), `Widgets`, `StripView`, `BusView`, `InsertRack` (insert list, plugin windows, presets, A/B), `Panels` (processor, send and wet/dry pop-overs), `AnalyserPanel` (analyser and the bus spectrum), `SoundboardPanel`, `SettingsPanel`, `HotkeysPanel`, `TrayIcon` |
| `Source/third_party/rnnoise/` | RNNoise files patched for MSVC |
| `tests/CoreTests.cpp` | Core unit tests |
| `resources/` | App icon: `make_icon.py` draws `icon.png` and `icon.ico` |
| `installer/OpenMix.iss` | Inno Setup installer, built by CI for version tags |
| `docs/DESIGN.md` | UI design direction and the reasons behind it |
| `ROADMAP.md` | What's done and what's next |

## Releases

Push a version tag (for example `git tag v0.2.0` then `git push --tags`). CI builds the app,
runs the tests, and attaches the zip and `OpenMix-<version>-setup.exe` to a GitHub release.

## Known limits

- No virtual audio driver of its own yet, so apps that need a microphone input from OpenMix
  need a virtual cable (VB-Audio Virtual Cable or similar) on a B bus.
- VST3 plugins have to start on the UI thread (a VST3 rule), so a slow plugin can pause the
  window briefly while it opens; its slot shows "Loading..." meanwhile.
- A plugin that crashes while processing audio (after it loaded fine) can still take OpenMix
  down. The loading probe only catches crashes at start-up; full out-of-process plugin
  hosting is not done yet.
- The phone / WebSocket remote has no password: anyone on the same network can control the
  mixer while it is on. Only enable it on a network you trust.
- Global hotkeys use a keyboard hook that only listens. A game with strict anti-cheat could
  in principle object to any keyboard hook; Discord and OBS use the same mechanism.
- "App's own output" uses the same Windows setting as Settings > Sound > Volume mixer. The
  interface behind it is undocumented, so on an unusual Windows build it can report that it's
  unavailable.

## License

OpenMix is licensed under the GNU AGPLv3 (see `LICENSE`). It's built on JUCE, used here
under its AGPLv3 option, and includes RNNoise v0.1.1 (BSD-3-Clause, see
`Source/third_party/rnnoise/COPYING`).
