# OpenMix Roadmap

Goal: a VoiceMeeter-class virtual mixer for Windows. It needs low-latency ASIO/WASAPI I/O,
VST3 plugin hosting per strip, an XY voice/pan pad, several physical and virtual devices
at once, and eventually its own virtual audio driver so other apps see OpenMix as a
speaker and a microphone.

Each phase ends in a usable app. Do them in order. Phase 7 (the driver) is the only one
that needs kernel work, and everything before it works with a third-party virtual cable.

## Beyond VoiceMeeter

What OpenMix should do that VoiceMeeter doesn't, and the phase each item lands in:

| Feature | Phase |
|---|---|
| Unlimited strips and buses (add/remove at runtime) instead of fixed 3/5/8 layouts | 3 |
| VST3 chains on every strip **and** every bus, drag to reorder, wet/dry, per-slot bypass | 1, 5 |
| Crash-safe plugin scanning and sandboxing: one bad plugin doesn't kill your audio | 5 |
| Built-in neural-network denoise (RNNoise / DeepFilterNet), no plugin needed | 4 |
| Built-in pitch correction and voice changer (formant shift, robot, radio) | 4 |
| **Per-app routing**: send Spotify, a game or Discord to any strip without changing the Windows default device | 3 |
| Auto-ducking (music dips when you talk), with any strip as the sidechain | 4 |
| Soundboard / sampler strip with hotkeys | 4 |
| Multitrack recording plus a **replay buffer** ("save the last 60 s") | 4 |
| Scenes: snapshot the whole mixer and switch with a hotkey | 6 |
| Send matrix with per-send levels instead of on/off bus buttons | 4 |
| Spectrum analyzer, LUFS loudness meters, loudness normalization per bus | 4 |
| Latency compensation for plugin chains | 5 |
| Phone/browser remote, WebSocket/OSC/MIDI API, Stream Deck | 8 |
| Modern resizable UI with themes, strip colors and custom names | 8 |

---

## Phase 0 - Scaffold (done)

- JUCE 9.0.5 + CMake + MSVC project, bundled ASIO
- 3 mono input strips: gain -> 3 VST3 slots -> XY pad -> fader/mute -> A1/A2/B1 sends
- 3 buses mapped to output pairs of one device, with fader, mute and meters
- Pad modes: Voice (tilt EQ, presence, echo) and Pan (constant-power pan, width)
- Session save/restore (device, strips, plugins + state, buses)

## Phase 1 - Build and stabilize the MVP  *(~1 week)*

- [ ] Build Debug + Release, fix compiler warnings
- [ ] Test on WASAPI (built-in mic + headphones) and ASIO (interface driver, or ASIO4ALL)
- [ ] Test loading, removing and replacing plugins while audio runs. Try a denoiser
      (RNNoise-based or DeepFilterNet) and a pitch corrector (Graillon / MAutoPitch)
- [ ] Changing sample rate or buffer size while running must not crash or glitch for long
- [x] Autosave every 10 s (only when something changed) and on exit
- [x] Core unit tests (plain C++, run in CI): filters, clock bridge, limiter, delay line

**Done when:** a mic runs through denoiser -> autotune -> pad -> A1 headphones for an hour
with no dropouts, and comes back exactly the same after a restart.

## Phase 2 - Multi-device I/O  *(~2-3 weeks, the hardest user-mode part)*

VoiceMeeter's key feature is that every bus can go to a different device: A1 to
headphones, A2 to speakers, B1 to a virtual cable for Discord.

- [ ] **Master device** (usually ASIO or the A1 device) drives the mix clock
- [ ] **Secondary devices**: one `AudioIODevice` each (WASAPI shared/exclusive), joined to the
      engine through lock-free ring buffers (`juce::AbstractFifo`)
- [ ] **Drift correction**: device clocks differ slightly (for example 48000 vs 48003 Hz).
      Measure how full each FIFO is and adjust a resampler ratio per device by a few ppm
      (`juce::LagrangeInterpolator`, or libsamplerate / r8brain for higher quality)
- [ ] Sample-rate conversion when a device runs at 44.1k and the master at 48k
- [ ] Per-bus device picker in the UI instead of an "output pair"
- [ ] Inputs from secondary devices too, e.g. a USB mic alongside an ASIO interface
- [ ] Device unplug and replug while running: the device goes silent, then reconnects

**Done when:** mic (USB) + interface (ASIO) -> A1 headphones, A2 speakers and B1
VB-Cable at the same time, for hours, with no drift clicks.

## Phase 3 - Virtual strips and app audio  *(~1 week)*

- [ ] **Virtual input strips** (stereo) that read desktop or app audio. Set Windows' default
      output to a virtual cable, and OpenMix captures that cable's other end
- [x] Stereo hardware strips (L/R input pair), selectable per strip
- [x] Configurable strip count (add or remove strips at runtime)
- [ ] WASAPI loopback capture as an option (records what a device plays without a cable)
- [x] **Per-app routing**: list running audio apps and assign each to a strip. Either use
      Windows' per-app output device setting (the same mechanism EarTrumpet uses) to send
      the app to an OpenMix virtual device, or use process loopback capture
      (`AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK`, Windows 10 2004+)

**Done when:** music, game and Discord audio each sit on their own strip with their own
fader, and the mic + effects go to B1 as Discord's input.

## Phase 4 - Built-in mixing tools  *(~2 weeks)*

Built-in so the basics work without third-party plugins:

- [x] Gate, compressor, 3-band EQ and de-esser per strip; safety limiter per bus
- [ ] Bus EQ (graphic / parametric) and a bus limiter to prevent clipping
- [x] Solo, mono, polarity invert, per-send levels (send matrix instead of on/off)
- [x] Ducking: lower music when the mic talks (sidechain from one strip to another)
- [ ] Recorder: record any bus to WAV/FLAC (`juce::AudioFormatWriter::ThreadedWriter`)
- [ ] Meter improvements: RMS + peak, peak hold, clip indicators, LUFS on buses
- [x] Built-in neural-network denoise: RNNoise (light, 48 kHz, 10 ms frames) with DeepFilterNet as a
      "high quality" option (more CPU, adds latency)
- [ ] Built-in pitch correction (YIN/McLeod pitch detection + PSOLA shift, key/scale, speed)
      and voice changer presets (formant shift, robot, radio, megaphone)
- [ ] Replay buffer: keep the last N seconds of chosen buses in RAM, save on hotkey
- [ ] Soundboard strip: pads that play WAV/MP3 files, with hotkeys, routable like any strip
- [ ] Spectrum analyzer per strip/bus (FFT on a UI-side FIFO copy, never on the audio thread)

## Phase 5 - Plugin hosting hardening  *(~2 weeks)*

- [x] **Out-of-process plugin scanning** (a child process scans each plugin; a crash only
      marks that plugin as bad) plus a `KnownPluginList` cache and a blacklist
- [x] Load plugins asynchronously with a "loading..." state on the slot
- [x] Per-slot bypass, drag to reorder slots, 6 slots per strip (wet/dry mix still open)
- [x] **Latency compensation**: read `getLatencySamples()` and delay the other strips so
      sends that reach the same bus stay aligned
- [x] Plugin presets: save and load per slot (A/B compare still open)
- [ ] Optional VST2 hosting (only if you own the old SDK; not available to new developers)
- [ ] Stretch: sandboxed plugin process so a crashing plugin can't take down the mixer

## Phase 6 - XY pad extensions  *(~1-2 weeks)*

- [ ] **Color mode**: X/Y blend between 4 corner presets (EQ + compression character)
- [ ] **Spatial mode**: 5.1/7.1 positioning, plus binaural (HRTF convolution with
      `juce::dsp::Convolution`) for headphones
- [ ] MIDI/OSC/hotkey control of pad position; snapshots (store and recall positions)
- [ ] Pad automation: smooth glide between snapshots

## Phase 7 - Own virtual audio driver  *(~1-2 months, kernel work)*

This removes the need for VB-Cable. Two parts:

### 7a. Virtual WDM devices (what Discord, OBS and games see)
- [ ] Start from Microsoft's **SysVAD** sample (Windows Driver Kit). Strip it down to a
      virtual render endpoint ("OpenMix Input") and a capture endpoint ("OpenMix Output")
- [ ] Shared-memory ring buffer between the driver and the OpenMix app (driver <-> user mode),
      with OpenMix's clock as the master
- [ ] Multiple endpoints (e.g. 3 virtual inputs, 2 virtual outputs)
- [ ] Develop in a **VM** with kernel debugging (WinDbg): a driver bug means a BSOD
- [ ] Test-signing mode (`bcdedit /set testsigning on`) during development
- [ ] **Release signing**: EV code-signing certificate -> Microsoft Partner Center
      (hardware dev program) -> attestation signing. Budget: a few hundred USD/year

### 7b. Virtual ASIO driver (optional; lets a DAW connect to OpenMix)
- [ ] A user-mode COM DLL implementing `IASIO`, registered under `HKLM\SOFTWARE\ASIO`
- [ ] Exchanges audio with OpenMix through shared memory, in step with the master clock

## Phase 8 - Product polish  *(~2 weeks)*

- [x] System tray icon, start with Windows, start minimized
- [ ] Global hotkeys (mute mic, push-to-talk), MIDI controller mapping, OSC / WebSocket API
      (Stream Deck plugins, a phone remote)
- [x] Dark/light themes, resizable UI (laptop to desktop), strip renaming (settings window still open)
- [ ] Crash handler + log file; "safe mode" start that skips plugins
- [x] Installer (Inno Setup, built by CI for tags) (driver and code signing still open)
- [ ] Auto-update check

## Phase 9 - Release

- [ ] **Licensing decision:** JUCE is AGPLv3 or commercial. Releasing OpenMix publicly
      under AGPLv3 (open source) is free; a closed-source release needs a JUCE licence.
      Check juce.com for the current tiers
- [ ] Docs: setup guide for Discord/OBS/games, troubleshooting (buffer sizes, ASIO tips)
- [ ] Beta testing on different hardware (Realtek, USB headsets, Focusrite/Behringer ASIO)

---

## Architecture target (after Phase 7)

```
 Hardware inputs (ASIO / WASAPI) ─┐
 Virtual inputs (OpenMix driver) ─┼─> Strips: gain -> VST3 chain -> built-in dyn/EQ -> XY pad -> fader
 Loopback capture ────────────────┘                                   │
                                                                      ▼
                                              Send matrix -> Buses (EQ, limiter, fader)
                                                                      │
                     ┌──────────────────┬─────────────────────────────┼─────────────────┐
                     ▼                  ▼                             ▼                 ▼
              A1 (ASIO master)   A2 (WASAPI + resampler)   B1/B2 (OpenMix driver)   Recorder
```

## Rules that apply to every phase

- **Audio thread:** no allocation, no locks that can block (try-lock only), no file I/O, no logging
- Every parameter the UI changes is an atomic and is smoothed on the audio thread
- Test each change with the smallest buffer you use day to day (64-128 samples on ASIO)
- Keep a "known good" session to check that nothing changed (load it, compare the sound)
