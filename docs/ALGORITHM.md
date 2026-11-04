# Detection algorithm (reference specification)

The Python implementation in `keydetect/detect.py` and `keydetect/audio.py` is the
reference. This document describes it exactly so it can be ported (e.g. to C++/JUCE).
Check a port against `tests/vectors/` (see the end of this file).

All constants are named as in `detect.py`.

## 1. Input

1. Decode to float samples. Mix down to mono by averaging all channels.
2. Resample to **22050 Hz** (`SR`) using a polyphase FIR resampler
   (`scipy.signal.resample_poly`: `up = 22050/g`, `down = sr/g`, `g = gcd`, Kaiser
   window beta 5.0, cutoff at the lower Nyquist). Any good anti-aliased resampler is fine; for
   44.1/88.2 kHz input this is a plain 2x/4x decimation.
3. If longer than `MAX_SECONDS = 240` s, keep only the centre 240 s
   (`y[mid-half : mid+half]`, `mid = len/2`, `half = 240*SR/2`).
4. Subtract the mean (DC removal).
5. If `max(|y|) < 1e-5` -> result "silence": no clear key, tuning 0, tonality 0, reason `"silence"`,
   no strongest frequency. Stop.

## 2. STFT

* `N_FFT = 8192`, `HOP = 2048`, window = Hann as `numpy.hanning(8192)`
  (the *symmetric* form, `0.5 - 0.5*cos(2*pi*n/(N-1))`, n = 0..N-1).
* No centre padding. Frame `t` covers samples `[t*HOP, t*HOP + N_FFT)`;
  number of frames = `1 + (len - N_FFT) // HOP`. If the signal is shorter than `N_FFT`, zero-pad
  it at the end to `N_FFT` (one frame).
* Magnitude spectrum `|rfft(frame * window)|`, bins `k = 0..4096`, `f_k = k * 22050/8192`
  (bin width 2.6917 Hz). Magnitudes are stored as float32 (precision is not critical).

## 3. Analysis band

* `lo = first bin with f_k >= 50 Hz` (`FMIN`) = 19, `hi = first bin with f_k >= 5000 Hz`
  (`FMAX`) = 1858.
* The band array uses bins `lo-1 .. hi` inclusive (one guard bin below and above): 1841 bins.
  Band index `j` corresponds to FFT bin `lo-1+j`. `power = mag^2` (float64).
* Frame energy `E_t = sum_j power[t, j]` (whole band). A frame is **kept** if
  `E_t > 1e-4 * max_t(E_t)`; non-kept frames contribute nothing anywhere below.

## 4. Peak picking

* `local[t, j]` = moving average of `power[t, :]` over 41 bins centred on j
  (`uniform_filter1d(size=41, mode="nearest")`, i.e. edge replicate), plus `1e-20`.
* Band index `j` in `1 .. n-2` of a kept frame is a **peak** if
  `power[j] > power[j-1]` and `power[j] >= power[j+1]` and `power[j] > PEAK_RATIO * local[j]`,
  `PEAK_RATIO = 10`.
* Frequency refinement (parabolic interpolation on log magnitude):
  `a=ln(mag[j-1]+1e-12), b=ln(mag[j]+1e-12), c=ln(mag[j+1]+1e-12)`,
  `delta = 0.5*(a-c)/(a-2b+c)` (0 if `|a-2b+c| <= 1e-9`), clipped to [-0.5, 0.5];
  `f_peak = f_{lo-1} + (j + delta) * 2.6917` (bin width = `22050/8192`).
  Peak magnitude `m = mag[t, j]` (not interpolated).
* `n_peaks` = total number of peaks over all kept frames.

## 5. Tonality, strongest frequency

* `tonality = sum over kept frames of (sum of power at that frame's peaks) / sum over kept frames of E_t`,
  clipped to [0, 1]. (Share of energy that sits in clear spectral peaks; ~0 for noise.)
* Long-term spectrum = mean of `power[t, :]` over kept frames, smoothed with a 3-bin moving
  average (`uniform_filter1d(size=3)`, mode reflect). `k = argmax`. `strongest_hz = f_{lo-1+k}`; if
  `0 < k < n-1`, refine with the same log-parabolic interpolation on that smoothed spectrum.
  `strongest_note` = nearest 12-TET note name (sharps, A4 = 440, C4 = MIDI 60, so `midi 69 -> "A4"`).
  Reported for noisy material ("strongest around 980 Hz").

## 6. Early exit: too few peaks

If `n_peaks < 8`: no clear key, `tuning_cents = 0`, `reason = "mostly noise"` if
`tonality < NOISE_TONALITY` else `"no pitched content"`, no chroma/correlations. Stop.

## 7. Tuning estimate (cents, relative to A4 = 440 Hz)

For every peak: `midi = 69 + 12*log2(f_peak/440)`, weight `w = sqrt(m)`.

```
z      = sum(w * exp(i*2*pi*midi)) / sum(w)         # circular mean, period = 1 semitone
tuning = angle(z) / (2*pi) * 100                     # cents, in (-50, +50]
```

Positive = the music is sharp of A440.

## 8. Chroma

For every peak: `midi_c = midi - tuning/100`, `nearest = round(midi_c)`, `resid = midi_c - nearest`,
`pc = nearest mod 12` (C = 0 ... B = 11; integer modulo with a non-negative result).

```
reg = exp(-0.5 * ((nearest - REG_CENTER) / REG_SIGMA)^2)       # REG_CENTER=55 (G3), REG_SIGMA=20
wt  = m^MAG_EXP * reg * (0.5 + 0.5*cos(2*pi*resid))            # MAG_EXP=0.5
frame_chroma[t, pc] += wt
```

* Each frame's 12-vector is normalised to sum 1 (frames with sum 0 stay 0).
* Frame weight `fw_t = sqrt(E_t)` for kept frames (0 otherwise).
* `chroma = sum_t fw_t * frame_chroma[t]`, then normalised to sum 1. If the sum is 0:
  no clear key, `reason = "no pitched content"`.

## 9. Key profiles

Index = pitch class relative to the key's tonic (0 = tonic). Two profile sets are used.

| step | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 |
|------|---|---|---|---|---|---|---|---|---|---|----|----|
| KK major | 6.35 | 2.23 | 3.48 | 2.33 | 4.38 | 4.09 | 2.52 | 5.19 | 2.39 | 3.66 | 2.29 | 2.88 |
| KK minor | 6.33 | 2.68 | 3.52 | 5.38 | 2.60 | 3.53 | 2.54 | 4.75 | 3.98 | 2.69 | 3.34 | 3.17 |
| Temperley major | 5.0 | 2.0 | 3.5 | 2.0 | 4.5 | 4.0 | 2.0 | 4.5 | 2.0 | 3.5 | 1.5 | 4.0 |
| Temperley minor | 5.0 | 2.0 | 3.5 | 4.5 | 2.0 | 4.0 | 2.0 | 4.5 | 3.5 | 2.0 | 1.5 | 4.0 |

KK = Krumhansl-Kessler (1982) probe-tone profiles; Temperley = Temperley (2001, Kostka-Payne
based). Key `tonic` is the profile rotated so that `profile_key[(tonic + step) mod 12] = profile[step]`
(`numpy.roll(profile, tonic)`). Each of the 4 x 12 rotated vectors is mean-subtracted and scaled to
unit L2 norm.

## 10. Correlation, confidence

```
c    = chroma - mean(chroma);   c = c / ||c||        (if ||c|| < 1e-12: all correlations = 0)
corr[key] = 0.5 * ( dot(KK_key, c) + dot(Temperley_key, c) )    # Pearson, averaged over the 2 sets
```

Key index 0..11 = C major .. B major, 12..23 = C minor .. B minor. `b1`, `b2` = indices of the
largest and second-largest value.

```
margin     = corr[b1] - corr[b2]
confidence = clip(margin / 0.15, 0, 1)
```

The second-best key is always reported (it is very often the relative or the dominant key).

## 11. "No clear key" decision

```
reason = "mostly noise"         if tonality < NOISE_TONALITY      (0.05)
         "ambiguous / atonal"   elif corr[b1] < MIN_BEST_CORR (0.35) or confidence < MIN_CONFIDENCE (0.02)
         ""                     otherwise   -> clear key
```

When not clear, `key`/`camelot` are null, `best_guess` still holds the top key, and
`strongest_hz`/`strongest_note` identify the dominant spectral region.

## 12. Names and Camelot codes

* Major names: `C Db D Eb E F F# G Ab A Bb B`; minor names: `C C# D Eb E F F# G G# A Bb B`.
* Camelot number: major `((7*pc + 7) mod 12) + 1`, suffix `B`; minor uses the relative major
  `rm = (pc+3) mod 12`: `((7*rm + 7) mod 12) + 1`, suffix `A`. (C major = 8B, A minor = 8A,
  G major = 9B, C# minor = 12A.)

## 13. Reference vectors for ports

`tests/vectors/*.wav` (mono, 44.1 kHz, 16-bit PCM, 1-3 s) are produced by
`scripts/make_test_vectors.py`; `tests/vectors/expected.json` holds the output of the Python
reference for each (`key`, `camelot`, `second_key`, `confidence`, `tuning_cents`, `tonality`,
`clear`, `reason`, `strongest_hz`, `n_peaks`, the 12-bin `chroma` and all 24 `correlations`).
Suggested tolerances for a port: key/camelot/second_key/clear identical; `tuning_cents` +-0.5;
`confidence` +-0.03; `tonality` +-0.02; each chroma bin +-0.01; `n_peaks` +-3 %. Differences
mainly come from the resampler and float32/float64 rounding. Noise/silence vectors stop early
(sections 1 and 6), so they carry no chroma.

Regenerate expectations after any deliberate algorithm change with
`python scripts/make_test_vectors.py`.
