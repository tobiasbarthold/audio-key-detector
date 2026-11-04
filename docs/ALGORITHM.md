# Detection algorithm

The key is detected with **Essentia** (<https://essentia.upf.edu>, <https://github.com/MTG/essentia>),
the open-source audio analysis library of the Music Technology Group, Universitat Pompeu Fabra,
Barcelona. We use the chain of its `KeyExtractor` algorithm unchanged and only add a noise gate,
a second-best key and a confidence value around it. The same steps run in Python
(`keydetect/detect.py`, Essentia's Python wheel) and in C++ (`plugin/Source/KeyEngine.cpp`,
Essentia's C++ library built from source); `tests/vectors/` checks that both agree.

Essentia is licensed under the GNU Affero General Public License v3 (or later), as is this
project. Copyright (C) 2006-2021 Music Technology Group - Universitat Pompeu Fabra. The C++ build
compiles a small subset of Essentia's sources (see `plugin/cmake/Essentia.cmake`) without
modification.

## 1. Input

1. Decode, mix to mono (average of the channels), resample to **44.1 kHz** (Python: `soundfile`
   + `scipy.signal.resample_poly`; C++: JUCE readers + windowed-sinc interpolation; the plugin
   resamples the incoming track audio the same way).
2. Files longer than 240 s: only the middle 240 s are analysed. The plugin analyses the last
   60 s it has heard.
3. If `max |sample| < 1e-5`: **"silence"**, no key.

## 2. Key: Essentia KeyExtractor (default parameters)

| step | Essentia algorithm | parameters |
|------|--------------------|------------|
| frames | `FrameCutter` | frameSize 4096, hopSize 4096, first frame centred on sample 0 |
| window | `Windowing` | Hann (Essentia's normalised, zero-phase) |
| spectrum | `Spectrum` | magnitude, 4096-point FFT |
| peaks | `SpectralPeaks` | 60 peaks, by magnitude, threshold 1e-4, 25-3500 Hz |
| whitening | `SpectralWhitening` | maxFrequency 3500 Hz |
| pitch-class profile | `HPCP` | 12 bins, 4 harmonics, cosine weighting, window 1 semitone, reference 440 Hz, no normalisation |
| average | | mean HPCP over all frames, normalised to max 1, bins < 0.2 set to 0 |
| key | `Key` | profile **shaath**, no polyphony, 12 bins |

This is exactly what `essentia.standard.KeyExtractor(profileType="shaath")` returns (the test
`test_same_result_as_essentia_key_extractor` compares key, scale and strength). It is run step by
step only so the averaged HPCP is available for the extras below. The one deliberate difference:
we use the standard-mode `FrameCutter`, which keeps silent frames as they are, while the streaming
`KeyExtractor` adds -100 dB random noise to silent frames; this only matters for digital silence
inside a file.

`Key` correlates the averaged HPCP with the 24 rotations of a major and a minor key profile
(Pearson correlation) and returns the best one; its **strength** is that correlation.

### Second-best key

The 24 correlations are recomputed with the same formula and profile values as Essentia's `Key`
(`correlations()` in `detect.py`, `allCorrelations()` in `KeyEngine.cpp`); the second-best key is
the highest one after the detected key. Often it is the relative major/minor, the parallel key or
the dominant.

### Confidence

`confidence = clip((strength - 0.5) / (0.9 - 0.5), 0, 1)`. On the evaluation files the strength
of correct and wrong results overlaps a lot (hits: median 0.85, misses: median 0.81), so treat the
confidence as a rough hint, not a probability.

## 3. "No clear key" gate

Essentia always returns a key, even for white noise (`C major`, strength 0.86). We therefore
measure how much of the signal is pitched:

* `FrameCutter` with frameSize = hopSize = **16384**, first frame starting at sample 0,
  Hann `Windowing`, `Spectrum` (bin width 2.69 Hz at 44.1 kHz).
* Analysis band: FFT bins from (first bin >= 50 Hz) - 1 to (first bin >= 5000 Hz), inclusive;
  power = magnitude^2.
* A bin is a **peak** if its power is greater than its left neighbour, at least its right
  neighbour and more than **10x** the moving average of the power over 41 bins centred on it
  (edges replicated).
* Frames whose band energy is below 1e-4 of the loudest frame are ignored.
* `tonality = sum(power at peaks) / sum(band power)` over the remaining frames.

`tonality < 0.025` gives **"mostly noise"** (no key; the best guess is still reported).

Measured on Ableton Live 12 Suite's Core Library: `One Shots/FX/Swell Short Noise.wav` has
tonality 0.019; the lowest of the 35 named tonal loops whose key was detected correctly has 0.033.
The gate marks 30 of 47 FX one-shots and 108 of 136 drum loops as "mostly noise" and costs one
correctly detected chord stab of 48 (a 0.27 s stab with tonality 0). Heavily distorted loops can
fall below the gate although they have a key.

## 4. Names and Camelot codes

* Major names: `C Db D Eb E F F# G Ab A Bb B`; minor names: `C C# D Eb E F F# G G# A Bb B`.
* Camelot number: major `((7*pc + 7) mod 12) + 1`, suffix `B`; minor uses the relative major
  `rm = (pc+3) mod 12`: `((7*rm + 7) mod 12) + 1`, suffix `A`. (C major = 8B, A minor = 8A.)

## 5. Why Essentia, why "shaath"

The previous version of this project used its own peak-picking chroma with four averaged key
profiles. On the evaluation set below it found 51 % of the keys exactly (57 % including the
relative major/minor). Essentia's KeyExtractor is clearly better on the same files, so the own
algorithm was replaced.

Evaluation: `python scripts/eval_named_loops.py <Core Library>/Samples/Loops --extra
<Core Library>/Samples/One\ Shots/Stab --extra <Core Library>/Samples/One\ Shots/Instrument
[--profile NAME]`. It compares the result with the key in the file name (hit: tonic and, if named,
mode equal; related: relative major/minor; anything else, including "no clear key", is a miss).
"Loops" are the 35 factory loops whose name states a key (sorted by name; even positions are the
"tuning" half, odd positions the "holdout" half). "Stabs" are the 48 chord stabs / one-shots whose
name states a key with a mode, an extra holdout set. All numbers include the noise gate.

| engine / profile | loops exact | loops incl. relative | stabs exact | stabs incl. relative |
|------------------|-------------|----------------------|-------------|----------------------|
| own algorithm (v0.1) | 18/35 (51 %) | 20/35 (57 %) | 27/48 (56 %) | 33/48 (69 %) |
| Essentia, **shaath** (default) | **24/35 (69 %)** | **26/35 (74 %)** | 30/48 (62 %) | 30/48 (62 %) |
| Essentia, edma | 23/35 (66 %) | 26/35 (74 %) | 31/48 (65 %) | 32/48 (67 %) |
| Essentia, bgate (Essentia's default) | 22/35 (63 %) | 25/35 (71 %) | 28/48 (58 %) | 33/48 (69 %) |

Combinations were checked before choosing (without the noise gate, so the stab numbers are one
file higher than above): taking the result with the higher strength of shaath and edma gives
24/35 and 26/35 on the loops (same as shaath) and 32/48 / 32/48 on the stabs (shaath alone: 31/48
/ 31/48); a majority vote of shaath, edma and bgate gives 23/35 / 26/35 and 32/48 / 33/48;
averaging the correlations of two or three profiles gives at most 24/35 / 26/35. None of them is
better than shaath alone by more than one or two files, which is within the noise of 35 + 48
files, so the simpler single profile is used. shaath was picked over edma because it is better on
the main loop set and both are equal including the relative key. Other profiles can be selected
in the CLI with `--profile` (shaath, edma, bgate).

Notes on the numbers: names in sample packs give the producer's root note, not always the
harmonic key; several misses are loops whose pad or bass plays in a different key than the name.
Major vs. relative minor (same notes) and tonic vs. dominant/subdominant remain the most common
confusions, which is why the second-best key is shown.

## 6. Plugin (live analysis)

The plugin collects the incoming audio of its track (mono, resampled to 44.1 kHz, last 60 s) and
re-runs sections 1-3 on everything collected about once per second on a background thread. The
displayed key is therefore the key of the last up to 60 s of audio that went through the track.
"Reset" clears the collected audio. Before 2 s of non-silent audio have been collected it shows
"listening...".

## 7. Reference vectors

`tests/vectors/*.wav` (mono, 44.1 kHz, 16-bit, 1-3 s, synthetic) are produced by
`scripts/make_test_vectors.py`; `tests/vectors/expected.json` holds the Python output for each
(`key`, `camelot`, `second_key`, `confidence`, `strength`, `tonality`, `clear`, `reason`,
`best_guess`, the 12 `hpcp` bins C..B and the 24 `correlations`). The C++ engine is checked against
the same file by `plugin/tests/compare_vectors.py`.
