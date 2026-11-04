// Audio preparation for KeyEngine: mono mix-down, resampling to 44.1 kHz, file loading.
//
// Copyright (C) 2026 Tobias Barthold. GNU Affero General Public License v3.0.

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include <vector>

namespace kd
{

// Streaming resampler from an arbitrary rate to kSampleRate. For integer rates it computes
// exactly what scipy.signal.resample_poly does in the Python version (polyphase FIR, Kaiser
// window beta 5, 20 * max(up, down) + 1 taps); at 44.1 kHz it copies.
class Resampler
{
public:
    explicit Resampler (double rate = 44100.0) { setSourceRate (rate); }

    void setSourceRate (double rate);
    double getSourceRate() const noexcept { return sourceRate; }
    void reset();

    // Appends the resampled output of `numSamples` input samples to `out`.
    void process (const float* input, int numSamples, std::vector<float>& out);
    // End of a file: treats the input after the last sample as zeros and appends the remaining
    // output, so that the total output length is ceil(numInputSamples * up / down).
    void flush (std::vector<float>& out);

private:
    void produce (std::vector<float>& out, long long available);

    double sourceRate = 44100.0;
    bool bypass = true;
    long long up = 1, down = 1, preRemove = 0;
    std::vector<double> filter;     // zero-padded at the front like scipy's resample_poly
    std::vector<float> buffer;      // input samples from index bufferStart on
    long long bufferStart = 0, inputCount = 0, outputCount = 0;
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
