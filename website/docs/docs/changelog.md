# Changelog

The full changelog lives in the repository at
[`CHANGELOG.md`](https://github.com/itzcaesar/OpenMix/blob/main/CHANGELOG.md).
This page is a summary — it does not duplicate the file, so the repo file
stays the single source of truth.

## 0.1.0 — first release (unreleased at the time this site was written)

OpenMix's first public release. There was no earlier release — `0.1.0`
is the version in `CMakeLists.txt`, and the first tag will be `v0.1.0`.

### Added

- **Core mixer:** up to 8 strips and 6 buses at runtime; per-app routing;
  multi-device I/O with drift-correcting resampling.
- **Voice and strips:** RNNoise noise removal, gate, 3-band EQ, compressor,
  de-esser, ducking, VoicePad; HPF/LPF per strip; per-strip presets;
  solo-safe; per-channel latency readout; gain slider under the source name.
- **Plugins and effects:** VST3 (6 slots per strip/bus) with crash-safe
  loading; VST2 opt-in only; built-in Reverb, tempo-synced Delay and
  Chorus as inserts, plus REV / DLY / CHO FX sends driven by tap tempo.
- **Meters and loudness:** RMS meters, loudness normalisation, spectrum
  and LUFS per bus; status bar with engine CPU% and tap tempo (40–240 BPM).
- **Creator tools and control:** multitrack recorder, replay buffer,
  soundboard, scenes, global hotkeys, OSC / phone / WebSocket / MIDI
  remote, Stream Deck via OSC or WebSocket.
- **Interface:** light/dark themes, 5 accent colours, compact mode,
  Hardware / Virtual bus groups, custom bus names, monitor bus shown by
  a 1 px accent outline.

### Known limits

No virtual audio driver yet (use a virtual cable on a B bus); the plugin
probe only catches load-time crashes; the phone remote has no password.
Full list: [Troubleshooting & known limits](troubleshooting.md).
