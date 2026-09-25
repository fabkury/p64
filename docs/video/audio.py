#!/usr/bin/env python3
"""Writes the video's soundtrack: a warm, slow ambient synth bed with subtle sound effects.

    python audio.py [--out build/audio.wav] [--mux ../p64b-concept-v4.mp4 ../p64b-concept-v4-audio.mp4]

Synthesised from scratch (no samples, nothing to license): filtered detuned sawtooth pads,
a sine sub bass, a sparse plucked motif, a soft pulse under the explosion and the finale,
and a short synthetic reverb. The effects follow storyboard.py: a filtered whoosh as the
parts fly out and back, quiet clicks on the knob detents and presses, a small chime on the
like. With --mux, ffmpeg normalises to -18 LUFS (quiet, for embedding) and muxes the track
into a copy of the silent video (the video stream is copied, not re-encoded).

The first version (2026-09-25) was a lo-fi chiptune loop with a drum kit; the user found
the sound and the tune wrong for the picture, and this replaced it the same day.
"""

import argparse
import os
import subprocess

import numpy as np
from scipy.ndimage import uniform_filter1d
from scipy.signal import butter, fftconvolve, sosfilt

import storyboard as sb

HERE = os.path.dirname(os.path.abspath(__file__))
SR = 48000
RNG = np.random.default_rng(64)

# --- harmony: one warm chord every 6.25 s, eight over the 50 s, in D major with added
# ninths and sixths so nothing pulls hard; midi roots and chord tones as semitones
CHORDS = [
    (50, (0, 7, 11, 14, 16)),   # Dmaj9       the opening
    (47, (0, 7, 10, 14, 17)),   # Bm11
    (55, (0, 7, 11, 14, 16)),   # Gmaj9       the explosion lifts onto this
    (45, (0, 7, 10, 14, 17)),   # Am11
    (50, (0, 7, 11, 14, 16)),   # Dmaj9       the knob scene
    (47, (0, 7, 10, 14, 17)),   # Bm11
    (55, (0, 7, 11, 14, 16)),   # Gmaj9       the frontal stretch
    (50, (0, 7, 11, 14, 16)),   # Dmaj9       the close
]
CHORD_LEN = 6.25
PENTA = [0, 2, 4, 7, 9]         # the motif's scale, over D


def midi(n):
    return 440.0 * 2 ** ((n - 69) / 12)


def t_axis(n):
    return np.arange(n) / SR


def lowpass(x, fc, order=2):
    sos = butter(order, min(fc, SR / 2 - 1) / (SR / 2), btype="low", output="sos")
    return sosfilt(sos, x, axis=0) if x.ndim == 2 else sosfilt(sos, x)


def highpass(x, fc, order=2):
    sos = butter(order, fc / (SR / 2), btype="high", output="sos")
    return sosfilt(sos, x)


def bandpass(x, lo, hi):
    return highpass(lowpass(x, hi), lo)


def env(n, a, d, s, r, hold):
    t = t_axis(n)
    e = np.clip(t / max(a, 1e-4), 0, 1)
    e = e * (1 - np.clip((t - a) / max(d, 1e-4), 0, 1) * (1 - s))
    e = e * (1 - np.clip((t - hold) / max(r, 1e-4), 0, 1))
    return np.clip(e, 0, 1)


def place(mix, start, sig, pan=0.0):
    """Adds a mono signal into the stereo mix at start seconds, pan -1 (left) .. 1 (right)."""
    i = int(start * SR)
    j = min(len(mix), i + len(sig))
    if j <= i:
        return
    g = np.array([np.cos((pan + 1) * np.pi / 4), np.sin((pan + 1) * np.pi / 4)])
    mix[i:j] += sig[: j - i, None] * g


def saw(f, t):
    """A sawtooth with its harmonics rolled off (the sum of the first 24), so it stays warm."""
    out = np.zeros_like(t)
    for k in range(1, 25):
        out += np.sin(2 * np.pi * f * k * t) / k
    return out * 2 / np.pi


def smooth_gain(n, fn, seconds=1.0):
    step = SR // 20
    pts = np.array([fn(i * step / SR) for i in range(n // step + 2)])
    g = np.repeat(pts, step)[:n]
    return uniform_filter1d(g, int(seconds * SR), mode="nearest")


# --- the arrangement, from the storyboard's beats ---------------------------------------
def pad_gain(t):
    if t < 7.0:
        return 0.75
    if t < sb.COLLAPSE_T1:
        return 1.0
    if t < sb.KNOB_SCENE[1]:
        return 0.7
    if t < sb.FADE_OUT[0]:
        return 1.0
    return 0.8


def pad_cutoff(t):                                   # the filter opens with the picture
    if t < 7.0:
        return 900
    if t < sb.COLLAPSE_T1:
        return 2600
    if t < sb.KNOB_SCENE[1]:
        return 1400
    if t < sb.FADE_OUT[0]:
        return 3000
    return 1200


def pulse_gain(t):                                   # the soft pulse: under the explosion and the finale only
    if 8.0 <= t < sb.COLLAPSE_T1 - 0.5:
        return 1.0
    if sb.STUDIO_DIM[1] <= t < sb.FADE_OUT[0]:
        return 1.0
    return 0.0


def motif_density(t):                                # plucked notes per second, roughly
    if t < 7.0:
        return 0.25
    if t < sb.COLLAPSE_T1:
        return 0.55
    if t < sb.KNOB_SCENE[1]:
        return 0.2
    if t < sb.FADE_OUT[0]:
        return 0.7
    return 0.0


def chord_at(t):
    return CHORDS[min(int(t / CHORD_LEN), len(CHORDS) - 1)]


# --- instruments ------------------------------------------------------------------------
def render_pads(n):
    mix = np.zeros((n, 2))
    for i, (root, tones) in enumerate(CHORDS):
        t0 = i * CHORD_LEN
        m = int((CHORD_LEN + 3.0) * SR)
        t = t_axis(m)
        e = env(m, 2.2, 1.0, 0.85, 3.0, CHORD_LEN)
        for j, k in enumerate(tones):
            f = midi(root + 12 + k)
            for det, pan in ((-0.6, -0.7), (0.0, 0.0), (0.6, 0.7)):
                v = saw(f * 2 ** (det / 1200), t + RNG.uniform(0, 1))
                place(mix, t0, v * e * 0.09 / len(tones) * 5 * (0.7 if j == 0 else 1.0), pan)
    return mix


def render_sub(n):
    mix = np.zeros((n, 2))
    for i, (root, tones) in enumerate(CHORDS):
        t0 = i * CHORD_LEN
        m = int((CHORD_LEN + 1.5) * SR)
        t = t_axis(m)
        f = midi(root - 12)
        v = np.sin(2 * np.pi * f * t) + 0.15 * np.sin(2 * np.pi * 2 * f * t)
        place(mix, t0, v * env(m, 1.5, 1.0, 0.9, 1.5, CHORD_LEN) * 0.35)
    return mix


def pluck(f, length=2.4):
    """A soft plucked note: a few decaying partials with a rounded attack, piano-like."""
    m = int(length * SR)
    t = t_axis(m)
    v = np.zeros(m)
    for k, amp, dec in ((1, 1.0, 1.1), (2, 0.35, 0.5), (3, 0.15, 0.3), (4, 0.06, 0.2)):
        v += amp * np.sin(2 * np.pi * f * k * t + RNG.uniform(0, 6.28)) * np.exp(-t / dec)
    return v * np.clip(t / 0.008, 0, 1) * np.clip((length - t) / 0.3, 0, 1)


def render_motif(n, duration):
    mix = np.zeros((n, 2))
    t = 0.6
    last = None
    while t < duration:
        d = motif_density(t)
        if d <= 0:
            t += 1.0
            continue
        root, _ = chord_at(t)
        choices = [x for x in PENTA if x != last]
        deg = choices[RNG.integers(len(choices))]
        last = deg
        octave = 24 if RNG.random() < 0.7 else 36
        f = midi(root + octave + deg)
        place(mix, t, pluck(f) * 0.28, RNG.uniform(-0.5, 0.5))
        t += RNG.uniform(0.6, 1.6) / d
    return mix


def render_pulse(n, duration):
    """A soft, slow pulse: a rounded low thump every 1.5 s with a breath of filtered noise."""
    mix = np.zeros((n, 2))
    t = 0.0
    while t < duration:
        m = int(0.9 * SR)
        tt = t_axis(m)
        f = 48 * np.exp(-tt * 9) + 46
        thump = np.sin(2 * np.pi * np.cumsum(f) / SR) * env(m, 0.01, 0.35, 0.0, 0.1, 0.3)
        breath = bandpass(RNG.standard_normal(m), 400, 2500) * env(m, 0.15, 0.5, 0.0, 0.2, 0.4) * 0.12
        g = pulse_gain(t)
        place(mix, t, (thump * 0.55 + breath) * g)
        t += 1.5
    return mix


# --- sound effects from the storyboard --------------------------------------------------
def whoosh(length, rising=True, gain=0.5):
    m = int(length * SR)
    t = t_axis(m)
    noise = RNG.standard_normal(m)
    # a band that sweeps up (out) or down (back), the level a smooth hump
    sweep = (t / length) if rising else (1 - t / length)
    out = np.zeros(m)
    seg = SR // 10
    for i in range(0, m, seg):
        c = 300 + 2200 * sweep[min(i, m - 1)] ** 1.6
        out[i:i + seg] = bandpass(noise[i:i + seg], c * 0.6, c * 1.5)
    hump = np.sin(np.pi * np.clip(t / length, 0, 1)) ** 1.5
    return out * hump * gain


def click(gain=0.25, tone=2400.0):
    m = int(0.05 * SR)
    t = t_axis(m)
    return (bandpass(RNG.standard_normal(m), tone * 0.5, tone * 2) * np.exp(-t * 180)
            + 0.3 * np.sin(2 * np.pi * tone * t) * np.exp(-t * 300)) * gain


def chime(gain=0.3):
    m = int(1.8 * SR)
    t = t_axis(m)
    v = np.zeros(m)
    for i, f in enumerate((midi(86), midi(93))):        # D6 then A6, a fifth up, a beat apart
        d = np.clip(t - 0.16 * i, 0, None)
        v += np.sin(2 * np.pi * f * d) * np.exp(-d * 2.2) * (d > 0) * (0.6 + 0.4 * i)
        v += 0.3 * np.sin(2 * np.pi * f * 3.01 * d) * np.exp(-d * 6) * (d > 0)
    return v * gain


def smooth(x):
    x = np.clip(x, 0.0, 1.0)
    return x * x * x * (x * (x * 6 - 15) + 10)


def render_fx(n):
    mix = np.zeros((n, 2))
    place(mix, sb.EXPLODE_T0 - 0.2, whoosh(sb.EXPLODE_T1 - sb.EXPLODE_T0 + 0.8, rising=True, gain=0.45))
    place(mix, sb.COLLAPSE_T0 - 0.2, whoosh(sb.COLLAPSE_T1 - sb.COLLAPSE_T0 + 0.8, rising=False, gain=0.4))
    for t0, t1, knob, kind, amount in sb.KNOB_ACTIONS:
        pan = -0.35 if knob == 1 else 0.35           # knob 1 is the left one seen from the back
        if kind == "turn":
            detents = max(1, int(round(abs(amount) / 15)))   # a 24-detent encoder: 15 degrees each
            for d in range(1, detents + 1):
                # the detent lands where the eased angle passes d/detents of the turn
                frac = d / detents
                lo, hi = 0.0, 1.0
                for _ in range(24):
                    mid = (lo + hi) / 2
                    lo, hi = (mid, hi) if smooth(mid) < frac else (lo, mid)
                place(mix, t0 + (t1 - t0) * lo, click(0.16, 3000), pan)
        else:
            place(mix, t0, click(0.3, 1600), pan)
            place(mix, t1, click(0.18, 1200), pan)
    place(mix, sb.HEART[0], chime(0.28), 0.35)
    return mix


def reverb(x, seconds=1.6, mix_level=0.28):
    m = int(seconds * SR)
    t = t_axis(m)
    ir = RNG.standard_normal((m, 2)) * np.exp(-t / (seconds / 4))[:, None]
    ir = lowpass(ir, 4000)
    ir /= np.sqrt(np.sum(ir ** 2, axis=0)) * 2.2
    wet = np.stack([fftconvolve(x[:, c], ir[:, c])[: len(x)] for c in range(2)], axis=1)
    return x + wet * mix_level


def render(duration):
    n = int(duration * SR)
    pads = render_pads(n)
    # the pads' filter opens and closes with the picture
    cut = smooth_gain(n, pad_cutoff, 1.5)
    out = np.zeros_like(pads)
    seg = SR // 4
    for i in range(0, n, seg):
        fc = float(cut[min(i, n - 1)])
        sos = butter(2, fc / (SR / 2), btype="low", output="sos")
        out[i:i + seg] = sosfilt(sos, pads[i:i + seg], axis=0)
    pads = out * smooth_gain(n, pad_gain, 1.2)[:, None]
    sub = render_sub(n)
    motif = lowpass(render_motif(n, duration), 6000)
    pulse = render_pulse(n, duration)
    fx = render_fx(n)
    music = reverb(pads * 0.9 + motif * 0.9, 1.8, 0.32) + sub + pulse
    mix = music + reverb(fx, 0.9, 0.18)
    t = t_axis(n)
    mix *= np.clip(t / sb.FADE_IN[1], 0, 1)[:, None]
    mix *= np.clip(1 - (t - sb.FADE_OUT[0]) / (sb.FADE_OUT[1] - sb.FADE_OUT[0]) * 0.5, 0.5, 1)[:, None]
    mix *= np.clip((duration - t) / 1.5, 0, 1)[:, None]
    mix /= np.max(np.abs(mix)) + 1e-9
    return (mix * 0.8).astype(np.float32)


def write_wav(path, x):
    import wave
    with wave.open(path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes((np.clip(x, -1, 1) * 32767).astype(np.int16).tobytes())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(HERE, "build", "audio.wav"))
    ap.add_argument("--mux", nargs=2, metavar=("VIDEO_IN", "VIDEO_OUT"))
    ap.add_argument("--lufs", type=float, default=-18.0)
    a = ap.parse_args()
    os.makedirs(os.path.dirname(a.out), exist_ok=True)
    write_wav(a.out, render(sb.DURATION))
    print("wrote", a.out)
    if a.mux:
        vin, vout = (os.path.abspath(p) for p in a.mux)
        subprocess.run(["ffmpeg", "-y", "-i", vin, "-i", a.out, "-map", "0:v", "-map", "1:a", "-c:v", "copy",
                        "-af", f"loudnorm=I={a.lufs}:TP=-1.5:LRA=11", "-c:a", "aac", "-b:a", "192k", "-shortest",
                        "-movflags", "+faststart", vout], check=True, capture_output=True)
        print("wrote", vout)


if __name__ == "__main__":
    main()
