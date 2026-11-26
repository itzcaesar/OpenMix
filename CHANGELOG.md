# Changelog

All notable changes to OpenMix will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [0.1.0] - Unreleased (first release)

This is OpenMix's first public release. There was no earlier release:
`0.1.0` is the version in `CMakeLists.txt`, and the first tag will be `v0.1.0`.

### Added

**Core mixer**

- Up to 8 input strips and up to 6 output buses, added and removed at runtime.
- Strip sources: a channel or stereo pair of the main device (ASIO, WASAPI or
  DirectSound), any other Windows input device, one app's audio (per-app
  loopback), or loopback of a Windows output device.
- Buses: A1–A3 hardware and B1–B3 virtual, each to an output pair of the main
  device or to any Windows output device, with EQ, compressor, plugins and a
  safety limiter. Buses are grouped **Hardware / Virtual** on the desk, and
  each bus can have a custom name (its group never changes when renamed).
  **+ Bus** asks for the group first, then an optional custom name.
- Per-app routing and per-app output device, multi-device I/O with
  drift-correcting resampling, and device unplug / replug while running.

**Voice and strip processing**

- Built-in voice processing on every strip: RNNoise noise removal, gate,
  3-band EQ, compressor, de-esser and ducking (any strip as sidechain).
- VoicePad XY pad with Voice, Pan, Colour and Space modes.
- **HPF / LPF per strip**, in the EQ panel, with an on/off key and a cutoff
  slider each (HPF 20 Hz–1 kHz, LPF 1–20 kHz), sitting before the EQ in the
  signal path, switchable without clicks.
- Input gain as a slider directly under the source name (-24 to +24 dB),
  with Ø invert polarity and MONO below it.
- **Per-strip presets**: save a strip's sound under a name and recall it on
  any strip. A preset carries gain, fader, processing, inserts, sends and
  the VoicePad, never the name, source, mute or solo state.
- **Solo-safe (SAFE)**: keeps a strip playing on the monitor bus while
  another strip is soloed.
- **Latency compensation**: each strip is delayed to match the slowest strip
  (noise removal + plugins), so sends stay aligned. The overall device
  latency is shown in the header readout; there is no per-strip latency readout
  on the desk.

**Plugins**

- VST3 plugins on every strip and bus: 6 slots each, drag to reorder,
  wet/dry, per-slot bypass, A/B compare, presets, latency compensation.
- Crash-safe plugin loading (on by default): a plugin is started in a
  separate test process before it is loaded into the mixer. A plugin that
  crashes while starting is skipped and put on the skip list instead of
  taking OpenMix down; one that is only slow to start asks before loading.
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
- Gain slider under the source name, monitor bus shown by a 1px amber
  outline and a lit name bar, and the bus row split into Hardware / Virtual
  groups with a divider.
- The strip reads top to bottom as INPUT, PROCESSING, INSERTS, VOICE PAD,
  SENDS (one key per bus, in a row), then the fader bay.

### Known limits

- No virtual audio driver of its own yet, so apps that need a microphone
  input from OpenMix need a virtual cable (VB-Audio Virtual Cable or similar)
  on a B bus.
- The loading probe only catches crashes at start-up. A plugin that crashes
  while processing audio (after it loaded fine) can still take OpenMix down;
  full out-of-process plugin hosting is not done yet.
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
