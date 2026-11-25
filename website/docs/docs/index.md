# OpenMix

A virtual audio mixer for Windows. Bring in mics, other devices, single apps
(Spotify, a game, Discord) or everything an output device plays, process them,
and send separate mixes to your headphones, speakers, stream and calls.

This is the documentation for **OpenMix 0.1.0, the first public release**.
There was no earlier release. OpenMix is built with C++20 and JUCE 9.0.5 and
is licensed under the GNU AGPLv3.

## At a glance

- **Inputs:** up to 8 strips. Each takes a channel or stereo pair of the main
  device (ASIO, WASAPI or DirectSound), any other Windows input, one app's
  audio, or a loopback of a Windows output device.
- **Voice processing built in:** AI noise removal (RNNoise), gate, 3-band EQ,
  compressor, de-esser and ducking on every strip, plus a VoicePad (XY pad
  with voice, pan, colour and space modes). HPF/LPF per strip, per-strip
  presets, solo-safe, and latency compensation.
- **VST3 plugins** on every strip and bus: 6 slots each, crash-safe scanning,
  bypass without clicks, wet/dry, A/B compare, presets, latency compensation,
  crash-safe loading.
- **Outputs:** up to 6 buses (A1–A3 hardware, B1–B3 virtual), each to an
  output pair of the main device or to any Windows output device, with EQ,
  compressor, plugins and a safety limiter. Buses are grouped
  **Hardware / Virtual** on the desk, and each bus can have a custom name
  (its group never changes when you rename it).
- **Status bar** along the bottom: LIVE, the engine's own CPU%, plugin scanning and recording progress, and a lamp for each remote you switched on (OSC, phone, MIDI), lit while it is listening.
- **Creator tools:** multitrack recorder, replay buffer, soundboard, spectrum
  and LUFS loudness on every bus.
- **Scenes and global hotkeys:** save whole mixes and switch between them;
  mute, push to talk, soundboard, recording and scenes on hotkeys that work
  while a game has focus.
- **Control:** keyboard throughout, OSC remote control, phone / browser
  remote, MIDI, Stream Deck, tray icon, start with Windows.

## Download

Every push to `main` is built by GitHub Actions: open the latest run under
**Actions** in the repository and download `OpenMix-windows-x64`.

Version tags publish a GitHub release with the zip and an installer,
`OpenMix-<version>-setup.exe`. The first release is **v0.1.0**, so look for
`OpenMix-0.1.0-setup.exe` on the
[releases page](https://github.com/itzcaesar/OpenMix/releases).

## Quick start: mic to Discord, music to your headphones

1. Click the **status readout** in the header (it shows the device) and
   choose your audio device. ASIO gives the lowest latency; Windows Audio
   works with anything.
2. On **Input 1**, open the source picker and choose your mic. Switch on
   **NR** to remove background noise.
3. On **Input 2**, choose **Application audio** and pick Spotify (or a game).
4. Bus **A1** is what you hear: point its output at your headphones.
5. Bus **B1** is for apps that need a microphone: point it at a virtual
   cable (for example VB-Audio Virtual Cable's "CABLE Input"), then choose
   the cable's other end ("CABLE Output") as the microphone in Discord
   or OBS.
6. On each strip, the send keys in the SENDS section decide where it goes:
   mic on A1 and B1, music on A1 only (or on B1 too, if the call should hear
   it). Right-click a send key to set its level or make it pre-fader.

Everything is saved automatically.

## Where to go next

- [Getting started](getting-started.md): install, build it yourself,
  and the first mix.
- [Strips & voice processing](strips.md): sources, filters, processing,
  VoicePad, presets, sends.
- [Buses & outputs](buses.md): Hardware / Virtual buses, limiter,
  loudness, analyser.
- [Plugins](plugins.md): VST3, crash-safe loading, VST2 (opt-in).
- [Remote control](remote.md): OSC, phone / WebSocket, MIDI, Stream Deck.
- [Scenes, hotkeys & creator tools](creator.md): scenes, global hotkeys,
  recording, replay, soundboard.
- [Troubleshooting & known limits](troubleshooting.md): read this before
  using OpenMix live.
