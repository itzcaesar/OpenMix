# Strips & voice processing

Up to 8 input strips, added with **+ Strip** (or **Mixer > Add strip**) and
removed from a strip's three-dot menu. Every strip is the same narrow
column, top to bottom in the order the audio flows: INPUT, PROCESSING,
INSERT, SENDS, the **Voice pad** button, then the fader bay with the name
plate at the bottom.

## 1. Input

The row at the top shows **INPUT n** and the strip's three-dot menu:
rename, colour, mono, invert polarity, solo-safe, strip presets and remove.

Under it, the **source** picker:

- **Main device:** one channel (spread to stereo) or a **stereo pair**.
- **Windows input device:** any other mic or capture device, alongside
  the main device.
- **Application audio:** one app's sound, using Windows' per-process
  loopback. If the app isn't playing or quits, the picker says
  "(not running)" and the strip reconnects when it starts again.
  **App's own output** (in the same menu) moves the app's direct sound to
  a device you don't listen to, so you only hear it through OpenMix; it
  goes back to normal when the strip's source changes or OpenMix quits.
- **Windows output (loopback):** everything one output device plays,
  without a virtual cable. Devices OpenMix itself plays into are greyed
  out, because capturing them would feed the sound back into itself.

A source that is missing (unplugged, not running) gets a red edge on the
picker and says why, never colour alone.

Then three knobs:

- **GAIN:** -24 to +24 dB. Double-click or Home for 0 dB.
- **HPF:** a high-pass filter from 20 Hz to 1 kHz. All the way left is
  off. Use it to cut rumble and desk thumps (80 to 120 Hz suits most voices).
- **LPF:** a low-pass filter from 1 kHz to 20 kHz. All the way right is off.

The value under each knob shows the setting, or "off". The knob's ring is
green while it does something and grey while it is off. The filters sit
before the EQ and switch on and off without clicks; the EQ panel has the
same controls.

**Ø** inverts the polarity, **MONO** sums the strip to mono.

!!! note "No separate 'virtual inputs' driver needed"
    Every strip can pick **Application audio** or **Windows output
    (loopback)** directly, because Windows 10 2004 and later support
    per-process and device loopback natively. No driver is installed for
    inputs.

## 2. Processing

Six keys in two columns, amber while on. Click a key for its settings;
Space switches it on or off without opening the panel.

- **NR** noise removal (RNNoise): fans, hum and keyboard noise. Adds
  20 ms at 48 kHz (RNNoise's own rate); at other rates it resamples to
  48 kHz and back, about 1 ms more. Other strips are delayed to match.
- **Gate** turns the strip down while it's quiet. **EQ**: low shelf,
  sweepable mid, high shelf, plus the HPF and LPF. **Comp** evens out loud
  and quiet parts. **DS** softens harsh S sounds. **Duck** lowers this
  strip while another strip has signal (music under your voice).
- Gate, compressor, de-esser and ducking show a gain-reduction meter in
  their panel.

## 3. Insert

Up to 6 VST3 plugins. See [Plugins](plugins.md).

## 4. Sends

One row per active bus: a key with the bus's name and a level knob.

- The key switches the send on or off (amber when on).
- The knob sets the send level (-inf to +12 dB); double-click or Home for
  0 dB. Its ring is grey while the send is off.
- Right-click the key (or press M on it) for **pre-fader**: the send then
  ignores the strip fader, for example a separate mix for a call. A
  pre-fader send shows "P" after the bus name.
- Keys show the bus's own name, so a renamed bus reads correctly here.

## 5. Voice pad

**Voice pad** opens the strip's XY pad in a popup. Double-click or Home
resets the puck.

- *Voice*: X = tone tilt (dark to bright), Y = presence and echo.
- *Pan*: X = constant-power pan, Y = stereo width (100 to 200%).
- *Color*: corners Warm, Bright, Radio and Close; the puck blends them.
- *Space*: X = pan, Y = distance (quieter, darker, with early reflections).

## 6. Fader bay

The bay is the same height on every strip and bus, so faders line up
across the desk.

- **Fader** (-inf to +12 dB) with its value above, and the **meter**
  (peak ladder, RMS bar, held peak above it; click the meter to reset the
  clip light).
- **M** mutes the strip (red). **S** solos it (yellow) on the **monitor
  bus**, the bus with **MON** lit. **SAFE** (solo-safe) keeps the strip
  playing on the monitor bus while another strip is soloed. Stream and
  call buses are never affected by solo.
- **LATENCY** shows this strip's own processing delay (noise removal plus
  plugins). Every strip is delayed to match the slowest one, so sends
  stay aligned.
- The **name plate** along the bottom, in the strip's colour. Double-click
  it, or press Enter or F2, to rename.

## Strip presets

Save the strip's sound under a name and recall it from the three-dot menu,
on this strip or any other. A preset carries gain, fader, filters,
processing, inserts, sends and the voice pad, never the name, source,
mute or solo state. If a preset's plugins are missing on recall (or are on
the crash skip list), those slots are skipped and you are told how many.
Plugin windows open on the strip close first, and a preset never makes a
strip duck under itself.

!!! note
    A strip preset does not store plugin state: a recalled preset
    restores the plugin choice and the strip settings, not a plugin's
    internal saved state.

**Signal path:** gain, polarity/mono, DC blocker, noise removal, gate,
HPF/LPF, EQ, compressor, de-esser, inserts, voice pad, ducking, latency
alignment, mute, fader, sends.
