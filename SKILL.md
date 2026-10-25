---
name: stm32-skill
description: >-
  Expert engineering runbook, design rules, calibrations, formulas, and reusable firmware pipelines
  for STM32 (specifically STM32G4 / STM32F4 / STM32F1) microcontroller projects. Covers PAM8403 DAC audio,
  W25Qxx SPI Flash, INMP441 I2S microphone, VAD, 24-channel Mel-FFT MFCC, DTW voice recognition,
  low-noise DMA architectures, ST-Link OpenOCD debugging, and sensor interfacing.
  Triggered automatically on STM32 DSP/audio tasks or via slash command /stmskill.
---

# STM32 High-Performance Embedded Engineering & DSP Runbook (`/stmskill`)

This skill encapsulates all architectural patterns, electrical calibrations, mathematical formulas, and verified register-level drivers developed across production STM32 projects.

---

## 0. MANDATORY INSTRUCTION: Always Generate Project-Specific Wiring Guide

Whenever proposing, designing, initializing, or modifying any STM32 project, firmware pinout, or peripheral interface, **the agent MUST ALWAYS generate**:
1. **A complete, customized ASCII System Architecture diagram** illustrating the exact microcontroller, bus connections (I2C, SPI, I2S, DAC, GPIO), pin assignments, and target peripherals.
2. **A structured Markdown Hardware Connection Matrix** modeled directly after `README.md` and [docs/07_wiring_and_hardware_connection_guide.md](./docs/07_wiring_and_hardware_connection_guide.md), strictly detailing:
   * **Subsystem**
   * **Signal Name**
   * **MCU Pin & Header Location**
   * **Voltage Rail** (explicitly separating 3.3V vs 5V)
   * **Pullup / Pulldown / Filter Requirements**
   * **Electrical Behavior & Critical Cautions**

Failure to provide this explicit wiring guide leaves hardware connections ambiguous and is UNACCEPTABLE.

---

## 1. Quick Component Reference & Pinout Compatibility

When designing or configuring an STM32 system (reference: **STM32G431CBT6** @ 170 MHz, 128KB Flash, 32KB RAM):

| Subsystem | Peripheral | Pins Used | Electrical Constraints & Notes |
|---|---|---|---|
| **DAC Audio Out** | DAC1_OUT1 + TIM6 + DMA1_CH3 | **PA4** | Output parked at Midpoint 2048 ($1.65\text{V}$). Limit swing to `PAM_SWING_LIMIT = 750` counts to prevent PAM8403 rail clipping. |
| **Microphone In** | I2S2 (SPI2) + DMA | **PB12** (WS), **PB13** (SCK), **PB15** (SD) | INMP441 24-bit in 32-bit slot. High-pass filter at $180\text{Hz}$ to eliminate DC offset. **3.3V ONLY**. |
| **External Flash** | Bit-Bang / SPI2 | **PB0** (CS), **PB2** (SCK), **PB10** (MOSI), **PA6** (MISO) | **CRITICAL**: PB10 shares `USART3_TX`. COMPLETELY REMOVE `Serial` COM from firmware (do not stub/disable) to eliminate UART EMI and cross-talk! |
| **LCD 16x2** | I2C1 | **PB6** (SCL), **PB7** (SDA) | PCF8574 backpack @ `0x27` (or `0x3F`). Use 4-bit bus commands. `0x08` for display sleep, `0x0C` for display wake. |
| **Keypad 4x4** | GPIO | Rows: **PC13, PC15, PF1, PA1**; Cols: **PA3, PA5, PA7, PB1** | Internal pull-ups, active-low scanning with 20ms debounce. |
| **Relay Board** | GPIO (Active-LOW) | **PB4** (Light), **PB5** (Fan), **PB3** (Game), **PB9** (Mode) | Use `RELAY_ACTIVE_LEVEL = LOW` for optocoupler inputs. Write HIGH before setting OUTPUT. |
| **LDR Sensor** | ADC1_IN1 | **PA0** | 12-bit ADC (0..4095). Ambient $< 30\%$ threshold indicates dark room. |
| **DHT11 Sensor** | GPIO (1-Wire) | **PA2** | Single-wire data with 4.7kΩ pullup. |

---

## 2. PAM8403 + DAC1 Audio Output Pipeline

### Electrical Saturation & Swing Calculation
* **DAC Resolution**: 12-bit ($0 \dots 4095$) with $V_{\text{ref}} = 3.3\text{V} \implies 0.80566\text{ mV/count}$.
* **Midpoint**: $2048 \implies 1.650\text{V}$ DC bias.
* **PAM8403 Amplifier**: Powered from $5.0\text{V}$. Voltage gain $A_v = 16\times$ ($24\text{ dB}$).
* **Verified Volume Scale & Swing Limit**:
  * Set `PAM_SWING_LIMIT = 750`.
  * Set 5-step volume scale: `k_peak = { 250, 360, 480, 600, 700 }` with default Volume 3 = 480.
  * Clamp all output samples cleanly:
    ```c
    int32_t dev = (x * s_peak) >> 15;
    if (dev > PAM_SWING_LIMIT) dev = PAM_SWING_LIMIT;
    if (dev < -PAM_SWING_LIMIT) dev = -PAM_SWING_LIMIT;
    ```
* **Anti-Distortion & Noise Rules**:
  * **CRITICAL - Zero-UART Architecture**: PB10 shares `USART3_TX` and SPI Flash MOSI. Physically purge all `Serial.begin()`, `Serial.print()`, and CLI handlers from the firmware. Do NOT use dummy serial wrappers. Purging UART completely silences switching clock noise and cross-talk into `PA4` (DAC).
  * **NEVER apply sample-to-sample slew-rate clamping in audio path**: Restricting sample-to-sample $\Delta$ (e.g. `diff > 350`) mutilates high-frequency speech consonants ($> 4\text{ kHz}$) and transient plosives into triangle distortion, generating massive noise, tearing, and pops.
  * **NEVER gate individual samples near zero** (e.g. `if (x > -35 && x < 35)`). Gating individual samples destroys zero crossings, causing severe crossover buzz.
  * **Hardware Start Preload**: In `hw_start()`, preload `DAC1->DHR12R1 = ring[0]` *before* triggering `TIM6->EGR = TIM_EGR_UG` so the DAC latches the initial sample without an abrupt jump from leftover register state.
  * **Anti-Pop Ramp**: Always ramp smoothly to/from midpoint using a 256-sample ($8\text{ ms}$) fade curve. Never step the DAC directly from 0 to 2048.
  * **Firmware-Flash Sample Rate Match**: Always ensure `PCM_RATE_FLASH_HZ` in `audio_synth.h` matches the exact sample rate of the burned SPI Flash voice pack (32,000 Hz).

### Rule: Inverse Proportionality (RMS Loudness vs. DAC Swing)
* **High-RMS Audio Files (-10 to -14 dBFS, e.g. Neural TTS / Sarvam AI)**: Have high continuous acoustic energy; require **lower DAC swing** (`120..320`) to prevent slamming into the 5V amplifier rails.
* **Low-RMS Audio Files (-22 to -28 dBFS, e.g. Raw Mic Audio)**: Have low continuous energy; require **higher DAC swing** (`300..400`) to achieve clear listening volume.
* **Formula**: $\text{Total Acoustic Energy} \propto \text{File RMS} \times \text{DAC Swing} \times A_v$. (Detailed guide: [docs/08_audio_specs_rms_loudness_and_dac_swing.md](./docs/08_audio_specs_rms_loudness_and_dac_swing.md)).

> [!NOTE]
> ### ⚠️ Reminder & Warning: Amplifier Input Capacitors
> Commercial pre-assembled PAM8403 breakout boards **almost always have onboard SMD DC-blocking capacitors** (labeled C1/C2 right behind $L_{\text{IN}}$ and $R_{\text{IN}}$). You do **NOT** need external capacitors in 99% of builds!
> **Photo Verification**: If unsure whether your module has onboard input caps, take a clear photo of the amplifier board, upload it to the chat, and ask the agent to visually inspect the PCB components!

---

## 3. W25Qxx SPI Flash & 4-bit IMA ADPCM Voice Pack

### Storage & Bandwidth
* **Sampling Rate**: 32,000 Hz Mono, 16-bit PCM.
* **Compression**: 4-bit IMA/DVI ADPCM ($4:1$ reduction).
  $$\text{Data Rate} = 32000 \times 4\text{ bits} = 16,000\text{ bytes/sec} \implies 15.625\text{ KB/sec}$$
* **Flash Budget**: 1.15 MB stores over **72 seconds** of studio-quality speech on a 4MB/8MB W25Qxx chip.
* **Direct Register Flash Read Timing**:
  To prevent DMA underruns during 32kHz playback, use cycle-accurate GPIO toggling with small NOP setup/hold delays (`spi_delay(25..35)`). A 256-byte cache refills in $1.6\text{ ms}$, well within the $8.0\text{ ms}$ half-buffer window.

---

## 4. INMP441 Microphone & Voice Activity Detection (VAD)

### Real-time Energy Tracking
* **Audio Frame**: 400 samples ($25\text{ ms}$) with 160-sample hop ($10\text{ ms}$).
* **RMS Log-Energy Calculation**:
  $$\text{RMS} = \sqrt{\frac{1}{N} \sum_{i=0}^{N-1} x[i]^2}, \quad \text{Energy (dB)} = 20 \log_{10}(\text{RMS})$$
* **Asymmetric Noise Floor Adaptation**:
  ```c
  if (log_e > noise_floor) {
      noise_floor = noise_floor * 0.990f + log_e * 0.010f; // slow rise
  } else {
      noise_floor = noise_floor * 0.920f + log_e * 0.080f; // fast decay
  }
  ```
* **Thresholds & Noise Calibration**:
  * `SPEECH_ENERGY_MARGIN = 7.5f` dB above noise floor (calibrated for high-noise and ambient environments).
  * `VAD_ONSET_CONFIRM_FRAMES = 2` ($20\text{ ms}$) for responsive onset capture.
  * `VAD_HANGOVER_FRAMES = 12` ($120\text{ ms}$) silence to bridge words smoothly.
  * `VAD_MAX_SPEECH_FRAMES = 150` ($1.5\text{ s}$) continuous speech cap to discard air conditioning or fan hum.
  * **Enrollment Training SNR Gate**: Relaxed to $\ge 7.0\text{ dB}$ to allow user enrollment in ambient background noise.
  * **Runtime Speech Filter**: SNR $\ge 6.5\text{ dB}$ and $\ge 8\text{ frames}$ ($160\text{ ms}$) minimum utterance length.

---

## 5. MFCC Feature Extraction & DTW Recognition Engine

### MFCC 24-Filter Mel Bank
* **Filter Center Frequencies (16kHz sample rate, 512 FFT)**:
  Triangular Mel filters from $100\text{ Hz}$ to $7500\text{ Hz}$.
  The 26 bin boundary indices are:
  `{3, 6, 8, 11, 15, 18, 22, 27, 32, 37, 43, 49, 56, 63, 72, 81, 91, 102, 114, 128, 142, 158, 176, 195, 217, 240}`.
* **Cepstral Mean Subtraction (CMS)**:
  Subtract the average of each coefficient across the entire utterance to remove microphone frequency bias:
  $$c_n'[t] = c_n[t] - \mu_n$$
* **DTW Distance & Sakoe-Chiba Constraint**:
  * Warping window: $\pm 25\%$.
  * Duration check: Reject if candidate/template length ratio $> 1.50\times$.
  * **Volume-Invariant Timbre Distance**: Evaluate Euclidean distance across $c_1 \dots c_{12}$ (skip $c_0$ energy):
    $$\text{dist}(f_1, f_2) = \frac{1}{\text{SCALE}} \sqrt{\sum_{c=1}^{12} (f_1[c] - f_2[c])^2}$$
* **Classification Thresholds**:
  * Wake word ("Jarvis"): $\le 6.80$ (speaker-dependent).
  * Commands (1..7): $\le 7.50$.
  * Winner must exceed runner-up by $\ge 15\%$ margin (`second_best >= best * 1.15`).

---

## 6. ST-Link & OpenOCD Flashing Runbook

### OpenOCD Command Line Reference
* Target: `target/stm32g4x.cfg`, Interface: `interface/stlink.cfg`.
* **Slow Adapter Clock (Essential for noisy wiring)**:
  `adapter speed 100` (prevents communication breakdown).
* **Reset & Unbrick Target**:
  ```bash
  openocd -f interface/stlink.cfg -f target/stm32g4x.cfg -c "init" -c "reset halt" -c "flash erase_sector 0 0 last" -c "exit"
  ```
* **Program & Verify Firmware**:
  ```bash
  openocd -f interface/stlink.cfg -f target/stm32g4x.cfg -c "init" -c "reset halt" -c "program {firmware.bin} 0x08000000 verify reset exit"
  ```

---

## 7. Specialized Documentation Map

* [docs/07_wiring_and_hardware_connection_guide.md](./docs/07_wiring_and_hardware_connection_guide.md) - Complete hardware connection matrix and wiring rules.
* [docs/01_audio_pam8403_dac_calculations.md](./docs/01_audio_pam8403_dac_calculations.md) - Register TIM6+DAC DMA setup and audio shaping.
* [docs/02_w25qxx_relay_and_adpcm.md](./docs/02_w25qxx_relay_and_adpcm.md) - W25Qxx SPI protocol, relay staging, and ADPCM encoder.
* [docs/03_inmp441_i2s_vad_dsp.md](./docs/03_inmp441_i2s_vad_dsp.md) - Digital microphone capture and adaptive VAD.
* [docs/04_mfcc_mel_filterbank_dtw.md](./docs/04_mfcc_mel_filterbank_dtw.md) - Real-FFT, Mel filterbank matrix, and DTW matching.
* [docs/05_stlink_openocd_troubleshooting.md](./docs/05_stlink_openocd_troubleshooting.md) - SWD flashing, recover commands, and clock settings.
* [scripts/](./scripts/) - Reusable Python and PowerShell tooling.
