#!/usr/bin/env python3
"""
IMA/DVI 4-bit ADPCM Encoder/Decoder Tool for STM32 Embedded Projects
Compatible with STM32 PAM8403 DAC audio pipeline.
"""
import sys
import wave
import struct
import argparse
import numpy as np

STEP_TABLE = [
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50,
    55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279,
    307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282,
    1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871,
    5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818,
    18500, 20350, 22385, 24623, 27086, 29794, 32767
]
INDEX_TABLE = [-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8]

def encode_adpcm(samples):
    pred, step_idx, nibbles = 0, 0, []
    for s in samples:
        diff = s - pred
        sign = 8 if diff < 0 else 0
        diff = abs(diff)
        step = STEP_TABLE[step_idx]
        delta = 0
        vpdiff = step >> 3
        if diff >= step:
            delta |= 4
            diff -= step
            vpdiff += step
        step >>= 1
        if diff >= step:
            delta |= 2
            diff -= step
            vpdiff += step
        step >>= 1
        if diff >= step:
            delta |= 1
            vpdiff += step
        nibble = sign | delta
        nibbles.append(nibble)
        pred = max(-32768, min(32767, pred - vpdiff if sign else pred + vpdiff))
        step_idx = max(0, min(88, step_idx + INDEX_TABLE[delta]))

    out = bytearray()
    for i in range(0, len(nibbles) - 1, 2):
        out.append((nibbles[i] & 0x0F) | ((nibbles[i+1] & 0x0F) << 4))
    if len(nibbles) % 2:
        out.append(nibbles[-1] & 0x0F)
    return bytes(out)

def decode_adpcm(data):
    pred, step_idx, samples = 0, 0, []
    for byte in data:
        for i in range(2):
            nibble = (byte & 0x0F) if i == 0 else ((byte >> 4) & 0x0F)
            step = STEP_TABLE[step_idx]
            vpdiff = step >> 3
            if nibble & 4: vpdiff += step
            if nibble & 2: vpdiff += (step >> 1)
            if nibble & 1: vpdiff += (step >> 2)

            if nibble & 8: pred -= vpdiff
            else:          pred += vpdiff
            pred = max(-32768, min(32767, pred))
            step_idx = max(0, min(88, step_idx + INDEX_TABLE[nibble]))
            samples.append(pred)
    return samples

def main():
    parser = argparse.ArgumentParser(description="IMA ADPCM 4-bit tool for STM32")
    parser.add_argument("mode", choices=["encode", "decode"], help="encode WAV->BIN or decode BIN->WAV")
    parser.add_argument("input", help="input file path")
    parser.add_argument("output", help="output file path")
    parser.add_argument("--sr", type=int, default=32000, help="sample rate (default 32000)")
    args = parser.parse_args()

    if args.mode == "encode":
        with wave.open(args.input, "rb") as wf:
            sr = wf.getframerate()
            n = wf.getnframes()
            raw = wf.readframes(n)
            samples = struct.unpack(f"<{n}h", raw)
        encoded = encode_adpcm(samples)
        with open(args.output, "wb") as f:
            f.write(encoded)
        print(f"Encoded {n} samples @ {sr}Hz ({len(raw)} bytes) -> {len(encoded)} ADPCM bytes ({len(raw)/len(encoded):.1f}:1 ratio)")
    else:
        with open(args.input, "rb") as f:
            data = f.read()
        samples = decode_adpcm(data)
        with wave.open(args.output, "wb") as wf:
            wf.setnchannels(1)
            wf.setsampwidth(2)
            wf.setframerate(args.sr)
            wf.writeframes(struct.pack(f"<{len(samples)}h", *samples))
        print(f"Decoded {len(data)} ADPCM bytes -> {len(samples)} samples @ {args.sr}Hz into {args.output}")

if __name__ == "__main__":
    main()
