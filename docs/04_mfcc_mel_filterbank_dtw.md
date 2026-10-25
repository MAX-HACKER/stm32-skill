# On-Device Voice Recognition: 24-Channel Mel-FFT, MFCC & DTW

This document details the mathematical architecture and memory-optimized implementation of an on-device, speaker-dependent voice recognition engine running entirely inside the 32 KB SRAM of an STM32 Cortex-M4.

---

## 1. Pipeline Overview

```
Input Frame (400 samples @ 16kHz = 25ms)
                 │
           [Hamming Window]
                 ▼
       [512-Point Fast Real FFT]  (Packed 256 Complex FFT)
                 ▼
           [Power Spectrum] (257 positive frequency bins)
                 ▼
       [24 Triangular Mel Filters] (100 Hz to 7500 Hz)
                 ▼
          [Log-Energy Summation]
                 ▼
        [Type-II DCT + Liftering]
                 ▼
      13 MFCC Coefficients (c0..c12) per Frame
                 │  (Accumulate entire utterance)
                 ▼
    [Linear Float Cepstral Mean Subtraction] (CMS)
                 ▼
       [Quantization to int8_t (x8.0)]
                 ▼
     [Sakoe-Chiba Constrained Dynamic Time Warping] (DTW)
                 ▼
          Command Recognized!
```

---

## 2. 24-Channel Mel Filterbank Construction

The Mel scale approximates human auditory pitch perception:
$$m = 2595 \log_{10}\left(1 + \frac{f}{700}\right) \iff f = 700 \left(10^{m / 2595} - 1\right)$$

### Precomputed Filter Boundaries:
For $f_s = 16,000\text{ Hz}$ and an $N_{\text{FFT}} = 512$ point FFT:
$$\text{Frequency Resolution per Bin} = \frac{16000\text{ Hz}}{512} = 31.25\text{ Hz/bin}$$
The 26 boundary bin indices covering $100\text{ Hz} \dots 7500\text{ Hz}$ are:
```c
static const uint16_t s_mel_bin_centers[26] = {
    3,   6,   8,  11,  15,  18,  22,  27,  32,  37,
   43,  49,  56,  63,  72,  81,  91, 102, 114, 128,
  142, 158, 176, 195, 217, 240
};
```
* **Filter $m$ ($0 \le m < 24$)**:
  * Left bin: `s_mel_bin_centers[m]`
  * Center peak: `s_mel_bin_centers[m + 1]`
  * Right bin: `s_mel_bin_centers[m + 2]`

### Triangular Weighting & Log Energy:
$$\text{FilterEnergy}[m] = \sum_{k=\text{left}}^{\text{center}-1} P[k] \cdot \frac{k - \text{left}}{\text{center} - \text{left}} + \sum_{k=\text{center}}^{\text{right}} P[k] \cdot \frac{\text{right} - k}{\text{right} - \text{center}}$$
$$\text{LogEnergy}[m] = \ln(\text{FilterEnergy}[m] + 1e-4)$$

---

## 3. Discrete Cosine Transform (DCT-II) & Liftering

Extract $13$ cepstral coefficients from the $24$ log-energies:
$$c_n = \sum_{m=0}^{23} \text{LogEnergy}[m] \cdot \cos\left(\frac{\pi n (m + 0.5)}{24}\right), \quad 0 \le n < 13$$

### Cepstral Lifter ($L = 22$):
De-emphasizes low and high order coefficients to improve distance metrics:
$$w_n = 1 + 11 \cdot \sin\left(\frac{\pi n}{22}\right), \quad c_n' = c_n \cdot w_n$$

---

## 4. Utterance-Level Cepstral Mean Subtraction (CMS)

To eliminate microphone hardware variance, room acoustics, and DC bias across candidate utterances:
1. Compute the mean of each coefficient over all frames in the utterance:
   $$\mu_c = \frac{1}{T} \sum_{t=0}^{T-1} c[t]$$
2. Subtract the mean from each frame:
   $$c_{\text{norm}}[t] = c[t] - \mu_c$$
3. Quantize to `int8_t` for high-speed integer SIMD processing:
   $$\text{quant}[t][c] = \text{clamp}\left(\text{round}\left(c_{\text{norm}}[t] \times 8.0\right), -128, 127\right)$$

---

## 5. Dynamic Time Warping (DTW) with Sakoe-Chiba Band

### Volume-Invariant Timbre Distance:
Energy ($c_0$) varies widely depending on how loud the user speaks. By evaluating Euclidean distance **strictly over $c_1 \dots c_{12}$**, recognition becomes completely volume-invariant:
$$\text{dist}(f_1, f_2) = \frac{1}{8.0} \sqrt{\sum_{c=1}^{12} (f_1[c] - f_2[c])^2}$$

### Sakoe-Chiba Constraint:
* Warping Window: $\pm 25\%$ around the diagonal path ($w = \max(L_{\text{cand}}, L_{\text{tmpl}}) \times 0.25$).
* Length ratio check: Reject immediately if $\frac{L_{\text{cand}}}{L_{\text{tmpl}}} > 1.50$ or $\frac{L_{\text{tmpl}}}{L_{\text{cand}}} > 1.50$.
* Memory Footprint: Uses two 81-float rows (`cost_prev` and `cost_curr`). Total RAM: **648 bytes**.
* Distance Normalization: Total accumulated cost divided by path length ($L_{\text{cand}} + L_{\text{tmpl}}$).

### Decision Rules:
* Wake Word ("Jarvis"): Distance $\le 6.80$.
* Commands: Best distance $\le 7.50$.
* **Confidence Margin**:
  $$\text{Dist}_{\text{second\_best}} \ge 1.15 \cdot \text{Dist}_{\text{best}}$$
  If two enrolled commands score similarly (less than 15% separation), the utterance is rejected as ambiguous rather than executing the wrong action!
