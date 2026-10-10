#!/usr/bin/env python3
"""Generates PebbleDiver's sound samples (resources/sfx/*.raw).

The speaker's built-in waveforms (sine/square/triangle/sawtooth) can't make a
bubble or a dull underwater knock, so these two are synthesised here and played
through speaker_play_tracks() as pitch-shiftable samples. They are computed, not
recorded, so there is nothing to license; rerun this script after changing them.

Output: mono, signed 8-bit PCM at 16 kHz (SpeakerPcmFormat_16kHz_8bit).

    python3 tools/gen_sfx.py            # writes resources/sfx/*.raw
    python3 tools/gen_sfx.py --wav DIR  # also writes .wav previews to DIR
"""
import argparse
import math
import os
import random
import struct
import wave

RATE = 16000
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', 'resources', 'sfx')


def bubble():
    """A single 'blub': a sine whose pitch shoots up as the bubble pinches off,
    with a fast attack and an exponential tail -- the classic Minnaert bubble."""
    n = int(RATE * 0.09)
    out, phase = [], 0.0
    for i in range(n):
        t = i / RATE
        f = 520 * (2.6 ** (t / 0.09))          # ~520 Hz -> ~1350 Hz
        phase += 2 * math.pi * f / RATE
        env = min(1.0, t / 0.004) * math.exp(-t / 0.03)
        out.append(math.sin(phase) * env)
    return out


def thud():
    """A dull knock against rock: a falling low tone, softly clipped so its
    harmonics reach a speaker too small for the fundamental, plus a short
    noise click at the start."""
    rnd = random.Random(7)
    n = int(RATE * 0.16)
    out, phase = [], 0.0
    for i in range(n):
        t = i / RATE
        f = 340 * math.exp(-t / 0.06) + 110      # ~450 Hz -> ~120 Hz
        phase += 2 * math.pi * f / RATE
        tone = math.tanh(2.2 * math.sin(phase)) * math.exp(-t / 0.05)
        click = rnd.uniform(-1, 1) * math.exp(-t / 0.006) * 0.7
        out.append(0.85 * tone + click)
    return out


def to_pcm8(samples):
    peak = max(abs(s) for s in samples) or 1.0
    return b''.join(struct.pack('b', max(-127, min(127, int(round(s / peak * 120)))))
                    for s in samples)


def write_wav(path, samples):
    peak = max(abs(s) for s in samples) or 1.0
    with wave.open(path, 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(b''.join(struct.pack('<h', int(s / peak * 30000)) for s in samples))


SOUNDS = {'bubble': bubble, 'thud': thud}

if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--wav', help='also write .wav previews to this directory')
    args = ap.parse_args()
    os.makedirs(OUT, exist_ok=True)
    for name, fn in SOUNDS.items():
        s = fn()
        with open(os.path.join(OUT, name + '.raw'), 'wb') as f:
            f.write(to_pcm8(s))
        if args.wav:
            write_wav(os.path.join(args.wav, name + '.wav'), s)
        print('%-7s %5d bytes' % (name, len(s)))
