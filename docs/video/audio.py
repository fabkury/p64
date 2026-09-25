#!/usr/bin/env python3
"""Writes the video's music bed: a lo-fi chiptune track synthesised from scratch.

    python audio.py [--out build/audio.wav] [--mux ../p64b-concept-v4.mp4 ../p64b-concept-v4-audio.mp4]

No samples, no licensing: square, triangle and pulse waves, noise drums, a low-pass "tape"
tone, a slow wobble, all from numpy. The arrangement follows storyboard.py: soft under the
opening, a lift at the explosion, sparse and clicky under the knob scene, full for the
last, frontal stretch, a fade with the picture and one held chord over the end card.
With --mux, ffmpeg normalises it to -18 LUFS (quiet, for embedding) and muxes it into a
copy of the silent video (the video stream is copied, not re-encoded).
"""

import argparse
import os
import subprocess

import numpy as np
from scipy.ndimage import uniform_filter1d
from scipy.signal import butter, sosfilt

import storyboard as sb

HERE = os.path.dirname(os.path.abspath(__file__))
SR = 48000
BPM = 84
BEAT = 60.0 / BPM
BAR = 4 * BEAT
RNG = np.random.default_rng(64)

# --- the harmony: a dreamy loop in A minor, one chord per bar -----------------------
# (root midi, chord tones as semitone offsets)
PROG = [(57, (0, 3, 7, 10)),      # Am7
        (53, (0, 4, 7, 11)),      # Fmaj7
        (48, (0, 4, 7, 11)),      # Cmaj7
        (55, (0, 4, 7, 10))]      # G7


def midi(n):
    return 440.0 * 2 ** ((n - 69) / 12)


def t_axis(n):
    return np.arange(n) / SR


def square(f, t, duty=0.5):
    return np.where((t * f) % 1.0 < duty, 1.0, -1.0)


def triangle(f, t):
    return 2 * np.abs(2 * ((t * f) % 1.0) - 1) - 1


def env(n, a, d, s, r, hold):
    """ADSR in seconds over n samples; hold is the gate length."""
    t = t_axis(n)
    e = np.zeros(n)
    e += np.clip(t / max(a, 1e-4), 0, 1)
    dec = np.clip((t - a) / max(d, 1e-4), 0, 1)
    e = e * (1 - dec * (1 - s))
    rel = np.clip((t - hold) / max(r, 1e-4), 0, 1)
    e = e * (1 - rel)
    return np.clip(e, 0, 1)


def lowpass(x, fc, order=2):
    sos = butter(order, fc / (SR / 2), btype="low", output="sos")
    return sosfilt(sos, x)


def highpass(x, fc, order=2):
    sos = butter(order, fc / (SR / 2), btype="high", output="sos")
    return sosfilt(sos, x)


def place(mix, start, sig):
    i = int(start * SR)
    j = min(len(mix), i + len(sig))
    if j > i:
        mix[i:j] += sig[: j - i]


# --- the arrangement: which layers play, bar by bar -----------------------------------
def layers_at(t):
    """(pad, bass, arp, drums, lead) gains at time t, from the storyboard's beats."""
    pad = bass = arp = drums = lead = 0.0
    if t < 7.0:                                   # the opening: pad and a slow arp
        pad, arp = 1.0, 0.6
    elif t < sb.COLLAPSE_T1:                      # explosion, cavity, reassembly: everything, lifting
        pad, bass, arp, drums, lead = 1.0, 1.0, 1.0, 0.9, 0.8 if t > 9.0 else 0.0
    elif t < sb.KNOB_SCENE[1]:                    # the knob scene: sparse, the bass and a light beat
        pad, bass, arp, drums = 0.7, 0.8, 0.35, 0.5
    elif t < sb.FADE_OUT[0]:                      # the frontal stretch: full, with the lead
        pad, bass, arp, drums, lead = 1.0, 1.0, 1.0, 0.9, 1.0
    elif t < sb.END_CARD[0]:                      # fade with the picture
        pad, bass, arp, drums, lead = 0.8, 0.5, 0.5, 0.3, 0.5
    else:                                         # the end card: one held chord
        pad = 0.8
    return pad, bass, arp, drums, lead


def gain_curve(n, idx):
    """A per-sample gain for layer idx, smoothed over 0.6 s so the changes breathe."""
    step = SR // 20
    pts = np.array([layers_at(i * step / SR)[idx] for i in range(n // step + 2)])
    g = np.repeat(pts, step)[:n]
    return uniform_filter1d(g, int(0.6 * SR), mode="nearest")


def render(duration):
    n = int(duration * SR)
    pad = np.zeros(n)
    bass = np.zeros(n)
    arp = np.zeros(n)
    drums = np.zeros(n)
    lead = np.zeros(n)
    nbars = int(np.ceil(duration / BAR))
    for bar in range(nbars):
        t0 = bar * BAR
        root, tones = PROG[bar % len(PROG)]
        # pad: three detuned pulse waves per chord tone, slow attack, through a low-pass
        for k in tones:
            f = midi(root + 12 + k)
            m = int(BAR * SR) + int(0.8 * SR)
            t = t_axis(m)
            v = (square(f * 0.997, t, 0.5) + square(f * 1.003, t, 0.5) + triangle(f, t)) / 3
            place(pad, t0, v * env(m, 0.5, 0.5, 0.7, 0.8, BAR) * 0.25)
        # bass: a triangle on the root, two notes per bar
        for b in (0, 2):
            m = int(2 * BEAT * SR)
            t = t_axis(m)
            v = triangle(midi(root - 12), t) + 0.3 * square(midi(root - 12), t, 0.25)
            place(bass, t0 + b * BEAT, v * env(m, 0.01, 0.3, 0.6, 0.15, 1.6 * BEAT) * 0.5)
        # arp: eighth notes up and down the chord, 25 % pulse
        seq = list(tones) + list(tones[::-1][1:-1])
        for s in range(8):
            k = seq[s % len(seq)]
            m = int(0.5 * BEAT * SR)
            t = t_axis(m)
            v = square(midi(root + 24 + k), t, 0.25)
            place(arp, t0 + s * 0.5 * BEAT, v * env(m, 0.005, 0.12, 0.2, 0.05, 0.3 * BEAT) * 0.28)
        # drums: a soft kick on 1 and 3, a noise snare on 2 and 4, hats on the eighths
        for b in range(4):
            m = int(0.4 * SR)
            t = t_axis(m)
            if b in (0, 2):
                f = 55 * np.exp(-t * 18) + 45
                kick = np.sin(2 * np.pi * np.cumsum(f) / SR) * env(m, 0.002, 0.12, 0.0, 0.05, 0.1)
                place(drums, t0 + b * BEAT, kick * 0.9)
            else:
                sn = lowpass(RNG.standard_normal(m), 4000) * env(m, 0.001, 0.09, 0.0, 0.03, 0.06)
                place(drums, t0 + b * BEAT, sn * 0.35)
            for h in (0, 0.5):
                mh = int(0.08 * SR)
                hat = highpass(RNG.standard_normal(mh), 7000) * env(mh, 0.001, 0.03, 0.0, 0.02, 0.02)
                place(drums, t0 + (b + h) * BEAT, hat * (0.12 if h == 0 else 0.07))
        # lead: a slow melody on the chord's colour tones, one or two notes a bar, 12.5 % pulse
        for s, k, length in ((0, tones[3], 2.0), (2.5, tones[1], 1.5)):
            if (bar + s) % 3 == 0:
                continue
            m = int(length * BEAT * SR)
            t = t_axis(m)
            vib = 1 + 0.004 * np.sin(2 * np.pi * 5.5 * t) * np.clip(t - 0.2, 0, 1)
            v = square(midi(root + 24 + k) * vib, t, 0.125)
            place(lead, t0 + s * BEAT, v * env(m, 0.02, 0.3, 0.6, 0.25, 0.9 * length * BEAT) * 0.22)

    pad = lowpass(pad, 1800)
    arp = lowpass(arp, 5000)
    lead = lowpass(lead, 4500)
    layers = [pad, bass, arp, drums, lead]
    mix = sum(x * gain_curve(n, i) for i, x in enumerate(layers))
    # the lo-fi finish: a gentle low-pass "tape" tone, a slow wobble in level, a little noise floor
    mix = lowpass(mix, 9000, 1)
    t = t_axis(n)
    mix *= 1 + 0.04 * np.sin(2 * np.pi * 0.9 * t)
    mix += lowpass(RNG.standard_normal(n), 3000) * 0.004
    # fades with the picture: in over the first 0.8 s, out with FADE_OUT, the end-card chord out at the end
    mix *= np.clip(t / sb.FADE_IN[1], 0, 1)
    mix *= np.clip(1 - (t - sb.FADE_OUT[0]) / (sb.FADE_OUT[1] - sb.FADE_OUT[0]) * 0.6, 0.4, 1)
    mix *= np.clip((duration - t) / 1.2, 0, 1)
    mix /= np.max(np.abs(mix)) + 1e-9
    return (mix * 0.7).astype(np.float32)


def write_wav(path, x):
    import wave
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
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
                        "-af", f"loudnorm=I={a.lufs}:TP=-1.5:LRA=11", "-c:a", "aac", "-b:a", "160k", "-shortest",
                        "-movflags", "+faststart", vout], check=True, capture_output=True)
        print("wrote", vout)


if __name__ == "__main__":
    main()
