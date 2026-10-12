# Generates the small wav files the lyrics harness plays:
# one second of sine tone per track, different pitch per file.
import math
import os
import struct
import sys
import wave

outdir = sys.argv[1]
os.makedirs(outdir, exist_ok=True)
rate = 8000
for n in range(1, 4):
    with wave.open(os.path.join(outdir, "track%d.wav" % n), "wb") as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(rate)
        freq = 300 + 200 * n
        frames = b"".join(
            struct.pack("<h", int(32767 * 0.5 * math.sin(2 * math.pi * freq * s / rate)))
            for s in range(rate))
        f.writeframes(frames)
print("generated 3 wav files in %s" % outdir)