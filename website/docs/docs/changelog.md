# Changelog

The full changelog lives in the repository at
[`CHANGELOG.md`](https://github.com/itzcaesar/OpenMix/blob/main/CHANGELOG.md).
This page is a summary; it does not duplicate the file, so the repo file
stays the single source of truth.

## 0.1.0: first release (unreleased at the time this site was written)

OpenMix's first public release. There was no earlier release: `0.1.0`
is the version in `CMakeLists.txt`, and the first tag will be `v0.1.0`.

### Added

- **Core mixer:** up to 8 strips and 6 buses at runtime; per-app routing;
  multi-device I/O with drift-correcting resampling; a routing matrix with
  bus-to-bus feeds; undo / redo of mix changes.
- **Voice and strips:** RNNoise and DeepFilterNet 3 noise removal, gate, 8-band EQ, compressor,
  de-esser, ducking, voice pad; gain, HPF and LPF knobs on every strip;
  per-strip presets (in the strip menu); solo-safe; latency compensation
  with a per-strip readout.
- **Plugins:** VST3 (6 slots per strip/bus) with crash-safe loading and
  out-of-process audio rendering; a watchdog isolates crashes and stalls,
  with dry fallback. Editors remain in the app. VST2 opt-in only.
- **Meters and loudness:** RMS meters, K-weighted loudness normalisation,
  spectrum and LUFS per bus; status bar with engine CPU%.
- **Creator tools and control:** multitrack recorder, replay buffer,
  soundboard, scenes, global hotkeys, OSC / phone / WebSocket / MIDI
  remote, Stream Deck via OSC or WebSocket.
- **Interface:** a desk of narrow, identical columns with coloured name
  plates, send knobs and one aligned fader bay; a menu bar with a scene
  stepper that doubles as the title bar; light/dark themes, compact mode,
  Hardware / Virtual bus groups, custom bus names, a MON key for the
  monitor bus.

### Known limits

No virtual audio driver yet (use a virtual cable on a B bus); plugin editors
remain in the app process; the phone remote has no password.
Full list: [Troubleshooting & known limits](troubleshooting.md).
