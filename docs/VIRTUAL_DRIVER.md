# OpenMix Virtual Audio Driver Guide (Phase 7)

This document captures the architecture and Windows driver engineering best
practices for **Phase 7** of [`ROADMAP.md`](../ROADMAP.md).

## 1. Why One Driver Is Enough (Inputs vs. Buses vs. Endpoints)

Unlike VoiceMeeter, OpenMix does not require a virtual audio driver for desktop
or per-app **inputs**, and adding output **buses** in the mixer never requires
adding one driver per bus:

- **Input strips scale in user mode:** Windows 10 2004+ provides per-process
  WASAPI loopback (`AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK` in
  `Source/engine/AppCapture.h`) and device loopback (`DeviceLoopbackSource` in
  `Source/engine/ExternalIO.h`). Strips are just software slots in
  `MixerEngine` (`kNumStrips` in `Source/engine/ChannelStrip.h`), so raising the
  strip limit requires zero kernel driver work.
- **Output buses are software buses inside `MixerEngine`:**
  - **Hardware (`A`) buses** route to output pairs on the main ASIO/WASAPI
    device (`BusTarget::MainPair`) or separate physical Windows output devices
    via `ClockBridge` (`BusTarget::Device`).
  - **Internal / offline buses (`BusTarget::Off`)** still feed `Recorder` and
    `ReplayBuffer` without any output endpoint.
  - **Virtual (`B`) buses** only need an OS-level capture endpoint ("virtual
    microphone") when *another app* (Discord, OBS, Zoom) needs to record from
    OpenMix.
- **One driver, multiple endpoints:** A single installed Windows virtual audio
  driver can expose **multiple virtual endpoints** (e.g., `B1–B3` virtual
  capture devices and optional virtual render devices for VoiceMeeter-style
  virtual speaker inputs). Adding a bus in the mixer uses one of those
  endpoints; it never installs a new driver.

## 2. Driver Framework: ACX (Modern) vs. SysVAD (Legacy)

- **ACX (Audio Class eXtension, recommended):** Built on KMDF (`audio/Acx/Samples`
  in Microsoft's `Windows-driver-samples`). Provides an object-oriented driver
  model (`AcxCircuit`, `AcxStream`, `AcxFactoryCircuit`), native KMDF PnP/power
  management, and RT packet streaming with far less boilerplate than PortCls.
- **SysVAD (PortCls / WaveRT, legacy reference):** Microsoft's classic virtual
  audio sample (`audio/sysvad`). Well-documented in community examples, so it
  remains a useful WaveRT reference if ACX hits an unexpected constraint, though
  PortCls carries legacy WDM complexity.

### Endpoint Provisioning Strategy

Even though ACX supports dynamic runtime endpoint creation (`AcxFactoryCircuit`
with `AcxDeviceAddCircuitDevice` / `AcxDeviceRemoveCircuitDevice`), **do not
create or destroy Windows PnP audio endpoints whenever the user clicks `+ Bus`
or `Remove bus`**:

- Windows Audio Endpoint Builder PnP events cause Discord, OBS, browsers, and
  games to re-enumerate audio devices and often reset or drop their selected
  microphone mid-call/mid-stream.
- **Recommended design:** Expose a fixed pool of endpoints (e.g., `B1–B3` virtual
  microphones, plus optional virtual playback inputs) or let the user manage
  the virtual endpoint count explicitly in a dedicated driver/settings dialog.

## 3. Real-Time Data Path (Kernel ↔ `OpenMix.exe`)

- **Never block in kernel audio callbacks or DPCs:** Inside the WaveRT / ACX RT
  packet path, never allocate pool memory, wait on user-mode mutexes, or touch
  files/registry. Only perform deterministic sample copies between the WaveRT
  cyclic buffer and a lock-free ring buffer.
- **Shared memory + inverted-call IPC:**
  - `OpenMix.exe` opens the driver's control device interface (`CreateFile` with
    `FILE_FLAG_OVERLAPPED`).
  - Exchange audio through a shared-memory ring buffer (kernel section or
    MDL-locked buffer) paired with pending asynchronous ("inverted-call")
    `DeviceIoControl` requests to wake the user-mode bridge thread on period
    boundaries.
  - **Treat user-mode state as untrusted:** Kernel code must mask/clamp any
    ring-buffer indices or lengths writable by `OpenMix.exe` (`idx & mask`) so
    a corrupted or crashed user-mode process can never cause an out-of-bounds
    access or BSOD.
- **Behaviour when `OpenMix.exe` is closed or stalled:**
  - **Virtual capture endpoints (`B` buses):** Emit clean **digital silence
    (`0`)** if OpenMix stops feeding frames: never let stale audio keep looping
    in the ring buffer.
  - **Virtual render endpoints (virtual inputs):** Keep advancing the WaveRT
    read cursor on the kernel period timer even when OpenMix is not reading,
    otherwise apps playing into the virtual speaker (browsers, games, Spotify)
    will stall.

## 4. Clocking & Drift Handling

OpenMix runs its main mix callback on a **master audio device** (ASIO or main
WASAPI) and already bridges independent clock domains with `ClockBridge`
(`Source/engine/ClockBridge.h`: lock-free FIFO + 16-tap windowed-sinc
drift-correcting resampler).

Two compatible driver clocking models:

1. **QPC-paced kernel timer + `ClockBridge` (recommended first milestone):**
   The driver paces WaveRT hardware positions from a high-resolution timer
   disciplined by `KeQueryPerformanceCounter`, and `OpenMix.exe` connects each
   virtual endpoint through `ClockBridge`. Clock drift and sample-rate
   mismatches are handled by existing tested code with zero risk of stalling
   Windows' audio engine if `OpenMix.exe` hiccups.
2. **Master-clock slaved mode (lowest latency):**
   Once stable, `OpenMix.exe`'s main callback can drive/nudge the ring buffer
   period to match the master device, **with a mandatory kernel QPC watchdog
   timer fallback** so loading a slow VST3 plugin or killing `OpenMix.exe`
   never freezes Windows audio.

## 5. Audio Formats, Metadata & Self-Loopback Safety

- **Formats:** Expose `32-bit IEEE float` (matching Windows Audio Engine and
  OpenMix's internal DSP pipeline) plus `24-bit` / `16-bit PCM`, stereo (`2 ch`),
  defaulting to `48 kHz`.
- **KS metadata & friendly names:** Use explicit `KSCATEGORY_CAPTURE` /
  `KSNODETYPE_MICROPHONE` for `B` bus endpoints (so voice/stream apps discover
  them as microphones) and `KSCATEGORY_RENDER` / `KSNODETYPE_SPEAKER` for
  virtual input endpoints, with clear names (`OpenMix B1`, etc.).
- **Prevent self-feedback in OpenMix:** Once OpenMix communicates with its own
  driver directly via shared memory instead of WASAPI, exclude OpenMix's own
  virtual render endpoints from WASAPI loopback/device capture menus
  (`MixerEngine::getOwnOutputDeviceNames()` / `BusView::isVirtualCable()`) so
  users cannot feed a virtual bus back into itself.

## 6. Optional Virtual ASIO Driver (User-Mode Only)

For DAWs (Reaper, Ableton, FL Studio), a virtual ASIO driver does **not**
require kernel code:

- Implement a user-mode COM DLL exposing `IASIO`, registered under
  `HKLM\SOFTWARE\ASIO\<DriverName>` (plus WOW6432Node if supporting 32-bit hosts).
- Exchange audio buffers with `OpenMix.exe` via `CreateFileMapping` /
  `MapViewOfFile` and named events, paced in step with OpenMix's master clock.

## 7. Testing, Signing & Installer Pipeline

- **Kernel debugging in a VM:** Develop and debug exclusively in a VM (Hyper-V
  or VMware) with WinDbg attached: a single IRQL or paging bug in kernel audio
  code causes a BSOD.
- **Validation & stress testing:** Enable **Driver Verifier** (Pool Tracking,
  Deadlock Detection, Special Pool) and test repeated `OpenMix.exe` kills/restarts,
  sample-rate changes, `D0`↔`D3` sleep/wake cycles, and active Discord/OBS
  capture streams.
- **Signing requirements:**
  - *Development:* Enable test signing (`bcdedit /set testsigning on`) with a
    self-signed test certificate.
  - *Production release:* Old CA cross-signing is deprecated and blocked on
    modern Windows 11 builds. Public distribution requires an **EV Code Signing
    Certificate** (stored on a hardware USB token or Cloud HSM such as Azure Key
    Vault / SSL.com eSigner; 1-year maximum certificate lifespan) to sign a
    `.cab` package and submit it to **Microsoft Partner Center (Hardware Dev
    Center)** for **Attestation Signing** (or full WHQL/WHCP testing for
    Windows Update distribution).
- **Installer (`installer/OpenMix.iss`):**
  - Package the driver as an **optional Inno Setup component** installed via
    `pnputil /add-driver ... /install` (requiring admin elevation only when the
    driver component is selected, keeping user-mode installs friction-free).
  - Close `OpenMix.exe` cleanly before upgrades/uninstall (`CloseApplications=yes`
    is already configured in `installer/OpenMix.iss`) and remove via
    `pnputil /delete-driver ... /uninstall`.
