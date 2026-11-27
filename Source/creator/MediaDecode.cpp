#include "creator/MediaDecode.h"

#ifndef NOMINMAX
 #define NOMINMAX
#endif
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#pragma comment (lib, "mfplat.lib")
#pragma comment (lib, "mfreadwrite.lib")
#pragma comment (lib, "mfuuid.lib")
#pragma comment (lib, "ole32.lib")

using Microsoft::WRL::ComPtr;

namespace
{
// COM on the calling thread (once per thread), then Media Foundation (once per process).
bool startMediaFoundation()
{
    struct Com
    {
        Com() : ok (SUCCEEDED (CoInitializeEx (nullptr, COINIT_MULTITHREADED))) {}
        ~Com() { if (ok) CoUninitialize(); }
        bool ok;   // false on the message thread, which already runs COM: that's fine
    };
    thread_local Com com;
    static const bool mf = SUCCEEDED (MFStartup (MF_VERSION, MFSTARTUP_LITE));
    return mf;
}

// A source reader on the file's first audio (32-bit float) or video (RGB32) stream.
ComPtr<IMFSourceReader> openReader (const juce::File& file, bool video)
{
    if (! file.existsAsFile() || ! startMediaFoundation())
        return {};

    ComPtr<IMFAttributes> attrs;
    if (video && (FAILED (MFCreateAttributes (&attrs, 1))
                  || FAILED (attrs->SetUINT32 (MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE))))
        return {};

    ComPtr<IMFSourceReader> reader;
    if (FAILED (MFCreateSourceReaderFromURL (file.getFullPathName().toWideCharPointer(), attrs.Get(), &reader)))
        return {};

    const auto stream = (DWORD) (video ? MF_SOURCE_READER_FIRST_VIDEO_STREAM : MF_SOURCE_READER_FIRST_AUDIO_STREAM);
    ComPtr<IMFMediaType> type;
    if (FAILED (reader->SetStreamSelection ((DWORD) MF_SOURCE_READER_ALL_STREAMS, FALSE))
        || FAILED (reader->SetStreamSelection (stream, TRUE))
        || FAILED (MFCreateMediaType (&type))
        || FAILED (type->SetGUID (MF_MT_MAJOR_TYPE, video ? MFMediaType_Video : MFMediaType_Audio))
        || FAILED (type->SetGUID (MF_MT_SUBTYPE, video ? MFVideoFormat_RGB32 : MFAudioFormat_Float))
        || FAILED (reader->SetCurrentMediaType (stream, nullptr, type.Get())))
        return {};
    return reader;
}

double durationSeconds (IMFSourceReader* reader)
{
    PROPVARIANT v;
    PropVariantInit (&v);
    double seconds = 0.0;
    if (SUCCEEDED (reader->GetPresentationAttribute ((DWORD) MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &v)) && v.vt == VT_UI8)
        seconds = (double) v.uhVal.QuadPart / 1.0e7;
    PropVariantClear (&v);
    return seconds;
}

bool seekReader (IMFSourceReader* reader, double seconds)
{
    PROPVARIANT v;
    PropVariantInit (&v);
    v.vt = VT_I8;
    v.hVal.QuadPart = (LONGLONG) (juce::jmax (0.0, seconds) * 1.0e7);
    const bool ok = SUCCEEDED (reader->SetCurrentPosition (GUID_NULL, v));
    PropVariantClear (&v);
    return ok;
}

constexpr DWORD readFailed = MF_SOURCE_READERF_ENDOFSTREAM | MF_SOURCE_READERF_ERROR;

class MediaFoundationReader final : public juce::AudioFormatReader
{
public:
    explicit MediaFoundationReader (ComPtr<IMFSourceReader> r)
        : AudioFormatReader (nullptr, "Media Foundation"), reader (std::move (r)) {}

    bool init()
    {
        ComPtr<IMFMediaType> type;
        if (FAILED (reader->GetCurrentMediaType ((DWORD) MF_SOURCE_READER_FIRST_AUDIO_STREAM, &type)))
            return false;
        const auto rate = MFGetAttributeUINT32 (type.Get(), MF_MT_AUDIO_SAMPLES_PER_SECOND, 0);
        const auto channels = MFGetAttributeUINT32 (type.Get(), MF_MT_AUDIO_NUM_CHANNELS, 0);
        if (rate == 0 || channels == 0)
            return false;
        sampleRate = rate;
        numChannels = channels;
        bitsPerSample = 32;
        usesFloatingPointData = true;
        lengthInSamples = (juce::int64) (durationSeconds (reader.Get()) * rate);
        return lengthInSamples > 0;
    }

    bool readSamples (int* const* dest, int numDest, int offset, juce::int64 start, int num) override
    {
        startMediaFoundation();   // COM on the read-ahead or waveform thread
        const auto ch = (int) numChannels;
        auto silence = [&] (int n)
        {
            for (int c = 0; c < numDest; ++c)
                if (dest[c] != nullptr)
                    juce::FloatVectorOperations::clear (reinterpret_cast<float*> (dest[c]) + offset, n);
            start += n; offset += n; num -= n;
        };

        while (num > 0)
        {
            const auto end = bufStart + bufFrames;
            if (start >= bufStart && start < end)
            {
                const int n = (int) juce::jmin ((juce::int64) num, end - start);
                const float* src = buf.data() + (start - bufStart) * ch;
                for (int c = 0; c < numDest; ++c)
                    if (dest[c] != nullptr)
                    {
                        auto* d = reinterpret_cast<float*> (dest[c]) + offset;
                        if (c < ch)
                            for (int i = 0; i < n; ++i)
                                d[i] = src[i * ch + c];
                        else
                            juce::FloatVectorOperations::clear (d, n);
                    }
                start += n; offset += n; num -= n;
                continue;
            }

            // Not buffered: decode on when it's just ahead, otherwise seek there first.
            if ((start < bufStart || start > end + (juce::int64) sampleRate) && ! seekTo (start))
                break;
            if (! decodeNext())
                break;
            if (start < bufStart)
                silence ((int) juce::jmin ((juce::int64) num, bufStart - start));   // the decoder resumed just past it
        }
        if (num > 0)
            silence (num);   // end of the file, or a decode error
        return true;
    }

private:
    bool seekTo (juce::int64 sample)
    {
        bufFrames = 0;
        bufStart = sample;
        afterSeek = true;
        return seekReader (reader.Get(), (double) sample / sampleRate);
    }

    bool decodeNext()
    {
        DWORD flags = 0;
        LONGLONG time = 0;
        ComPtr<IMFSample> sample;
        if (FAILED (reader->ReadSample ((DWORD) MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &flags, &time, &sample))
            || (flags & readFailed) != 0)
            return false;
        if (sample == nullptr)
            return true;   // a gap in the stream: nothing decoded this time

        bufStart = afterSeek ? (juce::int64) std::llround ((double) time * sampleRate / 1.0e7) : bufStart + bufFrames;
        afterSeek = false;
        bufFrames = 0;

        ComPtr<IMFMediaBuffer> mb;
        BYTE* data = nullptr;
        DWORD bytes = 0;
        if (FAILED (sample->ConvertToContiguousBuffer (&mb)) || FAILED (mb->Lock (&data, nullptr, &bytes)))
            return false;
        bufFrames = (int) (bytes / (sizeof (float) * numChannels));
        const auto* f = reinterpret_cast<const float*> (data);
        buf.assign (f, f + (size_t) bufFrames * numChannels);
        mb->Unlock();
        return true;
    }

    ComPtr<IMFSourceReader> reader;
    std::vector<float> buf;   // interleaved
    juce::int64 bufStart = 0;
    int bufFrames = 0;
    bool afterSeek = true;    // take the next block's position from its timestamp
};
} // namespace

std::unique_ptr<juce::AudioFormatReader> createMediaFoundationReader (const juce::File& file)
{
    auto source = openReader (file, false);
    if (source == nullptr)
        return {};
    auto reader = std::make_unique<MediaFoundationReader> (std::move (source));
    if (! reader->init())
        return {};
    return reader;
}

//==============================================================================
struct VideoFrames::Reader
{
    ComPtr<IMFSourceReader> source;
    int width = 0, height = 0;   // the visible picture, inside the decoded frame
    LONG stride = 0;
    ComPtr<IMFSample> pending;   // decoded, but not due yet
    double pendingTime = 0.0;
    bool ended = false;
};

VideoFrames::VideoFrames() : juce::Thread ("OpenMix video frames") {}

VideoFrames::~VideoFrames()
{
    close();
}

bool VideoFrames::open (const juce::File& file)
{
    close();
    auto source = openReader (file, true);
    ComPtr<IMFMediaType> type;
    if (source == nullptr || FAILED (source->GetCurrentMediaType ((DWORD) MF_SOURCE_READER_FIRST_VIDEO_STREAM, &type)))
        return false;

    UINT32 w = 0, h = 0;
    MFGetAttributeSize (type.Get(), MF_MT_FRAME_SIZE, &w, &h);
    if (w == 0 || h == 0)
        return false;

    auto r = std::make_unique<Reader>();
    r->source = std::move (source);
    r->width = (int) w;
    r->height = (int) h;
    r->stride = (LONG) (INT32) MFGetAttributeUINT32 (type.Get(), MF_MT_DEFAULT_STRIDE, w * 4);

    // Decoders pad the frame (1080 lines come out as 1088): show only the picture.
    MFVideoArea area {};
    if (SUCCEEDED (type->GetBlob (MF_MT_MINIMUM_DISPLAY_APERTURE, (UINT8*) &area, sizeof (area), nullptr)))
    {
        r->width = juce::jlimit (1, (int) w, (int) area.Area.cx);
        r->height = juce::jlimit (1, (int) h, (int) area.Area.cy);
    }

    reader = std::move (r);
    target.store (0.0);
    startThread (juce::Thread::Priority::low);
    return true;
}

void VideoFrames::close()
{
    stopThread (2000);
    reader.reset();
    const juce::ScopedLock sl (frameLock);
    frame = {};
}

juce::Image VideoFrames::getFrame() const
{
    const juce::ScopedLock sl (frameLock);
    return frame;
}

void VideoFrames::run()
{
    startMediaFoundation();   // COM on this thread
    auto& r = *reader;
    ComPtr<IMFSample> latest;
    double lastTarget = 0.0;

    auto publish = [&] (IMFSample* sample)
    {
        ComPtr<IMFMediaBuffer> mb;
        if (FAILED (sample->GetBufferByIndex (0, &mb)))
            return;
        ComPtr<IMF2DBuffer> mb2;
        BYTE* top = nullptr;
        LONG pitch = 0;
        const bool locked2d = SUCCEEDED (mb.As (&mb2)) && SUCCEEDED (mb2->Lock2D (&top, &pitch));
        if (! locked2d)
        {
            DWORD bytes = 0;
            if (FAILED (mb->Lock (&top, nullptr, &bytes)))
                return;
            pitch = r.stride;
            if (pitch < 0)
                top += (size_t) (r.height - 1) * (size_t) -pitch;   // bottom-up: the top line is last
        }

        // The bar shows a small picture: keep every step-th pixel (about 480 wide).
        const int step = juce::jmax (1, r.width / 480);
        juce::Image img (juce::Image::ARGB, r.width / step, r.height / step, false);
        {
            juce::Image::BitmapData out (img, juce::Image::BitmapData::writeOnly);
            for (int y = 0; y < img.getHeight(); ++y)
            {
                const auto* in = reinterpret_cast<const juce::uint32*> (top + (ptrdiff_t) y * step * pitch);
                auto* line = reinterpret_cast<juce::uint32*> (out.getLinePointer (y));
                for (int x = 0; x < img.getWidth(); ++x)
                    line[x] = in[x * step] | 0xff000000u;   // BGRX to opaque ARGB
            }
        }
        locked2d ? mb2->Unlock2D() : mb->Unlock();

        const juce::ScopedLock sl (frameLock);
        frame = img;
    };

    while (! threadShouldExit())
    {
        // Jumps in the player's position are seeks; small steps are playback.
        const double t = target.load();
        if (t < lastTarget - 0.25 || t > lastTarget + 2.0)
        {
            seekReader (r.source.Get(), t);
            r.pending.Reset();
            r.ended = false;
            latest.Reset();
        }
        lastTarget = t;

        if (r.pending == nullptr && ! r.ended)
        {
            DWORD flags = 0;
            LONGLONG time = 0;
            ComPtr<IMFSample> sample;
            if (FAILED (r.source->ReadSample ((DWORD) MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, &flags, &time, &sample))
                || (flags & readFailed) != 0)
                r.ended = true;
            else if (sample != nullptr)
            {
                r.pending = std::move (sample);
                r.pendingTime = (double) time / 1.0e7;
            }
        }

        // Skip through frames that are already due without drawing them; draw the last one.
        if (r.pending != nullptr && r.pendingTime <= t)
        {
            latest = std::move (r.pending);
            continue;
        }
        if (latest != nullptr)
        {
            publish (latest.Get());
            latest.Reset();
        }
        wait (r.ended ? 100 : 10);
    }
}
