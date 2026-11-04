// Live key analysis of the audio passing through the plugin.
//
// The audio thread only mixes to mono and copies samples into a lock-free FIFO. A background
// thread drains it, resamples to 44.1 kHz, keeps the last 60 s of non-silent audio and re-runs
// the key analysis on all of it about once per second.
//
// Copyright (C) 2026 Tobias Barthold. GNU Affero General Public License v3.0.

#pragma once

#include "../Engine/AudioPrep.h"
#include "../Engine/KeyEngine.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <mutex>

class LiveAnalyzer : private juce::Thread
{
public:
    static constexpr double historySeconds = 60.0;   // analysis window
    static constexpr double minSeconds = 2.0;        // "listening..." until this much audio
    static constexpr int intervalMs = 1000;          // re-analysis interval

    struct Snapshot
    {
        bool hasResult = false;      // false: still listening
        kd::KeyResult result;
        double secondsHeard = 0.0;   // audio collected for the current result (max 60 s)
        juce::uint32 serial = 0;     // changes whenever the snapshot changes
    };

    LiveAnalyzer();
    ~LiveAnalyzer() override;

    // Message thread / prepareToPlay: allocates.
    void prepare (double sampleRate, int maximumBlockSize);

    // Audio thread: no locks, no allocation. Drops samples if the FIFO is full.
    void pushAudio (const juce::AudioBuffer<float>& buffer, int numChannels, int numSamples) noexcept;

    // Any thread, also the audio thread (only sets a flag; the analysis thread polls it).
    void requestReset() noexcept { resetRequested.store (true); }

    Snapshot getSnapshot() const;

private:
    void run() override;
    void drainFifo();
    void publish (bool hasResult, const kd::KeyResult& r);

    // shared with the audio thread
    juce::AbstractFifo fifo { 1 << 20 };
    std::vector<float> fifoData;
    std::vector<float> scratch;
    std::atomic<double> hostRate { 44100.0 };
    std::atomic<bool> resetRequested { false };

    // background thread only
    kd::Resampler resampler;
    std::vector<float> drained, history;
    std::unique_ptr<kd::KeyEngine> engine;

    mutable std::mutex snapshotLock;
    Snapshot snapshot;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LiveAnalyzer)
};
