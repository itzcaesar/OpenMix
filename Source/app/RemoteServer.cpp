#include "app/RemoteServer.h"

// SHA-1, for the WebSocket handshake (RFC 6455). JUCE 9 no longer ships a
// SHA-1 class, and the handshake needs exactly SHA-1, so this is a small
// self-contained implementation (FIPS 180-1). Test vector: SHA1("abc") =
// a9993e364706816aba3e25717850c26c9cd0d89d.
namespace
{
    struct Sha1
    {
        static juce::MemoryBlock hash (const juce::String& text)
        {
            const auto bytes = text.toRawUTF8();
            const size_t len = std::strlen (bytes);
            uint32_t h[5] { 0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u };

            auto processBlock = [&h] (const uint8_t* block)
            {
                uint32_t w[80];
                for (int i = 0; i < 16; ++i)
                    w[i] = ((uint32_t) block[i * 4] << 24) | ((uint32_t) block[i * 4 + 1] << 16)
                         | ((uint32_t) block[i * 4 + 2] << 8) | (uint32_t) block[i * 4 + 3];
                for (int i = 16; i < 80; ++i)
                {
                    const uint32_t x = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16];
                    w[i] = (x << 1) | (x >> 31);
                }
                uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
                for (int i = 0; i < 80; ++i)
                {
                    uint32_t f = 0, k = 0;
                    if (i < 20)      { f = (b & c) | (~b & d);            k = 0x5A827999u; }
                    else if (i < 40) { f = b ^ c ^ d;                     k = 0x6ED9EBA1u; }
                    else if (i < 60) { f = (b & c) | (b & d) | (c & d);   k = 0x8F1BBCDCu; }
                    else              { f = b ^ c ^ d;                     k = 0xCA62C1D6u; }
                    const uint32_t temp = ((a << 5) | (a >> 27)) + f + e + k + w[i];
                    e = d; d = c; c = (b << 30) | (b >> 2); b = a; a = temp;
                }
                h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
            };

            // Padded message: data, 0x80, zeros, 64-bit big-endian bit length.
            std::vector<uint8_t> msg (bytes, bytes + len);
            const uint64_t bitLen = (uint64_t) len * 8;
            msg.push_back (0x80);
            while (msg.size() % 64 != 56)
                msg.push_back (0);
            for (int i = 7; i >= 0; --i)
                msg.push_back ((uint8_t) (bitLen >> (i * 8)));
            for (size_t i = 0; i < msg.size(); i += 64)
                processBlock (msg.data() + i);

            juce::MemoryBlock out;
            out.setSize (20);
            for (int i = 0; i < 5; ++i)
                for (int j = 0; j < 4; ++j)
                    ((uint8_t*) out.getData())[i * 4 + j] = (uint8_t) (h[i] >> (24 - j * 8));
            return out;
        }
    };

    bool readExact (juce::StreamingSocket& s, void* dest, int numBytes)
    {
        auto* p = (uint8_t*) dest;
        int got = 0;
        while (got < numBytes)
        {
            const int n = s.read (p + got, numBytes - got, true);
            if (n <= 0)
                return false;
            got += n;
        }
        return true;
    }

    juce::String headerValue (const juce::String& headers, const juce::String& name)
    {
        for (auto line : juce::StringArray::fromLines (headers))
        {
            if (line.startsWithIgnoreCase (name + ":"))
                return line.fromFirstOccurrenceOf (":", false, false).trim();
        }
        return {};
    }
}

RemoteServer::RemoteServer (MixerEngine& e) : engine (e) {}
RemoteServer::~RemoteServer() { stop(); }

bool RemoteServer::start (int newPort, juce::String& error)
{
    stop();
    listener = std::make_unique<juce::StreamingSocket>();
    if (! listener->createListener (newPort, "0.0.0.0"))
    {
        error = "Couldn't listen on TCP port " + juce::String (newPort) + ". Another program may be using it.";
        listener.reset();
        return false;
    }
    port = newPort;
    shouldStop.store (false);
    running.store (true);
    listenerThread = std::thread ([this] { listenLoop(); });
    juce::Logger::writeToLog ("Remote server listening on http://0.0.0.0:" + juce::String (newPort) + "/");
    return true;
}

void RemoteServer::stop()
{
    if (! running.load() && listenerThread.joinable() == false)
        return;
    shouldStop.store (true);
    running.store (false);
    // Wake the listener thread out of accept() with a dummy connection, so
    // the join below can't hang: closing a listening socket from another
    // thread does not reliably unblock accept() on Windows.
    if (port > 0)
    {
        juce::StreamingSocket wakeup;
        if (wakeup.connect ("127.0.0.1", port, 500))
            wakeup.close();
    }
    if (listenerThread.joinable())
        listenerThread.join();
    if (listener != nullptr)
        listener->close();
    {
        const std::lock_guard<std::mutex> sl (clientsLock);
        for (auto& c : clients)
            if (c->socket != nullptr)
                c->socket->close();
        clients.clear();
    }
    listener.reset();
}

void RemoteServer::listenLoop()
{
    while (! shouldStop.load())
    {
        auto* raw = listener != nullptr ? listener->waitForNextConnection() : nullptr;
        if (shouldStop.load())
        {
            delete raw;
            break;
        }
        if (raw == nullptr)
        {
            juce::Thread::sleep (50);
            continue;
        }
        auto client = std::make_shared<Client>();
        client->socket.reset (raw);
        {
            const std::lock_guard<std::mutex> sl (clientsLock);
            clients.push_back (client);
        }
        std::thread ([this, client] { handleClient (client); }).detach();
    }
}

bool RemoteServer::sendText (Client& client, const juce::String& text)
{
    if (client.socket == nullptr || ! client.socket->isConnected())
        return false;
    const auto bytes = text.toRawUTF8();
    const size_t len = std::strlen (bytes);
    std::vector<uint8_t> frame;
    frame.push_back (0x81);   // FIN + text opcode
    if (len < 126)
        frame.push_back ((uint8_t) len);
    else if (len < 65536)
    {
        frame.push_back (126);
        frame.push_back ((uint8_t) (len >> 8));
        frame.push_back ((uint8_t) len);
    }
    else
    {
        frame.push_back (127);
        for (int i = 7; i >= 0; --i)
            frame.push_back ((uint8_t) (len >> (i * 8)));
    }
    frame.insert (frame.end(), bytes, bytes + len);
    return client.socket->write (frame.data(), (int) frame.size()) == (int) frame.size();
}

void RemoteServer::broadcast (const juce::String& text)
{
    const std::lock_guard<std::mutex> sl (clientsLock);
    for (auto& c : clients)
        if (c->isWebSocket)
            sendText (*c, text);
}

void RemoteServer::broadcastStateIfChanged()
{
    if (! running.load())
        return;
    const int changes = engine.externalChanges.load();
    if (changes == lastExternalChanges)
        return;
    lastExternalChanges = changes;
    broadcast ("{\"type\":\"state\",\"state\":" + remoteStateJson (engine) + "}");
}

juce::String RemoteServer::httpResponse (int code, const juce::String& contentType, const juce::String& body) const
{
    const juce::String status = code == 200 ? "OK" : code == 404 ? "Not Found" : "Bad Request";
    return "HTTP/1.1 " + juce::String (code) + " " + status + "\r\n"
         + "Content-Type: " + contentType + "\r\n"
         + "Content-Length: " + juce::String (body.getNumBytesAsUTF8()) + "\r\n"
         + "Connection: close\r\n\r\n" + body;
}

void RemoteServer::handleClient (std::shared_ptr<Client> client)
{
    auto& socket = *client->socket;

    // Read the HTTP request head (up to 8 KB).
    juce::String head;
    {
        char buf[1024];
        while (head.length() < 8192 && ! head.contains ("\r\n\r\n"))
        {
            const int n = socket.read (buf, (int) sizeof (buf) - 1, true);
            if (n <= 0)
                return;
            buf[n] = '\0';
            head += juce::String (buf, (size_t) n);
        }
    }
    const auto firstLine = head.upToFirstOccurrenceOf ("\r\n", false, false);
    const auto method = firstLine.upToFirstOccurrenceOf (" ", false, false);
    const auto path = firstLine.fromFirstOccurrenceOf (" ", false, false).upToFirstOccurrenceOf (" ", false, false);

    // WebSocket upgrade.
    if (headerValue (head, "Upgrade").equalsIgnoreCase ("websocket"))
    {
        const auto key = headerValue (head, "Sec-WebSocket-Key");
        if (key.isEmpty())
            return;
        const auto hash = Sha1::hash (key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
        const auto accept = juce::Base64::toBase64 (hash.getData(), hash.getSize());
        const juce::String response =
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Accept: " + accept + "\r\n\r\n";
        if (socket.write (response.toRawUTF8(), (int) response.getNumBytesAsUTF8()) <= 0)
            return;
        client->isWebSocket = true;
        sendText (*client, "{\"type\":\"state\",\"state\":" + remoteStateJson (engine) + "}");

        // Frame loop: masked client frames, text only, one message per frame.
        while (! shouldStop.load() && socket.isConnected())
        {
            uint8_t hdr[2];
            if (! readExact (socket, hdr, 2))
                break;
            const int opcode = hdr[0] & 0x0F;
            const bool masked = (hdr[1] & 0x80) != 0;
            uint64_t len = hdr[1] & 0x7F;
            if (len == 126)
            {
                uint8_t ext[2];
                if (! readExact (socket, ext, 2)) break;
                len = ((uint64_t) ext[0] << 8) | ext[1];
            }
            else if (len == 127)
            {
                uint8_t ext[8];
                if (! readExact (socket, ext, 8)) break;
                len = 0;
                for (int i = 0; i < 8; ++i) len = (len << 8) | ext[i];
            }
            if (len > 1024 * 1024)
                break;
            uint8_t mask[4] { 0, 0, 0, 0 };
            if (masked && ! readExact (socket, mask, 4))
                break;
            std::vector<uint8_t> payload ((size_t) len);
            if (len > 0 && ! readExact (socket, payload.data(), (int) len))
                break;
            if (masked)
                for (size_t i = 0; i < payload.size(); ++i)
                    payload[i] ^= mask[i % 4];

            if (opcode == 8)   // close
                break;
            if (opcode == 9)   // ping -> pong
            {
                std::vector<uint8_t> pong { 0x8A, (uint8_t) len };
                pong.insert (pong.end(), payload.begin(), payload.end());
                socket.write (pong.data(), (int) pong.size());
                continue;
            }
            if (opcode != 1)
                continue;

            const juce::String text ((const char*) payload.data(), payload.size());
            juce::var parsed;
            if (juce::JSON::parse (text, parsed).failed() || ! parsed.isObject())
                continue;
            auto* obj = parsed.getDynamicObject();
            if (obj == nullptr)
                continue;
            RemoteCallbacks cb;
            cb.onRecord = onRecord;
            cb.onSaveReplay = onSaveReplay;
            cb.onRecallScene = onRecallScene;
            const bool ok = applyRemoteCommand (engine, obj->getProperty ("path").toString(),
                                                (float) obj->getProperty ("value"), cb);
            sendText (*client, ok ? "{\"type\":\"ok\"}" : "{\"type\":\"error\",\"error\":\"unknown path\"}");
            if (ok)
                broadcastStateIfChanged();
        }
        return;
    }

    // Plain HTTP.
    juce::String body, contentType = "text/plain";
    int code = 200;
    if (method == "GET" && path == "/")
    {
        body = phonePage();
        contentType = "text/html";
    }
    else if (method == "GET" && path == "/api/state")
    {
        body = remoteStateJson (engine);
        contentType = "application/json";
    }
    else if (method == "POST" && path == "/api/command")
    {
        const int contentLength = headerValue (head, "Content-Length").getIntValue();
        juce::String jsonBody = head.fromFirstOccurrenceOf ("\r\n\r\n", false, false);
        while ((int) jsonBody.getNumBytesAsUTF8() < contentLength)
        {
            char buf[1024];
            const int n = socket.read (buf, (int) sizeof (buf) - 1, true);
            if (n <= 0) break;
            buf[n] = '\0';
            jsonBody += juce::String (buf, (size_t) n);
        }
        juce::var parsed;
        bool ok = false;
        if (! juce::JSON::parse (jsonBody, parsed).failed() && parsed.isObject())
        {
            auto* obj = parsed.getDynamicObject();
            RemoteCallbacks cb;
            cb.onRecord = onRecord;
            cb.onSaveReplay = onSaveReplay;
            cb.onRecallScene = onRecallScene;
            ok = obj != nullptr && applyRemoteCommand (engine, obj->getProperty ("path").toString(),
                                                       (float) obj->getProperty ("value"), cb);
        }
        body = ok ? "{\"ok\":true}" : "{\"ok\":false}";
        contentType = "application/json";
        if (ok)
            broadcastStateIfChanged();
    }
    else
    {
        code = 404;
        body = "Not found. Try / for the phone remote, or /api/state.";
    }

    const auto response = httpResponse (code, contentType, body);
    socket.write (response.toRawUTF8(), (int) response.getNumBytesAsUTF8());
}

juce::String RemoteServer::phonePage()
{
    return R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>OpenMix remote</title>
<style>body{font-family:system-ui;background:#16181d;color:#eee;margin:0;padding:12px}
h1{font-size:18px;margin:4px 0 12px}h2{font-size:14px;color:#aaa;margin:18px 0 6px}
.row{display:flex;align-items:center;gap:8px;margin:6px 0}
.row span{width:90px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
input[type=range]{flex:1}button{background:#2a2e38;color:#eee;border:1px solid #444;border-radius:6px;padding:8px 12px}
button.on{background:#c98a2b;color:#111}.pad{width:22%;height:44px;margin:2px}
#status{color:#888;font-size:12px}</style></head>
<body><h1>OpenMix remote</h1><div id="status">connecting…</div>
<h2>Strips</h2><div id="strips"></div><h2>Buses</h2><div id="buses"></div>
<h2>Soundboard</h2><div id="pads"></div><h2>Scenes</h2><div id="scenes"></div>
<script>
let ws=null,state=null;
function cmd(path,value){const m=JSON.stringify({path,value});
 if(ws&&ws.readyState===1)ws.send(m);
 else fetch('/api/command',{method:'POST',headers:{'Content-Type':'application/json'},body:m});}
function render(){if(!state)return;
 const s=document.getElementById('strips');s.innerHTML='';
 state.strips.forEach(st=>{const d=document.createElement('div');d.className='row';
  d.innerHTML='<span>'+st.name+'</span><input type="range" min="-60" max="12" step="0.5" value="'+st.faderDb+'">'
   +'<button class="'+(st.mute?'on':'')+'">Mute</button>';
  d.querySelector('input').oninput=e=>cmd('/openmix/strip/'+st.index+'/fader',parseFloat(e.target.value));
  d.querySelector('button').onclick=()=>cmd('/openmix/strip/'+st.index+'/mute',st.mute?0:1);
  s.appendChild(d);});
 const b=document.getElementById('buses');b.innerHTML='';
 state.buses.forEach(bs=>{const d=document.createElement('div');d.className='row';
  d.innerHTML='<span>'+bs.name+'</span><input type="range" min="-60" max="12" step="0.5" value="'+bs.faderDb+'">'
   +'<button class="'+(bs.mute?'on':'')+'">Mute</button>';
  d.querySelector('input').oninput=e=>cmd('/openmix/bus/'+bs.name+'/fader',parseFloat(e.target.value));
  d.querySelector('button').onclick=()=>cmd('/openmix/bus/'+bs.name+'/mute',bs.mute?0:1);
  b.appendChild(d);});
 const p=document.getElementById('pads');p.innerHTML='';
 for(let i=1;i<=8;i++){const btn=document.createElement('button');btn.className='pad';btn.textContent='Pad '+i;
  btn.onclick=()=>cmd('/openmix/soundboard/'+i+'/play',1);p.appendChild(btn);}
 const sc=document.getElementById('scenes');sc.innerHTML='';
 state.scenes.forEach((name,i)=>{const btn=document.createElement('button');btn.textContent=name;
  btn.onclick=()=>cmd('/openmix/scene/'+(i+1),1);sc.appendChild(btn);});
 document.getElementById('status').textContent='connected';}
function poll(){fetch('/api/state').then(r=>r.json()).then(j=>{state=j;render();});}
try{ws=new WebSocket('ws://'+location.host+'/ws');
 ws.onmessage=e=>{const m=JSON.parse(e.data);if(m.type==='state'){state=m.state;render();}};
 ws.onclose=()=>{document.getElementById('status').textContent='WebSocket closed, polling';setInterval(poll,1000);};
 ws.onerror=()=>{setInterval(poll,1000);};}
catch(e){setInterval(poll,1000);}
setInterval(()=>{if(!ws||ws.readyState!==1)poll();},2000);poll();
</script></body></html>)HTML";
}
