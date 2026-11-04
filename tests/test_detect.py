import json
import os
import shutil
import subprocess

import essentia.standard as es
import numpy as np
import pytest
import soundfile as sf
from scipy.signal import butter, sosfilt

from keydetect.audio import load_audio
from keydetect.cli import collect_files, main
from keydetect.detect import (HOP_SIZE, SR as ANALYSIS_SR, _ESS_PC, analyze, camelot, confidence_from_strength,
                              detect_key, fitting_keys, key_name, note_name)

from synth import SR, note, progression, scale

HERE = os.path.dirname(os.path.abspath(__file__))
assert SR == ANALYSIS_SR


def run(y):
    return analyze(np.asarray(y, dtype=np.float32), ANALYSIS_SR)


@pytest.mark.parametrize("pc", range(12))
@pytest.mark.parametrize("minor", [False, True])
def test_triad_progressions_all_keys(pc, minor):
    r = run(progression(pc, minor))
    assert r.clear
    assert r.key == key_name(pc, minor)
    assert r.camelot == camelot(pc, minor)
    assert r.confidence > 0.5
    assert r.second_key not in (None, r.key)


@pytest.mark.parametrize("pc", range(12))
def test_major_scale_all_keys(pc):
    assert run(scale(pc, False)).key == key_name(pc, False)


@pytest.mark.parametrize("pc", [0, 3, 6, 9])
def test_minor_scale_is_minor_or_relative_major(pc):
    # a natural-minor scale has the same pitch classes as its relative major
    r = run(scale(pc, True))
    assert r.key in (key_name(pc, True), key_name((pc + 3) % 12, False))


@pytest.mark.parametrize("cents", [-30, 30])
def test_detuned_progression(cents):
    assert run(progression(9, True, cents=cents)).key == "A minor"


@pytest.mark.parametrize("pc,minor", [(0, False), (9, True), (6, True), (10, False)])
def test_same_result_as_essentia_key_extractor(pc, minor):
    """The step-by-step chain must reproduce essentia.standard.KeyExtractor exactly."""
    y = np.asarray(progression(pc, minor, cents=12), dtype=np.float32)
    y = y + np.float32(0.01) * np.random.RandomState(pc).randn(len(y)).astype(np.float32)
    for profile in ("shaath", "edma", "bgate"):
        key, scale_, strength = es.KeyExtractor(profileType=profile, hopSize=HOP_SIZE)(y)
        r = analyze(y, ANALYSIS_SR, profile)
        assert r.best_guess == key_name(_ESS_PC[key], scale_ == "minor")
        assert r.strength == pytest.approx(float(strength), abs=1e-5)


def test_correlations_rank_best_key_first():
    r = run(progression(4, False))
    corr = np.array(r.correlations)
    assert len(corr) == 24 and len(r.hpcp) == 12
    assert key_name(int(np.argmax(corr)) % 12, int(np.argmax(corr)) >= 12) == r.key
    assert r.strength == pytest.approx(float(corr.max()), abs=1e-4)
    i2 = int(np.argsort(corr)[-2])
    assert r.second_key == key_name(i2 % 12, i2 >= 12)


def test_confidence_mapping():
    assert confidence_from_strength(0.3) == 0.0
    assert confidence_from_strength(0.7) == pytest.approx(0.5)
    assert confidence_from_strength(0.99) == 1.0


def test_white_noise_has_no_clear_key():
    r = run(np.random.RandomState(0).randn(5 * SR) * 0.1)
    assert not r.clear and r.key is None
    assert r.reason == "mostly noise"


def test_bandpassed_noise_has_no_clear_key():
    sos = butter(4, [900 / (SR / 2), 1060 / (SR / 2)], btype="band", output="sos")
    y = sosfilt(sos, np.random.RandomState(1).randn(3 * SR))
    r = run(y / np.max(np.abs(y)))
    assert not r.clear and r.reason == "mostly noise"


def test_pitched_material_is_far_above_the_noise_gate():
    from keydetect.detect import NOISE_TONALITY
    assert run(note(57, 3.0)).tonality > 30 * NOISE_TONALITY
    assert run(progression(0, False)).tonality > 30 * NOISE_TONALITY


def test_silence():
    r = run(np.zeros(SR))
    assert not r.clear and r.reason == "silence"


def test_very_short_file_does_not_crash():
    assert run(note(60, 0.05)).tonality >= 0
    assert run(np.zeros(10)).reason == "silence"


def test_camelot_codes():
    assert camelot(9, True) == "8A"      # A minor
    assert camelot(0, False) == "8B"     # C major
    assert camelot(7, False) == "9B"     # G major
    assert camelot(1, True) == "12A"     # C# minor
    assert camelot(6, False) == "2B"     # F# major
    assert camelot(8, True) == "1A"      # G# minor


# ----- audio loading -------------------------------------------------------

def test_24bit_stereo_48k_wav(tmp_path):
    y = progression(2, False)
    p = tmp_path / "x.wav"
    sf.write(str(p), np.stack([y, y * 0.5], axis=1), 48000, subtype="PCM_24")
    assert detect_key(str(p)).tonality > 0.2   # loads, mixes down, resamples


@pytest.mark.parametrize("name,kw", [("a.wav", dict(subtype="PCM_24")), ("b.flac", {}),
                                     ("c.aiff", {}), ("d.ogg", {})])
def test_formats(tmp_path, name, kw):
    p = tmp_path / name
    sf.write(str(p), progression(0, False), SR, **kw)
    assert detect_key(str(p)).key == "C major"


@pytest.mark.skipif(shutil.which("afconvert") is None, reason="needs macOS afconvert")
def test_m4a_via_afconvert(tmp_path):
    wav = tmp_path / "a.wav"
    sf.write(str(wav), progression(7, False), SR, subtype="PCM_16")
    m4a = tmp_path / "a.m4a"
    subprocess.run(["afconvert", "-f", "m4af", "-d", "aac", str(wav), str(m4a)], check=True)
    assert detect_key(str(m4a)).key == "G major"


# ----- CLI -----------------------------------------------------------------

def test_cli_folder_scan_and_json(tmp_path, capsys):
    sub = tmp_path / "sub"
    sub.mkdir()
    sf.write(str(tmp_path / "amin.wav"), progression(9, True), SR)
    sf.write(str(sub / "noise.flac"), np.random.RandomState(2).randn(2 * SR) * 0.1, SR)
    (tmp_path / "notes.txt").write_text("x")
    assert len(collect_files([str(tmp_path)])) == 2
    assert main(["--json", str(tmp_path)]) == 0
    data = json.loads(capsys.readouterr().out)
    by = {os.path.basename(d["file"]): d for d in data}
    assert by["amin.wav"]["key"] == "A minor" and by["amin.wav"]["camelot"] == "8A"
    assert by["noise.flac"]["key"] is None and by["noise.flac"]["reason"] == "mostly noise"


def test_cli_text_output(tmp_path, capsys):
    sf.write(str(tmp_path / "c.wav"), progression(0, False), SR)
    assert main([str(tmp_path / "c.wav")]) == 0
    out = capsys.readouterr().out
    assert "C major" in out and "8B" in out and "confidence" in out and "2nd:" in out


def test_cli_profile_option(tmp_path, capsys):
    sf.write(str(tmp_path / "c.wav"), progression(0, False), SR)
    assert main(["--json", "--profile", "edma", str(tmp_path / "c.wav")]) == 0
    assert json.loads(capsys.readouterr().out)[0]["profile"] == "edma"


def test_cli_missing_file_returns_error(tmp_path, capsys):
    assert main([str(tmp_path / "nope.wav")]) == 1


# ----- reference vectors (the C++ engine is checked against the same file) ----

def test_reference_vectors_still_match():
    with open(os.path.join(HERE, "vectors", "expected.json")) as f:
        expected = json.load(f)
    assert len(expected) >= 5
    for name, exp in expected.items():
        got = detect_key(os.path.join(HERE, "vectors", name + ".wav")).to_dict()
        for k in ("key", "camelot", "second_key", "reason", "clear", "best_guess"):
            assert got[k] == exp[k], (name, k)
        for k in ("confidence", "strength", "tonality"):
            assert got[k] == pytest.approx(exp[k], abs=0.002), (name, k)


# ----- resonances and fitting keys for "no clear key" ---------------------------

def test_note_names():
    assert note_name(69) == "A4" and note_name(82) == "A#5" and note_name(60) == "C4"
    assert note_name(94) == "A#6" and note_name(12) == "C0"


def test_fitting_keys_all_contained():
    fits, partial = fitting_keys([10, 11, 6])          # A#, B, F#
    assert not partial
    assert fits == ["B major", "F# major", "Eb minor", "G# minor"]   # tonic/fifth among them first
    assert fitting_keys([])[0] == []


def test_fitting_keys_partial_when_no_key_contains_all():
    fits, partial = fitting_keys([0, 1, 2, 3])         # chromatic cluster: no 7-note scale has it
    assert partial and 1 <= len(fits) <= 4
    assert fitting_keys([0, 4, 7])[1] is False


def test_resonant_noise_reports_resonances_and_fits():
    y = _resonant_noise()
    r = run(y)
    assert not r.clear and r.reason == "mostly noise"
    assert r.resonances == ["A#5", "B5", "F#6"]
    for hz, want in zip(r.resonance_hz, (926, 980, 1491)):
        assert hz == pytest.approx(want, abs=15)
    assert {"B major", "F# major", "Eb minor", "G# minor"} == set(r.fits) and not r.fits_partial
    # the same sound looped (as the plugin hears it) and started elsewhere
    long = np.tile(y, 7)[12345:]
    assert run(long).resonances == r.resonances


def test_plain_noise_has_no_resonances():
    r = run(np.random.RandomState(5).randn(4 * SR) * 0.1)
    assert not r.clear and r.resonances == [] and r.fits == []
    r = run(np.random.RandomState(6).randn(SR // 4) * 0.1)      # very short: few frames
    assert r.resonances == []


def test_tonal_results_have_no_resonances():
    r = run(progression(9, True))
    assert r.clear and r.resonances == [] and r.fits == [] and not r.fits_partial


def test_cli_prints_resonances_for_no_clear_key(tmp_path, capsys):
    sf.write(str(tmp_path / "swell.wav"), _resonant_noise(), SR)
    sf.write(str(tmp_path / "c.wav"), progression(0, False), SR)
    assert main([str(tmp_path / "swell.wav")]) == 0
    out = capsys.readouterr().out
    assert "no clear key" in out and "resonances: A#5 \u00b7 B5 \u00b7 F#6" in out
    assert "fits: B major \u00b7 F# major \u00b7 Eb minor \u00b7 G# minor" in out
    assert main(["--json", str(tmp_path)]) == 0
    by = {os.path.basename(d["file"]): d for d in json.loads(capsys.readouterr().out)}
    assert by["swell.wav"]["resonances"] == ["A#5", "B5", "F#6"]
    assert by["swell.wav"]["fits_partial"] is False and len(by["swell.wav"]["resonance_hz"]) == 3
    assert by["c.wav"]["resonances"] == [] and by["c.wav"]["fits"] == []
    assert main([str(tmp_path / "c.wav")]) == 0
    assert "resonances" not in capsys.readouterr().out


def _resonant_noise():
    """White noise plus three narrow resonances at 926, 980 and 1491 Hz (A#5, B5, F#6)."""
    from scipy.signal import iirpeak, lfilter
    wn = np.random.RandomState(2).randn(3 * SR)
    y = wn.copy()
    for f, g in ((926, 4.0), (980, 3.2), (1491, 2.4)):
        b, a = iirpeak(f / (SR / 2), 100)
        y += g * lfilter(b, a, wn)
    return (0.5 * y / np.max(np.abs(y))).astype(np.float32)


ABLETON_NOISE = ("/Applications/Ableton Live 12 Suite.app/Contents/App-Resources/Core Library/"
                 "Samples/One Shots/FX/Swell Short Noise.wav")


@pytest.mark.skipif(not os.path.exists(ABLETON_NOISE), reason="needs Ableton Live 12 Suite Core Library")
def test_ableton_swell_short_noise_has_no_clear_key():
    r = detect_key(ABLETON_NOISE)
    assert not r.clear and r.reason == "mostly noise"


@pytest.mark.skipif(not os.path.exists(ABLETON_NOISE), reason="needs Ableton Live 12 Suite Core Library")
def test_ableton_swell_short_noise_resonances_and_fits():
    r = detect_key(ABLETON_NOISE)
    assert r.resonances == ["A#5", "B5", "F#6"]                  # strongest first
    for hz, want in zip(r.resonance_hz, (926, 980, 1491)):
        assert hz == pytest.approx(want, abs=8)
    assert "Eb minor" in r.fits and not r.fits_partial and len(r.fits) <= 4
    # looped, as the plugin hears it
    y, sr = load_audio(ABLETON_NOISE, ANALYSIS_SR)
    looped = analyze(np.tile(y, 8), ANALYSIS_SR)
    assert looped.resonances == r.resonances and looped.fits == r.fits
