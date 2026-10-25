# Audio Engineering Specifications: RMS Loudness, DAC Swing & Amplifier Saturation

This document explains the core principles of embedded audio dynamics, the inverse proportionality between RMS loudness and DAC swing, the exact physical meaning of each audio specification, and how each parameter affects acoustic quality, distortion, noise, and amplifier behavior.

---

## 1. Quick Glossary of Audio Specifications

| Specification | Technical Definition | Unit | Physical Effect on Audio |
|---|---|---|---|
| **RMS Loudness** | Root Mean Square average energy of the audio waveform across time | **dBFS / LUFS** | Dictates continuous perceived volume, vocal thickness, and acoustic energy density. High RMS sounds loud and punchy; low RMS sounds thin or distant. |
| **Peak Amplitude** | Maximum instantaneous absolute sample voltage in the digital waveform | **dBFS Peak** | Dictates dynamic headroom. If peak exceeds $0\text{ dBFS}$ ($32,767$ in 16-bit PCM), digital clipping (flat-topping) occurs immediately. |
| **Crest Factor** | Ratio of peak amplitude to average RMS power ($\text{Peak} - \text{RMS}$) | **dB** | Measures dynamic punch. High crest factor = natural percussion/classical ($15..20\text{ dB}$); low crest factor = heavily compressed voice ($6..10\text{ dB}$). |
| **DAC Swing Limit** | Maximum count deviation from the 12-bit DC midpoint ($2048 \pm \Delta$) | **Counts ($0..2047$)** | Sets the maximum physical AC voltage ($V_{\text{in(pk-pk)}}$) presented to the amplifier's input stage. |
| **Amplifier Gain ($A_v$)**| Voltage multiplication factor of the amplifier ($V_{\text{out}} / V_{\text{in}}$) | **V/V (or dB)** | For PAM8403, $A_v$ is fixed at $16\times$ ($24\text{ dB}$) in Bridge-Tied Load (BTL) configuration. |
| **Rail Saturation** | Output stage transistors hitting the power supply rails ($V_{\text{DD}}$ / GND) | **Volts** | Causes acoustic "tearing", harsh buzzing, severe odd-order harmonic distortion, and speaker coil overheating. |

---

## 2. The Golden Law: Inverse Proportionality (RMS Loudness vs. DAC Swing)

$$\mathbf{\text{RMS Loudness (Energy)}} \times \mathbf{\text{DAC Swing (Gain)}} \le \mathbf{V_{\text{rail, clean}}}$$

### Why They Are Inversely Proportional:
The total electrical power delivered to the speaker cone is determined by the combination of two stages:
1. **The Digital Source Energy (RMS Loudness of the WAV file)**.
2. **The Microcontroller Analog Drive (DAC Swing scaling in firmware)**.

```
+-----------------------------------------------------------------------------------+
| Audio Source File                  STM32 DAC Stage               PAM8403 (16x BTL)|
| [ High RMS Loudness (-12 dBFS) ] -> [ Lower DAC Swing (120..350) ] -> [ Clean & Loud ] |
|                                                                                   |
| [ Low RMS Loudness (-24 dBFS)  ] -> [ Higher DAC Swing (400..600)] -> [ Clean & Loud ] |
+-----------------------------------------------------------------------------------+
```

* **When Audio File has HIGH RMS (e.g. -12 to -14 dBFS, Neural TTS / Sarvam AI)**:
  * The speech energy is dense, compressed, and constantly sits near full scale.
  * The firmware uses calibrated volume levels: `PAM_SWING_LIMIT = 750`, `k_peak = {250, 360, 480, 600, 700}`.
  * Levels 1..3 provide safe, high-volume clear speech without hitting rail saturation.

* **When Audio File has LOW RMS (e.g. -24 to -28 dBFS, Raw Uncompressed Mic Audio)**:
  * The average sample energy is low with large crest factor.
  * The firmware can use higher DAC levels (Level 4 or 5: 600..700) to bring quiet speech up to an audible room listening level.
  * Rail saturation does not occur because the peaks are transient and infrequent.

---

## 3. Mathematical Derivations & Formulas

### A. DAC Voltage Calculation
For a 12-bit DAC with reference voltage $V_{\text{ref}} = 3.300\text{V}$:
$$\text{Resolution} = \frac{3.300\text{V}}{4096} = 0.80566\text{ mV per count}$$
$$\text{DC Midpoint (Silence)} = 2048 \text{ counts} \implies 1.650\text{V}$$
$$\text{AC Peak Voltage} = \text{Swing Counts} \times 0.80566\text{ mV}$$
$$\text{AC Peak-to-Peak Voltage} = 2 \times \text{Swing Counts} \times 0.80566\text{ mV}$$

### B. PAM8403 Bridge-Tied Load (BTL) Differential Output
The PAM8403 provides differential drive across the speaker terminals (OUT+ and OUT-):
$$V_{\text{out differential}} = A_v \times V_{\text{in}} = 16 \times V_{\text{in(pk-pk)}}$$

Given a 5.0V supply rail and internal MOSFET $R_{\text{DS(on)}}$ drops ($\approx 0.35\text{V}$ per transistor):
$$V_{\text{out(differential, max clean)}} \approx 2 \times (5.0\text{V} - 2 \times 0.35\text{V}) \approx 8.6\text{V}_{\text{pk-pk}}$$

### C. Calibrated Safe Swing Table
| DAC Swing Counts | $V_{\text{in(pk-pk)}}$ into Amp | $V_{\text{out diff}}$ (PAM8403 16x) | Headroom vs Rail | Acoustic Quality |
|---|---|---|---|---|
| **250** (Level 1) | $0.403\text{V}$ | $6.45\text{V}$ | Clean headroom | Soft, warm background speech |
| **360** (Level 2) | $0.580\text{V}$ | $9.28\text{V}$ | Full scale | Clear room presence |
| **480** (Level 3 - Default) | $0.773\text{V}$ | $12.37\text{V}$ | High output | **Optimal**: High loudness, strong clarity |
| **600** (Level 4) | $0.967\text{V}$ | $15.47\text{V}$ | High drive | Extra-loud punch |
| **700** (Level 5) | $1.128\text{V}$ | $18.05\text{V}$ | Max drive | Maximum volume for quiet audio |
| **750** (Hardware Limit) | $1.208\text{V}$ | $19.34\text{V}$ | Threshold boundary | Absolute clamp ceiling (`PAM_SWING_LIMIT`) |

---

## 4. Root Causes of Audio Noise & Distortion in Firmware

### 1. Instantaneous Sample-Level Noise Gates (Crossover Distortion)
* **The Bug**: Checking `if (x > -35 && x < 35)` on individual samples to "mute noise".
* **Why it destroys audio**: Every single audio cycle passes through zero. Clamping or modifying values near zero creates a non-linear kink at the zero-crossing, generating harsh third-order and higher harmonic buzzing across all frequencies.
* **The Rule**: **NEVER gate individual samples in an audio stream**. True noise gating requires an envelope tracker (RMS detector) with attack ($5\text{ ms}$) and release ($50\text{ ms}$) times.

### 2. Software 1-Pole Filters with Integer Truncation
* **The Bug**: `s_smoothed += ((dev - s_smoothed) * 3) >> 2;` using integer math.
* **Why it destroys audio**: Integer division truncates towards zero. Small differences ($1, 2, 3$) round to $0$, creating dead-bands, limit cycles, and quantization fuzz on low-amplitude decay tails.
* **The Rule**: For 32 kHz playback, rely on the hardware timer DMA transfer and external RC / amplifier bandwidth. If software filtering is necessary, accumulate in 32-bit fixed point (Q15/Q31) with proper rounding.

### 3. Missing Anti-Pop Ramps
* **The Bug**: Starting or stopping playback abruptly between $0$ and midpoint $2048$.
* **Why it destroys audio**: A step discontinuity creates an impulse response $f(t) = \delta(t)$, causing an audible "thump" or "crack" in the speaker cone.
* **The Rule**: Always ramp smoothly between $0$ and midpoint $2048$ over 256 samples ($8\text{ ms}$) using a raised cosine or linear ramp.
