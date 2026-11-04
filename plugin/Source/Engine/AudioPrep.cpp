// Copyright (C) 2026 Tobias Barthold. GNU Affero General Public License v3.0.

#include "AudioPrep.h"
#include "KeyEngine.h"

namespace kd
{

void Resampler::setSourceRate (double rate)
{
    sourceRate = rate > 0.0 ? rate : kSampleRate;
    ratio = sourceRate / kSampleRate;
    reset();
}

void Resampler::reset()
{
    interpolator.reset();
    pending.clear();
}

void Resampler::process (const float* input, int numSamples, std::vector<float>& out)
{
    if (numSamples <= 0)
        return;
    if (std::abs (ratio - 1.0) < 1e-9)
    {
        out.insert (out.end(), input, input + numSamples);
        return;
    }
    pending.insert (pending.end(), input, input + numSamples);
    const int numOut = (int) (((double) pending.size() - 4.0) / ratio);
    if (numOut <= 0)
        return;
    const auto old = out.size();
    out.resize (old + (size_t) numOut);
    const int used = interpolator.process (ratio, pending.data(), out.data() + old, numOut);
    pending.erase (pending.begin(), pending.begin() + juce::jlimit (0, (int) pending.size(), used));
}

void Resampler::flush (std::vector<float>& out)
{
    if (std::abs (ratio - 1.0) < 1e-9)
        return;
    std::vector<float> zeros ((size_t) (juce::WindowedSincInterpolator::getBaseLatency() * 2 + 8), 0.0f);
    process (zeros.data(), (int) zeros.size(), out);
}

LoadedAudio loadAudioFile (const juce::File& file, juce::AudioFormatManager& formats)
{
    LoadedAudio result;
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
    {
        result.error = "cannot decode " + file.getFileName();
        return result;
    }
    const auto sr = reader->sampleRate;
    const auto channels = (int) reader->numChannels;
    if (sr <= 0 || channels <= 0)
    {
        result.error = "invalid audio format";
        return result;
    }
    const juce::int64 total = reader->lengthInSamples;
    const juce::int64 maxIn = (juce::int64) (sr * 3600.0);   // refuse > 1 h of audio
    if (total <= 0)
    {
        result.error = "contains no audio";
        return result;
    }
    if (total > maxIn)
    {
        result.error = "file longer than one hour";
        return result;
    }

    Resampler resampler (sr);
    const double expectedOut = (double) total * kSampleRate / sr;
    result.samples.reserve ((size_t) expectedOut + 1024);
    const int block = 1 << 16;
    juce::AudioBuffer<float> buf (channels, block);
    std::vector<float> mono ((size_t) block);
    for (juce::int64 pos = 0; pos < total; pos += block)
    {
        const int n = (int) std::min<juce::int64> (block, total - pos);
        if (! reader->read (&buf, 0, n, pos, true, true))
        {
            result.error = "read error";
            return result;
        }
        for (int i = 0; i < n; ++i)
        {
            float s = 0.0f;
            for (int c = 0; c < channels; ++c)
                s += buf.getSample (c, i);
            mono[(size_t) i] = s / (float) channels;
        }
        resampler.process (mono.data(), n, result.samples);
    }
    if (std::abs (sr - kSampleRate) > 1e-6)
    {
        // drop the interpolator's delay at the start, then flush the tail
        resampler.flush (result.samples);
        const auto delay = (size_t) std::lround (juce::WindowedSincInterpolator::getBaseLatency() / (sr / kSampleRate));
        result.samples.erase (result.samples.begin(), result.samples.begin() + (long) std::min (delay, result.samples.size()));
        result.samples.resize ((size_t) std::llround (expectedOut), 0.0f);
    }
    for (auto& s : result.samples)
        if (! std::isfinite (s))
            s = 0.0f;
    return result;
}

bool isAudioFile (const juce::File& f)
{
    static const juce::StringArray exts { ".wav", ".aif", ".aiff", ".flac", ".mp3", ".m4a", ".ogg", ".caf", ".aac" };
    return f.existsAsFile() && ! f.getFileName().startsWithChar ('.')
           && exts.contains (f.getFileExtension().toLowerCase());
}

juce::Array<juce::File> collectAudioFiles (const juce::StringArray& paths)
{
    juce::Array<juce::File> files;
    for (auto& p : paths)
    {
        juce::File f (p);
        if (f.isDirectory())
        {
            juce::Array<juce::File> found;
            for (const auto& entry : juce::RangedDirectoryIterator (f, true, "*", juce::File::findFiles, juce::File::FollowSymlinks::no))
            {
                auto file = entry.getFile();
                const auto rel = file.getRelativePathFrom (f);
                if (isAudioFile (file) && ! rel.startsWithChar ('.') && ! rel.contains ("/."))
                    found.add (file);
            }
            found.sort();
            files.addArray (found);
        }
        else if (f.existsAsFile())
        {
            files.add (f);
        }
    }
    return files;
}

} // namespace kd
