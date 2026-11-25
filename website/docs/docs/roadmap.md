# Roadmap

The full roadmap lives in the repository at
[`ROADMAP.md`](https://github.com/itzcaesar/OpenMix/blob/main/ROADMAP.md).
This page is a summary of where 0.1.0 stands and what is still open.

**Goal:** a VoiceMeeter-class virtual mixer for Windows: low-latency
ASIO/WASAPI I/O, VST3 plugin hosting per strip, an XY voice/pan pad,
several physical and virtual devices at once, and eventually its own
virtual audio driver so other apps see OpenMix as a speaker and a
microphone.

## Done in 0.1.0

- Strips and buses added/removed at runtime (up to 8 and 6); per-app
  routing; loopback capture; multi-device I/O with drift correction.
- Built-in processing: RNNoise denoise, gate, 3-band EQ, compressor,
  de-esser, ducking; HPF/LPF.
- VST3 chains on strips and buses; crash-safe scanning and crash-safe
  loading; VST2 opt-in only; latency compensation; per-strip presets.
- RMS meters, loudness normalisation, spectrum and LUFS per bus.
- Recorder, replay buffer, soundboard, scenes, global hotkeys.
- OSC, phone / WebSocket, MIDI remote, Stream Deck via OSC/WebSocket.
- Light/dark themes, compact mode, Hardware / Virtual bus groups, custom
  bus names, status bar with engine CPU%.
- Installer (Inno Setup, built by CI for tags), opt-in update check,
  AGPLv3 licensing.

## Still open

- **Built-in pitch correction and voice changer** (formant shift, robot,
  radio): Phase 4, open. DeepFilterNet as a higher-quality denoise
  option: also open.
- **Full out-of-process plugin hosting**: the probe only catches
  load-time crashes; a plugin crashing while processing audio can still
  take OpenMix down. Phase 5 stretch goal.
- **Surround / binaural positioning, pad automation**: Phase 6, open.
- **Own virtual audio driver** (Phase 7, kernel work): **one** installed
  driver exposing multiple virtual endpoints (`B1–B3` virtual mics for
  Discord/OBS/games, plus optional virtual playback inputs) and an
  optional user-mode virtual ASIO COM DLL for DAWs. Planned around
  Microsoft's modern **ACX** (KMDF) audio framework (with SysVAD as a
  WaveRT reference), a fixed/explicitly managed endpoint pool to avoid
  PnP glitches during calls, a non-blocking shared-memory ring buffer +
  `ClockBridge` path, VM + Driver Verifier testing, and EV
  (hardware/Cloud HSM) + Microsoft Partner Center attestation signing.
  See the full guide at
  [`docs/VIRTUAL_DRIVER.md`](https://github.com/itzcaesar/OpenMix/blob/main/docs/VIRTUAL_DRIVER.md).
- **Release checklist (Phase 9):** troubleshooting guide and beta testing
  on different hardware (Realtek, USB headsets, Focusrite/Behringer ASIO).
  The troubleshooting guide is now partly covered by this site's
  [Troubleshooting](troubleshooting.md) page.

## Architecture target (after Phase 7)

```
 Hardware inputs (ASIO / WASAPI) ─┐
 Virtual inputs (OpenMix driver) ─┼─> Strips: gain -> built-in dyn/EQ -> VST3 chain -> XY pad -> fader
 Loopback capture ────────────────┘                                   │
                                                                      ▼
                                              Send matrix -> Buses (EQ, comp, VST3, limiter, fader)
                                                                      │
                     ┌──────────────────┬─────────────────────────────┼─────────────────┐
                     ▼                  ▼                             ▼                 ▼
              A1 (ASIO master)   A2 (WASAPI + resampler)   B1/B2 (OpenMix driver)   Recorder
```
