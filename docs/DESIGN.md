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
| One accent colour, amber by default, with four more lamp colours to pick from | One accent keeps the meters as the only colour; which lamp colour it is is personal. Every accent is light enough for dark text on it, so "on" states stay readable. |
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
| Signal-flow order top to bottom: source + gain, processing, inserts, pad, then the bay (fader, sends, mute) | It is the order audio actually travels through the strip. |
| Labelled sections with small-caps legends on every strip (INPUT, FILTER, PROCESSING, PRESET, INSERTS, VOICE PAD, SENDS, FX SENDS) and bus (OUTPUT, LIMITER, LOUDNESS, PROCESSING, INSERTS, ANALYSER) | The strip reads like the signal path; each section is labelled so a control is found without hunting. |
| Gain as a horizontal slider directly under the source name, Ø/MONO keys below it | Gain is set by ear against the meter; a slider shows where it sits. Polarity and mono are per-channel state, next to the gain they affect. |
| HPF/LPF as on/off keys with a cutoff knob each, before the processing keys | Filters are set-and-forget; the key shows the state, the knob shows the cutoff. |
| Strip presets: recall list, Save and Delete on the strip | A strip's sound is reusable; a preset never carries name, source, mute or solo, so it applies to any strip. Missing plugins are skipped on recall, never fatal. |
| Built-in effects (Reverb, Delay, Chorus) in the same insert slots as VST plugins | Same bypass, wet/dry, move and remove; no second insert system to learn. A delay insert is tempo-synced from the start. |
| FX sends as three small sliders (REV / DLY / CHO) under the bus sends | Sends sit where routing already lives; three shared effects need three levels, not three more keys. |
| Solo-safe as a SAFE key beside Mute and Solo | It is a per-strip solo behaviour, so it sits with the solo control, not in a menu. |
| Per-channel latency readout in the fader bay | Latency is a mix fact (what the other strips are delayed to match), so it reads next to the fader, not in a tooltip. |
| Solid-colour name bar along the bottom of each strip, in the strip's own colour | The strip's colour is visible at the fader end too, where your eyes are while mixing; the top tape stays the rename control. |
| Bus groups HARDWARE (A1-A3) and VIRTUAL (B1-B3) with group labels and a divider | A and B buses go to different places (real devices vs virtual cables); the desk shows the split instead of making you remember slot numbers. |
| Custom bus names; the group stays slot-based | Names are for people; the slot decides where audio goes, so renaming can never re-route a bus. The "+ Bus" flow asks for the group first, then the name. |
| Monitor bus: a 1 px accent outline plus the name bar lit in the accent colour, and no text | One bus is special; the outline and the lit name bar say so at a glance without spending a label on it. |
| Limiter gain-reduction meter next to the LIMIT lamp | The lamp says it is limiting; the meter says by how much. |
| Loudness: short-term LUFS on the bus strip, full numbers only in the analyser | The strip shows the number you mix to; momentary and integrated live one click away. |
| Bottom status bar: device, sample rate, buffer size, engine CPU%, LIVE, tap tempo | The header readout opens Audio settings; the bottom bar is the always-visible health check. CPU% is the engine's own usage, not the device's. |
| Tap tempo in the status bar, BPM next to it (click to type) | Tempo is global — every tempo-synced delay follows it — so it lives outside any one strip. |
| Master section (buses) separated by a gap and a divider | Desks separate input channels from the master section; it tells you where audio ends up. |
| Channel colours (default cream, yellow, green, blue, pink, grey) as the name plate's cap and the strip's bottom name bar | They help find a channel at a glance on a full desk. The one place colour goes beyond the 2-3 core colours, on purpose, and only as identification. |
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
