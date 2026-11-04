// Key detection with Essentia's KeyExtractor chain (C++ port of keydetect/detect.py).
// Every constant and step mirrors the Python reference; see docs/ALGORITHM.md.
//
// Copyright (C) 2026 Tobias Barthold. GNU Affero General Public License v3.0.

#include "KeyEngine.h"

#include "algorithmfactory.h"
#include "essentia.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <vector>

namespace kd
{
namespace
{
using essentia::Real;
using essentia::standard::Algorithm;
using essentia::standard::AlgorithmFactory;

// Essentia KeyExtractor defaults
constexpr int kFrameSize = 4096;
constexpr int kHopSize = 4096;
constexpr Real kMinFreq = 25.0f;
constexpr Real kMaxFreq = 3500.0f;
constexpr int kMaxPeaks = 60;
constexpr Real kPeakThreshold = 1e-4f;
constexpr Real kPcpThreshold = 0.2f;
const char* const kProfile = "shaath";

// Confidence mapping and noise gate (same values as detect.py)
constexpr float kConfLo = 0.5f, kConfHi = 0.9f;
constexpr int kTonalFrame = 16384;
constexpr int kTonalHop = 4096;
constexpr double kTonalFMin = 50.0, kTonalFMax = 5000.0;
constexpr double kTonalPeakRatio = 15.0;
constexpr int kTonalAvgBins = 41;
constexpr double kNoiseTonality = 0.0027;
constexpr float kSilencePeak = 1e-5f;

// shaath profile as in Essentia's key.cpp (index 0 = tonic); used for all 24 correlations
const double kMajor[12] = { 6.6, 2.0, 3.5, 2.3, 4.6, 4.0, 2.5, 5.2, 2.4, 3.7, 2.3, 3.4 };
const double kMinor[12] = { 6.5, 2.7, 3.5, 5.4, 2.6, 3.5, 2.5, 5.2, 4.0, 2.7, 4.3, 3.2 };

const char* const kMajorNames[12] = { "C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
const char* const kMinorNames[12] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "G#", "A", "Bb", "B" };

void initEssentia()
{
    static std::once_flag once;
    std::call_once (once, []
    {
        essentia::infoLevelActive = false;
        essentia::warningLevelActive = false;
        essentia::init();
        essentia::infoLevelActive = false;
        essentia::warningLevelActive = false;
    });
}

int essentiaKeyToPitchClass (const std::string& k)
{
    static const std::map<std::string, int> m { { "A", 9 }, { "Bb", 10 }, { "B", 11 }, { "C", 0 },
        { "C#", 1 }, { "D", 2 }, { "Eb", 3 }, { "E", 4 }, { "F", 5 }, { "F#", 6 }, { "G", 7 },
        { "Ab", 8 }, { "G#", 8 }, { "Db", 1 }, { "D#", 3 }, { "A#", 10 }, { "Gb", 6 } };
    auto it = m.find (k);
    return it == m.end() ? -1 : it->second;
}

// Pearson correlation with all 24 rotated profiles, as Essentia's Key::correlation.
// pcp is in Essentia order (bin 0 = A); result: 0..11 = C..B major, 12..23 = C..B minor.
std::array<float, 24> allCorrelations (const std::vector<Real>& pcp)
{
    std::array<float, 24> out {};
    double mp = 0.0;
    for (auto v : pcp) mp += v;
    mp /= 12.0;
    double sp = 0.0;
    for (auto v : pcp) sp += (v - mp) * (v - mp);
    sp = std::sqrt (sp);
    for (int mode = 0; mode < 2; ++mode)
    {
        const double* prof = mode == 0 ? kMajor : kMinor;
        double mq = 0.0;
        for (int i = 0; i < 12; ++i) mq += prof[i];
        mq /= 12.0;
        double sq = 0.0;
        for (int i = 0; i < 12; ++i) sq += (prof[i] - mq) * (prof[i] - mq);
        sq = std::sqrt (sq);
        for (int shift = 0; shift < 12; ++shift)
        {
            double r = 0.0;
            if (sp > 0.0 && sq > 0.0)
            {
                for (int i = 0; i < 12; ++i)
                    r += (pcp[(size_t) i] - mp) * (prof[((i - shift) % 12 + 12) % 12] - mq);
                r /= sp * sq;
            }
            out[(size_t) (mode * 12 + (shift + 9) % 12)] = (float) r;
        }
    }
    return out;
}

} // namespace

std::string keyName (int pc, bool minor)
{
    pc = ((pc % 12) + 12) % 12;
    return std::string (minor ? kMinorNames[pc] : kMajorNames[pc]) + (minor ? " minor" : " major");
}

std::string camelotCode (int pc, bool minor)
{
    int ref = minor ? (pc + 3) % 12 : pc % 12;
    return std::to_string (((7 * ref + 7) % 12) + 1) + (minor ? "A" : "B");
}

float confidenceFromStrength (float strength)
{
    return std::clamp ((strength - kConfLo) / (kConfHi - kConfLo), 0.0f, 1.0f);
}

struct KeyEngine::Impl
{
    std::unique_ptr<Algorithm> frameCutter, window, spectrum, peaks, whitening, hpcp, key;
    std::unique_ptr<Algorithm> tonalCutter, tonalWindow, tonalSpectrum;

    Impl()
    {
        initEssentia();
        frameCutter.reset (AlgorithmFactory::create ("FrameCutter", "frameSize", kFrameSize, "hopSize", kHopSize,
                                                      "startFromZero", false));
        window.reset (AlgorithmFactory::create ("Windowing", "size", kFrameSize, "type", "hann"));
        spectrum.reset (AlgorithmFactory::create ("Spectrum", "size", kFrameSize));
        peaks.reset (AlgorithmFactory::create ("SpectralPeaks", "orderBy", "magnitude", "magnitudeThreshold", kPeakThreshold,
                                                "minFrequency", kMinFreq, "maxFrequency", kMaxFreq,
                                                "maxPeaks", kMaxPeaks, "sampleRate", (Real) kSampleRate));
        whitening.reset (AlgorithmFactory::create ("SpectralWhitening", "maxFrequency", kMaxFreq,
                                                    "sampleRate", (Real) kSampleRate));
        hpcp.reset (AlgorithmFactory::create ("HPCP", "bandPreset", false, "harmonics", 4, "maxFrequency", kMaxFreq,
                                               "minFrequency", kMinFreq, "nonLinear", false, "normalized", "none",
                                               "referenceFrequency", (Real) 440.0f, "sampleRate", (Real) kSampleRate,
                                               "size", 12, "weightType", "cosine", "windowSize", (Real) 1.0f,
                                               "maxShifted", false));
        key.reset (AlgorithmFactory::create ("Key", "usePolyphony", false, "useThreeChords", false, "numHarmonics", 4,
                                              "slope", (Real) 0.6f, "profileType", kProfile, "pcpSize", 12));
        tonalCutter.reset (AlgorithmFactory::create ("FrameCutter", "frameSize", kTonalFrame, "hopSize", kTonalHop,
                                                      "startFromZero", true));
        tonalWindow.reset (AlgorithmFactory::create ("Windowing", "size", kTonalFrame, "type", "hann"));
        tonalSpectrum.reset (AlgorithmFactory::create ("Spectrum", "size", kTonalFrame));
    }

    // Average HPCP over all KeyExtractor frames (Essentia order, bin 0 = A).
    std::vector<Real> meanHpcp (const std::vector<Real>& y)
    {
        std::vector<Real> frame, windowed, spec, freqs, mags, white, h;
        frameCutter->reset();
        frameCutter->input ("signal").set (y);
        frameCutter->output ("frame").set (frame);
        window->input ("frame").set (frame);
        window->output ("frame").set (windowed);
        spectrum->input ("frame").set (windowed);
        spectrum->output ("spectrum").set (spec);
        peaks->input ("spectrum").set (spec);
        peaks->output ("frequencies").set (freqs);
        peaks->output ("magnitudes").set (mags);
        whitening->input ("spectrum").set (spec);
        whitening->input ("frequencies").set (freqs);
        whitening->input ("magnitudes").set (mags);
        whitening->output ("magnitudes").set (white);
        hpcp->input ("frequencies").set (freqs);
        hpcp->input ("magnitudes").set (white);
        hpcp->output ("hpcp").set (h);

        std::vector<double> sum (12, 0.0);
        size_t n = 0;
        for (;;)
        {
            frameCutter->compute();
            if (frame.empty())
                break;
            window->compute();
            spectrum->compute();
            peaks->compute();
            whitening->compute();
            hpcp->compute();
            for (size_t i = 0; i < 12; ++i)
                sum[i] += h[i];
            ++n;
        }
        std::vector<Real> mean (12, 0.0f);
        if (n > 0)
            for (size_t i = 0; i < 12; ++i)
                mean[i] = (Real) (sum[i] / (double) n);
        return mean;
    }

    // Share of the 50 Hz - 5 kHz energy in clear spectral peaks (0 = noise).
    double tonality (const std::vector<Real>& y)
    {
        std::vector<Real> frame, windowed, mag;
        tonalCutter->reset();
        tonalCutter->input ("signal").set (y);
        tonalCutter->output ("frame").set (frame);
        tonalWindow->input ("frame").set (frame);
        tonalWindow->output ("frame").set (windowed);
        tonalSpectrum->input ("frame").set (windowed);
        tonalSpectrum->output ("spectrum").set (mag);

        const double binHz = kSampleRate / kTonalFrame;
        const int lo = (int) std::ceil (kTonalFMin / binHz);   // first bin >= 50 Hz
        const int hi = (int) std::ceil (kTonalFMax / binHz);   // first bin >= 5 kHz
        const int nb = hi - lo + 2;                            // bins lo-1 .. hi
        const int half = kTonalAvgBins / 2;
        std::vector<double> p ((size_t) nb), padded ((size_t) (nb + 2 * half)), cum ((size_t) (nb + 2 * half + 1));
        std::vector<double> energies, peakEnergies;

        for (;;)
        {
            tonalCutter->compute();
            if (frame.empty())
                break;
            tonalWindow->compute();
            tonalSpectrum->compute();
            double e = 0.0;
            for (int j = 0; j < nb; ++j)
            {
                double m = mag[(size_t) (lo - 1 + j)];
                p[(size_t) j] = m * m;
                e += p[(size_t) j];
            }
            for (int j = 0; j < half; ++j)
            {
                padded[(size_t) j] = p.front();
                padded[(size_t) (nb + half + j)] = p.back();
            }
            std::copy (p.begin(), p.end(), padded.begin() + half);
            cum[0] = 0.0;
            for (size_t j = 0; j < padded.size(); ++j)
                cum[j + 1] = cum[j] + padded[j];
            double pe = 0.0;
            for (int j = 1; j < nb - 1; ++j)
            {
                double local = (cum[(size_t) (j + kTonalAvgBins)] - cum[(size_t) j]) / kTonalAvgBins + 1e-20;
                double c = p[(size_t) j];
                if (c > p[(size_t) (j - 1)] && c >= p[(size_t) (j + 1)] && c > kTonalPeakRatio * local)
                    pe += c;
            }
            energies.push_back (e);
            peakEnergies.push_back (pe);
        }
        if (energies.empty())
            return 0.0;
        const double emax = *std::max_element (energies.begin(), energies.end());
        double tot = 0.0, pk = 0.0;
        for (size_t i = 0; i < energies.size(); ++i)
            if (energies[i] > 1e-4 * emax)
            {
                tot += energies[i];
                pk += peakEnergies[i];
            }
        return tot > 0.0 ? std::clamp (pk / tot, 0.0, 1.0) : 0.0;
    }

    KeyResult fromHpcp (std::vector<Real> pcp, float tonal)
    {
        KeyResult r;
        r.tonality = tonal;
        // normalise to max 1, gate bins below the threshold (as Essentia's streaming Key)
        Real mx = *std::max_element (pcp.begin(), pcp.end());
        if (mx > 0.0f)
            for (auto& v : pcp)
                v = v / mx;
        for (auto& v : pcp)
            if (v < kPcpThreshold)
                v = 0.0f;
        if (*std::max_element (pcp.begin(), pcp.end()) <= 0.0f)
        {
            r.reason = "no pitched content";
            return r;
        }
        std::string keyStr, scale;
        Real strength = 0.0f, firstToSecond = 0.0f;
        key->input ("pcp").set (pcp);
        key->output ("key").set (keyStr);
        key->output ("scale").set (scale);
        key->output ("strength").set (strength);
        key->output ("firstToSecondRelativeStrength").set (firstToSecond);
        key->compute();

        const bool minor = scale == "minor";
        const int pc = essentiaKeyToPitchClass (keyStr);
        const int best = pc + (minor ? 12 : 0);
        r.correlations = allCorrelations (pcp);
        int second = -1;
        for (int i = 0; i < 24; ++i)
            if (i != best && (second < 0 || r.correlations[(size_t) i] > r.correlations[(size_t) second]))
                second = i;
        for (size_t i = 0; i < 12; ++i)
            r.hpcp[i] = pcp[(i + 3) % 12];      // A..Ab -> C..B

        r.strength = strength;
        r.confidence = confidenceFromStrength (strength);
        r.bestGuess = keyName (pc, minor);
        r.secondKey = keyName (second % 12, second >= 12);
        r.secondCamelot = camelotCode (second % 12, second >= 12);
        r.reason = tonal < kNoiseTonality ? "mostly noise" : "";
        r.clear = r.reason.empty();
        if (r.clear)
        {
            r.key = r.bestGuess;
            r.camelot = camelotCode (pc, minor);
        }
        return r;
    }
};

KeyEngine::KeyEngine() : impl (std::make_unique<Impl>()) {}
KeyEngine::~KeyEngine() = default;

KeyResult KeyEngine::analyze (const float* samples, std::size_t numSamples)
{
    const auto maxLen = (std::size_t) (kMaxSeconds * kSampleRate);
    std::size_t start = 0;
    if (numSamples > maxLen)
    {
        const std::size_t mid = numSamples / 2, half = (std::size_t) (kMaxSeconds * kSampleRate / 2);
        start = mid - half;
        numSamples = 2 * half;
    }
    std::vector<Real> y (samples + start, samples + start + numSamples);

    float peak = 0.0f;
    for (auto v : y)
        peak = std::max (peak, std::abs (v));
    if (peak < kSilencePeak)
    {
        KeyResult r;
        r.reason = "silence";
        return r;
    }
    try
    {
        auto tonal = (float) impl->tonality (y);
        return impl->fromHpcp (impl->meanHpcp (y), tonal);
    }
    catch (const std::exception& e)
    {
        KeyResult r;
        r.reason = std::string ("analysis error: ") + e.what();
        return r;
    }
}

} // namespace kd
