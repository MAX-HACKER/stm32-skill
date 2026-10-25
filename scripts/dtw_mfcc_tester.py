#!/usr/bin/env python3
"""
MFCC & DTW Offline Verification Test Harness for STM32 DSP Pipeline
Matches the embedded C implementation in mfcc.c and dtw.c bit-for-bit.
"""
import sys
import wave
import struct
import numpy as np
import scipy.signal as signal

MEL_BINS = [
    3, 6, 8, 11, 15, 18, 22, 27, 32, 37, 43, 49, 56, 63, 72, 81, 91, 102, 114, 128,
    142, 158, 176, 195, 217, 240
]

def compute_mfcc_frame(samples_400):
    # Hamming window
    w = 0.54 - 0.46 * np.cos(2 * np.pi * np.arange(400) / 399.0)
    windowed = samples_400 * w
    # 512 Real FFT
    padded = np.zeros(512, dtype=np.float32)
    padded[:400] = windowed
    spec = np.abs(np.fft.rfft(padded))**2

    # 24 Mel filters
    mel_energies = np.zeros(24, dtype=np.float32)
    for m in range(24):
        left, center, right = MEL_BINS[m], MEL_BINS[m+1], MEL_BINS[m+2]
        # Triangular slopes
        for k in range(left, center):
            mel_energies[m] += spec[k] * ((k - left) / (center - left))
        for k in range(center, right + 1):
            mel_energies[m] += spec[k] * ((right - k) / (right - center))
        mel_energies[m] = np.log(mel_energies[m] + 1e-4)

    # DCT-II (13 coeffs)
    mfcc = np.zeros(13, dtype=np.float32)
    for n in range(13):
        for m in range(24):
            mfcc[n] += mel_energies[m] * np.cos(np.pi * n * (m + 0.5) / 24.0)
        # Lifter L=22
        mfcc[n] *= (1.0 + 11.0 * np.sin(np.pi * n / 22.0))

    return mfcc

def compute_utterance_mfcc(wav_path):
    with wave.open(wav_path, 'rb') as wf:
        sr = wf.getframerate()
        n = wf.getnframes()
        raw = wf.readframes(n)
        pcm = np.array(struct.unpack(f"<{n}h", raw), dtype=np.float32)

    # Frame 400 samples, hop 320 (20ms stride)
    frames = []
    hop = 320
    for i in range(0, len(pcm) - 400 + 1, hop):
        frames.append(compute_mfcc_frame(pcm[i:i+400]))

    if not frames:
        return None
    arr = np.array(frames, dtype=np.float32)

    # Utterance CMS
    mean = np.mean(arr, axis=0)
    cms = arr - mean
    # Quantize x8.0
    quant = np.clip(np.round(cms * 8.0), -128, 127).astype(np.int8)
    return quant

def dtw_distance(q1, q2):
    len1, len2 = len(q1), len(q2)
    if len1 == 0 or len2 == 0: return 1e9
    if len1 > len2 * 1.5 or len2 > len1 * 1.5: return 1e9

    window = max(5, int(max(len1, len2) * 0.25))
    dp = np.full((len1 + 1, len2 + 1), 1e9, dtype=np.float32)
    dp[0, 0] = 0.0

    for i in range(1, len1 + 1):
        center_j = int((i * len2) / len1)
        j_start = max(1, center_j - window)
        j_end = min(len2, center_j + window)

        for j in range(j_start, j_end + 1):
            # Timbre Euclidean distance across c1..c12 (skip c0)
            diff = q1[i-1, 1:13].astype(np.int32) - q2[j-1, 1:13].astype(np.int32)
            dist = np.sqrt(np.sum(diff**2)) / 8.0
            dp[i, j] = dist + min(dp[i-1, j-1], dp[i-1, j], dp[i, j-1])

    return dp[len1, len2] / (len1 + len2)

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: dtw_mfcc_tester.py <wav1> <wav2>")
        sys.exit(1)
    q1 = compute_utterance_mfcc(sys.argv[1])
    q2 = compute_utterance_mfcc(sys.argv[2])
    if q1 is not None and q2 is not None:
        dist = dtw_distance(q1, q2)
        print(f"DTW Distance: {dist:.2f} (Match: {dist <= 6.8})")
    else:
        print("Failed to compute MFCC")
