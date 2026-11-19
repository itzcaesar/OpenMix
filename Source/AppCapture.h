#pragma once

// Per-app audio capture using Windows' process loopback API (Windows 10 2004 / build 19041+).
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

class AppCapture
{
public:
    static constexpr double sampleRate = 48000.0;

    AppCapture();
    ~AppCapture();

    // Starts capturing everything `pid` (and its child processes) plays, pushing stereo
    // 48 kHz float audio into `target`. Returns false and fills `error` on failure.
    bool start (uint32_t pid, ClockBridge& target, std::string& error);
    void stop();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
