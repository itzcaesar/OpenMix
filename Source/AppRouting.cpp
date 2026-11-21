// Per-app output device (the "App volume and device preferences" setting in Windows).
// Kept apart from AppCapture.cpp because it needs the Windows Runtime headers.

#ifndef NOMINMAX
 #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <roapi.h>
#include <winstring.h>
#include <inspectable.h>
#include <wrl/client.h>

#include <string>
#include <thread>
#include <vector>

#include "AppCapture.h"

using Microsoft::WRL::ComPtr;

namespace
{
    // Undocumented: the factory behind Windows' per-app device setting. The vtable layout is the
    // one used by EarTrumpet; only the last three methods are called. Placeholder slots must stay
    // in order or the wrong method is called.
    struct IAudioPolicyConfigFactory : public IInspectable
    {
        virtual HRESULT STDMETHODCALLTYPE slot00() = 0;  // add_CtxVolumeChange
        virtual HRESULT STDMETHODCALLTYPE slot01() = 0;  // remove_CtxVolumeChanged
        virtual HRESULT STDMETHODCALLTYPE slot02() = 0;  // add_RingerVibrateStateChanged
        virtual HRESULT STDMETHODCALLTYPE slot03() = 0;  // remove_RingerVibrateStateChange
        virtual HRESULT STDMETHODCALLTYPE slot04() = 0;  // SetVolumeGroupGainForId
        virtual HRESULT STDMETHODCALLTYPE slot05() = 0;  // GetVolumeGroupGainForId
        virtual HRESULT STDMETHODCALLTYPE slot06() = 0;  // GetActiveVolumeGroupForEndpointId
        virtual HRESULT STDMETHODCALLTYPE slot07() = 0;  // GetVolumeGroupsForEndpoint
        virtual HRESULT STDMETHODCALLTYPE slot08() = 0;  // GetCurrentVolumeContext
        virtual HRESULT STDMETHODCALLTYPE slot09() = 0;  // SetVolumeGroupMuteForId
        virtual HRESULT STDMETHODCALLTYPE slot10() = 0;  // GetVolumeGroupMuteForId
        virtual HRESULT STDMETHODCALLTYPE slot11() = 0;  // SetRingerVibrateState
        virtual HRESULT STDMETHODCALLTYPE slot12() = 0;  // GetRingerVibrateState
        virtual HRESULT STDMETHODCALLTYPE slot13() = 0;  // SetPreferredChatApplication
        virtual HRESULT STDMETHODCALLTYPE slot14() = 0;  // ResetPreferredChatApplication
        virtual HRESULT STDMETHODCALLTYPE slot15() = 0;  // GetPreferredChatApplication
        virtual HRESULT STDMETHODCALLTYPE slot16() = 0;  // GetCurrentChatApplications
        virtual HRESULT STDMETHODCALLTYPE slot17() = 0;  // add_ChatContextChanged
        virtual HRESULT STDMETHODCALLTYPE slot18() = 0;  // remove_ChatContextChanged
        virtual HRESULT STDMETHODCALLTYPE SetPersistedDefaultAudioEndpoint (UINT32 processId, EDataFlow flow, ERole role, HSTRING deviceId) = 0;
        virtual HRESULT STDMETHODCALLTYPE GetPersistedDefaultAudioEndpoint (UINT32 processId, EDataFlow flow, ERole role, HSTRING* deviceId) = 0;
        virtual HRESULT STDMETHODCALLTYPE ClearAllPersistedApplicationDefaultEndpoints() = 0;
    };

    // Windows 10 21H2 / Windows 11 and later, and the older interface ID.
    constexpr GUID iidNew { 0xab3d4648, 0xe242, 0x459f, { 0xb0, 0x2f, 0x54, 0x1c, 0x70, 0x30, 0x63, 0x24 } };
    constexpr GUID iidOld { 0x2a59116d, 0x6c4f, 0x45e0, { 0xa7, 0x4f, 0x70, 0x7e, 0x3f, 0xef, 0x92, 0x58 } };

    DWORD windowsBuild()
    {
        using RtlGetVersionFn = LONG (WINAPI*) (PRTL_OSVERSIONINFOW);
        if (auto ntdll = GetModuleHandleW (L"ntdll.dll"))
            if (auto fn = reinterpret_cast<RtlGetVersionFn> (GetProcAddress (ntdll, "RtlGetVersion")))
            {
                RTL_OSVERSIONINFOW info {};
                info.dwOSVersionInfoSize = sizeof (info);
                if (fn (&info) == 0)
                    return info.dwBuildNumber;
            }
        return 0;
    }

    std::string hrText (const char* what, HRESULT hr)
    {
        char buf[128];
        sprintf_s (buf, "%s failed (0x%08lX)", what, (unsigned long) hr);
        return buf;
    }

    bool setOnThisThread (uint32_t pid, const std::wstring& deviceId, std::string& error)
    {
        static constexpr wchar_t className[] = L"Windows.Media.Internal.AudioPolicyConfig";
        HSTRING cls = nullptr;
        HRESULT hr = WindowsCreateString (className, (UINT32) (std::size (className) - 1), &cls);
        if (FAILED (hr))
        {
            error = hrText ("WindowsCreateString", hr);
            return false;
        }

        const bool newer = windowsBuild() >= 21390;
        ComPtr<IAudioPolicyConfigFactory> factory;
        hr = RoGetActivationFactory (cls, newer ? iidNew : iidOld, (void**) factory.GetAddressOf());
        if (FAILED (hr))
            hr = RoGetActivationFactory (cls, newer ? iidOld : iidNew, (void**) factory.GetAddressOf());
        WindowsDeleteString (cls);

        if (FAILED (hr) || factory == nullptr)
        {
            error = "This version of Windows doesn't let other apps choose a per-app output device ("
                  + hrText ("activation", hr) + ").";
            return false;
        }

        // The device is passed as an interface path, not the plain endpoint ID.
        HSTRING device = nullptr;   // null = back to the Windows default
        std::wstring path;
        if (! deviceId.empty())
        {
            path = L"\\\\?\\SWD#MMDEVAPI#" + deviceId + L"#{e6327cad-dcec-4949-ae8a-991e976a79d2}";
            if (FAILED (hr = WindowsCreateString (path.c_str(), (UINT32) path.size(), &device)))
            {
                error = hrText ("WindowsCreateString", hr);
                return false;
            }
        }

        for (const auto role : { eConsole, eMultimedia })
        {
            hr = factory->SetPersistedDefaultAudioEndpoint ((UINT32) pid, eRender, role, device);
            if (FAILED (hr))
                break;
        }

        if (device != nullptr)
            WindowsDeleteString (device);

        if (FAILED (hr))
        {
            error = hrText ("Setting the app's output device", hr);
            return false;
        }
        return true;
    }
}

std::vector<RenderDevice> listRenderDevices()
{
    std::vector<RenderDevice> result;
    const HRESULT init = CoInitializeEx (nullptr, COINIT_MULTITHREADED);

    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IMMDeviceCollection> devices;
    if (SUCCEEDED (CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS (&enumerator)))
        && SUCCEEDED (enumerator->EnumAudioEndpoints (eRender, DEVICE_STATE_ACTIVE, &devices)))
    {
        UINT count = 0;
        devices->GetCount (&count);
        for (UINT i = 0; i < count; ++i)
        {
            ComPtr<IMMDevice> device;
            ComPtr<IPropertyStore> props;
            LPWSTR id = nullptr;
            if (FAILED (devices->Item (i, &device)) || FAILED (device->GetId (&id)))
                continue;

            RenderDevice d;
            d.id = id;
            CoTaskMemFree (id);

            if (SUCCEEDED (device->OpenPropertyStore (STGM_READ, &props)))
            {
                PROPVARIANT name;
                PropVariantInit (&name);
                if (SUCCEEDED (props->GetValue (PKEY_Device_FriendlyName, &name)) && name.vt == VT_LPWSTR)
                    d.name = name.pwszVal;
                PropVariantClear (&name);
            }

            if (d.name.empty())
                d.name = d.id;
            result.push_back (std::move (d));
        }
    }

    if (SUCCEEDED (init))
        CoUninitialize();
    return result;
}

bool setAppOutputDevice (uint32_t pid, const std::wstring& deviceId, std::string& error)
{
    // Run on a fresh thread with its own Windows Runtime apartment, so the caller's (UI thread)
    // COM state doesn't matter.
    bool ok = false;
    std::thread worker ([&]
    {
        const HRESULT init = RoInitialize (RO_INIT_MULTITHREADED);
        ok = setOnThisThread (pid, deviceId, error);
        if (SUCCEEDED (init))
            RoUninitialize();
    });
    worker.join();
    return ok;
}
