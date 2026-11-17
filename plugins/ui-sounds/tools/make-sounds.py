#!/usr/bin/env python3
# Writes ui-sounds' bundled sounds into res/sounds/ from sine tones, so
# they're this repo's own work, which it dedicates to the public domain
# under CC0 1.0 (see res/sounds/SOURCES.txt).
#
#   python3 -I plugins/ui-sounds/tools/make-sounds.py

import math
import os
import struct
import sys

RATE = 22050
PEAK = 0.45  # of full scale, before the plugin's volume

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "res", "sounds")


def tone(notes, attack_ms=3):
    """notes: (start_hz, end_hz, ms, decay) sweeps played one after another,
    0 Hz for a rest. decay is how fast each note fades, 0 for an even level."""
    samples = []
    phase = 0.0
    for start, end, ms, decay in notes:
        n = int(RATE * ms / 1000)
        if start == 0 and end == 0:
            samples.extend([0.0] * n)
            phase = 0.0
            continue
        attack = int(RATE * attack_ms / 1000)
        for i in range(n):
            t = i / n
            hz = start + (end - start) * t
            phase += 2 * math.pi * hz / RATE
            level = math.exp(-decay * t) if decay else 1.0
            if i < attack:
                level *= i / attack
            # a short fade at the end, so it doesn't click
            tail = n - i
            if tail < attack:
                level *= tail / attack
            samples.append(math.sin(phase) * level)
    return samples


def write(name, samples):
    data = b"".join(struct.pack("<h", int(round(s * PEAK * 32767))) for s in samples)
    fmt = struct.pack("<HHIIHH", 1, 1, RATE, RATE * 2, 2, 16)
    body = b"WAVE" + b"fmt " + struct.pack("<I", len(fmt)) + fmt + b"data" + struct.pack("<I", len(data)) + data
    with open(os.path.join(OUT, name + ".wav"), "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", len(body)) + body)


SOUNDS = {
    # a soft tick
    "move": [(1800, 1700, 25, 6)],
    # a blip up, and the same down
    "select": [(880, 880, 35, 1), (1320, 1320, 45, 4)],
    "back": [(1320, 1320, 35, 1), (880, 880, 45, 4)],
    # two low pulses
    "error": [(220, 200, 90, 2), (0, 0, 40, 0), (220, 180, 120, 3)],
    # a rising arpeggio
    "launch": [(523, 523, 70, 1), (659, 659, 70, 1), (784, 784, 70, 1), (1047, 1047, 160, 4)],
    # a sweep up, and down
    "menu_open": [(600, 1200, 80, 2)],
    "menu_close": [(1200, 600, 80, 2)],
    # a chime
    "toast": [(1568, 1568, 60, 2), (2093, 2093, 160, 5)],
}


def main():
    os.makedirs(OUT, exist_ok=True)
    for name, notes in SOUNDS.items():
        write(name, tone(notes))
    return 0


if __name__ == "__main__":
    sys.exit(main())
