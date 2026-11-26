# Remote control

All three remotes speak the same paths, so a fader moved from a phone, a
MIDI knob or a Stream Deck does exactly what the same OSC address does.
They are off by default (Settings).

!!! warning
    While one is on, anyone on your network can control the mixer, so only
    turn them on at home or on a network you trust. **There is no password:
    the LAN is the trust boundary.** Web pages are kept out: the phone
    remote refuses any browser request that comes from another website, so
    a page you have open can't reach the mixer through `localhost`.

## Phone / browser remote and WebSocket API

Settings > "Phone / browser remote and WebSocket API" (TCP port 8080
unless you change it). Open `http://<this PC>:8080/` on your phone:
faders, mutes, soundboard pads and scenes, live over the WebSocket
(changes made in the app, from MIDI or from another phone show up too),
with HTTP polling as a fallback if the WebSocket can't connect.

The same port is the API:

- `GET /api/state`: the mixer's state as JSON (strips, buses, soundboard,
  scenes, recording).
- `POST /api/command` with `{"path": "/openmix/strip/1/fader", "value": -6}`:
  one command.
- `ws://<this PC>:8080/ws`: send `{"path": ..., "value": ...}`, receive
  `{"type": "state", "state": {...}}` whenever the mix changes, plus
  `{"type": "ok"}` or `{"type": "error", ...}` for each command.

## OSC

Off by default (Settings). While it's on, OpenMix listens on a UDP port
(9000 unless you change it). Values are dB unless noted (out-of-range
values are clamped, NaN and infinity are ignored); strips and pads count
from 1.

| Address | Argument |
|---|---|
| `/openmix/strip/<n>/fader`, `/gain` | dB |
| `/openmix/strip/<n>/mute`, `/solo` | 0 or 1 |
| `/openmix/strip/<n>/send/<bus>` | 0 or 1 (bus name, e.g. `B1`) |
| `/openmix/strip/<n>/sendlevel/<bus>` | dB |
| `/openmix/bus/<bus>/fader` | dB |
| `/openmix/bus/<bus>/mute` | 0 or 1 |
| `/openmix/bus/<bus>/norm` | 0 or 1 (loudness normalisation on/off) |
| `/openmix/soundboard/<n>/play` | none (plays or stops pad n) |
| `/openmix/record` | 1 starts, 0 stops |
| `/openmix/replay/save` | none |
| `/openmix/scene/<n>` | none (switches to scene n) |

## MIDI controllers

Settings > "MIDI controller": pick a device, then pick a target, press
**Learn...**, and move a knob or press a key on the controller. Targets:
each strip's fader, gain, mute, solo, send on/off and send level per bus;
each bus's fader, mute and loudness normalisation; soundboard pads,
Record, Save replay and scenes 1-8.

- A CC moves a fader, gain or send level across its range.
- A note, or a CC crossing the middle (64) upwards, toggles a mute, solo,
  send or normalisation. It flips whatever the mixer has now, so it stays
  in step with changes made in the app. It also plays a pad, starts or
  stops recording, saves the replay or switches scene.
- A learned mapping is saved straight away. If the controller is
  unplugged, OpenMix opens it again when it's back.

## Stream Deck

There is no native OpenMix Stream Deck plugin. Use the Stream Deck
software's OSC plugin with the addresses above, or a WebSocket plugin
against the phone-remote port; both speak the same paths, so a button
that sends `/openmix/strip/1/mute` with 1 works either way.
