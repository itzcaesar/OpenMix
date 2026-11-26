#include "creator/Recorder.h"

static std::unique_ptr<juce::AudioFormatWriter> openWav (const juce::File& file, double sampleRate)
{
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
    if (static_cast<juce::FileOutputStream*> (stream.get())->failedToOpen())
        return {};

    return juce::WavAudioFormat().createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                               .withSampleRate (sampleRate)
                                                               .withNumChannels (2)
                                                               .withBitsPerSample (24));
}

//==============================================================================
// Recorder

Recorder::Recorder()
{
    diskThread.startThread (juce::Thread::Priority::normal);
}

Recorder::~Recorder()
{
    stop();
    diskThread.stopThread (5000);
}

juce::File Recorder::getRecordingsFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("OpenMix Recordings");
}

bool Recorder::start (const std::vector<Target>& targets, double sampleRate, juce::String& error)
{
    stop();

    // A new folder every time: a second start in the same second must not reuse (and wipe) the last one.
    const auto folder = getRecordingsFolder().getNonexistentChildFile (juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S"), {}, true);
    if (! folder.createDirectory())
    {
        error = "Couldn't create " + folder.getFullPathName();
        return false;
    }

    std::vector<Slot> opened;
    for (const auto& t : targets)
    {
        if ((int) opened.size() >= maxTargets)
            break;

        const auto file = folder.getChildFile (juce::File::createLegalFileName (t.name) + ".wav");
        auto writer = openWav (file, sampleRate);
        if (writer == nullptr)
        {
            error = "Couldn't write " + file.getFullPathName();
            continue;
        }

        Slot s;
        s.isBus = t.isBus;
        s.index = t.index;
        // ~3 s of FIFO: plenty for a slow disk.
        s.writer = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (writer.release(), diskThread, (int) (sampleRate * 3));
        opened.push_back (std::move (s));
    }

    if (opened.empty())
    {
        if (error.isEmpty())
            error = "Nothing selected to record.";
        return false;
    }

    {
        const juce::ScopedLock sl (lock);
        slots = std::move (opened);
    }
    lastFolder = folder;
    startedAtMs = juce::Time::getMillisecondCounter();
    startedAtRate = sampleRate;
    recording = true;
    return true;
}

void Recorder::stop()
{
    std::vector<Slot> old;
    {
        const juce::ScopedLock sl (lock);
        std::swap (old, slots);
    }
    recording = false;
    old.clear();   // deleting a ThreadedWriter flushes and closes its file
}

double Recorder::getElapsedSeconds() const
{
    return recording.load() ? (juce::Time::getMillisecondCounter() - startedAtMs) / 1000.0 : 0.0;
}

void Recorder::write (bool isBus, int index, const float* left, const float* right, int numSamples) noexcept
{
    if (! recording.load (std::memory_order_relaxed))
        return;

    const juce::ScopedTryLock sl (lock);
    if (! sl.isLocked())
        return;

    for (auto& s : slots)
        if (s.isBus == isBus && s.index == index && s.writer != nullptr)
        {
            const float* chans[2] { left, right };
            s.writer->write (chans, numSamples);
        }
}

void Recorder::writeBus (int bus, const float* l, const float* r, int n) noexcept     { write (true, bus, l, r, n); }
void Recorder::writeStrip (int strip, const float* l, const float* r, int n) noexcept { write (false, strip, l, r, n); }

//==============================================================================
// ReplayBuffer

void ReplayBuffer::configure (double sampleRate, double newSeconds)
{
    const juce::ScopedLock sl (lock);
    rate = sampleRate;
    seconds = newSeconds;
    writePos = filled = 0;

    if (seconds <= 0.0)
    {
        enabled = false;
        ring.setSize (2, 0);
        return;
    }

    ring.setSize (2, juce::jmax (1, (int) (sampleRate * seconds)));
    ring.clear();
    enabled = true;
}

void ReplayBuffer::push (const float* left, const float* right, int numSamples) noexcept
{
    if (! enabled.load (std::memory_order_relaxed))
        return;

    const juce::ScopedTryLock sl (lock);
    if (! sl.isLocked() || ring.getNumSamples() == 0)
        return;

    const int size = ring.getNumSamples();
    int done = 0;
    while (done < numSamples)
    {
        const int chunk = juce::jmin (numSamples - done, size - writePos);
        ring.copyFrom (0, writePos, left + done, chunk);
        ring.copyFrom (1, writePos, right + done, chunk);
        writePos = (writePos + chunk) % size;
        done += chunk;
    }
    filled = juce::jmin (size, filled + numSamples);
}

void ReplayBuffer::save (std::function<void (juce::File, juce::String)> onDone)
{
    // Copy the ring in time order (a quick memcpy under the lock), write it on another thread.
    auto copy = std::make_shared<juce::AudioBuffer<float>>();
    double sr = 48000.0;
    {
        const juce::ScopedLock sl (lock);
        if (! enabled.load() || filled == 0)
        {
            juce::MessageManager::callAsync ([onDone] { onDone ({}, "The replay buffer is empty or switched off."); });
            return;
        }

        const int size = ring.getNumSamples();
        copy->setSize (2, filled);
        const int start = (writePos - filled + size) % size;
        const int first = juce::jmin (filled, size - start);
        for (int c = 0; c < 2; ++c)
        {
            copy->copyFrom (c, 0, ring, c, start, first);
            if (first < filled)
                copy->copyFrom (c, first, ring, c, 0, filled - first);
        }
        sr = rate;
    }

    const auto folder = Recorder::getRecordingsFolder();
    folder.createDirectory();
    const auto file = folder.getNonexistentChildFile ("Replay " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S"), ".wav", true);
    file.create();   // claim the name now: a second save in the same second picks the next one

    juce::Thread::launch ([copy, sr, file, onDone]
    {
        file.getParentDirectory().createDirectory();
        juce::String error;
        if (auto writer = openWav (file, sr))
        {
            if (! writer->writeFromAudioSampleBuffer (*copy, 0, copy->getNumSamples()))
                error = "Couldn't write " + file.getFullPathName();
        }
        else
        {
            error = "Couldn't create " + file.getFullPathName();
        }

        juce::MessageManager::callAsync ([onDone, file, error] { onDone (error.isEmpty() ? file : juce::File(), error); });
    });
}
