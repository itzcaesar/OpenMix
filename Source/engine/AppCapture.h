#pragma once

// Loopback capture: one app's audio (process loopback, Windows 10 2004 / build 19041+), or
// everything an output device plays (device loopback).
// Plain C++ on purpose: the Win32/COM details stay inside AppCapture.cpp.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class ClockBridge;

struct AudioApp
{
    uint32_t pid = 0;
    std::wstring exeName;       // e.g. "Spotify.exe"
    bool active = false;        // currently playing sound
};

// Apps that have an audio session on any active output device, sorted by name.
std::vector<AudioApp> listAudioApps();

// Finds the audio-session process for an exe name (case-insensitive); 0 if not running.
uint32_t findAudioAppByExe (const std::wstring& exeName);

bool isProcessAlive (uint32_t pid);

// Output devices, for choosing where an app's own sound goes.
struct RenderDevice
{
    std::wstring id;     // MMDevice endpoint ID
    std::wstring name;   // friendly name
};
std::vector<RenderDevice> listRenderDevices();

// Points one app's own output at another device (empty id = back to the Windows default).
// Used so a captured app isn't heard twice: OpenMix captures it, and its direct output goes to
// a device nobody listens to. This is the per-app device setting from Windows' Sound settings;
// the interface behind it is undocumented, so this returns false with an error rather than
// guessing if the running Windows build doesn't offer it.
bool setAppOutputDevice (uint32_t pid, const std::wstring& deviceId, std::string& error);

class AppCapture
{
public:
    static constexpr double sampleRate = 48000.0;

    AppCapture();
    ~AppCapture();

    // Starts capturing everything `pid` (and its child processes) plays, pushing stereo
    // 48 kHz float audio into `target`. Returns false and fills `error` on failure.
    bool start (uint32_t pid, ClockBridge& target, std::string& error);

    // Starts capturing everything the output device `endpointId` plays (WASAPI loopback).
    bool startDevice (const std::wstring& endpointId, ClockBridge& target, std::string& error);

    void stop();

    // True once the capture stream has died (e.g. Windows invalidated it after a device change).
    bool hasFailed() const noexcept;

private:
    bool launch (uint32_t pid, const std::wstring& endpointId, ClockBridge& target, std::string& error);
    struct Impl;
    std::unique_ptr<Impl> impl;
};
