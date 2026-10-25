# Audio Output Pipeline: STM32 DAC1 + TIM6 + DMA + PAM8403

This document contains the complete engineering specification, mathematical derivations, anti-pop techniques, and register-level drivers for playing high-quality PCM audio on an STM32 with a PAM8403 Class-D amplifier.

---

## 1. Hardware Architecture & Signal Path

```
STM32 Internal Flash / External SPI Flash (W25Qxx)
                      │
            [4-bit IMA ADPCM]
                      ▼
            CPU Software Decoder (Half-Buffer Ring Refill)
                      ▼
     SRAM Ring Buffer (512 samples = 2 x 256 sample halves)
                      │
              [DMA1 Channel 3] (Circular Mode)
                      ▼
                 DAC1_OUT1 (Pin PA4)
                      │
                      ▼
      PAM8403 Audio Amplifier Board (5V Power)
                      ▼
                3W 4Ω / 8Ω Speaker
```

> [!NOTE]
> ### ⚠️ Reminder & Warning: Amplifier Input Capacitors
> Almost all commercially available pre-assembled PAM8403 breakout boards **already have onboard SMD DC-blocking capacitors** (typically $1\mu\text{F}$ ceramic capacitors labeled C1/C2 or located immediately behind the $L_{\text{IN}}$ and $R_{\text{IN}}$ pads).
> You do **NOT** need to wire external capacitors in 99% of builds!
> **If unsure**: Take a clear, well-lit photo of your amplifier breakout board, upload the image to the chat, and ask the agent to visually inspect the PCB components to confirm whether input capacitors are present.

* **Sample Clock**: Hardware `TIM6` running at the sample rate (e.g., $32,000\text{ Hz}$).
* **Trigger**: `TIM6_TRGO` triggers DAC conversion synchronously.
* **DMA Transfer**: `DMA1_CH3` feeds DAC data register (`DAC1->DHR12R1`) automatically.
* **CPU Load**: Under $4\%$ CPU utilization @ 170MHz. The CPU only services half-transfer (`HT`) and transfer-complete (`TC`) flags to decode the next 256 samples ($8.0\text{ ms}$).

---

## 2. Electrical Saturation & Rail Headroom Calculations

The single most common bug in embedded audio is **amplifier rail saturation**, which causes harsh acoustic tearing, severe buzzing, and distorted speech.

### The Physics:
* **STM32 DAC**: 12-bit ($0 \dots 4095$). Reference voltage $V_{\text{ref}} = 3.3\text{V}$.
  $$\text{LSB Step} = \frac{3.3\text{V}}{4096} = 0.80566\text{ mV}$$
* **DC Midpoint (Silence)**:
  $$\text{Code} = 2048 \implies 2048 \times 0.80566\text{ mV} = 1.650\text{V}$$
* **PAM8403 Amplifier Specifications**:
  * Operating Supply: $V_{\text{DD}} = 5.0\text{V}$.
  * Internal Fixed Gain: $A_v = 16\times$ ($24\text{ dB}$).
  * Differential Bridge-Tied Load (BTL) output voltage swing:
    $$V_{\text{out(pk-pk, max)}} \le V_{\text{DD}} - 2 \cdot V_{\text{sat}} \approx 5.0\text{V} - 0.7\text{V} = 4.3\text{V} \implies 8.6\text{V}_{\text{pk-pk differential}}$$

### Maximum Allowable Clean Input Swing:
To prevent the PAM8403 from clipping into a square wave on high-RMS speech:
$$V_{\text{in(pk-pk, max)}} = \frac{V_{\text{out(max)}}}{A_v} \le \frac{8.6\text{V}}{16} \approx 0.537\text{V}_{\text{pk-pk}} \implies \pm 0.282\text{V} \text{ (Peak from Midpoint)}$$
In DAC counts:
$$\Delta \text{Counts}_{\text{max}} = \frac{0.282\text{V}}{0.00080566\text{V}} \approx \mathbf{350 \dots 380 \text{ counts}}$$

### The Golden Rule:
* **`PAM_SWING_LIMIT = 750` counts**: Absolute hardware clamp ceiling.
* **Calibrated 5-Step Volume Scaling**:
  ```c
  static const int16_t k_peak[5] = { 250, 360, 480, 600, 700 };
  ```
  * Level 1 (Quiet): 250 counts peak (-9.5 dB vs limit)
  * Level 2 (Soft): 360 counts peak (-6.4 dB)
  * Level 3 (Default, Optimal): 480 counts peak (-3.9 dB, loud & clear)
  * Level 4 (Loud): 600 counts peak (-1.9 dB)
  * Level 5 (Maximum Clean): 700 counts peak (-0.6 dB)

> [!TIP]
> ### RMS Loudness vs. DAC Swing Inverse Proportionality
> The RMS loudness of the source audio and firmware DAC swing are **inversely proportional**.
> High-RMS files (neural TTS / Sarvam AI) require **lower DAC swing** (150..350) to prevent amplifier rail clipping.
> Low-RMS files (raw mic speech) require **higher DAC swing** (300..400) to reach equivalent listening loudness.
> See full engineering guide: [08_audio_specs_rms_loudness_and_dac_swing.md](./08_audio_specs_rms_loudness_and_dac_swing.md).

---

## 3. Pop & Click Elimination (Ramp Shaping & Hardware Preload)

A sudden jump from 0V to 1.65V (or from 1.65V back to 0V) produces an audible high-frequency transient ("pop" or "thump") in the speaker cone.

### Complete Solution:
1. **Never turn off DAC bias between clips**. Keep the output parked at code `2048` ($1.65\text{V}$).
2. **Hardware Preload on Start**: In `hw_start()`, preload `DAC1->DHR12R1 = ring[0]` *before* issuing `TIM6->EGR = TIM_EGR_UG` so the initial TRGO pulse transfers the true starting sample (2048) rather than an undefined leftover register value.
3. **256-sample Fade-In Ramp**:
   ```c
   if (s_fade_in < 256) {
       dev = (dev * (int32_t)s_fade_in) / 256;
       s_fade_in++;
   }
   ```
4. **256-sample Fade-Out Ramp**: When audio clip reaches EOF, smoothly decay the last output code back to midpoint code `2048` over 256 samples ($8.0\text{ ms}$).
5. **NEVER Apply Slew-Rate Clamping to Sample Deltas**: Restricting sample-to-sample $\Delta$ (e.g. `diff > 350`) truncates steep audio slopes, mutilating speech harmonics ($> 4\text{ kHz}$) and causing harsh tearing noise and pops.
6. **Zero-UART Architecture**: PB10 shares `USART3_TX` and SPI Flash MOSI. Physically remove all Serial COM from firmware. Do not use dummy stubs. Disabling USART gates its peripheral clock completely, eliminating cross-talk and supply EMI onto `PA4` (DAC).

---

## 4. Hardware Ring Refill Loop (Double-Buffering)

```c
#define PAM_RING_SAMPLES 512u
#define PAM_HALF         256u

static uint32_t s_ring[PAM_RING_SAMPLES];

// Polling loop in pam_play():
while (s_state != ST_DONE) {
    uint32_t f = hw_flags();
    if (f & HWF_HT) {
        hw_clear(HWF_HT);
        fill_half(&s_ring[0], PAM_HALF); // Fill lower half
    }
    if (f & HWF_TC) {
        hw_clear(HWF_TC);
        fill_half(&s_ring[PAM_HALF], PAM_HALF); // Fill upper half
    }
}
```
This guarantees continuous, glitch-free audio playback even if SPI flash read latency varies slightly.
