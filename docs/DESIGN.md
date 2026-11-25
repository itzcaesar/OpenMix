# OpenMix design direction

Dial: ENERGY 2 / RHYTHM 2 / MOTION 1

**Design read:** a native mixing console for streamers and home-studio users, finished like
a pro audio plugin: dark modules, crisp controls, lamps for state. People keep it open for
hours next to a game, a DAW or OBS, so it has to read at a glance and then stay out of the way.

## Identity

The app should feel like a well-made audio plugin laid out as a desk: each channel is a
module with a name plate, its controls in signal-flow order, and a fader bay at the bottom.

**Identity motif: the aligned fader bay.** Every column (strips and buses) ends in
the same bay at the same height: fader, meter, and the column's keys beside it, mute along the
bottom. Faders line up across the whole desk, as on a real console.

**Name plate:** a recessed field with the channel's colour as a thin cap along its top edge.
The colour helps find a channel on a full desk; the name is editable in place. The same
colour returns as a solid name bar along the bottom of the strip, where your eyes are while
mixing.

## Decisions and reasons

| Decision | Reason |
|---|---|
| Neutral graphite modules (dark) / warm light-grey (light) | Neutral surfaces keep the meters and lamps as the only colour. |
| Modules shade slightly lighter at the top, with a 1 px top highlight | Gives each module depth against the window, the way plugin panels separate sections; the only surface gradient. |
| One shared fader-bay line for every column | Faders, meters and mute rows line up, so levels compare at a glance across the desk. |
| Sends beside the fader in the fader bay (bus keys in a 2-column grid, FX sliders underneath) | Routing sits where your hand already is, next to the fader it feeds; the sends column takes what is left after the fader's preferred width, so the keys stay readable. |
| Status readout doubles as the Audio settings button | One control instead of a readout plus a button; the chevron shows it opens something. |
| Header groups: name, status, transport (record, replay), window tools (soundboard, settings) | Grouping by job instead of one row of identical buttons; a divider separates transport from tools. |
| Soundboard as a bar under the whole desk | Pads are triggers, not a channel; a bar keeps them in reach without breaking the fader line between inputs and outputs. |
| The desk fills the window; a bus gets about 3/4 of a strip's width | No dead margins on a big screen; strips keep the wider share because they hold more controls. |
| Held-peak number above every meter, wider LED ladders | Headroom is read as a number at a glance; a red bar under it marks 0 dBFS as a second cue. |
| Live mini spectrum in each bus, opening the full analyser | Uses the bus space above the bay for something you actually watch on an output. |
| Compact, wide voice pad at the bottom of the strip's top section | It is a gesture surface, not a square display; the inserts list gets the height it frees. |
| Add buttons ("+ Strip", "+ Bus") in the section legends | They belong to the section they add to and no longer take a column of width. |
| Dark and light themes, switchable | Dark for dim rooms next to games and DAWs; light for daylight desks. Both are first-class. |
| One accent colour: lamp amber | One accent keeps the meters as the only colour. Amber is light enough for dark text on it, so "on" states stay readable. |
| Compact density: narrower strips and buses, tighter gaps, shorter plugin rows | More channels fit on a laptop screen. The fader bay stays aligned; only the widths and spacing shrink. |
| One accent: lamp amber | Indicator lamps on real desks are amber. Used only for "on" sends, the pad puck and the active pad mode. |
| Red only for mute and clipping, green/amber/red only in meters | These are signal-state colours every audio user already reads; they are not decoration. |
| Lamp buttons carry an LED square that fills when on | State is shown by shape as well as colour, so it reads without colour vision. |
| Meter windows stay dark in both themes | LED ladders need a dark window to be readable, on hardware and here. |
| Segoe UI for text | The native Windows UI face, hinted for small sizes, matches the system audio dialogs users know. |
| Cascadia Mono for numeric readouts only (dB, kHz, ms) | Fixed-width digits stop values jittering sideways while they change. Never used for headings. |
| Small uppercase legends with light tracking (INPUTS, INSERTS) | They play the role of silk-screened panel legends; kept small and only on section labels. |
| Radii: 3 px on controls, 6 px on modules, never pills | Crisp edges read as precise; the larger radius marks the module as the container. |
| Shadow only under fader caps and knob caps | They are the only raised parts you grab; everything else sits flat on the module. |
| No glow; gradients only on the module shade and the cap highlight | Nothing in a mixer should compete with the meters for attention. |
| Signal-flow order top to bottom: source + gain, processing, inserts, voice pad, then the bay (fader + sends, mute/solo, name bar) | It is the order audio actually travels through the strip. |
| Labelled sections with small-caps legends on every strip (INPUT, PROCESSING, INSERTS, VOICE PAD, SENDS) and bus (OUTPUT, PROCESSING, INSERTS, ANALYSER, LIMITER, LOUDNESS) | The strip reads like the signal path; each section is labelled so a control is found without hunting. Sections touch, divided only by a 1 px hairline with 7 px padding inside each one (mockup proportions); the fader bay holds the fader, Mute/Solo and the name bar. |
| The desk follows the openmix-ui-mockup layout, except the sends sit beside the fader instead of in a full-width section above it: section order, 1 px dividers, 7 px padding, 94 px voice pad, 196 px fader bay | The port had drifted (extra PRESET and FILTER sections) and the strips came out cramped on a real machine; the mockup is the spec for the rest, and the console's minimum height keeps the strips from squeezing again -- shorter windows scroll instead. |
| Gain as a horizontal slider directly under the source name, Ø/MONO keys below it | Gain is set by ear against the meter; a slider shows where it sits. Polarity and mono are per-channel state, next to the gain they affect. |
| HPF/LPF live in the EQ panel: on/off keys plus cutoff sliders above the EQ knobs, on strips only | Filters are set-and-forget, so they don't earn a strip section; the EQ panel is where tone-shaping already lives. There is deliberately no FILTER section on the strip, so the desk matches the mockup. |
| Strip presets live in the strip ⋯ menu (Save / recall / delete) | A strip's sound is reusable; a preset never carries name, source, mute or solo, so it applies to any strip. Missing plugins are skipped on recall, never fatal. There is deliberately no PRESET section on the strip, so the desk matches the mockup. |
| Solo-safe lives in the strip ⋯ menu | The fader bay holds Mute/Solo only, as in the mockup; solo-safe is set-and-forget, so the menu is a fine home for it. |
| No per-channel latency readout on the strip | The mockup has none, and the readout was one more thing crowding the bay. Latency compensation still runs in the engine; the number lives nowhere on the strip. |
| Solid-colour name bar along the bottom of each strip and bus, in the channel's own colour, saturated and darkened to a bold bar with contrast-chosen text | The strip's colour is visible at the fader end too, where your eyes are while mixing; the top tape stays the rename control. Buses use fixed per-slot hues; the monitor bus's bar is lit in amber instead. |
| Bus groups HARDWARE (A1-A3) and VIRTUAL (B1-B3) with group labels and a divider | A and B buses go to different places (real devices vs virtual cables); the desk shows the split instead of making you remember slot numbers. |
| Custom bus names; the group stays slot-based | Names are for people; the slot decides where audio goes, so renaming can never re-route a bus. The "+ Bus" flow asks for the group first, then the name. |
| Monitor bus: a 1 px amber outline plus the name bar lit in amber, and no text | One bus is special; the outline and the lit name bar say so at a glance without spending a label on it. |
| Limiter gain-reduction meter under the LIMIT lamp, with a 0 / -3 / -6 / -12 dB scale | The lamp says it is limiting; the meter says by how much. |
| Loudness: short-term LUFS on the bus strip, full numbers only in the analyser | The strip shows the number you mix to; momentary and integrated live one click away. |
| Bottom status bar: LIVE, engine CPU%, scan/recording progress, remote-service lamps | The header readout opens Audio settings; the bottom bar is the always-visible health check. CPU% is the engine's own usage, not the device's. |
| Master section (buses) separated by a gap and a divider | Desks separate input channels from the master section; it tells you where audio ends up. |
| Channel colours (default cream, yellow, green, blue, pink, grey) as the name plate's cap; the bottom name bar uses a saturated, darkened version of the same colour | They help find a channel at a glance on a full desk. The one place colour goes beyond the 2-3 core colours, on purpose, and only as identification. |
| App icon: a dark module with three faders, the middle cap lit amber (`resources/make_icon.py`) | It is the app's own fader bay in miniature, in the same graphite and amber, and reads at 16 px. |
| Processing as a row of six keys (NR, Gate, EQ, Comp, DS, Duck), details in a panel | Shows what's on at a glance without eating the height the fader needs; the panel holds the knobs and a gain-reduction meter. |
| Sends: click toggles, right-click (or M) opens level and pre/post | On/off is the common action; levels are set once. A level other than 0 dB is printed on the key. |
| Motion: hover and press feedback only | A mixer is watched for hours; nothing moves except the meters, which move because the audio does. |

## Accessibility bar

- Text 4.5:1, large text and component boundaries 3:1, checked for both themes
  (see the table in `Source/ui/Theme.h`).
- Every control reachable by Tab, operable by keyboard, with a 2 px focus ring in the text
  colour of the active theme.
- Every control has an accessible title for screen readers.
