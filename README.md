# OpenMix

A virtual mixer for Windows: ASIO / WASAPI / DirectSound I/O, VST3 plugin hosting on
every input strip, an IntelliPad-style XY voice/pan pad, and A1/A2/B1 bus routing.

Built with C++20 + JUCE 9.0.5 (bundled in `external/JUCE`, includes JUCE's ASIO headers).

## Build

Requirements: Visual Studio 2026 with the **Desktop development with C++** workload
(it includes CMake).

**In Visual Studio:** File > Open > Folder... > `C:\vmix`, pick the `vs2026` preset,
set the startup item to `OpenMix.exe`, press F5.

**From a "Developer PowerShell for VS 2026":**

```powershell
cmake --preset vs2026
cmake --build --preset release
.\build\OpenMix_artefacts\Release\OpenMix.exe
```

## Using it

1. **Audio Settings** - pick the driver type (ASIO for lowest latency, or Windows Audio),
   then the device, the active input/output channels, sample rate and buffer size.
2. Each **strip** has a source button at the top. Pick one of:
   - a channel of the main device (mono, spread to stereo)
   - **Windows input device**: any other mic or capture device, at the same time as the main device
   - **Application audio**: one app's sound (Spotify, a game, Discord...). This uses
     Windows' per-process loopback capture (Windows 10 2004+). The app keeps playing on its
     own output too; in Windows' Volume Mixer, point that app at a device you don't
     listen to if you only want to hear it through OpenMix. If the app quits, the strip
     shows "(waiting)" and reconnects when it starts again.

   Signal path: gain -> 3 VST3 slots -> IntelliPad -> fader/mute -> A1/A2/B1 buttons.
3. Click an empty **VST slot** to pick a plugin from `C:\Program Files\Common Files\VST3`.
   Click a loaded slot to open its editor, replace it or remove it.
   Put the denoiser before the autotune.
4. **IntelliPad** (double-click to reset):
   - *Voice*: X = tone tilt (Lo <-> Hi shelves, +/-6 dB), Y = presence (5 kHz) + echo.
   - *Pan*: X = constant-power pan, Y = stereo width (100-200%).
5. **Buses**: click the button under the bus name to send it to an output pair of the main
   device, or to **any Windows output device**. Each bus can use a different device,
   e.g. A1 headphones, A2 speakers, B1 a virtual cable that Discord/OBS use as their mic.
   Extra devices run on their own clocks; OpenMix resamples them and corrects drift, which
   adds about 10-25 ms of latency on those devices only.

Everything (device, strips, plugins and their settings, buses) is saved to
`%APPDATA%\OpenMix\session.xml` when the app closes.

## Layout

| File | What it does |
|---|---|
| `Source/MixerEngine.*` | Audio device, real-time callback, strip -> bus -> output summing |
| `Source/ChannelStrip.*` | Per-strip DSP and lock-safe VST3 slot loading |
| `Source/IntelliPad.*` | XY pad DSP (tilt EQ, presence, echo, pan/width) and its UI |
| `Source/Biquad.h` | Allocation-free RBJ filters for the audio thread |
| `Source/ClockBridge.h` | Lock-free FIFO + drift-correcting resampler between clock domains |
| `Source/ExternalIO.*` | Extra Windows input/output devices and app sources |
| `Source/AppCapture.*` | Per-app capture (WASAPI process loopback) and audio-app listing |
| `Source/Session.*` | Save/restore to XML |
| `Source/MainComponent.*` | Mixer UI: strips, buses, meters, plugin windows |

## License

OpenMix is licensed under the GNU AGPLv3 (see `LICENSE`). It's built on JUCE, which is
used here under its AGPLv3 option.

## Known limits / next steps

- Extra devices use simple (Hermite) resampling; fine for voice and monitoring, but a
  higher-quality resampler is planned for 44.1k <-> 48k music.
- An extra device that's unplugged shows "(offline)"; pick it again after plugging it back in.
- No own virtual audio driver yet; use a virtual cable meanwhile.
- Plugins load on the UI thread, so a slow plugin briefly freezes the window while loading.
- Session is saved only on a clean exit.
