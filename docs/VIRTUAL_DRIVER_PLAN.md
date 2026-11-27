# Virtual Driver: Build Plan

This is the working plan for Phase 7: OpenMix's own virtual audio driver, so
Discord, OBS, Zoom and games can pick "OpenMix B1" as a microphone without
VB-Cable or Voicemeeter. The design background (why one driver, data path,
clocking, signing) is in [`VIRTUAL_DRIVER.md`](VIRTUAL_DRIVER.md). This file is
the order of work.

Until the driver ships, virtual buses keep working through a virtual cable
(`BusTarget::Device` pointed at VB-Cable or a Voicemeeter input).

## Decisions to make first

| Decision | Options | Notes |
|---|---|---|
| Driver model | **WaveRT / PortCls** (SysVAD-style) or **ACX** | WaveRT runs on Windows 10 and 11 and matches the README's "Windows 10 2004 or later". ACX is cleaner and less code but is Windows 11 only. |
| Test VM | VMware Workstation Pro (free) or VirtualBox | Windows 11 Home has no Hyper-V. Never load a test driver on the main PC. |
| Endpoints in v1 | B1 only, then B1-B4 | Start with one capture endpoint; the pool is fixed (no endpoints created or removed at runtime). |
| Release signing | EV certificate + Microsoft attestation | Needed only for public release (about $300-600 a year). Test signing is enough until then. |

## Phase 0: Tools

- [ ] Install the **Windows Driver Kit** matching the installed SDK
      (10.0.26100) and its Visual Studio extension.
- [ ] Install **WinDbg** (Microsoft Store or the SDK debugging tools).
- [ ] Create a Windows 11 VM (and a Windows 10 VM if WaveRT is chosen).
- [ ] In the VM only: `bcdedit /set testsigning on`, reboot, set up kernel
      debugging (network KDNET) from the host.
- [ ] Clone `microsoft/Windows-driver-samples` outside this repo.

**Done when:** a driver sample builds on the host and installs in the VM.

## Phase 1: Sample driver unchanged

- [ ] Build the matching sample as is (`audio/sysvad` for WaveRT, or the
      `audio/Acx/Samples` audio sample for ACX).
- [ ] Install it in the VM with `pnputil /add-driver <inf> /install`.
- [ ] Confirm its endpoints appear in Sound settings and that uninstall
      (`pnputil /delete-driver <oem#.inf> /uninstall`) removes them cleanly.

**Done when:** install, use and uninstall work three times in a row in the VM.

## Phase 2: OpenMix B1, silence only

- [ ] New folder `driver/` in this repo with its own solution (not part of the
      CMake app build): INF, sources, a README with build and install steps.
- [ ] Strip the sample down to **one stereo capture endpoint** named
      "OpenMix B1": `KSCATEGORY_CAPTURE`, `KSNODETYPE_MICROPHONE`.
- [ ] Formats: 48 kHz stereo, 32-bit float plus 24-bit and 16-bit PCM.
- [ ] Position and timing from a QPC-paced timer; output digital silence.
- [ ] Hardware IDs and INF strings with OpenMix names only.

**Done when:** Discord and OBS in the VM list "OpenMix B1" and record silence
with no glitches for an hour.

## Phase 3: Connect it to OpenMix

Driver side:
- [ ] A control device interface the app opens with `CreateFile`.
- [ ] A shared ring buffer (locked pages) per endpoint, plus pending
      IOCTLs to wake the app on period boundaries (inverted call).
- [ ] Every index or length from user mode is masked/clamped before use.
- [ ] If the app stops feeding audio, the endpoint plays silence (never loops
      stale audio).

App side (`Source/engine/`):
- [ ] `DriverBridge.h/.cpp`: opens the control device, maps the ring buffer,
      runs the feeder thread.
- [ ] `BusTarget::OpenMixDriver` (new value in `MixerEngine.h`): a bus feeds
      its endpoint through the existing `ClockBridge` (drift correction is
      already tested there).
- [ ] Bus output menu lists "OpenMix B1" when the driver is present; falls
      back cleanly when it is not.
- [ ] Hide OpenMix's own endpoints from loopback/device capture menus
      (`MixerEngine::getOwnOutputDeviceNames()`) so a bus can't feed itself.
- [ ] Tests: ring buffer index math and silence-on-stall in `tests/CoreTests.cpp`
      (pure user-mode code, no driver needed).

**Done when:** in the VM, Discord hears the B1 bus from OpenMix; closing or
killing OpenMix gives silence, and reopening resumes audio.

## Phase 4: The full set

- [ ] Capture endpoints for **B1-B4**, one per virtual bus.
- [ ] Optional render endpoints ("OpenMix Input 1", ...) that apps can play
      into, each available as a new strip source.
- [ ] Render endpoints keep advancing even when OpenMix is not reading, so
      apps playing into them never stall.
- [ ] A small "Virtual devices" section in Settings: which endpoints are on.
      Changing it is explicit, never automatic on `+ Bus`.

**Done when:** all endpoints survive OpenMix restarts, sleep and wake, and
Windows sample-rate changes.

## Phase 5: Hardening

- [ ] Driver Verifier on (special pool, pool tracking, deadlock detection,
      IRQL checking) for every test run.
- [ ] Stress: kill OpenMix mid-stream 100 times, sleep/wake 20 times, switch
      formats while Discord records, unplug the main audio device.
- [ ] Long run: 8 hours streaming in OBS with no dropouts or growing latency.
- [ ] Test on real hardware via a spare PC or a second boot, still test-signed.

**Done when:** no bugchecks, no Driver Verifier hits, and no stuck audio
across the whole checklist.

## Phase 6: Signing and installer

- [ ] Buy an EV code-signing certificate (hardware token or cloud HSM).
- [ ] Register in Microsoft Partner Center (Hardware Dev Center).
- [ ] Build the `.cab`, sign it with the EV certificate, submit for
      **attestation signing**, download the Microsoft-signed package.
- [ ] `installer/OpenMix.iss`: an optional "Virtual devices" component that
      runs `pnputil /add-driver ... /install` (admin only when ticked), and
      removes it on uninstall.
- [ ] Document it on the wiki: install, choose "OpenMix B1" in Discord/OBS,
      uninstall.

**Done when:** a clean Windows 10 and 11 PC (test signing off) installs the
driver from the setup, and Discord picks up OpenMix B1.

## Later (optional)

- Virtual ASIO (user-mode COM DLL, no kernel work) for DAWs: see section 6 of
  `VIRTUAL_DRIVER.md`.
- Master-clock mode: the app's main callback paces the endpoints for lower
  latency, with the kernel timer as a watchdog.

## Rules while working on it

- Kernel code never allocates, waits, or touches files or the registry in the
  audio path.
- Never test an unsigned or test-signed driver on the main PC: a driver bug
  is a blue screen (or a boot loop), and test signing needs Secure Boot off,
  which stops anti-cheat games (VALORANT's Vanguard, FACEIT) from running.
- Keep `driver/` building separately so the app build and CI don't need the WDK.
