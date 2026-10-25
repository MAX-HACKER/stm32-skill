# STM32 High-Performance DSP, Audio & Embedded Engineering Skill Repository

Welcome to the **STM32 Skill Repository** (`/stmskill`). This repository is designed as an all-in-one runbook, calibration handbook, calculation guide, and verified driver collection for developers and engineers building high-performance audio, voice recognition, digital signal processing (DSP), and real-time control systems on the **STM32** family (specifically STM32G4, STM32F4, STM32F1).

---

## Repository Structure

```
STM32_SKILL/
├── SKILL.md                                  # Antigravity skill cheatsheet (activated via /stmskill)
├── README.md                                 # This overview & quick start
├── docs/
│   ├── 01_audio_pam8403_dac_calculations.md  # DAC swing, PAM8403 rail limits, TIM6+DMA ping-pong
│   ├── 02_w25qxx_relay_and_adpcm.md         # W25Qxx SPI protocol, ST-Link relay, IMA ADPCM
│   ├── 03_inmp441_i2s_vad_dsp.md             # I2S MEMS mic, asymmetric energy VAD, hangover
│   ├── 04_mfcc_mel_filterbank_dtw.md         # 256 Real-FFT, 24 Mel filters, CMS, DTW matching
│   └── 05_stlink_openocd_troubleshooting.md  # SWD flashing, recover commands, adapter speed
└── scripts/
    ├── stlink_recover.ps1                    # Automated OpenOCD unbrick script
    ├── adpcm_tool.py                         # 4-bit IMA ADPCM encoder/decoder
    └── sarvam_audio_builder.py               # Sarvam AI Voice Clone & high-RMS audio processor
```

---

## How to Activate

Type `/stmskill` in the chat, or ask any question related to:
* STM32 DAC audio playback, DMA double-buffering, or PAM8403 distortion
* External SPI flash (W25Qxx) programming without UART
* INMP441 I2S microphone capture & Voice Activity Detection (VAD)
* On-device speech recognition via MFCC & Dynamic Time Warping (DTW)
* ST-Link recovery, OpenOCD scripting, or bricked STM32 rescue

---

## Hardware Pinout Reference (STM32G431CBT6)

| Pin | Function | Peripheral / Module | Key Considerations |
|---|---|---|---|
| **PA4** | DAC Audio Out | DAC1_OUT1 | Park at 2048 ($1.65\text{V}$). Limit swing to $\pm 420$ counts ($\le 0.34\text{V}_{\text{pk}}$) to prevent PAM8403 clipping. |
| **PB12** | I2S Word Select | I2S2_WS | Frame sync (16 kHz). |
| **PB13** | I2S Serial Clock | I2S2_CK | Bit clock ($16\text{kHz} \times 32\text{ bits} \times 2 = 1.024\text{ MHz}$). |
| **PB15** | I2S Serial Data | I2S2_SD | 24-bit audio in 32-bit slot from INMP441 digital microphone. |
| **PB0** | Flash Chip Select | GPIO OUT | Active-low. Dedicated line for W25Qxx. |
| **PB2** | Flash Serial Clock | GPIO OUT | SPI Clock. Low slew rate avoids breadboard ringing. |
| **PB10** | Flash MOSI | GPIO OUT | **CRITICAL**: Shares pin with `USART3_TX`. Never initialize `Serial` when using W25Qxx! |
| **PA6** | Flash MISO | GPIO IN | SPI Data in from W25Qxx. |
| **PB6** | I2C SCL | I2C1_SCL | 100 kHz standard mode with 4.7k pull-ups. |
| **PB7** | I2C SDA | I2C1_SDA | LCD 16x2 PCF8574 communication. |
| **PA0** | ADC Light Sensor | ADC1_IN1 | 12-bit analog input for LDR voltage divider. |
| **PB14** | DHT11 Data | GPIO (1-Wire) | Precise microsecond timing for humidity/temperature. |
| **PB4** | Relay 1 (Light) | GPIO OUT | Active-low optocoupler control. |
| **PB5** | Relay 2 (Fan) | GPIO OUT | Active-low optocoupler control. |
| **PB3** | Relay 3 (Game) | GPIO OUT | Active-low optocoupler control. |
| **PB9** | Relay 4 (Mode) | GPIO OUT | Active-low optocoupler control. |
| **PB1,8,11, PC13** | Keypad Rows | GPIO OUT | Active-low scan outputs. |
| **PC14,15, PA1,2** | Keypad Columns | GPIO IN_PULLUP | High-impedance inputs with internal pull-ups. |
