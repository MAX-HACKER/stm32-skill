# STM32 Hardware Wiring & Connection Guide Specification

This guide provides the definitive wiring patterns, electrical rules, and standard connection matrix for STM32 microcontrollers (reference: **STM32G431CBT6** @ 170 MHz) with displays, microphones, audio amplifiers, SPI flash, keypads, sensors, and relays.

---

## 1. System Architecture Diagram

```
                                  +---------------------------------------------------+
                                  |            STM32G431 Microcontroller              |
                                  |     (ARM Cortex-M4 @ 170 MHz, 128KB Flash)        |
                                  +---------------------------------------------------+
                                            |                       |
                  I2C1 Bus                  |                       |        Hardware / Bit-Bang SPI
             +---------------------------+  |                       |  +---------------------------+
             | 16x2 HD44780 LCD (3.3V)   |--+                       +--| W25Qxx NOR SPI Flash      |
             | - SCL  -> PB6             |                             | - CS   -> PB0             |
             | - SDA  -> PB7             |                             | - SCK  -> PB2             |
             +---------------------------+                             | - MISO -> PA6             |
                                            |                          | - MOSI -> PB10 (NO UART!) |
                     I2S Digital Mic Bus    |                          +---------------------------+
             +---------------------------+  |
             | INMP441 I2S MEMS Mic      |--+                          Hardware DAC & Audio Amp
             | - WS   -> PB12            |                             +---------------------------+
             | - SCK  -> PB13            |-----------------------------| PAM8403 Class-D Amp (5V)  |
             | - SD   -> PB15            |                             | - DAC1_OUT1 -> PA4        |
             | - L/R  -> GND (Left)      |                             | - PWM Out   -> PA8        |
             +---------------------------+                             | - Spk Out   -> 4Ω/8Ω Spk  |
                                            |                          +---------------------------+
                     Analog & 1-Wire        |
             +---------------------------+  |                          4x4 Matrix Keypad (Ribbon)
             | Environmental Sensors     |--+                          +---------------------------+
             | - LDR (A0)   -> PA0 (ADC) |                             | Cols: PC13, PC15, PF1, PA1|
             | - DHT11 Data -> PA2       |                             | Rows: PA3,  PA5,  PA7, PB1|
             +---------------------------+                             +---------------------------+
                                            |
                                            |       4-Channel 3.3V Relays (Active-LOW)
                                            +---------------------------------------+
                                            | Relay 1 (Room Light)    -> PB4        |
                                            | Relay 2 (Fan)           -> PB5        |
                                            | Relay 3 (Gaming Socket) -> PB3        |
                                            | Relay 4 (Lighting Mode) -> PB9        |
                                            +---------------------------------------+
```

---

## 2. Standard Hardware Connection Matrix

| Subsystem | Signal Name | MCU Pin | Voltage Rail | Pullup / Filter | Electrical Description & Behavior |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Audio Output** | **L_IN (Audio In)** | **`PA4`** | — | Direct to Amp L_IN | 12-bit DAC1_OUT1. Output parked at 2048 ($1.65\text{V}$). Limit swing to $\pm 290$ counts. |
| | **PWM Fallback** | **`PA8`** | — | — | High-frequency 64 kHz ultrasonic carrier fallback. |
| | **Amp +5V** | — | **5V Rail** | $100\mu\text{F} + 0.1\mu\text{F}$ | Clean 5V power supply for PAM8403 class-D bridge. |
| | **Amp GND** | — | **GND Rail** | Star ground | System ground return. Keep separate from mic analog ground if possible. |
| | **Speaker Out** | — | — | Twisted pair | Differential Bridge-Tied Load (BTL) output to 4Ω or 8Ω speaker. |
| **I2S Microphone** | **WS (Word Select)** | **`PB12`** | — | — | I2S Frame Sync ($16\text{ kHz}$). |
| | **SCK (Bit Clock)** | **`PB13`** | — | — | Continuous Bit Clock ($1.024\text{ MHz}$). |
| | **SD (Serial Data)**| **`PB15`** | — | — | 24-bit audio data in 32-bit slot from INMP441. |
| | **L/R (Channel)** | — | **GND Rail** | Tied to GND | Selects Left audio channel (data asserted while WS is LOW). |
| | **VDD** | — | **3.3V Rail** | $0.1\mu\text{F}$ bypass | **3.3V ONLY**. Never connect 5V to the INMP441! |
| **SPI Flash** | **`CS`** | **`PB0`** | — | 10k pull-up | Active-LOW Chip Select for W25Qxx. |
| | **`CLK`** | **`PB2`** | — | Low slew rate | Serial Clock. Avoid long unshielded jumper wires. |
| | **`MISO` (D0)** | **`PA6`** | — | — | Master In, Slave Out data line. |
| | **`MOSI` (D1)** | **`PB10`** | — | — | **CRITICAL**: Shares pin with `USART3_TX`. NEVER enable Serial on PB10! |
| | **VCC** | — | **3.3V Rail** | $0.1\mu\text{F}$ bypass | 3.3V supply for W25Qxx NOR Flash. |
| **I2C Display** | **SCL** | **`PB6`** | — | 4.7kΩ pull-up | Hardware or Software I2C Clock ($100\text{ kHz}$). |
| | **SDA** | **`PB7`** | — | 4.7kΩ pull-up | Hardware or Software I2C Data. |
| | **VCC** | — | **3.3V or 5V** | — | PCF8574 backpack (default address `0x27`). |
| **Keypad 4x4** | **Col 1** | **`PC13`** | — | — | Column 1 drive output (driven LOW during scan). |
| | **Col 2** | **`PC15`** | — | — | Column 2 drive output. |
| | **Col 3** | **`PF1`** | — | — | Column 3 drive output. |
| | **Col 4** | **`PA1`** | — | — | Column 4 drive output. |
| | **Row 1** | **`PA3`** | — | Internal Pull-Up | Row 1 sense input (read as LOW when button pressed). |
| | **Row 2** | **`PA5`** | — | Internal Pull-Up | Row 2 sense input. |
| | **Row 3** | **`PA7`** | — | Internal Pull-Up | Row 3 sense input. |
| | **Row 4** | **`PB1`** | — | Internal Pull-Up | Row 4 sense input. |
| **Sensors** | **LDR (Light)** | **`PA0`** | — | 10kΩ to 3.3V | Voltage divider on `ADC1_IN1`. Ambient $< 30\%$ triggers dark room. |
| | **DHT11 Data** | **`PA2`** | — | 4.7kΩ pull-up | Bidirectional single-wire data for temperature & humidity. |
| **Relays** | **Relay 1 (Light)** | **`PB4`** | — | Active-LOW | Optocoupler input. Driven LOW to energize relay coil. |
| | **Relay 2 (Fan)** | **`PB5`** | — | Active-LOW | Driven LOW to turn fan on. |
| | **Relay 3 (Game)** | **`PB3`** | — | Active-LOW | Driven LOW to switch game mode socket. |
| | **Relay 4 (Mode)** | **`PB9`** | — | Active-LOW | Driven LOW to change lighting mode. |
| | **Relay VCC** | — | **5V / 3.3V** | — | Coil power (typically 5V with 3.3V logic compatibility). |

---

## 3. Mandatory Hardware Rules & Traps

1. **PB10 / USART3_TX Collision**:
   If an external SPI flash is wired to `PB10`, `USART3` must NEVER be initialized. Initializing `Serial.begin(...)` on boards where USART3 defaults to PB10 causes the GPIO to switch to UART alternate function, breaking SPI communication immediately.
2. **PAM8403 Input Capacitors**:
   Pre-assembled PAM8403 breakout boards already include surface-mount input DC-blocking capacitors (C1/C2). Do not add external series capacitors unless visual inspection confirms the board lacks them.
3. **INMP441 Microphone Voltage**:
   The INMP441 MEMS microphone is strictly rated for $1.8\text{V} \dots 3.3\text{V}$. Connecting it to 5V will permanently destroy the MEMS sensor element.
4. **Relay Pre-Initialization Chatter Prevention**:
   Always execute `digitalWrite(pin, HIGH)` **before** executing `pinMode(pin, OUTPUT)` for active-LOW relays. Configuring the pin as output before setting the output latch high causes a momentary $10\text{ µs}$ pulse to ground, creating relay chatter during boot.
