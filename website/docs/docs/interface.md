# Interface & themes

OpenMix is designed as a native mixing console for streamers and
home-studio users, finished like a pro audio plugin: dark modules, crisp
controls, lamps for state. People keep it open for hours next to a game,
a DAW or OBS, so it has to read at a glance and then stay out of the way.

The full design direction lives in the repo at `docs/DESIGN.md`.

## Layout

- **Identity motif: the aligned fader bay.** Every column (strips and
  buses) ends in the same bay at the same height: fader and meter, mute
  along the bottom. Faders line up across
  the whole desk, as on a real console.
- **Name plate:** a recessed field with the channel's colour as a thin cap
  along its top edge. The same colour returns as a solid name bar along
  the bottom of the strip.
- **Labelled sections** with small-caps legends on every strip (INPUT,
  PROCESSING, INSERTS, VOICE PAD, SENDS) and
  bus (OUTPUT, LIMITER, LOUDNESS, PROCESSING, INSERTS, ANALYSER), in
  signal-flow order top to bottom. There is no FILTER or PRESET section
  on a strip: HPF/LPF live in the EQ panel, and presets and solo-safe
  live in the strip's three-dot menu.
- **Bus groups** HARDWARE (A1–A3) and VIRTUAL (B1–B3) with group labels
  and a divider; the master section is separated by a gap and a divider.
- **Monitor bus:** a 1 px amber outline plus the name bar lit in amber,
  and no text.
- **Soundboard** as a bar under the whole desk, so pads stay in reach
  without breaking the fader line.
- **Bottom status bar:** LIVE, engine CPU%, scan and recording progress, and remote-service lamps. The header readout opens Audio settings; the bottom
  bar is the always-visible health check. CPU% is the engine's own usage,
  not the device's.

## Themes

- Dark and light themes, both first-class and switchable in Settings.
- **One accent colour, lamp amber**, for "on" lamps, the pad puck and the
  monitor bus. It is light enough for dark text on it, so "on" states stay
  readable.
- **Compact mode:** narrower strips and buses, tighter gaps, shorter
  plugin rows and soundboard bar, so more channels fit on a laptop
  screen. The fader bay stays aligned; only the widths and spacing shrink.
- Channel colours (default cream, yellow, green, blue, pink, grey) help
  find a channel at a glance on a full desk.
- Red only for mute and clipping, green/amber/red only in meters. Lamp
  buttons carry an LED square that fills when on, so state reads by shape
  as well as colour.
- Segoe UI for text; Cascadia Mono for numeric readouts only (dB, kHz,
  ms). Meter windows stay dark in both themes.

## Accessibility

- Text 4.5:1, large text and component boundaries 3:1, checked for both
  themes (see the table in `Source/ui/Theme.h`).
- Every control reachable by Tab, operable by keyboard, with a 2 px focus
  ring in the text colour of the active theme.
- Every control has an accessible title for screen readers.
