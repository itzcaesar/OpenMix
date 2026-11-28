# OpenMix

A free, open-source audio mixer for Windows. Put your mic, other devices, single apps
(Spotify, a game, Discord) or everything an output device plays on their own strips, clean
them up, and send a separate mix to your headphones, the stream and the call.

![The OpenMix desk: three input strips (Mic, Music, Room mic) and three buses (Phones, Stream, Call).](website/docs/assets/openmix-desk.png)

**[Download](https://github.com/itzcaesar/OpenMix/releases/latest)** ·
**[User guide](https://itzcaesar.github.io/OpenMix/docs/)** ·
**[Changelog](CHANGELOG.md)** ·
**[Roadmap](ROADMAP.md)**

## Features

- **Inputs:** up to 8 strips. Each takes a channel of your audio interface (ASIO, WASAPI or
  DirectSound), any other Windows input, one app's audio, everything an output device plays,
  or the built-in media player.
- **Voice processing on every strip:** RNNoise and DeepFilterNet 3 noise removal, gate,
  filters, 8-band EQ, compressor, de-esser, ducking, and a voice pad.
- **VST3 plugins** on every strip and bus, with crash-safe scanning and loading.
- **Outputs:** up to 8 buses (A1-A4 hardware, B1-B4 virtual) to any output, each with EQ,
  compressor, plugins and a safety limiter. A routing matrix shows every send in one grid.
- **Creator tools:** multitrack recorder, replay buffer, soundboard, media player (audio and
  video), spectrum and LUFS loudness on every bus.
- **Scenes and global hotkeys** that work while a game has focus.
- **Remote control:** OSC, a phone/browser remote, MIDI controllers and Stream Deck.

## Install

Download `OpenMix-<version>-setup.exe` (or the portable zip) from
[Releases](https://github.com/itzcaesar/OpenMix/releases/latest). Needs Windows 10 2004 or
later, 64-bit.

The builds aren't code-signed yet, so SmartScreen may say "Windows protected your PC":
click **More info**, then **Run anyway**.

## Quick start: mic to Discord, music to your headphones

1. **Setup > Audio device:** pick your audio device (ASIO for the lowest latency).
2. **Input 1:** choose your mic as the source and switch on **NR** to remove background noise.
3. **Input 2:** choose **Application audio** and pick Spotify or a game.
4. **A1** is what you hear: set its output to your headphones.
5. **B1** is for apps that need a microphone: set its output to a virtual cable such as
   [VB-Cable](https://vb-audio.com/Cable/) ("CABLE Input"), then pick "CABLE Output" as the
   mic in Discord or OBS.
6. Use each strip's **SENDS** keys to choose where it goes: mic on A1 and B1, music on A1.

Everything saves automatically. The [user guide](https://itzcaesar.github.io/OpenMix/docs/getting-started/)
walks through the rest.

## User guide

| Page | What's in it |
|---|---|
| [Getting started](https://itzcaesar.github.io/OpenMix/docs/getting-started/) | First run, the window, keyboard shortcuts, where files are saved |
| [Strips & voice processing](https://itzcaesar.github.io/OpenMix/docs/strips/) | Sources, noise removal, EQ, dynamics, voice pad, copy/paste and multi-select |
| [Buses & outputs](https://itzcaesar.github.io/OpenMix/docs/buses/) | Hardware and virtual buses, virtual cables, routing, loudness |
| [Plugins](https://itzcaesar.github.io/OpenMix/docs/plugins/) | VST3 hosting, presets, crash safety, VST2 (local builds) |
| [Remote control](https://itzcaesar.github.io/OpenMix/docs/remote/) | OSC, phone remote, WebSocket API, MIDI, Stream Deck |
| [Scenes, hotkeys & creator tools](https://itzcaesar.github.io/OpenMix/docs/creator/) | Scenes, global hotkeys, recorder, replay, soundboard, media player |
| [Troubleshooting](https://itzcaesar.github.io/OpenMix/docs/troubleshooting/) | Known limits, common problems, reliability |

## Build from source

Needs Visual Studio 2026 with **Desktop development with C++** (includes CMake) and the
stable Rust toolchain (for DeepFilterNet).

```powershell
git clone --recursive https://github.com/itzcaesar/OpenMix.git
cd OpenMix
cmake --preset vs2026
cmake --build --preset release
ctest --test-dir build -C Release
.\build\OpenMix_artefacts\Release\OpenMix.exe
```

Or in Visual Studio: **File > Open > Folder**, pick the `vs2026` preset, set the startup item
to `OpenMix.exe` and press F5.

| Folder | What's in it |
|---|---|
| `Source/app/` | Window, menus, session saving, settings, hotkeys, OSC/MIDI/phone remote |
| `Source/engine/` | Mixer engine, strips, plugin hosting and sandbox, device and app capture |
| `Source/creator/` | Recorder, replay buffer, soundboard, media player |
| `Source/dsp/` | Real-time DSP: filters, dynamics, limiter, loudness, noise removal |
| `Source/ui/` | Theme, widgets, strip and bus views, panels |
| `tests/` | `CoreTests` (DSP, no JUCE) and `AppTests` (engine, sessions, remote), run by `ctest` |
| `installer/` | Inno Setup installer and the signing script |
| `docs/`, `website/` | Design notes, [code signing](docs/SIGNING.md), the [virtual driver plan](docs/VIRTUAL_DRIVER_PLAN.md), and the user guide source |

**Releases:** push a version tag (`git tag v0.2.0`, `git push origin v0.2.0`). CI builds and
tests the app and attaches the zip and installer to a GitHub release.

## License

GNU AGPLv3 (see `LICENSE`). Built on JUCE (AGPLv3 option), with RNNoise v0.1.1
(BSD-3-Clause, `Source/third_party/rnnoise/COPYING`) and DeepFilterNet `libDF` v0.5.6 (MIT or
Apache-2.0, `external/DeepFilterNet/`). DeepFilterNet model weights are downloaded
separately and are not included in releases.
