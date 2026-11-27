# Buses & outputs

The bus row is OpenMix's **Master section** on the right side of the
desk, separated from the input strips by a divider. Up to 8 buses, added
and removed at runtime with **+ Bus**. The bus row is split into
**Hardware** (A1–A4) and **Virtual** (B1–B4) groups with a divider
between them. **+ Bus** asks for the group first, then an optional
custom name, and takes the next free slot in that group. Once B1–B4 are
in use, a new virtual bus takes a free hardware slot (B5, B6 and so on),
so with only two hardware buses you can have six virtual ones. Hardware
buses never take virtual slots.

## Output

An output pair of the main device, any Windows output device, or **Not
connected** (internal only, which still feeds recording and the replay
buffer). Each bus can use a different device: A1 headphones, A2
speakers, B1 a virtual cable for Discord or OBS. Extra devices run on
their own clocks; OpenMix resamples them and corrects the drift, which
adds about 10–25 ms on those devices only.

!!! note "Adding buses vs. drivers"
    Output buses are software mixing buses inside OpenMix: adding or
    activating a bus does **not** require a separate audio driver per bus.
    **Hardware (`A`)** buses route to physical outputs or extra channels on
    your main interface. **Virtual (`B`)** buses only need a virtual
    audio endpoint when another app (Discord, OBS, Zoom) needs to pick
    that bus as a microphone: today you point a `B` bus at a third-party
    virtual cable (`CABLE Input`), and in [Phase 7](roadmap.md) **one**
    OpenMix virtual driver will expose multiple virtual endpoints at
    once.

## Custom names

Rename a bus from its three-dot menu (empty = the default A1..A4 /
B1..B4). The name is shown on the bus's name plate, on the strips' send keys, in
the soundboard and in recording. A bus never changes group when renamed:
slots A1–A4 are always **Hardware**, B1–B4 always **Virtual**.

## Limiter, loudness and meters

- **LIMITER:** the gain reduction of the safety limiter (-0.3 dBFS
  ceiling), as a number and a bar. It reads "LIMITING" in red while the
  limiter is working hard.
- **MON** is lit on the **monitor bus**, the bus where soloed strips are
  heard. Click MON on another bus to move it there, or on the lit one to
  have no monitor bus.
- **Loudness** is the short-term LUFS (the number streaming platforms
  normalise to), with the normalisation target and gain when normalisation
  is on. The bus strip shows the live spectrum only; the full analyser
  has the momentary and integrated numbers.
- **Meters** show peak (the LED ladder) and RMS (the solid bar inside
  it): peak catches transients, RMS shows how loud the bus actually
  sounds. The same RMS bar is on every strip and on the soundboard.
- **Loudness normalisation** (the box under the LUFS readout, or the
  bus's three-dot menu): a slow gain that
  pulls the bus towards -14 LUFS (streaming), -16 or -23 (broadcast),
  clamped to +/-12 dB and smoothed so it never pumps on a drum hit. It
  measures the bus the way the LUFS readout does (K-weighted, about a 3 s
  window), so the target and the readout agree, and it sits before the bus
  fader, so the fader still trims the result. It holds its gain in silence
  instead of turning the noise floor up, and glides back to unity when you
  switch it off. It is saved with the session and with scenes.

## Processing and inserts

**EQ** and **Comp** keys and up to 6 **inserts**, the same as on a strip.
The compressor has adjustable Threshold, Ratio, Knee (0–18 dB), Attack
(0.1–100 ms) and Release (10–1000 ms).
Switching EQ or Comp fades over about 20 ms, so it never clicks.

**Bus path:** sum, EQ, compressor, inserts, loudness normalisation,
fader, safety limiter.

## Analyser

A live spectrum on the bus column. Click it for the full **analyser**:
spectrum plus momentary, short-term and integrated loudness.

## Routing matrix

**Mixer > Routing matrix** shows every strip and bus down the side and
every bus across the top. A cell's key turns the send on and its knob sets
the level.

- **Strip rows** are the strips' own sends (the same keys and knobs as on
  the strip).
- **Bus rows** feed one bus into another, after its fader and limiter. For
  example, feed B1 (the stream mix) into A1 to hear what the stream hears.
- A feed that would make a loop (B1 into A1 while A1 already feeds B1) is
  greyed out.
- While something is soloed, feeds into the monitor bus pause, so solo
  still plays only the soloed strips.

Feeds are saved with the session and in scenes.

## Bus menu

The three-dot menu: rename the bus, analyser, loudness normalisation,
monitor bus (where solo plays), remove the bus.
