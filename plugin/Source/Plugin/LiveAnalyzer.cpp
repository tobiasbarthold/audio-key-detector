// Copyright (C) 2026 Tobias Barthold. GNU Affero General Public License v3.0.

#include "LiveAnalyzer.h"

namespace
{
constexpr float kSilence = 1e-5f;      // chunks quieter than this are not collected
constexpr int kChunk = 1024;
} // namespace

LiveAnalyzer::LiveAnalyzer() : juce::Thread ("Key analysis")
{
    fifoData.resize ((size_t) fifo.getTotalSize());
    scratch.resize (4096);
    drained.reserve ((size_t) fifo.getTotalSize());
    history.reserve ((size_t) ((historySeconds + 2.0) * kd::kSampleRate));
    startThread (juce::Thread::Priority::low);
}

LiveAnalyzer::~LiveAnalyzer()
{
    stopThread (4000);
}

void LiveAnalyzer::prepare (double sampleRate, int maximumBlockSize)
{
    if (maximumBlockSize > (int) scratch.size())
        scratch.resize ((size_t) maximumBlockSize);
    if (sampleRate > 0.0 && std::abs (sampleRate - hostRate.load()) > 1e-6)
    {
        hostRate.store (sampleRate);
        requestReset();
    }
}

void LiveAnalyzer::pushAudio (const juce::AudioBuffer<float>& buffer, int numChannels, int numSamples) noexcept
{
    numChannels = juce::jmin (numChannels, buffer.getNumChannels());
    if (numChannels <= 0)
        return;
    const float gain = 1.0f / (float) numChannels;
    for (int offset = 0; offset < numSamples;)
    {
        const int n = juce::jmin (numSamples - offset, (int) scratch.size());
        auto* mono = scratch.data();
        juce::FloatVectorOperations::copyWithMultiply (mono, buffer.getReadPointer (0, offset), gain, n);
        for (int c = 1; c < numChannels; ++c)
            juce::FloatVectorOperations::addWithMultiply (mono, buffer.getReadPointer (c, offset), gain, n);

        int start1, size1, start2, size2;
        fifo.prepareToWrite (n, start1, size1, start2, size2);
        if (size1 > 0)
            std::copy (mono, mono + size1, fifoData.data() + start1);
        if (size2 > 0)
            std::copy (mono + size1, mono + size1 + size2, fifoData.data() + start2);
        fifo.finishedWrite (size1 + size2);
        offset += n;
    }
}

void LiveAnalyzer::drainFifo()
{
    drained.clear();
    int start1, size1, start2, size2;
    fifo.prepareToRead (fifo.getNumReady(), start1, size1, start2, size2);
    drained.insert (drained.end(), fifoData.data() + start1, fifoData.data() + start1 + size1);
    drained.insert (drained.end(), fifoData.data() + start2, fifoData.data() + start2 + size2);
    fifo.finishedRead (size1 + size2);
}

void LiveAnalyzer::publish (bool hasResult, const kd::KeyResult& r)
{
    std::lock_guard<std::mutex> lock (snapshotLock);
    snapshot.hasResult = hasResult;
    snapshot.result = r;
    snapshot.secondsHeard = (double) history.size() / kd::kSampleRate;
    ++snapshot.serial;
}

LiveAnalyzer::Snapshot LiveAnalyzer::getSnapshot() const
{
    std::lock_guard<std::mutex> lock (snapshotLock);
    return snapshot;
}

void LiveAnalyzer::run()
{
    engine = std::make_unique<kd::KeyEngine>();
    const auto maxHistory = (size_t) (historySeconds * kd::kSampleRate);
    const auto minHistory = (size_t) (minSeconds * kd::kSampleRate);
    juce::uint32 lastAnalysis = 0;
    bool newAudio = false, haveResult = false;
    kd::KeyResult lastResult;

    while (! threadShouldExit())
    {
        wait (50);

        if (resetRequested.exchange (false))
        {
            drainFifo();                              // discard what was queued before the reset
            history.clear();
            resampler.setSourceRate (hostRate.load());
            newAudio = haveResult = false;
            lastResult = {};
            publish (false, lastResult);
            continue;
        }
        if (std::abs (resampler.getSourceRate() - hostRate.load()) > 1e-6)
            resampler.setSourceRate (hostRate.load());

        drainFifo();
        for (size_t i = 0; i < drained.size(); i += kChunk)
        {
            const int n = (int) juce::jmin ((size_t) kChunk, drained.size() - i);
            const auto range = juce::FloatVectorOperations::findMinAndMax (drained.data() + i, n);
            if (juce::jmax (-range.getStart(), range.getEnd()) < kSilence)
                continue;                             // ignore silence (e.g. transport stopped)
            resampler.process (drained.data() + i, n, history);
            newAudio = true;
        }
        if (history.size() > maxHistory + (size_t) kd::kSampleRate)   // trim in 1 s steps
            history.erase (history.begin(), history.begin() + (long) (history.size() - maxHistory));

        const auto now = juce::Time::getMillisecondCounter();
        if (! newAudio || now - lastAnalysis < (juce::uint32) intervalMs)
            continue;
        if (history.size() < minHistory)
        {
            publish (false, lastResult);              // update "seconds heard" while listening
            continue;
        }
        lastAnalysis = now;
        newAudio = false;
        const size_t n = juce::jmin (history.size(), maxHistory);
        lastResult = engine->analyze (history.data() + (history.size() - n), n);
        haveResult = true;
        publish (haveResult, lastResult);
    }
    engine.reset();
}
