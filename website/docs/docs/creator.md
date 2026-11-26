# Scenes, hotkeys & creator tools

## Scenes

A scene is a saved mix: strip and bus levels, mutes, solos, sends (on,
level, pre-fader), processing settings, plugin bypass and wet/dry, the
voice pad, bus EQ and compressor, and the soundboard's level and buses.
Sources, devices and the plugins themselves are not part of a scene, so
switching is instant, and every level glides to its new value without a
click.

The scene stepper in the header switches scenes: **<** and **>** step
through them, the picker lists them, **Store** saves the current mix into
the selected scene and **New** saves it as a new one. The picker's menu
also renames or deletes scenes and opens Hotkeys. Deleting a scene moves the
hotkeys and MIDI buttons of later scenes up with them. Scenes are saved with
the session. Over OSC, `/openmix/scene/<n>` switches to scene n (counting
from 1).

## Global hotkeys

**Settings > Hotkeys** (or **Hotkeys...** in the Scene menu). These work
in any app, also while a game is in front, and the keys still reach that
app, so pick combinations it doesn't use (for example with Ctrl+Alt).

- **+ Add hotkey**, pick what it does, then press the keys (Escape
  cancels). Click a hotkey's keys to change them; **Remove** deletes it.
  A combination can only be used once.
- Actions: mute or unmute a strip, **push to talk** on a strip (open while
  you hold the keys, muted when you let go), play a soundboard pad, stop
  all sounds, start or stop recording, save the replay, switch to a scene.
- Keys sent by other software (macro tools, remote desktop) are ignored
  on purpose.

!!! note
    Global hotkeys use a keyboard hook that only listens. A game with
    strict anti-cheat could in principle object to any keyboard hook;
    Discord and OBS use the same mechanism. The hook runs on its own
    thread, so a busy OpenMix never slows typing in other apps.

## Recording and replay

- **Record** (Ctrl+Shift+R) writes every bus, every strip, or both, to
  one WAV file each in `Documents\OpenMix Recordings\<date and time>`
  (a second take in the same second gets its own folder).
  While recording, the button shows the elapsed time. Changing the sample
  rate in Audio settings stops the recording, so the files never play back
  at the wrong speed.
- The dots next to Record choose what to record, set the **replay
  buffer** and open the recordings folder.
- **Replay buffer:** keeps the last 30, 60 or 120 seconds of one bus in
  memory. **Replay** (Ctrl+Shift+S) saves them as a WAV file; saving
  twice never overwrites the first.

## Soundboard

A bar along the bottom of the window (the header's **Soundboard** button
shows or hides it).

- Eight pads for WAV, MP3, FLAC or Ogg files up to 60 seconds. Click a
  pad, or press 1–8 anywhere in OpenMix, to play; again to stop (with a
  short fade, so it doesn't click).
  Right-click a pad (or M) to load, rename or clear it.
- Its own volume and meter, and **Play on** keys for the buses it plays on
  (A1 and B1 by default, so you and your stream both hear it).
  **Stop all** stops every pad.
