"""Tiny synthesizers used by the tests and the test-vector generator."""
import numpy as np

SR = 44100


def note(midi, dur, sr=SR, cents=0.0, harmonics=6, amp=0.3):
    f = 440.0 * 2 ** ((midi - 69 + cents / 100.0) / 12)
    t = np.arange(int(dur * sr)) / sr
    y = np.zeros_like(t)
    for h in range(1, harmonics + 1):
        if f * h < sr / 2:
            y += np.sin(2 * np.pi * f * h * t) / h
    env = np.minimum(1, np.minimum(t / 0.02, (dur - t) / 0.05))
    return amp * y * env


def chord(midis, dur, **kw):
    return sum(note(m, dur, **kw) for m in midis)


def progression(tonic_pc, minor, cents=0.0, dur=1.0):
    """I-IV-V-I (major) or i-iv-V-i (minor) triads around the tonic, octave 3-4."""
    base = 48 + tonic_pc

    def triad(root, minor_chord):
        return [root, root + (3 if minor_chord else 4), root + 7]
    chords = [triad(base, minor), triad(base + 5, minor), triad(base + 7, False), triad(base, minor)]
    return np.concatenate([chord(c, dur, cents=cents) for c in chords])


def scale(tonic_pc, minor, cents=0.0, dur=0.4):
    steps = [0, 2, 3, 5, 7, 8, 10, 12] if minor else [0, 2, 4, 5, 7, 9, 11, 12]
    return np.concatenate([note(60 + tonic_pc + s, dur, cents=cents) for s in steps])
