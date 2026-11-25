#pragma once

#include <JuceHeader.h>
#include "engine/MixerEngine.h"
#include "app/RemoteCommands.h"

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
// start recordings while it is on.
class RemoteServer
{
public:
    explicit RemoteServer (MixerEngine&);
    ~RemoteServer();

    bool start (int port, juce::String& error);
    void stop();
    bool isRunning() const noexcept { return running.load(); }
    int getPort() const noexcept { return port; }

    // Called from the UI timer: sends the state to every WebSocket client
    // when something outside the UI changed the mix.
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

    void listenLoop();
    void handleClient (std::shared_ptr<Client>);
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
    mutable std::mutex stateLock;
    juce::String cachedStateJson;
};
