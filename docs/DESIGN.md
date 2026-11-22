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
The colour helps find a channel on a full desk; the name is editable in place.

## Decisions and reasons

| Decision | Reason |
|---|---|
| Neutral graphite modules (dark) / warm light-grey (light) | Neutral surfaces keep the meters and lamps as the only colour. |
| Modules shade slightly lighter at the top, with a 1 px top highlight | Gives each module depth against the window, the way plugin panels separate sections; the only surface gradient. |
| One shared fader-bay line for every column | Faders, meters and mute rows line up, so levels compare at a glance across the desk. |
| Sends stacked beside the fader | Routing sits where your hand already is, and it saves a whole row per strip. |
| Status readout doubles as the Audio settings button | One control instead of a readout plus a button; the chevron shows it opens something. |
| Header groups: name, status, transport (record, replay), window tools (soundboard, settings) | Grouping by job instead of one row of identical buttons; a divider separates transport from tools. |
| Soundboard as a bar under the whole desk | Pads are triggers, not a channel; a bar keeps them in reach without breaking the fader line between inputs and outputs. |
| The desk fills the window; a bus gets about 3/4 of a strip's width | No dead margins on a big screen; strips keep the wider share because they hold more controls. |
| Held-peak number above every meter, wider LED ladders | Headroom is read as a number at a glance; a red bar under it marks 0 dBFS as a second cue. |
| Live mini spectrum in each bus, opening the full analyser | Uses the bus space above the bay for something you actually watch on an output. |
| Compact, wide voice pad at the bottom of the strip's top section | It is a gesture surface, not a square display; the inserts list gets the height it frees. |
| Add buttons ("+ Strip", "+ Bus") in the section legends | They belong to the section they add to and no longer take a column of width. |
| Dark and light themes, switchable | Dark for dim rooms next to games and DAWs; light for daylight desks. Both are first-class. |
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
| Signal-flow order top to bottom: source + gain, processing, inserts, pad, then the bay (fader, sends, mute) | It is the order audio actually travels through the strip. |
| Master section (buses) separated by a gap and a divider | Desks separate input channels from the master section; it tells you where audio ends up. |
| Optional tape colours per channel (yellow, green, blue, pink, grey) | Real masking tape comes in colours; they help find a channel at a glance on a full desk. The one place colour goes beyond the 2-3 core colours, on purpose. |
| Processing as a row of six keys (NR, Gate, EQ, Comp, DS, Duck), details in a panel | Shows what's on at a glance without eating the height the fader needs; the panel holds the knobs and a gain-reduction meter. |
| Sends: click toggles, right-click (or M) opens level and pre/post | On/off is the common action; levels are set once. A level other than 0 dB is printed on the key. |
| Motion: hover and press feedback only | A mixer is watched for hours; nothing moves except the meters, which move because the audio does. |

## Accessibility bar

- Text 4.5:1, large text and component boundaries 3:1, checked for both themes
  (see the table in `Source/ui/Theme.h`).
- Every control reachable by Tab, operable by keyboard, with a 2 px focus ring in the text
  colour of the active theme.
- Every control has an accessible title for screen readers.
