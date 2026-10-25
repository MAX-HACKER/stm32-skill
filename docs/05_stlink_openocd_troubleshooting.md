# ST-Link & OpenOCD Flashing, Debugging & Hardware Unbricking Runbook

This runbook documents production flashing commands, adapter speed tuning, OpenOCD scripting, and recovery procedures for STM32 microcontrollers connected via ST-Link V2 / V3.

---

## 1. Electrical & Interface Best Practices

* **Clock Speed on Breadboards**:
  Standard jumper wires and breadboards have significant stray capacitance ($2 \dots 10\text{ pF}$) and inductance. High SWD clock rates ($> 2\text{ MHz}$) will produce ringing and corrupt DP/AP transactions.
  * **Always specify**: `adapter speed 100` ($100\text{ kHz}$) in OpenOCD commands for Rock-Solid stability!
  * Increase to `adapter speed 1000` ($1\text{ MHz}$) only on finished custom PCBs with short ground-plane traces.
* **Target Voltage**:
  ST-Link Pin 1 (TVCC) is a **voltage sense pin**, NOT a 3.3V power output. Ensure Pin 1 is connected to target 3.3V so the ST-Link level shifters activate.

---

## 2. Essential OpenOCD Command Templates

### 1. Diagnostic / Connection Check
```bash
openocd -f interface/stlink.cfg -f target/stm32g4x.cfg \
  -c "adapter speed 100" \
  -c "init" \
  -c "reset halt" \
  -c "targets" \
  -c "exit"
```

### 2. Full Chip Erase (Mass Erase)
```bash
openocd -f interface/stlink.cfg -f target/stm32g4x.cfg \
  -c "adapter speed 100" \
  -c "init" \
  -c "reset halt" \
  -c "flash erase_sector 0 0 last" \
  -c "exit"
```

### 3. Flash Program & Verify
```bash
openocd -f interface/stlink.cfg -f target/stm32g4x.cfg \
  -c "adapter speed 100" \
  -c "init" \
  -c "reset halt" \
  -c "program {firmware.bin} 0x08000000 verify reset exit"
```

### 4. Reading SRAM Diagnostic Words (Memory Inspection)
```bash
# Read 4 words (16 bytes) at address 0x20004000
openocd -f interface/stlink.cfg -f target/stm32g4x.cfg \
  -c "adapter speed 100" \
  -c "init" \
  -c "mdw 0x20004000 4" \
  -c "exit"
```

---

## 3. Unbricking a Locked or Crashing Target

If firmware enters `HardFault`, crashes into an invalid clock configuration, or disables the SWD GPIO pins (`PA13`/`PA14`), normal SWD connection will fail with `Error: init mode failed (unable to connect to the target)`.

### Hardware Recovery (Connect Under Reset):
1. Connect the ST-Link `NRST` pin to the STM32 `NRST` pin.
2. Run OpenOCD with `connect_assert_srst`:
   ```bash
   openocd -f interface/stlink.cfg -f target/stm32g4x.cfg \
     -c "adapter speed 100" \
     -c "reset_config srst_only srst_nogate connect_assert_srst" \
     -c "init" \
     -c "reset halt" \
     -c "flash erase_sector 0 0 last" \
     -c "exit"
   ```

### Software Boot0 Recovery (No NRST Wire Available):
1. Tie the `BOOT0` pin to `3.3V` (activates internal ROM bootloader).
2. Power cycle the board.
3. Run the standard erase command.
4. Tie `BOOT0` back to `GND`.
5. Flash new firmware.
