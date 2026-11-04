# audio-key-detector

A small, fast command line tool that detects the musical key of audio samples and loops.
Built for producers: drop in a folder of samples, get key, Camelot code, confidence and tuning.
Only `numpy`, `scipy` and `soundfile` are needed (no librosa, no ML models).

```
$ keydetect "Piano Warped G Minor 98 bpm.wav" "Swell Short Noise.wav"
Piano Warped G Minor 98 bpm.wav
  G minor  (6A)  confidence 1.00  |  2nd: Eb major (5B)  |  tuning -1 cents
Swell Short Noise.wav
  no clear key  (mostly noise; best guess Bb minor, confidence 0.70)  strongest around 926 Hz (A#5)
```

## Install

Python 3.9 or newer. On macOS (Apple Silicon or Intel):

```
pipx install git+https://github.com/tobiasbarthold/audio-key-detector
```

or from a clone: `pip install .` (preferably in a venv).

WAV, AIFF, FLAC and OGG are read directly. MP3 and M4A are decoded through macOS `afconvert`
(or `ffmpeg` if installed); the audio is read-only and nothing is written next to your files.

## Usage

```
keydetect <files or folders...> [--json]
```

* Folders are scanned recursively for wav / aif / aiff / flac / mp3 / m4a / ogg.
* Per file you get: key, Camelot code, confidence 0..1, second-best key, estimated tuning
  offset in cents (positive = sharp of A440).
* `--json` prints a JSON array with the same data (plus `tonality`, and for noisy material
  `strongest_hz` / `strongest_note`).
* Exit code is 1 if any file could not be read.

**Drag and drop:** type `keydetect ` (with the trailing space) in Terminal, then drag files or
folders from Finder - or from the Ableton browser via "Show in Finder" - into the Terminal window and press
Return. Terminal pastes the paths for you.

## How it works

Short version (the full, exact specification is in [docs/ALGORITHM.md](docs/ALGORITHM.md)):

1. Mono, 22.05 kHz, STFT (8192-sample Hann window, hop 2048).
2. Pick clear spectral peaks between 50 Hz and 5 kHz (so rumble and hiss are ignored) and
   refine their frequency.
3. Estimate the tuning of the whole file from how far the peaks sit from the 12-tone grid and
   compensate it before folding to 12 pitch classes (so a sample detuned by +30 cents still
   lands on the right notes).
4. Fold the peaks into a chroma vector, weighted towards the low-mid register.
5. Correlate the chroma with 24 key profiles. Two profile sets are used - Krumhansl-Kessler
   and Temperley - and their correlations are averaged, which reduces the known bias of
   either one (KK tends to major and the dominant, Temperley is flatter).
6. Confidence is the margin between the best and second-best key, scaled to 0..1.
7. Mostly noisy or atonal material is reported as "no clear key" together with the strongest
   spectral region, instead of a fake key.

## Accuracy and limits

Honest numbers: on the 35 factory loops of Ableton Live 12 Suite whose file names state a key
(`Loops/`), the detected key matches the name in 13 cases (37 %), another 4 are the relative
major/minor (49 % with those), and 18 are wrong or "no clear key". Names in loop packs are the
producer's root note, not always the harmonic key, so treat the number as a rough guide. Full
table: run `python scripts/eval_named_loops.py <folder>` on your own library.

* **Relative major/minor** (e.g. A minor vs C major) share the same notes and are the most common
  confusion; the second-best key is printed for exactly this reason.
  A bare natural-minor scale is genuinely ambiguous.
* **Dominant/subdominant confusion** is the second most common error, especially for bass-heavy
  loops and short ostinatos that never state the tonic.
* **Drums, noise, risers, atonal FX**: reported as "no clear key" (tonality below 5 %). Loops
  with a lot of distorted or heavily detuned layers can fall into this bucket although they have
  a key.
* Short one-shots with a single note give the note's family of keys, not a real key: the tool
  has no harmony to look at.
* Modulations and mixed-key loops produce a blend; only the middle 4 minutes of very long files
  are analysed.
* Assumes equal temperament; fixed tuning offsets up to +-50 cents are handled automatically.

## Development

```
python3 -m venv .venv && .venv/bin/pip install -e '.[dev]'
.venv/bin/python -m pytest
```

`tests/vectors/` contains small synthetic WAVs with the reference output (for ports);
regenerate with `python scripts/make_test_vectors.py`.

## License

GNU Affero General Public License v3.0 - see [LICENSE](LICENSE). Copyright (C) 2026 Tobias Barthold.
