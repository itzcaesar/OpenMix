# OpenMix Roadmap

Goal: a VoiceMeeter-class virtual mixer for Windows. It needs low-latency ASIO/WASAPI I/O,
VST3 plugin hosting per strip, an XY voice/pan pad, several physical and virtual devices
at once, and eventually its own virtual audio driver so other apps see OpenMix as a
speaker and a microphone.

Each phase ends in a usable app. Do them in order. Phase 7 (the driver) is the only one
that needs kernel work, and everything before it works with a third-party virtual cable.

## Beyond VoiceMeeter

What OpenMix should do that VoiceMeeter doesn't, the phase each item lands in, and where it
stands:

| Feature | Phase | Status |
|---|---|---|
| Strips and buses added and removed at runtime (up to 8 and 6) instead of fixed layouts | 3 | Done |
| VST3 chains on every strip **and** every bus, drag to reorder, wet/dry, per-slot bypass | 1, 5 | Done |
| Crash-safe plugin scanning: one bad plugin doesn't kill your audio | 5 | Done (loading probe done; full audio sandbox open) |
| Built-in neural-network denoise, no plugin needed | 4 | RNNoise done, DeepFilterNet open |
| Built-in pitch correction and voice changer (formant shift, robot, radio) | 4 | Open |
| **Per-app routing**: Spotify, a game or Discord on its own strip without changing the Windows default device | 3 | Done |
| Capture what any output device plays, no virtual cable | 3 | Done |
| Auto-ducking (music dips when you talk), with any strip as the sidechain | 4 | Done |
| Soundboard with hotkeys | 4 | Done (global hotkeys) |
| Multitrack recording plus a **replay buffer** ("save the last 60 s") | 4 | Done |
| Scenes: snapshot the whole mixer and switch with a hotkey | 6 | Done |
| Per-send levels and pre-fader sends instead of on/off bus buttons | 4 | Done |
| Spectrum analyser and LUFS loudness per bus | 4 | Done (RMS meters and loudness normalization done) |
| Latency compensation for plugin chains | 5 | Done |
| OSC remote control | 8 | Done |
| Phone/browser remote, WebSocket/MIDI API, Stream Deck | 8 | Done (Stream Deck via OSC/WebSocket plugins) |
| Resizable UI with light and dark themes, channel colours and names | 8 | Done |

---

## Phase 0 - Scaffold (done)

- JUCE 9.0.5 + CMake + MSVC project, bundled ASIO
- 3 mono input strips: gain -> 3 VST3 slots -> XY pad -> fader/mute -> A1/A2/B1 sends
- 3 buses mapped to output pairs of one device, with fader, mute and meters
- Pad modes: Voice (tilt EQ, presence, echo) and Pan (constant-power pan, width)
- Session save/restore (device, strips, plugins + state, buses)

## Phase 1 - Build and stabilize the MVP  *(~1 week)*

- [x] Build Debug + Release, fix compiler warnings (CI builds Release on every push)
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

- [x] **Master device** (usually ASIO or the A1 device) drives the mix clock
- [x] **Secondary devices**: one `AudioIODevice` each (WASAPI shared), joined to the engine
      through lock-free ring buffers (`ClockBridge`)
- [x] **Drift correction**: the bridge measures how full each FIFO is and adjusts its
      resampler ratio by a few ppm (tested at +/-200 ppm with jitter in the core tests)
- [x] Sample-rate conversion when a device runs at 44.1k and the master at 48k
      (16-tap windowed sinc)
- [x] Per-bus device picker: an output pair of the main device or any Windows output
- [x] Inputs from secondary devices too, e.g. a USB mic alongside an ASIO interface
- [x] Device unplug and replug while running: the device goes silent, then reconnects

**Done when:** mic (USB) + interface (ASIO) -> A1 headphones, A2 speakers and B1
VB-Cable at the same time, for hours, with no drift clicks.

## Phase 3 - Virtual strips and app audio  *(~1 week)*

- [x] **Virtual input strips** (stereo) that read desktop or app audio: a strip can take a
      virtual cable's output as a Windows input device, or use device loopback (below)
- [x] Stereo hardware strips (L/R input pair), selectable per strip
- [x] Configurable strip count (add or remove strips at runtime)
- [x] WASAPI loopback capture as a strip source (what a device plays, without a cable)
- [x] **Per-app routing**: list running audio apps and assign each to a strip. Either use
      Windows' per-app output device setting (the same mechanism EarTrumpet uses) to send
      the app to an OpenMix virtual device, or use process loopback capture
      (`AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK`, Windows 10 2004+)

**Done when:** music, game and Discord audio each sit on their own strip with their own
fader, and the mic + effects go to B1 as Discord's input.

## Phase 4 - Built-in mixing tools  *(~2 weeks)*

Built-in so the basics work without third-party plugins:

- [x] Gate, compressor, 3-band EQ and de-esser per strip; safety limiter per bus
- [x] Bus EQ and compressor, VST3 inserts on every bus; safety limiter per bus
- [x] Solo, mono, polarity invert, per-send levels (send matrix instead of on/off)
- [x] Ducking: lower music when the mic talks (sidechain from one strip to another)
- [x] Recorder: every bus and/or strip to WAV (`juce::AudioFormatWriter::ThreadedWriter`)
- [x] Meter improvements: peak hold with a held-peak readout, latching clip lamp, LUFS on buses
- [x] RMS alongside peak on the meters (solid bar inside the peak ladder, on strips, buses and the soundboard)
- [x] Loudness normalisation per bus: slow gain towards -14 / -16 / -23 LUFS, clamped to +/-12 dB, saved with the session and scenes
- [x] Built-in neural-network denoise: RNNoise (light, 10 ms frames, any sample rate)
- [ ] DeepFilterNet as a "high quality" option (more CPU, adds latency)
- [ ] Built-in pitch correction (YIN/McLeod pitch detection + PSOLA shift, key/scale, speed)
      and voice changer presets (formant shift, robot, radio, megaphone)
- [x] Replay buffer: keep the last 30-120 s of one bus in RAM, save on Ctrl+Shift+S
- [x] Soundboard: 8 pads (WAV/MP3/FLAC/Ogg), keys 1-8, own volume and bus sends
- [x] Spectrum analyzer per bus (FFT on a UI-side FIFO copy, never on the audio thread)
- [x] Per-strip HPF/LPF (20 Hz-1 kHz / 1-20 kHz) with on/off, before the EQ; saved in sessions and strip presets
- [x] Built-in effects as insert slots: Reverb, tempo-synced Delay and Chorus, with settings panels (zero latency, saved in sessions and strip presets)
- [x] FX sends per strip (post-fader Reverb/Delay/Chorus) into shared global effects
- [x] Tap tempo (40-240 BPM) driving tempo-synced delays, with a Tap button and BPM readout in the status bar
- [x] Per-strip presets: save/recall a strip's sound under a name (missing plugins skipped on recall)
- [x] Solo-safe per strip; per-channel latency readout in the fader bay

## Phase 5 - Plugin hosting hardening  *(~2 weeks)*

- [x] **Out-of-process plugin scanning** (a child process scans each plugin; a crash only
      marks that plugin as bad) plus a `KnownPluginList` cache and a blacklist
- [x] Load plugins asynchronously with a "loading..." state on the slot
- [x] Per-slot bypass, drag to reorder slots, 6 slots per strip, wet/dry mix per slot
- [x] **Latency compensation**: read `getLatencySamples()` and delay the other strips so
      sends that reach the same bus stay aligned
- [x] Plugin presets: save and load per slot, A/B compare
- [x] Optional VST2 hosting, opt-in only (`-DOPENMIX_ENABLE_VST2=ON -DVST2_SDK_DIR=...`): JUCE_PLUGINHOST_VST on, PluginScanner scans VST + VST3, same slots/presets/latency handling. OFF by default and off in CI; the SDK is never in the repo. Only usable if you already own the old SDK / licence — Steinberg stopped issuing new VST2 licences in Oct 2018, so this cannot be shipped in a release build for new developers.
- [x] **Crash-safe plugin loading**: before a plugin is loaded into the mixer, it is started
      in a child process (`OpenMix.exe --probe-plugin <file>`, create + prepare + one silent
      block). A plugin that crashes or hangs while starting is blacklisted instead of taking
      the mixer down. On by default (Settings), and a failed probe is retried from
      "Retry skipped plugins".
- [ ] Stretch: sandboxed plugin process so a crashing plugin can't take down the mixer
      *while processing audio*. The probe above only catches crashes at load time; full
      out-of-process audio hosting (shared-memory audio, parameter and GUI forwarding) is
      still open and is the real sandbox.

## Phase 6 - XY pad extensions  *(~1-2 weeks)*

- [x] **Color mode**: X/Y blend between 4 corner EQ presets (Warm, Bright, Radio, Close)
- [x] **Space mode** (stereo): pan + distance with early reflections
- [ ] Surround 5.1/7.1 positioning, binaural (HRTF convolution with `juce::dsp::Convolution`)
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
- [x] OSC remote control (faders, mutes, sends, soundboard, record, replay)
- [x] Global hotkeys: mute, push to talk, soundboard pads, recording, replay, scenes
- [x] Scenes: save and recall the mix (levels, mutes, sends, processing), OSC recall
- [x] MIDI controller mapping (learnable CC/note to fader, mute, soundboard, record), WebSocket API + phone remote (built-in page at `http://<PC>:8080/`, `GET /api/state`, `POST /api/command`, `ws://.../ws`), Stream Deck via its OSC plugin or a WebSocket plugin against the same paths
- [x] Dark/light themes, resizable UI (laptop to desktop), channel names and colours, settings window
- [x] Accent colour (amber default, plus sky, green, violet, cyan) and a compact mode with narrower strips and buses
- [x] Desk UI: labelled sections on strips (Input / Filter / Processing / Preset / Inserts / Voice pad / Sends) and buses (Output / Limiter / Loudness / Processing / Inserts / Analyser); gain slider under the source name, Ø/MONO keys, solid-colour name bar at the bottom of each strip
- [x] Bus groups: Hardware (A1-A3) and Virtual (B1-B3) with group labels and a divider; custom bus names (group stays slot-based); "+ Bus" asks for the group, then an optional name
- [x] Limiter gain-reduction meter per bus; monitor bus shown only by a 1 px accent outline and a lit name bar
- [x] Bottom status bar: device, sample rate, buffer size, engine CPU%, LIVE indicator, tap tempo
- [x] Crash handler + log file; "safe mode" start that skips plugins
- [x] Installer (Inno Setup, built by CI for tags) (driver and code signing still open)
- [x] Update check (opt-in, GitHub releases)

## Phase 9 - Release

- [x] **Licensing decision:** OpenMix is released under AGPLv3, using JUCE under its AGPLv3
      option (a closed-source release would need a JUCE licence)
- [x] Docs: README with a Discord/OBS quick start
- [ ] Troubleshooting guide (buffer sizes, ASIO tips, common device problems)
- [ ] Beta testing on different hardware (Realtek, USB headsets, Focusrite/Behringer ASIO)

---

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

## Rules that apply to every phase

- **Audio thread:** no allocation, no locks that can block (try-lock only), no file I/O, no logging
- Every parameter the UI changes is an atomic and is smoothed on the audio thread
- Test each change with the smallest buffer you use day to day (64-128 samples on ASIO)
- Keep a "known good" session to check that nothing changed (load it, compare the sound)
