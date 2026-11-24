# Plugins & built-in effects

## VST3 plugins

"+ Add plugin" lists the VST3 plugins in
`C:\Program Files\Common Files\VST3`, grouped by maker when there are
many. They are scanned in the background, each in its own process, so a
plugin that crashes while being scanned is skipped instead of taking
OpenMix down (Settings > Retry skipped plugins tries them again).

- Click a plugin to open its window; its lamp bypasses it (with a short
  crossfade, and its latency kept so timing doesn't jump).
- Drag a plugin up or down to reorder the chain (or Alt+Up/Down).
- The dots menu: **Wet/dry mix** (the dry signal is delayed to match the
  plugin's latency), **A/B compare** (two settings to switch between; the
  row shows "(B)" while you hear B), save or load a **preset**
  (`.omxpreset`), replace, remove.
- A plugin that outputs invalid audio (NaN/Inf) is stopped automatically
  and marked, instead of breaking the mix. "Try again" in its menu
  restarts it.

## Crash-safe loading

**Crash-safe loading** (on by default, Settings): before a plugin is
loaded into the mixer, it is started in a separate test process —
created, prepared and run through one silent block. A plugin that crashes
or hangs while starting is skipped and blacklisted instead of taking
OpenMix down (Settings > Retry skipped plugins tries it again).

!!! warning
    This protects *loading*. A plugin that crashes later, while processing
    audio, can still take the mixer down — full out-of-process audio
    hosting is not done yet. See [Known limits](troubleshooting.md).

## Built-in effects

No plugin to install. **Reverb**, **Delay** and **Chorus** are in the
"+ Add plugin" menu under **Built-in effect**, in any insert slot on a
strip or a bus. Click one to open its settings panel; it has the same
bypass, wet/dry mix, move and remove as a VST slot, and it is saved in
sessions and strip presets. A Delay added this way is tempo-synced from
the start: it follows the tap-tempo BPM (change the note value in its
panel, or switch tempo sync off and set the time directly). Built-ins
have zero latency.

The reverb wet level is not yet tuned by ear — treat it as a starting
point and adjust to taste.

## FX sends

Under the bus sends on every strip, three small **FX sends**
(REV / DLY / CHO) feed the shared global reverb, delay and chorus; the
effect return is mixed into every bus the strip is routed to. The Delay
send follows the tap tempo. FX sends ignore solo.

## VST2 plugins (opt-in, local build only)

VST2 hosting is **off by default** and is not in the released builds.
JUCE 9 still contains the VST2 host code, but it needs `aeffect.h` /
`aeffectx.h` from the VST2 SDK, which JUCE no longer bundles — and
Steinberg stopped issuing new VST2 licences in October 2018, so a new
developer cannot legally distribute a VST2 host. Only enable this if you
already hold a VST2 licence and SDK. The SDK is never committed to this
repo, never fetched by CMake, and CI builds with VST2 off.

To build with it, from a Developer PowerShell for VS 2026:

```powershell
cmake --preset vs2026 -DOPENMIX_ENABLE_VST2=ON -DVST2_SDK_DIR="C:/SDKs/vstsdk2.4"
cmake --build --preset release
```

`VST2_SDK_DIR` must be the SDK folder containing
`pluginterfaces/vst2.x/aeffect.h` (from
`vstsdk3610_11_06_2018_build_37` or older). With it on, OpenMix also
scans the usual VST2 locations (`C:\Program Files\Steinberg\VstPlugins`,
`C:\Program Files\VstPlugins`, and the `VST_PATH` environment variable)
for `.dll` plugins, in the same crash-safe child process as VST3. VST2
plugins then appear in "+ Add plugin" alongside VST3 ones and use the
same slots, bypass, wet/dry, presets and latency compensation. Session
files already store the plugin's format, so a session saved with a VST2
plugin loads it back when built with VST2 on — and that slot will be
empty on a default (VST2-off) build.
