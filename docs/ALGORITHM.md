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

1. Decode, mix to mono (average of the channels), resample to **44.1 kHz** with a polyphase FIR
   filter: Python uses `scipy.signal.resample_poly` (Kaiser window, beta 5), C++
   (`plugin/Source/Engine/AudioPrep.cpp`) builds the same filter and produces the same samples
   (max. difference 1e-8), also in streaming form for the plugin.
2. Files longer than 240 s: only the middle 240 s are analysed. The plugin analyses the last
   60 s it has heard.
3. If `max |sample| < 1e-5`: **"silence"**, no key.

## 2. Key: Essentia KeyExtractor (default parameters, hop size 1024)

| step | Essentia algorithm | parameters |
|------|--------------------|------------|
| frames | `FrameCutter` | frameSize 4096, **hopSize 1024** (KeyExtractor default: 4096), first frame centred on sample 0 |
| window | `Windowing` | Hann (Essentia's normalised, zero-phase) |
| spectrum | `Spectrum` | magnitude, 4096-point FFT |
| peaks | `SpectralPeaks` | 60 peaks, by magnitude, threshold 1e-4, 25-3500 Hz |
| whitening | `SpectralWhitening` | maxFrequency 3500 Hz |
| pitch-class profile | `HPCP` | 12 bins, 4 harmonics, cosine weighting, window 1 semitone, reference 440 Hz, no normalisation |
| average | | mean HPCP over all frames, normalised to max 1, bins < 0.2 set to 0 |
| key | `Key` | profile **shaath**, no polyphony, 12 bins |

This is exactly what `essentia.standard.KeyExtractor(profileType="shaath", hopSize=1024)` returns (the test
`test_same_result_as_essentia_key_extractor` compares key, scale and strength). It is run step by
step only so the averaged HPCP is available for the extras below. The one deliberate difference:
we use the standard-mode `FrameCutter`, which keeps silent frames as they are, while the streaming
`KeyExtractor` adds -100 dB random noise to silent frames; this only matters for digital silence
inside a file.

Why hop 1024 instead of the default 4096: with frames that do not overlap, the result depends on
where the frames fall. That matters for the plugin, which hears a clip looped and from an
arbitrary start. Measured on the evaluation files below, each also analysed looped to 8, 12, 20
and 30 s with different start offsets: the key stayed the same in all variants for 32/35 loops
and 43/48 stabs with hop 4096, and for 33/35 and 47/48 with hop 1024. Accuracy is unchanged
(loops 24/35 exact with both; stabs 29/48 exact with both). Cost: 4x more frames, still well under
0.1 s for a minute of audio.

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
measure how much of the signal sits in clear spectral peaks:

* `FrameCutter` with frameSize **16384**, hopSize **4096**, first frame starting at sample 0,
  Hann `Windowing`, `Spectrum` (bin width 2.69 Hz at 44.1 kHz).
* Analysis band: FFT bins from (first bin >= 50 Hz) - 1 to (first bin >= 5000 Hz), inclusive;
  power = magnitude^2.
* A bin is a **peak** if its power is greater than its left neighbour, at least its right
  neighbour and more than **15x** the moving average of the power over 41 bins centred on it
  (edges replicated).
* Frames whose band energy is below 1e-4 of the loudest frame are ignored.
* `tonality = sum(power at peaks) / sum(band power)` over the remaining frames.

`tonality < 0.0027` gives **"mostly noise"** (no key; the best guess is still reported).

How the values were chosen (Ableton Live 12 Suite Core Library): `One Shots/FX/Swell Short
Noise.wav` is resonant noise with weak but stable resonances (around 480, 710, 1490 and 2730 Hz,
10-17x above the local average), so it is not plain noise. A first version (peaks > 10x, no frame
overlap) separated it from the tonal loops only for one particular framing: played in a loop, as
the plugin hears it, its value rose above the weakest correctly detected loops. With peaks > 15x
and 75 % frame overlap the measure is stable: Swell Short Noise has at most 0.0014 (single file
or looped), the weakest correctly detected named loop has at least 0.0053 (looped or not), and
0.0027 is roughly the geometric middle. The gate marks 31 of 47 FX one-shots and 106 of 136 drum
loops as "mostly noise"; of the evaluation files it gates 4 that were wrong anyway and 2 correctly
detected very short chord stabs. Heavily distorted material can fall below the gate although it
has a key.

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
| Essentia, **shaath** (default) | **24/35 (69 %)** | **26/35 (74 %)** | 29/48 (60 %) | 30/48 (62 %) |
| Essentia, edma | 24/35 (69 %) | 26/35 (74 %) | 31/48 (65 %) | 33/48 (69 %) |
| Essentia, bgate (Essentia's default) | 22/35 (63 %) | 25/35 (71 %) | 29/48 (60 %) | 34/48 (71 %) |

Per half of the loops (shaath): tuning half 10/18 exact, 12/18 incl. relative; holdout half 14/17
and 14/17. The own algorithm had 9/18, 11/18 and 9/17, 9/17.

The profile was chosen with KeyExtractor's default hop size 4096 (the numbers in the task
description: shaath 24/35 and 26/35, edma 23/35 and 26/35, bgate 22/35 and 25/35 on the loops).
Combinations were checked at that point (hop 4096, without the noise gate, so the stab numbers are
up to two files higher than above): taking the result with the higher strength of shaath and edma gives
24/35 and 26/35 on the loops (same as shaath) and 32/48 / 32/48 on the stabs (shaath alone: 31/48
/ 31/48); a majority vote of shaath, edma and bgate gives 23/35 / 26/35 and 32/48 / 33/48;
averaging the correlations of two or three profiles gives at most 24/35 / 26/35. None of them is
better than shaath alone by more than one or two files, which is within the noise of 35 + 48
files, so the simpler single profile is used. shaath was picked over edma because it is better on
the main loop set and both are equal including the relative key. With hop 1024 edma ties
shaath on the loops and is one or two files better on the stabs; that is within noise, so the
choice was not revisited after the fact. Other profiles can be selected
in the CLI with `--profile` (shaath, edma, bgate).

Notes on the numbers: names in sample packs give the producer's root note, not always the
harmonic key; several misses are loops whose pad or bass plays in a different key than the name.
Major vs. relative minor (same notes) and tonic vs. dominant/subdominant remain the most common
confusions, which is why the second-best key is shown.

## 6. Plugin (live analysis)

The plugin collects the incoming audio of its track (mono, resampled to 44.1 kHz, silent blocks
skipped, last 60 s) and re-runs sections 1-3 on everything collected about once per second on a
background thread. The displayed key is therefore the key of the last up to 60 s of audio that
went through the track. "Reset" clears the collected audio (optionally also whenever the host
transport starts). Before 2 s of non-silent audio have been collected it shows "listening...".

A looping clip is not exactly the same input as the single file: the analysis frames fall on
different positions of the loop. In close major/minor or tonic/dominant cases the result can
therefore differ from the file analysis (see the hop size note in section 2; with hop 1024 this
happened for 2 of 35 loops and 1 of 48 stabs in the looping test). `plugin/tests/live_test.cpp`
feeds files through the plugin's analyzer the way a host does (blocks of 512 samples at the file's
own rate, looped to about 20 s) and compares with the offline analysis of the same looped audio;
all checked files agree.

## 7. Reference vectors

`tests/vectors/*.wav` (mono, 44.1 kHz, 16-bit, 1-3 s, synthetic) are produced by
`scripts/make_test_vectors.py`; `tests/vectors/expected.json` holds the Python output for each
(`key`, `camelot`, `second_key`, `confidence`, `strength`, `tonality`, `clear`, `reason`,
`best_guess`, the 12 `hpcp` bins C..B and the 24 `correlations`). The C++ engine is checked against
the same file by `plugin/tests/compare_vectors.py`, which can also compare both implementations
directly on any folder: on all 502 files of the Core Library's `Samples/Loops`, `One Shots/Stab`,
`One Shots/Instrument` and `One Shots/FX` (44.1 and 48 kHz) key, second key, best guess and
"no clear key" decision are identical and confidence/strength/tonality differ by less than 0.01.
