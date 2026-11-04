import json
import os
import shutil
import subprocess

import numpy as np
import pytest
import soundfile as sf
from scipy.signal import butter, resample_poly, sosfilt

from keydetect.cli import collect_files, main
from keydetect.detect import SR as ANALYSIS_SR, analyze, camelot, detect_key, key_name

from synth import SR, note, progression, scale

HERE = os.path.dirname(os.path.abspath(__file__))


def run(y, sr=SR):
    return analyze(resample_poly(y, ANALYSIS_SR, sr).astype(np.float32), ANALYSIS_SR)


@pytest.mark.parametrize("pc", range(12))
@pytest.mark.parametrize("minor", [False, True])
def test_triad_progressions_all_keys(pc, minor):
    r = run(progression(pc, minor))
    assert r.clear
    assert r.key == key_name(pc, minor)
    assert r.confidence > 0.0


@pytest.mark.parametrize("pc", range(12))
def test_major_scale_all_keys(pc):
    assert run(scale(pc, False)).key == key_name(pc, False)


@pytest.mark.parametrize("pc", [0, 3, 6, 9])
def test_minor_scale_is_minor_or_relative_major(pc):
    # a natural-minor scale has the same pitch classes as its relative major
    r = run(scale(pc, True))
    assert r.key in (key_name(pc, True), key_name((pc + 3) % 12, False))


@pytest.mark.parametrize("cents", [-30, 30])
def test_detuned_progression_key_and_tuning(cents):
    r = run(progression(9, True, cents=cents))
    assert r.key == "A minor"
    assert abs(r.tuning_cents - cents) < 4


def test_in_tune_has_zero_tuning():
    assert abs(run(progression(0, False)).tuning_cents) < 4


@pytest.mark.parametrize("midi", [45, 57, 62, 66])
def test_single_sustained_note_with_harmonics(midi):
    r = run(note(midi, 3.0, cents=30))
    assert abs(r.tuning_cents - 30) < 4
    assert int(np.argmax(r.chroma)) == midi % 12   # fundamental's pitch class dominates
    assert r.tonality > 0.3


def test_harmonic_downweighting_reduces_fifth_bias(monkeypatch):
    # a bass-heavy ostinato on C: the 3rd harmonic of every C sits on G
    y = np.concatenate([note(36, 0.5, harmonics=8), note(36, 0.5, harmonics=8), note(39, 0.5, harmonics=8),
                        note(36, 0.5, harmonics=8)] * 2)
    with_discount = run(y)
    monkeypatch.setattr("keydetect.detect.HARM_K", 0.0)
    without = run(y)
    assert with_discount.chroma[7] < without.chroma[7]           # less G
    assert int(np.argmax(with_discount.chroma)) == 0              # C stays on top


def test_white_noise_has_no_clear_key():
    r = run(np.random.RandomState(0).randn(5 * SR) * 0.1)
    assert not r.clear and r.key is None
    assert r.reason == "mostly noise"


def test_bandpassed_noise_reports_strongest_frequency():
    sos = butter(4, [900 / (SR / 2), 1060 / (SR / 2)], btype="band", output="sos")
    y = sosfilt(sos, np.random.RandomState(1).randn(3 * SR))
    r = run(y / np.max(np.abs(y)))
    assert not r.clear
    assert 900 < r.strongest_hz < 1060


def test_silence():
    r = run(np.zeros(SR))
    assert not r.clear and r.reason == "silence"


def test_very_short_file_does_not_crash():
    assert run(note(60, 0.1)).tonality >= 0


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
    assert by["noise.flac"]["key"] is None


def test_cli_text_output(tmp_path, capsys):
    sf.write(str(tmp_path / "c.wav"), progression(0, False), SR)
    assert main([str(tmp_path / "c.wav")]) == 0
    out = capsys.readouterr().out
    assert "C major" in out and "8B" in out and "cents" in out


def test_cli_missing_file_returns_error(tmp_path, capsys):
    assert main([str(tmp_path / "nope.wav")]) == 1


# ----- reference vectors for ports ---------------------------------------------

def test_reference_vectors_still_match():
    with open(os.path.join(HERE, "vectors", "expected.json")) as f:
        expected = json.load(f)
    assert len(expected) >= 5
    for name, exp in expected.items():
        got = detect_key(os.path.join(HERE, "vectors", name + ".wav")).to_dict()
        for k in ("key", "camelot", "second_key", "reason", "clear"):
            assert got[k] == exp[k], (name, k)
        assert got["tuning_cents"] == pytest.approx(exp["tuning_cents"], abs=0.2)
        assert got["confidence"] == pytest.approx(exp["confidence"], abs=0.02)
        assert got["tonality"] == pytest.approx(exp["tonality"], abs=0.01)
