// Copyright (C) 2026 Tobias Barthold. GNU Affero General Public License v3.0.

#include "AudioPrep.h"
#include "KeyEngine.h"

#include <numeric>

namespace kd
{

namespace
{
double besselI0 (double x)
{
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 200; ++k)
    {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
        if (term < sum * 1e-17)
            break;
    }
    return sum;
}

// scipy.signal.firwin (numTaps, cutoff, window=("kaiser", beta)), cutoff relative to Nyquist
std::vector<double> firwinKaiser (int numTaps, double cutoff, double beta)
{
    std::vector<double> h ((size_t) numTaps);
    const double alpha = 0.5 * (numTaps - 1);
    double sum = 0.0;
    for (int n = 0; n < numTaps; ++n)
    {
        const double m = n - alpha;
        const double x = cutoff * m;
        const double sinc = x == 0.0 ? 1.0 : std::sin (juce::MathConstants<double>::pi * x) / (juce::MathConstants<double>::pi * x);
        const double r = (n - alpha) / alpha;
        const double w = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / besselI0 (beta);
        h[(size_t) n] = cutoff * sinc * w;
        sum += h[(size_t) n];
    }
    for (auto& v : h)
        v /= sum;
    return h;
}
} // namespace

void Resampler::setSourceRate (double rate)
{
    sourceRate = rate > 0.0 ? rate : kSampleRate;
    auto in = (long long) std::llround (sourceRate);
    const auto outRate = (long long) kSampleRate;
    if (std::max (outRate, in) / std::gcd (in, outRate) > 4000)
        in = std::max (100LL, (in + 50) / 100 * 100);   // unusual rate: round to 100 Hz (< 2 cents)
    const auto g = std::gcd (in, outRate);
    up = outRate / g;
    down = in / g;
    bypass = up == down;
    filter.clear();
    if (! bypass)
    {
        // same construction as scipy.signal.resample_poly (window=("kaiser", 5.0))
        const long long maxRate = std::max (up, down);
        const long long halfLen = 10 * maxRate;
        auto h = firwinKaiser ((int) (2 * halfLen + 1), 1.0 / (double) maxRate, 5.0);
        const long long prePad = down - halfLen % down;
        preRemove = (halfLen + prePad) / down;
        filter.assign ((size_t) prePad, 0.0);
        for (auto v : h)
            filter.push_back (v * (double) up);
    }
    reset();
}

void Resampler::reset()
{
    buffer.clear();
    bufferStart = inputCount = outputCount = 0;
}

void Resampler::produce (std::vector<float>& out, long long available)
{
    const auto len = (long long) filter.size();
    for (;;)
    {
        const long long t = (outputCount + preRemove) * down;     // position on the upsampled grid
        const long long jMax = t / up;
        if (jMax >= available)
            break;
        const long long jMin = std::max (0LL, (t - (len - 1) + up - 1) / up);
        double acc = 0.0;
        for (long long j = jMin; j <= jMax; ++j)
        {
            const long long k = j - bufferStart;
            if (k >= 0 && k < (long long) buffer.size())
                acc += (double) buffer[(size_t) k] * filter[(size_t) (t - j * up)];
        }
        out.push_back ((float) acc);
        ++outputCount;
    }
    // drop input that no future output needs
    const long long tNext = (outputCount + preRemove) * down;
    const long long keepFrom = std::max (0LL, (tNext - (len - 1) + up - 1) / up);
    if (keepFrom > bufferStart)
    {
        const auto drop = (size_t) std::min ((long long) buffer.size(), keepFrom - bufferStart);
        buffer.erase (buffer.begin(), buffer.begin() + (long) drop);
        bufferStart += (long long) drop;
    }
}

void Resampler::process (const float* input, int numSamples, std::vector<float>& out)
{
    if (numSamples <= 0)
        return;
    if (bypass)
    {
        out.insert (out.end(), input, input + numSamples);
        return;
    }
    buffer.insert (buffer.end(), input, input + numSamples);
    inputCount += numSamples;
    produce (out, inputCount);
}

void Resampler::flush (std::vector<float>& out)
{
    if (bypass)
        return;
    const long long total = (inputCount * up + down - 1) / down;   // scipy's output length
    const long long zeros = (long long) filter.size() / up + 2;
    buffer.insert (buffer.end(), (size_t) zeros, 0.0f);
    produce (out, inputCount + zeros);
    if (outputCount > total)
        out.resize (out.size() - (size_t) (outputCount - total));
    inputCount += zeros;
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
    resampler.flush (result.samples);
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
