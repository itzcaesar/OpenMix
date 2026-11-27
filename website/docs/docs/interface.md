# Interface & themes

OpenMix is laid out like a hardware mixing desk: narrow columns side by
side, every one built from the same sections in the same order. People
keep it open for hours next to a game, a DAW or OBS, so it has to read at a
glance and then stay out of the way.

The full design direction lives in the repo at `docs/DESIGN.md`.

## The window

- **Header:** OpenMix, the **Mixer**, **View** and **Setup** menus, the
  scene stepper (**<**, the scene picker, **>**, **Store**, **New**),
  **Record** with its options, **Replay**, **Soundboard**, and the window
  buttons. The header is also the title bar: drag it to move the window,
  double-click it to maximise or restore.
- **Desk:** input strips on the left, then the buses, grouped
  **HARDWARE** (A1 to A3) and **VIRTUAL** (B1 to B3) with a divider
  between the groups.
- **Soundboard:** a bar under the desk (View > Soundboard).
- **Bottom bar:** LIVE, the engine's own CPU%, plugin scanning and
  recording progress, a lamp for each remote you switched on (OSC, phone,
  MIDI), and on the right the audio device, sample rate, buffer, latency
  and dropouts. Click the device readout for Audio settings.

## Menus

- **Mixer:** add a strip, add a bus, the routing matrix, undo and redo,
  save the mix as a new scene, scenes, hotkeys.

**Undo** (Ctrl+Z) and **Redo** (Ctrl+Y or Ctrl+Shift+Z) step through mix
changes: levels, mutes, sends, feeds, processing, the voice pad, plugin
bypass and wet/dry. A whole fader drag is one step, and up to 100 steps are
kept. Pasting a strip is one step too, and undoing it brings back the
strip's plugins and their settings. Adding or removing strips, buses and
plugins, and sources and devices, are not undone.
- **View:** soundboard, compact desk, dark theme.
- **Setup:** audio device; **Restart audio engine** (Ctrl+R) reopens the
  main device and every extra device; **Auto restart audio engine** does it
  by itself when the main device stops, or whenever any device is plugged
  in or removed; **Release media file** stops the media player and lets go
  of its file. **Load settings** and **Save settings** use a settings file
  (loading restarts OpenMix and keeps your current settings as
  `session-backup.xml`); **Load settings on startup** starts with a chosen
  file every time; **Reset settings** restarts with a fresh desk (also with
  a backup). Then settings, hotkeys, recording options, the recordings
  folder, and checkboxes for close to the tray, run on Windows startup,
  show the window on startup, always on top and **Lock controls** (the desk
  ignores the mouse and keyboard; menus, scenes and hotkeys still work).

## Columns

- **The same grid on every column.** Small-caps section labels (INPUT,
  PROCESSING, INSERT, SENDS on a strip; OUTPUT, PROCESSING, INSERT,
  ANALYSER, LIMITER, LOUDNESS on a bus) with a hairline between sections.
- **One fader bay for the whole desk.** Every column ends in the same bay
  at the same height: fader and meter, the M / S / SAFE (or M / MON) keys,
  latency, and the name plate. Faders line up across the desk, as on a
  real console.
- **Name plates** along the bottom in the channel's colour: grey, teal,
  blue, purple, pink, olive, orange, red or green for strips (from the
  strip menu), fixed colours for buses. The text on each plate is black or
  white, whichever reads better.
- **Monitor bus:** its **MON** key is lit.

## Colour means state

- Panels are neutral grey; colour only says what something is doing.
- **Amber:** a key that is on (processing, sends, MON, the soundboard).
- **Red:** mute, and clipping in the meters.
- **Yellow:** solo.
- **Green:** level: knob rings, meters and the analyser.
- Every state also shows by shape or text (a lit key, "off" under a knob,
  a red edge plus words on a missing device), never by colour alone.

## Themes and density

- Dark and light themes (View > Dark theme, or Settings).
- **Compact desk** (View > Compact desk): narrower strips and buses and
  shorter plugin rows, so more channels fit on a laptop screen. The fader
  bay stays aligned.
- Segoe UI for text; Cascadia Mono for numeric readouts (dB, kHz, ms).
  Nothing is drawn smaller than 10 px, so text stays sharp at 100% display
  scaling.

## Accessibility

- Text 4.5:1, large text and component boundaries 3:1, in both themes.
- Every control reachable by Tab, operable by keyboard, with a 2 px focus
  ring in the text colour of the active theme.
- Every control has an accessible title for screen readers.
