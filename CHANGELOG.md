# Changelog

All notable changes to OpenMix will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [0.1.0] - 2026-11-27 (first release)

This is OpenMix's first public release. There was no earlier release:
`0.1.0` is the version in `CMakeLists.txt`, tagged `v0.1.0`.

### Added

**Core mixer**

- Up to 8 input strips and up to 8 output buses, added and removed at runtime.
- Strip sources: a channel or stereo pair of the main device (ASIO, WASAPI or
  DirectSound), any other Windows input device, one app's audio (per-app
  loopback), loopback of a Windows output device, or the media player.
- Buses: A1–A4 hardware and B1–B4 virtual, each to an output pair of the main
  device or to any Windows output device, with EQ, compressor, plugins and a
  safety limiter. Buses are grouped **Hardware / Virtual** on the desk, and
  each bus can have a custom name (its group never changes when renamed).
  **+ Bus** asks for the group first, then an optional custom name.
- **Routing matrix** (Mixer menu): every strip's sends and **bus-to-bus feeds**
  (a bus after its fader into another bus, e.g. B1 into A1) in one grid; a
  feed that would make a loop is refused. Feeds are saved in sessions and scenes.
- **Undo / redo** of mix changes (Ctrl+Z, Ctrl+Y or Ctrl+Shift+Z, also in the
  Mixer menu), up to 100 steps; a whole fader drag is one step.
- Per-app routing and per-app output device, multi-device I/O with
  drift-correcting resampling, and device unplug / replug while running.

**Voice and strip processing**

- Built-in voice processing on every strip: RNNoise noise removal, gate
  with adjustable attack / hold / release, 8-band parametric EQ (low shelf,
  six bells, high shelf; on buses too), compressor with adjustable knee,
  attack and release (on buses too), de-esser with adjustable detection
  frequency, and ducking (any strip as sidechain).
- DeepFilterNet 3 as a higher-quality noise removal option, with a verified
  in-app download for its model (weights are not bundled).
- VoicePad XY pad with Voice, Pan, Colour and Space modes. Voice mode tilts
  the tone from Lo to Hi (about +/-9 dB) and adds presence, then a doubler
  and a little reverb as the puck goes up.
- **Gain, HPF and LPF knobs on every strip** (gain -24 to +24 dB, HPF 20 Hz
  to 1 kHz, LPF 1 to 20 kHz; a filter knob at its end is off), sitting before
  the EQ in the signal path, switchable without clicks, with Ø invert polarity
  and MONO below them.
- **Per-strip presets**: save a strip's sound under a name and recall it on
  any strip. A preset carries gain, fader, processing, inserts, sends and
  the VoicePad, never the name, source, mute or solo state.
- **Solo-safe (SAFE)**: keeps a strip playing on the monitor bus while
  another strip is soloed.
- **Latency compensation**: each strip is delayed to match the slowest strip
  (noise removal + plugins), so sends stay aligned. Each strip shows its own
  latency in the fader bay.

**Plugins**

- VST3 plugins on every strip and bus: 6 slots each, drag to reorder,
  wet/dry, per-slot bypass, A/B compare, presets, latency compensation.
- Crash-safe plugin loading (on by default): a plugin is started in a
  separate test process before it is loaded into the mixer. A plugin that
  crashes while starting is skipped and put on the skip list instead of
  taking OpenMix down; one that is only slow to start asks before loading.
- Audio rendering runs in a duplicate plugin chain in a child process. A
  watchdog disables a crashed or stalled slot, restarts the remaining chain,
  and uses latency-matched dry audio during recovery. This adds two device
  callback blocks of latency and a second plugin instance; editors stay in
  OpenMix.
- VST2 hosting, opt-in only and OFF by default. It needs a licensed VST2 SDK
  supplied separately: the SDK is not included in the repo, and Steinberg
  stopped issuing new VST2 licences in October 2018.

**Meters, loudness and status**

- RMS meters, spectrum analyser and LUFS loudness on every bus.
- Loudness normalisation per bus (-14, -16 or -23 LUFS), K-weighted like the
  LUFS readout, before the bus fader.
- Status bar along the bottom: LIVE, the engine's own CPU%, plugin scanning and recording progress, and a lamp for each remote you switched on (OSC, phone, MIDI), lit while it is listening.

**Creator tools, scenes and control**

- Multitrack recorder, replay buffer ("save the last 60 s") and soundboard.
- Soundboard: 32 pads in pages of 8, a volume, colour and mode per pad (play
  once, restart, loop, hold to play), a progress bar while a pad plays, and
  drag and drop to load.
- **Media player:** a bar under the desk with a playlist (streamed from disk,
  with repeat and shuffle) that any strip can use as its source, so it gets
  the strip's processing, ducking and sends. Plays WAV, MP3, FLAC, Ogg and
  AIFF plus what Windows Media Foundation decodes (M4A, AAC, WMA, MP4, MOV,
  MKV, AVI, WMV and more); shows the waveform (click to seek) and the
  picture of video files.
- **Strips:** copy and paste a whole strip (plugins keep their settings);
  select several strips (Shift / Ctrl+click) and change them together; drag
  across M, S or SAFE keys to set several strips at once; pick an icon for a
  strip's name plate.
- **Desk:** strips and buses widen or narrow with the window; a mic or device input
  that gives digital silence for 10 seconds shows a note; undo also restores
  a strip's name, colour and icon.
- **Buses:** A4 and B4, for up to 8 buses. Existing sessions, scenes, hotkeys
  and OSC/MIDI mappings keep pointing at the same buses. Once B1-B4 are in use,
  a new virtual bus can take a free hardware slot (B5, B6...).
- **Setup menu:** restart the audio engine (Ctrl+R) and auto restart it on device
  changes; load, save, reset and load-on-startup settings files; release the
  media file; always on top; lock controls; tray and startup options.
- **Undo:** undoing a strip paste brings back the strip's plugins and their
  settings. OSC and MIDI reach borrowed buses as B5, B6... as well as by slot.
- **Solo before the fader (PFL)** as an option in the Mixer menu; after-fader
  listen stays the default.
- Release builds are code-signed when the repository has a certificate
  (`installer/sign.ps1`, `docs/SIGNING.md`).
- Scenes: snapshot the whole mixer and switch with a hotkey.
- Global hotkeys that work while a game has focus: mute, push-to-talk,
  soundboard, recording and scenes.
- Remote control: OSC, phone / browser remote over WebSocket, MIDI mappings
  (learned, saved straight away, reconnecting when the controller is plugged
  back in), and Stream Deck via OSC / WebSocket plugins.
- Session save / restore and autosave every 10 seconds (only when something
  changed; plugin state is refreshed at most once a minute, so heavy plugins
  don't stutter the window) and on exit.

**Interface**

- Resizable UI with light and dark themes, channel colours and names.
- Compact mode: narrower strips and buses, tighter gaps, shorter plugin rows
  and soundboard bar, so more channels fit on a laptop screen.
- A desk of narrow, identical columns: every strip reads INPUT, PROCESSING,
  INSERT, SENDS (a key and a level knob per bus), the voice pad button, then
  one fader bay aligned across the desk with M / S / SAFE, latency and a
  coloured name plate. Buses follow the same grid, with a MON key for the
  monitor bus, and are split into Hardware / Virtual groups.
- Colour means state: amber for keys that are on, red for mute, yellow for
  solo, green for level.
- A menu bar (Mixer, View, Setup) with a scene stepper that is also the
  title bar, one window-button design for the main window and dialogs,
  and a two-column Settings window.

### Known limits

- No virtual audio driver of its own yet, so apps that need a microphone
  input from OpenMix need a virtual cable (VB-Audio Virtual Cable or similar)
  on a B bus.
- Plugin editors and state/control calls remain in OpenMix, so failures in
  plugin UI code or another non-rendering call can still affect the app.
- VST3 plugins have to start on the UI thread (a VST3 rule), so a slow plugin
  can pause the window briefly while it opens; its slot shows "Loading..."
  meanwhile.
- The phone / WebSocket remote has no password: anyone on the same network
  can control the mixer while it is on. Only enable it on a network you trust.
  (Web pages from other sites are refused.)
- Global hotkeys use a keyboard hook that only listens. A game with strict
  anti-cheat could in principle object to any keyboard hook; Discord and OBS
  use the same mechanism. The hook runs on its own thread, so a busy OpenMix
  never slows typing in other apps.
- "App's own output" uses the same Windows setting as Settings > Sound >
  Volume mixer. The interface behind it is undocumented, so on an unusual
  Windows build it can report that it's working when it isn't.
- Windows runtime test with real plugins is still to be done before release.
