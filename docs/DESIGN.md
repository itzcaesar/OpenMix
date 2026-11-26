# OpenMix design direction

Dial: ENERGY 2 / RHYTHM 1 / MOTION 1

**Design read:** a mixing desk for streamers and home-studio users on Windows, laid out like a
compact digital console: narrow identical columns, every control where the signal passes,
colour only for state. People keep it open for hours next to a game, a DAW or OBS, so it has
to read at a glance and then stay out of the way. Layout reference: the column grid of
compact console apps such as GP Mix-Lite (as inspiration for density and order, not as a
look to copy).

## Identity

**One grid, repeated.** Every strip and every bus is the same narrow column built from the
same sections in the same order, so the desk reads like a row of hardware channels. RHYTHM 1
on purpose: uniform columns are what makes a console scannable.

**Identity motif: the coloured name plate under one aligned fader bay.** Every column ends in
the same bay at the same height (fader and meter, keys, latency) with a solid name plate in
the channel's colour along the bottom, where your eyes are while mixing.

## Decisions and reasons

| Decision | Reason |
|---|---|
| Narrow columns (about 106-132 px strips, 110-136 px buses), the desk centred past that | All 8 strips and 6 buses fit on a 1080p screen; wider columns only added empty space. |
| The same sections on every column, labelled with small-caps legends and 1 px hairlines | A control is found by position, not by hunting; the label says what the section is. |
| Signal-flow order: INPUT, PROCESSING, INSERT, SENDS, Voice pad, then the fader bay | It is the order audio travels through the strip. |
| Knobs for gain, HPF, LPF and send levels, with a green value ring | Set-and-check controls take little width as knobs; the ring shows the value without reading the number. |
| HPF all the way left and LPF all the way right mean "off", shown as "off" under the knob and a grey ring | A filter at its limit does nothing, so the knob's end is the off switch; one control instead of a key plus a slider. |
| Processing as six keys (NR, Gate, EQ, Comp, DS, Duck) in two columns, settings in a panel | Shows what's on at a glance in a narrow column; the panel holds the knobs and a gain-reduction meter. |
| Sends as a row per bus: a key (on/off) and a level knob | Both the routing and the level are visible at once; right-click keeps pre-fader one step away. |
| The voice pad opens in a popup from a button | A square XY surface doesn't fit a narrow column; it is set occasionally. |
| One fader bay for the whole desk: fader, meter, M / S / SAFE (M / MON on buses), latency, name plate | Faders, meters and keys line up, so levels compare across the desk. |
| Solid name plates in nine channel colours (grey, teal, blue, purple, pink, olive, orange, red, green); buses use fixed colours | Finding a channel on a full desk by colour; the text on each plate is black or white, picked by measured contrast (4.5:1 or more on every plate). |
| Colour means state: amber = on, red = mute and clipping, yellow = solo, green = level | Panels stay neutral grey, so whatever is coloured is telling you something. Every state also shows by shape or text. |
| MON key on buses for the monitor bus | A lit key is clearer than an outline, and it is also the control that changes it. |
| Per-strip LATENCY readout in the bay | Noise removal and plugins add delay; the number tells you which strip sets the desk's alignment. |
| Menu bar: Mixer / View / Setup, the scene stepper in the middle, transport and soundboard on the right | Grouping by job; scenes get one-click stepping instead of a dropdown. |
| The header is the title bar; window buttons are drawn by one shared function for the main window and every dialog | One row instead of two, and one window-button design everywhere. |
| Device readout as plain text on the right of the bottom bar | It is status, read occasionally; clicking it still opens Audio settings. |
| Soundboard as a bar under the desk | Pads are triggers, not a channel; a bar keeps them in reach without breaking the fader line. |
| Settings in two columns sized to the main window | Everything is visible without scrolling on a normal screen; rows use the desk's 24 px keys and toggles. |
| Flat panels with a 1 px edge; 3 px radius on controls and panels | Crisp edges read as precise; nothing competes with the meters. |
| Segoe UI for text, Cascadia Mono for numeric readouts | The native Windows faces; fixed-width digits stop values jittering. |
| No text under 10 px | Smaller text renders ragged on Windows at 100% scaling. |
| Dark and light themes, switchable | Dark for dim rooms next to games and DAWs; light for daylight desks. Meter windows stay dark in both. |
| Compact desk: narrower columns, shorter rows | More channels fit on a laptop; the fader bay stays aligned. |
| Windows 11 rounded window corners, square while maximised | The window looks like a native one; a maximised window has no corners to round. |
| Motion: hover and press feedback, short fades on audio switches | A mixer is watched for hours; nothing moves except the meters, because the audio does. |

## Accessibility bar

- Text 4.5:1, large text and component boundaries 3:1, checked for both themes, including
  every name-plate colour with its chosen text colour.
- Every control reachable by Tab, operable by keyboard, with a 2 px focus ring in the text
  colour of the active theme.
- Every control has an accessible title for screen readers.
