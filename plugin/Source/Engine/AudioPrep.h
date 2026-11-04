// Audio preparation for KeyEngine: mono mix-down, resampling to 44.1 kHz, file loading.
//
// Copyright (C) 2026 Tobias Barthold. GNU Affero General Public License v3.0.

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include <vector>

namespace kd
{

// Streaming resampler from an arbitrary rate to kSampleRate (windowed sinc; plain copy at 44.1 kHz).
class Resampler
{
public:
    explicit Resampler (double rate = 44100.0) { setSourceRate (rate); }

    void setSourceRate (double sourceRate);
    double getSourceRate() const noexcept { return sourceRate; }
    void reset();

    // Appends the resampled output of `numSamples` input samples to `out`.
    void process (const float* input, int numSamples, std::vector<float>& out);
    // Pushes zeros through the filter so the last input samples come out (end of a file).
    void flush (std::vector<float>& out);

private:
    double sourceRate = 44100.0, ratio = 1.0;
    juce::WindowedSincInterpolator interpolator;
    std::vector<float> pending;
};

struct LoadedAudio
{
    std::vector<float> samples;   // mono, 44.1 kHz
    juce::String error;           // empty on success
};

// Reads any format JUCE/macOS can decode (wav, aiff, flac, ogg, mp3, m4a/aac, caf).
LoadedAudio loadAudioFile (const juce::File& file, juce::AudioFormatManager& formats);

bool isAudioFile (const juce::File& file);
// Files, and folders scanned recursively (hidden entries skipped), sorted by path.
juce::Array<juce::File> collectAudioFiles (const juce::StringArray& paths);

} // namespace kd
