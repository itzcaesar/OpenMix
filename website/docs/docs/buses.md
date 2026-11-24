# Buses & outputs

Up to 6 buses, added and removed at runtime with **+ Bus**. The bus row
is split into **Hardware** (A1–A3) and **Virtual** (B1–B3) groups with a
divider between them. **+ Bus** asks for the group first, then an optional
custom name, and takes the next free slot in that group.

## Output

An output pair of the main device, or any Windows output device. Each bus
can use a different device: A1 headphones, A2 speakers, B1 a virtual cable
for Discord or OBS. Extra devices run on their own clocks; OpenMix
resamples them and corrects the drift, which adds about 10–25 ms on those
devices only.

## Custom names

Rename a bus from its three-dot menu (empty = the default A1..A3 /
B1..B3). The name is shown on the bus strip, on the strips' send keys, in
the soundboard and in recording. A bus never changes group when renamed:
slots A1–A3 are always **Hardware**, B1–B3 always **Virtual**.

## Limiter, loudness and meters

- **LIMITER:** the lamp lights while the safety limiter (-0.3 dBFS
  ceiling) is reducing gain, and the gain-reduction meter next to it
  shows how much (in dB).
- The **monitor bus** is shown only by a thin accent-coloured outline
  around the strip and its name bar lit in the accent colour — there is no
  "monitor" text on the strip.
- **Loudness** is the short-term LUFS (the number streaming platforms
  normalise to), with the normalisation target and gain when normalisation
  is on. The bus strip shows the live spectrum only; the full analyser
  has the momentary and integrated numbers.
- **Meters** show peak (the LED ladder) and RMS (the solid bar inside
  it): peak catches transients, RMS shows how loud the bus actually
  sounds. The same RMS bar is on every strip and on the soundboard.
- **Loudness normalisation** (a bus's three-dot menu): a slow gain that
  pulls the bus towards -14 LUFS (streaming), -16 or -23 (broadcast),
  clamped to ±12 dB and smoothed so it never pumps on a drum hit. It holds
  its gain in silence instead of turning the noise floor up. It is saved
  with the session and with scenes.

## Processing and inserts

**EQ** and **Comp** keys and up to 6 **inserts**, the same as on a strip
(VST3 or built-in effects).

**Bus path:** sum, EQ, compressor, inserts, fader, loudness
normalisation, safety limiter.

## Analyser

A live spectrum on the bus strip. Click it for the full **analyser**:
spectrum plus momentary, short-term and integrated loudness.

## Bus menu

The three-dot menu: rename the bus, analyser, loudness normalisation,
monitor bus (where solo plays), remove the bus.
