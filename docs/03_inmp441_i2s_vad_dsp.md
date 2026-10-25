# INMP441 I2S Microphone Capture & Real-Time Voice Activity Detection (VAD)

This document specifies the digital microphone acquisition pipeline, I2S DMA buffering, DC rejection filtering, and adaptive energy Voice Activity Detection (VAD) for STM32.

---

## 1. Hardware Interface & I2S Configuration

* **Microphone**: INMP441 Omnidirectional Digital MEMS Microphone with I2S interface.
* **Pins on STM32G431**:
  * `PB12`: `I2S2_WS` (Word Select / LRCLK)
  * `PB13`: `I2S2_CK` (Continuous Serial Clock / BCLK)
  * `PB15`: `I2S2_SD` (Serial Data / SDIN)
* **Channel Select (L/R)**: Connect `L/R` to `GND` for Left channel (data captured on WS low).
* **Clock Calculation**:
  $$\text{Sample Rate } f_s = 16,000\text{ Hz}$$
  $$\text{Bit Clock } f_{\text{BCLK}} = f_s \times 32\text{ bits} \times 2\text{ channels} = 1.024\text{ MHz}$$
* **Data Packing**: INMP441 outputs 24-bit signed 2's complement audio packed MSB-first in a 32-bit slot.
  ```c
  // Extract 16-bit PCM from raw 32-bit I2S DMA word:
  int32_t raw24 = (int32_t)(raw_i2s_sample & 0xFFFFFF00UL);
  int16_t pcm16 = (int16_t)(raw24 >> 16);
  ```

---

## 2. High-Pass Filter (DC Offset Removal)

Digital MEMS microphones often exhibit a small DC offset ($+50 \dots +300$ LSBs) caused by sigma-delta converter bias. A simple single-pole IIR filter at $180\text{ Hz}$ cleans the baseline:

$$y[n] = x[n] - x[n-1] + \alpha \cdot y[n-1], \quad \text{where } \alpha \approx 0.965$$

---

## 3. Real-Time Adaptive Energy VAD Algorithm

To reliably detect speech in changing environments (quiet room vs. fan/AC running) and **completely reject phone notification chimes, clicks, and door slams**, the VAD uses continuous asymmetric baseline adaptation and onset confirmation.

### Mathematical Formulation:
1. **Frame Energy**:
   Computed over $400$ samples ($25\text{ ms}$):
   $$\text{RMS} = \sqrt{\frac{1}{N} \sum_{i=0}^{N-1} x[i]^2}$$
   $$\text{Log Energy (dB)} = 20 \log_{10}(\text{RMS} + 1e-4)$$

2. **Asymmetric Noise Floor Adaptation**:
   Adapts continuously only while in the `SILENCE` state:
   * **Rising ambient noise (slow adaptation, $\tau \approx 1\text{ s}$)**:
     $$E_{\text{floor}} \leftarrow 0.990 \cdot E_{\text{floor}} + 0.010 \cdot E_{\text{frame}}$$
   * **Decaying ambient noise (fast adaptation, $\tau \approx 125\text{ ms}$)**:
     $$E_{\text{floor}} \leftarrow 0.920 \cdot E_{\text{floor}} + 0.080 \cdot E_{\text{frame}}$$
   * Clamp range: $5.0\text{ dB} \le E_{\text{floor}} \le 75.0\text{ dB}$.

3. **Dynamic Decision Thresholds (Calibrated for High Noise)**:
   $$\text{Threshold}_{\text{speech}} = E_{\text{floor}} + 7.5\text{ dB}$$
   $$\text{Threshold}_{\text{silence}} = E_{\text{floor}} + 3.5\text{ dB} \quad (\text{Hysteresis})$$

4. **Transient & Chirp Rejection (`VAD_ONSET_CONFIRM_FRAMES = 2`)**:
   Energy must stay above $\text{Threshold}_{\text{speech}}$ for **2 consecutive frames ($20\text{ ms}$)** before entering `VAD_STATE_SPEECH`. This ensures quick, responsive onset capture in noisy rooms while still filtering out single-sample electrical spikes.

5. **Word Bridging (`VAD_HANGOVER_FRAMES = 12`)**:
   Speech does not terminate immediately when energy drops (e.g., stop consonants /p/, /t/, /k/). A hangover buffer of 12 silence frames ($120\text{ ms}$) prevents mid-sentence fragmentation even during soft syllable transitions.

6. **Continuous Noise Guard (`VAD_MAX_SPEECH_FRAMES = 150`)**:
   Speech lasting longer than $1.5\text{ seconds}$ without silence is flagged as continuous background noise (fan roar, hair dryer) and discarded.

7. **SNR Quality Gates**:
   * **Enrollment Training Gate**: SNR $\ge 7.0\text{ dB}$ (relaxed from 14 dB to allow user training in noisy environments).
   * **Runtime Utterance Gate**: SNR $\ge 6.5\text{ dB}$ and duration $\ge 8\text{ frames}$ ($160\text{ ms}$).
