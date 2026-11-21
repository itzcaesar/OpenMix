#ifndef NOMINMAX
 #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <mmreg.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <audiopolicy.h>
#include <avrt.h>
#include <wrl/client.h>
#include <wrl/implements.h>

#include <algorithm>
#include <atomic>
#include <cwctype>
#include <future>
#include <thread>

#include "engine/AppCapture.h"
#include "engine/ClockBridge.h"

using Microsoft::WRL::ComPtr;

namespace
{
    // Initializes COM for the current thread if it isn't already (any apartment is fine).
    struct ComScope
    {
        ComScope()  { const HRESULT hr = CoInitializeEx (nullptr, COINIT_MULTITHREADED); owns = SUCCEEDED (hr); }
        ~ComScope() { if (owns) CoUninitialize(); }
        bool owns = false;
    };

    std::wstring exeNameForPid (DWORD pid)
    {
        std::wstring result;
        if (HANDLE h = OpenProcess (PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid))
        {
            wchar_t path[MAX_PATH] {};
            DWORD size = MAX_PATH;
            if (QueryFullProcessImageNameW (h, 0, path, &size))
            {
                std::wstring full (path, size);
                const auto slash = full.find_last_of (L"\\/");
                result = slash == std::wstring::npos ? full : full.substr (slash + 1);
            }
            CloseHandle (h);
        }
        return result;
    }

    bool equalsIgnoreCase (const std::wstring& a, const std::wstring& b)
    {
        return a.size() == b.size()
            && std::equal (a.begin(), a.end(), b.begin(),
                           [] (wchar_t x, wchar_t y) { return std::towlower (x) == std::towlower (y); });
    }

    class ActivationHandler
        : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
                                              Microsoft::WRL::FtmBase,
                                              IActivateAudioInterfaceCompletionHandler>
    {
    public:
        ActivationHandler()  { done = CreateEventW (nullptr, TRUE, FALSE, nullptr); }
        ~ActivationHandler() override { if (done != nullptr) CloseHandle (done); }

        STDMETHOD (ActivateCompleted) (IActivateAudioInterfaceAsyncOperation* op) override
        {
            HRESULT activateResult = E_FAIL;
            ComPtr<IUnknown> unknown;
            const HRESULT hr = op->GetActivateResult (&activateResult, &unknown);

            if (FAILED (hr))                  result = hr;
            else if (FAILED (activateResult)) result = activateResult;
            else                              result = unknown.As (&client);

            SetEvent (done);
            return S_OK;
        }

        HANDLE done = nullptr;
        HRESULT result = E_FAIL;
        ComPtr<IAudioClient> client;
    };

    std::string hrToString (const char* what, HRESULT hr)
    {
        char buf[96];
        sprintf_s (buf, "%s failed (0x%08lX)", what, (unsigned long) hr);
        return buf;
    }
}

//==============================================================================
std::vector<AudioApp> listAudioApps()
{
    ComScope com;
    std::vector<AudioApp> apps;

    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED (CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS (&enumerator))))
        return apps;

    ComPtr<IMMDeviceCollection> devices;
    if (FAILED (enumerator->EnumAudioEndpoints (eRender, DEVICE_STATE_ACTIVE, &devices)))
        return apps;

    UINT numDevices = 0;
    devices->GetCount (&numDevices);
    const DWORD self = GetCurrentProcessId();

    for (UINT d = 0; d < numDevices; ++d)
    {
        ComPtr<IMMDevice> device;
        ComPtr<IAudioSessionManager2> manager;
        ComPtr<IAudioSessionEnumerator> sessions;

        if (FAILED (devices->Item (d, &device))
            || FAILED (device->Activate (__uuidof (IAudioSessionManager2), CLSCTX_ALL, nullptr, (void**) manager.GetAddressOf()))
            || FAILED (manager->GetSessionEnumerator (&sessions)))
            continue;

        int numSessions = 0;
        sessions->GetCount (&numSessions);

        for (int i = 0; i < numSessions; ++i)
        {
            ComPtr<IAudioSessionControl> control;
            ComPtr<IAudioSessionControl2> control2;
            if (FAILED (sessions->GetSession (i, &control)) || FAILED (control.As (&control2)))
                continue;

            if (control2->IsSystemSoundsSession() == S_OK)
                continue;

            AudioSessionState state = AudioSessionStateExpired;
            control2->GetState (&state);
            if (state == AudioSessionStateExpired)
                continue;

            DWORD pid = 0;
            if (FAILED (control2->GetProcessId (&pid)) || pid == 0 || pid == self)
                continue;

            auto existing = std::find_if (apps.begin(), apps.end(), [pid] (const AudioApp& a) { return a.pid == pid; });
            if (existing != apps.end())
            {
                existing->active = existing->active || state == AudioSessionStateActive;
                continue;
            }

            AudioApp app;
            app.pid = pid;
            app.exeName = exeNameForPid (pid);
            app.active = state == AudioSessionStateActive;
            if (! app.exeName.empty())
                apps.push_back (std::move (app));
        }
    }

    std::sort (apps.begin(), apps.end(), [] (const AudioApp& a, const AudioApp& b)
    {
        return _wcsicmp (a.exeName.c_str(), b.exeName.c_str()) < 0;
    });

    return apps;
}

uint32_t findAudioAppByExe (const std::wstring& exeName)
{
    uint32_t best = 0;
    for (const auto& app : listAudioApps())
        if (equalsIgnoreCase (app.exeName, exeName))
        {
            if (app.active)
                return app.pid;
            if (best == 0)
                best = app.pid;
        }
    return best;
}

bool isProcessAlive (uint32_t pid)
{
    HANDLE h = OpenProcess (SYNCHRONIZE, FALSE, pid);
    if (h == nullptr)
        return false;
    const bool alive = WaitForSingleObject (h, 0) == WAIT_TIMEOUT;
    CloseHandle (h);
    return alive;
}

//==============================================================================
struct AppCapture::Impl
{
    std::thread thread;
    std::atomic<bool> stopRequested { false };
    std::atomic<bool> failed { false };

    // Device loopback: an ordinary audio client on the render endpoint, opened in loopback mode.
    static HRESULT activateDevice (const std::wstring& endpointId, ComPtr<IAudioClient>& client)
    {
        ComPtr<IMMDeviceEnumerator> enumerator;
        HRESULT hr = CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS (&enumerator));
        if (FAILED (hr))
            return hr;

        ComPtr<IMMDevice> device;
        if (FAILED (hr = enumerator->GetDevice (endpointId.c_str(), &device)))
            return hr;

        return device->Activate (__uuidof (IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**> (client.GetAddressOf()));
    }

    void run (uint32_t pid, std::wstring endpointId, ClockBridge* target, std::promise<std::string> ready)
    {
        ComScope com;
        const bool deviceMode = ! endpointId.empty();
        ComPtr<IAudioClient> client;
        HRESULT hr = S_OK;

        if (deviceMode)
        {
            if (FAILED (hr = activateDevice (endpointId, client)))
                return ready.set_value (hrToString ("Opening the output device", hr));
        }
        else
        {
        // 1. Activate a process-loopback audio client for the target process tree.
        AUDIOCLIENT_ACTIVATION_PARAMS params {};
        params.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
        params.ProcessLoopbackParams.TargetProcessId = pid;
        params.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;

        PROPVARIANT pv {};
        pv.vt = VT_BLOB;
        pv.blob.cbSize = sizeof (params);
        pv.blob.pBlobData = reinterpret_cast<BYTE*> (&params);

        auto handler = Microsoft::WRL::Make<ActivationHandler>();
        ComPtr<IActivateAudioInterfaceAsyncOperation> op;
        hr = ActivateAudioInterfaceAsync (VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof (IAudioClient),
                                                  &pv, handler.Get(), &op);
        if (FAILED (hr))
            return ready.set_value (hrToString ("ActivateAudioInterfaceAsync", hr));

        // Don't let a stuck activation hang the app (the handler is ref-counted, so it's safe
        // for Windows to complete it later).
        if (WaitForSingleObject (handler->done, 3000) != WAIT_OBJECT_0)
            return ready.set_value ("Windows didn't respond to the app-capture request in time");
        if (FAILED (handler->result))
            return ready.set_value (hrToString ("Process loopback activation", handler->result));

        client = handler->client;
        }

        // 2. Ask for 48 kHz stereo float (Windows converts); fall back to 16-bit, and for a
        //    device, to its own mix format (any rate and channel count, float).
        WAVEFORMATEX fmt {};
        fmt.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
        fmt.nChannels = 2;
        fmt.nSamplesPerSec = (DWORD) AppCapture::sampleRate;
        fmt.wBitsPerSample = 32;
        fmt.nBlockAlign = (WORD) (fmt.nChannels * fmt.wBitsPerSample / 8);
        fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;

        const DWORD flags = AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK
                          | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
        double captureRate = AppCapture::sampleRate;
        int channels = 2;
        const REFERENCE_TIME bufferDuration = 200000;   // 20 ms

        bool isFloat = true;
        hr = client->Initialize (AUDCLNT_SHAREMODE_SHARED, flags, bufferDuration, 0, &fmt, nullptr);
        if (FAILED (hr))
        {
            isFloat = false;
            fmt.wFormatTag = WAVE_FORMAT_PCM;
            fmt.wBitsPerSample = 16;
            fmt.nBlockAlign = (WORD) (fmt.nChannels * fmt.wBitsPerSample / 8);
            fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;
            hr = client->Initialize (AUDCLNT_SHAREMODE_SHARED, flags, bufferDuration, 0, &fmt, nullptr);
        }
        if (FAILED (hr) && deviceMode)
        {
            // Some drivers refuse conversion in loopback: take the device's own (float) format.
            WAVEFORMATEX* mix = nullptr;
            if (SUCCEEDED (client->GetMixFormat (&mix)) && mix != nullptr)
            {
                const bool mixFloat = mix->wFormatTag == WAVE_FORMAT_IEEE_FLOAT
                    || (mix->wFormatTag == WAVE_FORMAT_EXTENSIBLE
                        && reinterpret_cast<WAVEFORMATEXTENSIBLE*> (mix)->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
                if (mixFloat && mix->wBitsPerSample == 32)
                {
                    hr = client->Initialize (AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                             bufferDuration, 0, mix, nullptr);
                    if (SUCCEEDED (hr))
                    {
                        isFloat = true;
                        captureRate = (double) mix->nSamplesPerSec;
                        channels = std::max (1, (int) mix->nChannels);
                    }
                }
                CoTaskMemFree (mix);
            }
        }
        if (FAILED (hr))
            return ready.set_value (hrToString ("IAudioClient::Initialize", hr));

        HANDLE event = CreateEventW (nullptr, FALSE, FALSE, nullptr);
        ComPtr<IAudioCaptureClient> capture;
        if (FAILED (hr = client->SetEventHandle (event))
            || FAILED (hr = client->GetService (IID_PPV_ARGS (&capture)))
            || FAILED (hr = client->Start()))
        {
            CloseHandle (event);
            return ready.set_value (hrToString ("Starting capture", hr));
        }

        ready.set_value ({});   // success

        // 3. Capture loop
        DWORD taskIndex = 0;
        HANDLE mmcss = AvSetMmThreadCharacteristicsW (L"Pro Audio", &taskIndex);

        std::vector<float> left (4096), right (4096);
        int idleWaits = 0;
        while (! stopRequested.load() && ! failed.load())
        {
            // Windows sends nothing while the device (or app) is silent. Keep the bridge fed
            // with silence then, so a quiet source doesn't count as dropouts.
            if (WaitForSingleObject (event, 10) != WAIT_OBJECT_0)
            {
                if (++idleWaits >= 3)
                {
                    const int frames = (int) (captureRate * 0.01);
                    if ((size_t) frames > left.size()) { left.resize ((size_t) frames); right.resize ((size_t) frames); }
                    std::fill (left.begin(), left.begin() + frames, 0.0f);
                    std::fill (right.begin(), right.begin() + frames, 0.0f);
                    const float* silent[2] { left.data(), right.data() };
                    target->write (silent, 2, frames, captureRate);
                }
                continue;
            }
            idleWaits = 0;

            UINT32 packet = 0;
            for (;;)
            {
                if (FAILED (capture->GetNextPacketSize (&packet)))
                {
                    failed = true;   // stream invalidated; the engine will reconnect
                    break;
                }
                if (packet == 0)
                    break;

                BYTE* data = nullptr;
                UINT32 frames = 0;
                DWORD bufferFlags = 0;
                if (FAILED (capture->GetBuffer (&data, &frames, &bufferFlags, nullptr, nullptr)))
                {
                    failed = true;
                    break;
                }

                if (frames > left.size())
                {
                    left.resize (frames);
                    right.resize (frames);
                }

                if ((bufferFlags & AUDCLNT_BUFFERFLAGS_SILENT) != 0 || data == nullptr)
                {
                    std::fill (left.begin(), left.begin() + frames, 0.0f);
                    std::fill (right.begin(), right.begin() + frames, 0.0f);
                }
                else if (isFloat)
                {
                    // Interleaved; the first two channels (a mono device feeds both sides).
                    const auto* f = reinterpret_cast<const float*> (data);
                    const int rightCh = channels > 1 ? 1 : 0;
                    for (UINT32 i = 0; i < frames; ++i) { left[i] = f[channels * i]; right[i] = f[channels * i + rightCh]; }
                }
                else
                {
                    const auto* s = reinterpret_cast<const int16_t*> (data);
                    for (UINT32 i = 0; i < frames; ++i) { left[i] = s[2 * i] / 32768.0f; right[i] = s[2 * i + 1] / 32768.0f; }
                }

                const float* chans[2] { left.data(), right.data() };
                target->write (chans, 2, (int) frames, captureRate);
                capture->ReleaseBuffer (frames);
            }
        }

        client->Stop();
        if (mmcss != nullptr)
            AvRevertMmThreadCharacteristics (mmcss);
        CloseHandle (event);
    }
};

AppCapture::AppCapture() : impl (std::make_unique<Impl>()) {}

AppCapture::~AppCapture()
{
    stop();
}

bool AppCapture::hasFailed() const noexcept
{
    return impl->failed.load();
}

bool AppCapture::start (uint32_t pid, ClockBridge& target, std::string& error)
{
    return launch (pid, {}, target, error);
}

bool AppCapture::startDevice (const std::wstring& endpointId, ClockBridge& target, std::string& error)
{
    return launch (0, endpointId, target, error);
}

bool AppCapture::launch (uint32_t pid, const std::wstring& endpointId, ClockBridge& target, std::string& error)
{
    stop();
    impl->stopRequested = false;
    impl->failed = false;

    std::promise<std::string> ready;
    auto result = ready.get_future();
    impl->thread = std::thread ([this, pid, endpointId, &target, p = std::move (ready)]() mutable
    {
        impl->run (pid, endpointId, &target, std::move (p));
    });

    error = result.get();
    if (! error.empty())
    {
        impl->thread.join();
        return false;
    }
    return true;
}

void AppCapture::stop()
{
    impl->stopRequested = true;
    if (impl->thread.joinable())
        impl->thread.join();
}
