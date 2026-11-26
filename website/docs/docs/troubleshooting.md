# Troubleshooting & known limits

## Known limits (v0.1.0)

Read these before using OpenMix live. They are honest limits, not bugs
to work around silently.

- **No virtual audio driver of its own yet**, so apps that need a
  microphone input from OpenMix need a virtual cable (VB-Audio Virtual
  Cable or similar) on a B bus. A native driver is Phase 7 in the
  [roadmap](roadmap.md).
- **A plugin that crashes while processing audio (after it loaded fine)
  can still take OpenMix down.** The loading probe only catches crashes
  at start-up; full out-of-process plugin hosting is not done yet.
- **The phone / WebSocket remote has no password:** anyone on the same
  network can control the mixer while it is on. Only enable it on a
  network you trust. (Browser requests from other websites are refused.)
- VST3 plugins have to start on the UI thread (a VST3 rule), so a slow
  plugin can pause the window briefly while it opens; its slot shows
  "Loading..." meanwhile.
- Global hotkeys use a keyboard hook that only listens. A game with
  strict anti-cheat could in principle object to any keyboard hook;
  Discord and OBS use the same mechanism. The hook runs on its own
  thread, so a busy OpenMix never slows typing in other apps.
- "App's own output" uses the same Windows setting as Settings > Sound >
  Volume mixer. The interface behind it is undocumented, so on an
  unusual Windows build it can report that it's unavailable.

## Common problems

### Crackling when input and output are different devices

A warning banner appears if the main device's input and output are two
different Windows devices, because their clocks drift and that crackles.
The banner explains the fix: use one device for both, or let OpenMix
resample the extra device (which adds about 10–25 ms on that device
only).

### A plugin was skipped

Plugins are scanned in the background, each in its own process, so a
plugin that crashes while being scanned is skipped instead of taking
OpenMix down. A plugin that only takes too long to scan, or is still
being scanned when you quit, is not skipped: it is scanned again next
time. Try again from Settings > Retry skipped plugins. The
crash-safe loading probe also skips a plugin that crashes while starting;
retry it the same way. A plugin that is only slow to start is not
skipped: OpenMix asks whether to load it anyway.

### A plugin outputs bad audio

A plugin that outputs invalid audio (NaN/Inf) is stopped automatically
and marked, instead of breaking the mix. "Try again" in its menu
restarts it.

### OpenMix crashed while a session was loading

The next start skips plugins and keeps a copy of the session in
`session-before-crash.xml`. Check the log and crash report in
`%APPDATA%\OpenMix` (Settings > Open log folder).

### An app's audio strip shows "(not running)"

The app quit. The strip reconnects when the app starts again.

### A device was unplugged

Extra devices, app captures and loopbacks come back after unplugging,
stalls or Windows audio changes (checked every 2 seconds). If a device
does not come back, re-pick it in the strip or bus source/output menu.

## Reliability

- **Audio thread:** no allocation, no blocking locks, no I/O. Every UI
  change is an atomic, smoothed per sample. Denormals are flushed.
- **Safety limiter** on every bus protects ears, speakers and apps fed by
  virtual buses.
- **NaN/Inf guard** on every plugin, and a last guard on every strip.
- **DC blocker** (5 Hz) on every input; click-free plugin bypass and
  enable.
- **Latency compensation:** every strip is delayed to match the slowest
  one (plugins and noise removal), and a bypassed plugin keeps its
  latency, so strips that meet on a bus stay in time.
- **Reconnects by itself:** extra devices, app captures and loopbacks
  come back after unplugging, stalls or Windows audio changes.
- **Safe start:** if a plugin crashes OpenMix while a session loads, the
  next start skips plugins.
- **Core tests** (`ctest`, no JUCE needed): clock drift and jitter,
  resampling pitch, limiter, NaN detection, DC blocker, filter
  stability, dynamics, denoiser latency at 44.1 and 48 kHz, the EBU
  loudness reference, RMS metering and loudness normalisation. App tests
  cover session loading (including old files), scenes, presets, plugin
  slot moves, the remote and MIDI commands, the soundboard and the hotkey
  thread. Both run in CI on every push.
