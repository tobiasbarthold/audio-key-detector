// Key detection with Essentia's KeyExtractor chain (C++ port of keydetect/detect.py).
// See docs/ALGORITHM.md. This header does not depend on JUCE or Essentia.
//
// Copyright (C) 2026 Tobias Barthold. GNU Affero General Public License v3.0.

#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace kd
{

constexpr double kSampleRate = 44100.0;    // analysis rate (Essentia KeyExtractor default)
constexpr double kMaxSeconds = 240.0;      // files: the middle 4 minutes are analysed

struct KeyResult
{
    bool clear = false;          // false: "no clear key" (see reason)
    std::string key;             // e.g. "A minor"; empty when not clear
    std::string camelot;         // e.g. "8A"; empty when not clear
    std::string secondKey, secondCamelot;
    std::string bestGuess;       // best key even when not clear (empty for silence)
    std::string reason;          // "", "silence", "mostly noise", "no pitched content"
    float confidence = 0.0f;     // 0..1, mapped from strength
    float strength = 0.0f;       // Essentia's key strength (correlation of the best key)
    float tonality = 0.0f;       // share of energy in clear spectral peaks
    // "no clear key" only (empty otherwise): prominent peaks of the averaged spectrum, strongest
    // first, e.g. "A#5", and the major/minor keys whose scale contains their notes
    std::vector<std::string> resonances;
    std::vector<float> resonanceHz;
    std::vector<std::string> fits;
    bool fitsPartial = false;    // no key contains all resonance notes; fits contain the most
    std::array<float, 12> hpcp {};          // averaged HPCP, C..B, max 1
    std::array<float, 24> correlations {};  // C..B major, then C..B minor
};

std::string keyName (int pitchClass, bool minor);
std::string camelotCode (int pitchClass, bool minor);
float confidenceFromStrength (float strength);
std::string noteName (int midi);   // "A#5" for MIDI 82 (A4 = 69)

// "A#5 \u00b7 B5 \u00b7 F#6" (UTF-8 middle dot as separator), for resonances and fits
std::string joinWithDots (const std::vector<std::string>& items);

// Not thread-safe: use one engine per thread. Constructing it is cheap.
class KeyEngine
{
public:
    KeyEngine();
    ~KeyEngine();

    // mono samples at kSampleRate. Long input is cut to the middle kMaxSeconds.
    KeyResult analyze (const float* samples, std::size_t numSamples);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace kd
