# OpenMix design direction

Dial: ENERGY 2 / RHYTHM 2 / MOTION 1

**Design read:** a native mixing console for streamers and home-studio users, in a
studio-hardware language. People keep it open for hours next to a game, a DAW or OBS, so it
has to read at a glance and then stay out of the way.

## Identity

The app should feel like a small hardware desk: a faceplate with recessed meter windows,
physical fader caps and knobs, lit lamp buttons, and a strip of masking tape at the top of each
channel where you write its name.

**Identity motif: the scribble strip.** Every channel and bus starts with a tape label
(off-white, dark marker text). It is the one decorative element, and it carries real
information: the channel name, editable in place.

## Decisions and reasons

| Decision | Reason |
|---|---|
| Neutral graphite faceplate (dark) / warm light-grey desk (light) | Hardware is anodized metal; neutral surfaces keep the meters and lamps as the only colour. |
| Dark and light themes, switchable | Dark for dim rooms next to games and DAWs; light for daylight desks. Both are first-class. |
| One accent: lamp amber | Indicator lamps on real desks are amber. Used only for "on" sends, the pad puck and the active pad mode. |
| Red only for mute and clipping, green/amber/red only in meters | These are signal-state colours every audio user already reads; they are not decoration. |
| Lamp buttons carry an LED square that fills when on | State is shown by shape as well as colour, so it reads without colour vision. |
| Meter windows stay dark in both themes | LED ladders need a dark window to be readable, on hardware and here. |
| Segoe UI for text | The native Windows UI face, hinted for small sizes, matches the system audio dialogs users know. |
| Cascadia Mono for numeric readouts only (dB, kHz, ms) | Fixed-width digits stop values jittering sideways while they change. Never used for headings. |
| Small uppercase legends with light tracking (INPUTS, INSERTS) | They play the role of silk-screened panel legends; kept small and only on section labels. |
| Radii: 3 px on controls, 6 px on faceplates, never pills | Hardware edges are crisp; a slightly larger radius marks the faceplate as the container. |
| Shadow only under fader caps and knob caps | They are the only physically raised parts; everything else sits flat on the faceplate. |
| No glow, no gradients except the cap highlight | A desk has no glowing surfaces; the cap highlight shows which parts you can grab. |
| Signal-flow order top to bottom: source, gain, inserts, pad, sends, mute, fader | It is the order audio actually travels through the strip, the same as on a real channel strip. |
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
