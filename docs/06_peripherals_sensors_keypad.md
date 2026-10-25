# Peripherals, Sensors, Displays & Actuators Interfacing Guide

This document covers wiring, timing, registers, and driver implementations for typical auxiliary hardware integrated with STM32 microcontrollers.

---

## 1. HD44780 16x2 LCD via PCF8574 I2C Backpack

* **I2C Address**: Default `0x27` (PCF8574T) or `0x3F` (PCF8574AT).
* **Bus Speed**: 100 kHz Standard Mode.
* **4-bit Initialization Sequence**:
  1. Wait $> 40\text{ ms}$ after $V_{\text{CC}}$ rises to 4.5V.
  2. Send nibble `0x03` three times with delays ($4.5\text{ ms}$, $4.5\text{ ms}$, $150\text{ µs}$).
  3. Send nibble `0x02` to engage 4-bit mode.
  4. Function Set: `0x28` (4-bit, 2 lines, 5x8 dots).
  5. Display Control: `0x0C` (Display ON, cursor OFF, blink OFF).
  6. Clear Display: `0x01` (requires $> 1.5\text{ ms}$ execution delay).
  7. Entry Mode: `0x06` (Increment cursor, no display shift).
* **Power-Saving Display Sleep Mode**:
  ```cpp
  // Enter display sleep (backlight off, driver quiescent):
  lcd.backlight(false);
  lcd.command(0x08); // HD44780 Display OFF (preserves DDRAM content)

  // Wake display:
  lcd.command(0x0C); // HD44780 Display ON
  lcd.backlight(true);
  ```

---

## 2. 4x4 Matrix Keypad Scanning

* **Wiring**:
  * 4 Row Pins: `PB1`, `PB8`, `PB11`, `PC13` configured as `OUTPUT` (driven `LOW` sequentially).
  * 4 Column Pins: `PC14`, `PC15`, `PA1`, `PA2` configured as `INPUT_PULLUP` (read as `LOW` when pressed).
* **Scan Algorithm**:
  ```cpp
  char getKey() {
      for (int r = 0; r < 4; r++) {
          digitalWrite(rowPins[r], LOW);
          for (int c = 0; c < 4; c++) {
              if (digitalRead(colPins[c]) == LOW) {
                  delay(20); // Debounce
                  while (digitalRead(colPins[c]) == LOW); // Wait release
                  digitalWrite(rowPins[r], HIGH);
                  return keymap[r][c];
              }
          }
          digitalWrite(rowPins[r], HIGH);
      }
      return 0;
  }
  ```

---

## 3. DHT11 Humidity & Temperature (Single-Wire Protocol)

* **Protocol Handshake**:
  1. **MCU Start Signal**: Pull bus line `LOW` for $\ge 18\text{ ms}$, then pull `HIGH` for $20 \dots 40\text{ µs}$.
  2. **Sensor Response**: Sensor pulls line `LOW` for $80\text{ µs}$, then `HIGH` for $80\text{ µs}$.
  3. **Data Transmission (40 bits = 5 bytes)**:
     * Bit `0`: $50\text{ µs}$ LOW, then $26 \dots 28\text{ µs}$ HIGH.
     * Bit `1`: $50\text{ µs}$ LOW, then $70\text{ µs}$ HIGH.
  4. **Checksum Verification**:
     $$\text{Byte 4} = (\text{Byte 0} + \text{Byte 1} + \text{Byte 2} + \text{Byte 3}) \ \& \ 0\text{xFF}$$
  5. Minimum reading interval: **$1.0\text{ second}$** (reading faster causes sensor self-heating).

---

## 4. LDR (Light Dependent Resistor) Ambient Light Sensing

* **Circuit**: $10\text{ k}\Omega$ resistor to $3.3\text{V}$, LDR to `GND`, midpoint connected to `PA0` (`ADC1_IN1`).
* **Conversion**:
  * Bright sunlight: LDR resistance $\approx 1\text{ k}\Omega \implies V_{\text{ADC}} \approx 0.3\text{V} \implies \text{ADC} \approx 370$ ($90\%$).
  * Pitch dark: LDR resistance $> 100\text{ k}\Omega \implies V_{\text{ADC}} \approx 3.0\text{V} \implies \text{ADC} \approx 3720$ ($9\%$).
* **Dark Room Automatic Lighting Rule**:
  ```cpp
  uint8_t lightPercentage = (uint8_t)((4095 - rawAdc) * 100UL / 4095UL);
  if (lightPercentage < 30 && autoLightArmed && !userDisabledAutoLight) {
      setRoomLight(true, /* causedByAuto */ true);
  }
  ```

---

## 5. 4-Channel Relays (Active-LOW Optocouplers)

* **Optocoupler Isolation**:
  * Drive pins: `PB4`, `PB5`, `PB3`, `PB9`.
  * Active state: `LOW` ($0\text{V}$) turns on optocoupler LED, switching relay contacts.
  * Inactive state: `HIGH` ($3.3\text{V}$).
  * **Critical Startup Rule**: Always write `HIGH` to relay output latch **before** configuring pin as `OUTPUT` to prevent momentary relay chatter during MCU power-on reset.
