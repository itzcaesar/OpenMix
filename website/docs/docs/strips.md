# Strips & voice processing

Up to 8 input strips, added and removed at runtime with **+ Strip**.
Each strip, top to bottom, in the order the audio flows:

## 1. Name plate

Double-click, Enter or F2 to rename. The three-dot menu holds rename,
colour, mono, invert polarity, and remove the strip. The strip's colour
shows as a thin cap on the name plate and as a solid name bar along the
bottom of the strip.

## 2. Source and gain

The **source**, then the **input gain slider** directly under the source
name (-24 to +24 dB, value readout, double-click or Home for 0 dB), then
**Ø** (invert polarity) and **MONO** below the slider.

Sources:

- **Main device:** one channel (spread to stereo) or a **stereo pair**.
- **Windows input device:** any other mic or capture device, alongside
  the main device.
- **Application audio:** one app's sound, using Windows' per-process
  loopback. If the app quits, the strip shows "(not running)" and
  reconnects when it starts again. **App's own output** (in the same menu)
  moves the app's direct sound to a device you don't listen to, so you
  only hear it through OpenMix; it goes back to normal when the strip's
  source changes or OpenMix quits.
- **Windows output (loopback):** everything one output device plays,
  without a virtual cable. Devices OpenMix itself plays into are greyed
  out, because capturing them would feed the sound back into itself.

## 3. Filter: HPF / LPF

**HPF** and **LPF** keys with a cutoff knob each (HPF 20 Hz–1 kHz,
LPF 1–20 kHz). The filters sit before the EQ and switch on/off without
clicks.

## 4. Processing

Processing keys, lit when on. Click for settings, Space switches on or off.

- **NR** noise removal (RNNoise): fans, hum and keyboard noise. Adds
  20 ms at 48 kHz (RNNoise's own rate); at other rates it resamples to
  48 kHz and back, about 1 ms more. Other strips are delayed to match.
- **Gate** turns the strip down while it's quiet. **EQ**: low shelf,
  sweepable mid, high shelf. **Comp** evens out loud and quiet parts.
  **DS** softens harsh S sounds. **Duck** lowers this strip while another
  strip has signal (music under your voice).
- Gate, compressor, de-esser and ducking show a gain-reduction meter in
  their panel.

## 5. Preset

Save the strip's sound under a name and recall it here (or on any other
strip). A preset carries gain, fader, processing, inserts, sends and the
VoicePad — never the name, source, mute or solo state. If a preset's
plugins are missing on recall, those slots are skipped and you are told
how many.

!!! note
    A strip preset does not store plugin state blobs — a recalled preset
    restores the plugin choice and the strip settings, not a plugin's
    internal saved state.

## 6. Inserts

Up to 6 slots, VST3 plugins or built-in effects (Reverb, tempo-synced
Delay, Chorus). See [Plugins & built-in effects](plugins.md).

## 7. VoicePad

Double-click or Home resets the puck:

- *Voice*: X = tone tilt (dark to bright), Y = presence and echo.
- *Pan*: X = constant-power pan, Y = stereo width (100–200%).
- *Color*: corners Warm, Bright, Radio and Close; the puck blends them.
- *Space*: X = pan, Y = distance (quieter, darker, with early reflections).

## 8. Fader bay

Fader and meter, the **send keys** beside them, **Mute**, **Solo** and
**SAFE**, a per-channel **latency** readout, and a solid-colour **name
bar** along the bottom of the strip.

- Click a send key to switch it. Right-click (or M) for its level and
  **pre-fader** (the send ignores the strip fader, for example a separate
  mix for a call). A level other than 0 dB and "P" for pre-fader are
  printed on the key. Send keys show the bus's own name, so a renamed bus
  reads correctly here.
- Under the bus sends, three small **FX sends** (REV / DLY / CHO) feed
  the shared global reverb, delay and chorus; the effect return is mixed
  into every bus the strip is routed to. The Delay send follows the tap
  tempo. FX sends ignore solo.
- **Solo** plays the strip alone on the **monitor bus** (A1 unless you
  choose another in a bus menu). **SAFE** (solo-safe) keeps the strip
  playing on the monitor bus while another strip is soloed. Stream and
  call buses are not affected.
- **Latency** is the strip's total (noise removal + plugins) in
  milliseconds; the other strips are delayed to match it, so sends stay
  aligned.

**Signal path:** gain, polarity/mono, DC blocker, noise removal, gate,
HPF/LPF, EQ, compressor, de-esser, inserts, voice pad, ducking, latency
alignment, mute, fader, sends.
