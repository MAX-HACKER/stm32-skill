#!/usr/bin/env python3
"""
Sarvam AI Voice Clone & Audio Conditioning Tool for STM32 Embedded Projects
Generates clips, trims silence with attack/decay margins, applies cosine anti-pop fades,
and matches loudness metrics.
"""
import os
import sys
import base64
import requests
import wave
import argparse
import numpy as np
import scipy.signal as signal
import soundfile as sf

URL = "https://api.sarvam.ai/voices/clone"

def generate_sarvam_clip(text, voice_id, api_key, out_path, sr=32000):
    response = requests.post(
        URL,
        headers={"api-subscription-key": api_key},
        data={
            "text": text,
            "language_code": "en-IN",
            "speech_sample_rate": sr,
            "enable_qc": True,
            "enable_text_normalization": True,
            "voice_id": voice_id,
        },
        timeout=60
    )
    if response.status_code == 200:
        res = response.json()
        b64 = res.get("audio_b64") or res.get("audio")
        if b64:
            with open(out_path, "wb") as f:
                f.write(base64.b64decode(b64))
            return True, "OK"
    return False, f"HTTP {response.status_code}: {response.text[:200]}"

def condition_audio(in_path, out_path, target_sr=32000, max_peak=0.92):
    data, sr = sf.read(in_path, dtype='float32')
    if len(data.shape) > 1:
        data = data.mean(axis=1)

    # DC offset removal
    data = data - np.mean(data)

    # Trim silence (< 1.5%) with 25ms attack and 35ms decay margins
    abs_d = np.abs(data)
    start, end = 0, len(abs_d)
    lead = int(sr * 0.025)
    trail = int(sr * 0.035)
    for i in range(len(abs_d)):
        if abs_d[i] > 0.015:
            start = max(0, i - lead)
            break
    for i in range(len(abs_d)-1, -1, -1):
        if abs_d[i] > 0.015:
            end = min(len(abs_d), i + trail)
            break
    data = data[start:end]

    # Polyphase resample if needed
    if sr != target_sr:
        gcd = np.gcd(target_sr, sr)
        data = signal.resample_poly(data, target_sr // gcd, sr // gcd)

    # 15ms cosine anti-pop fades
    flen = int(target_sr * 0.015)
    if len(data) > flen * 2:
        fade_in = 0.5 * (1.0 - np.cos(np.linspace(0, np.pi, flen)))
        fade_out = 0.5 * (1.0 + np.cos(np.linspace(0, np.pi, flen)))
        data[:flen] *= fade_in
        data[-flen:] *= fade_out

    # Peak normalize
    p = np.max(np.abs(data))
    if p > 0:
        data = data * (max_peak / p)

    pcm = np.clip(np.round(data * 32768.0), -32768, 32767).astype(np.int16)
    with wave.open(out_path, "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(target_sr)
        wf.writeframes(pcm.tobytes())

    act = data[np.abs(data) > 0.05 * np.max(np.abs(data))]
    rms = np.sqrt(np.mean(act**2)) if len(act) > 0 else 0
    return rms, np.max(np.abs(data))

if __name__ == "__main__":
    print("Sarvam Audio Builder Utility ready.")
