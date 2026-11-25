#pragma once

#include <JuceHeader.h>
#include "engine/MixerEngine.h"
#include "app/RemoteCommands.h"

#include <condition_variable>
#include <mutex>
#include <thread>

// Phone / browser remote and WebSocket API. Off by default.
//
//   http://<this PC>:<port>/            phone remote (faders, mutes, soundboard, scenes)
//   GET  /api/state                   the mixer's state as JSON (see remoteStateJson)
//   POST /api/command  {"path": "/openmix/strip/1/fader", "value": -6}
//   ws://<this PC>:<port>/ws          WebSocket: send {"path": ..., "value": ...},
//                                      receive {"type": "state", "state": {...}}
//
// The same paths work over OSC and MIDI mapping (see RemoteCommands.h), so a
// Stream Deck can use its OSC plugin, or a WebSocket plugin, against this API.
// Only enable it on a network you trust: anyone on it can move faders and
// start recordings while it is on. Browsers are held to the page this server
// serves: a request carrying an Origin from any other site is refused, so a
// web page open on this PC can't drive the mixer through localhost.
//
// Threads: one listener thread plus one thread per connection. stop() closes
// every socket and waits for all of them, so none outlives the server.
class RemoteServer
{
public:
    explicit RemoteServer (MixerEngine&);
    ~RemoteServer();

    bool start (int port, juce::String& error);
    void stop();
    bool isRunning() const noexcept { return running.load(); }
    int getPort() const noexcept { return port; }

    // Called from the UI timer: sends the state to every WebSocket client when the
    // mix changed, wherever it changed (desktop UI, OSC, MIDI, hotkeys, another phone).
    void broadcastStateIfChanged();

    std::function<void (bool)> onRecord;
    std::function<void()> onSaveReplay;
    std::function<void (int scene)> onRecallScene;

private:
    struct Client
    {
        std::unique_ptr<juce::StreamingSocket> socket;
        std::mutex sendLock;
        bool isWebSocket = false;
    };

    // Connection threads still running. Shared with the threads themselves, so the last
    // thread can sign off safely even while the server is being destroyed.
    struct ThreadCount
    {
        std::mutex lock;
        std::condition_variable done;
        int active = 0;
    };

    void listenLoop();
    void handleClient (std::shared_ptr<Client>);
    static bool originAllowed (const juce::String& head);
    void removeClient (const std::shared_ptr<Client>&);
    void refreshCachedState();
    juce::String getCachedState() const;
    RemoteCallbacks makeSafeCallbacks() const;
    bool sendText (Client&, const juce::String& text);
    void broadcast (const juce::String& text);
    juce::String httpResponse (int code, const juce::String& contentType, const juce::String& body) const;
    static juce::String phonePage();

    MixerEngine& engine;
    std::atomic<bool> running { false };
    std::atomic<bool> shouldStop { false };
    int port = 0;
    int lastExternalChanges = -1;
    int stateRefreshTicks = 0;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);

    std::unique_ptr<juce::StreamingSocket> listener;
    std::thread listenerThread;
    std::mutex clientsLock;
    std::vector<std::shared_ptr<Client>> clients;
    std::shared_ptr<ThreadCount> threads = std::make_shared<ThreadCount>();
    mutable std::mutex stateLock;
    juce::String cachedStateJson;
    juce::String lastBroadcastMix;   // state without meters, as last sent (message thread)
};
