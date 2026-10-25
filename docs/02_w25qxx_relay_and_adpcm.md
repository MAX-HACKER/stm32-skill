# External SPI Flash (W25Qxx) & IMA ADPCM Pipeline

This guide covers interfacing Winbond **W25Q32 / W25Q64** SPI NOR Flash memory with an STM32, the **IMA ADPCM** audio codec, and the **ST-Link Audio Relay** architecture for programming external flash chips without a serial port.

---

## 1. Pin Sharing Trap: PB10 (MOSI vs USART3_TX)

In the standard STM32G4 / STM32F4 pinout, pin **PB10** is the default transmit pin for `USART3_TX`.
* If your board connects W25Qxx `MOSI` to **PB10**, calling `Serial.begin(...)` or initializing USART3 will **reconfigure PB10 to alternate function push-pull (UART)**.
* **Symptom**: SPI communication immediately dies; reading JEDEC ID returns `0x000000` or `0xFFFFFF`.
* **Design Rule**:
  * If PB10 is wired to MOSI, **disable USART3 completely** in firmware, or remap USART3 to PC10/PC11 or PB3/PB4.
  * Use ST-Link SWD (or an isolated auxiliary UART on PA9/PA10) for debug logging.

---

## 2. IMA/DVI 4-Bit ADPCM Codec

### Theoretical Overview
Linear 16-bit PCM requires $64,000\text{ bytes/sec}$ at $32\text{ kHz}$. IMA ADPCM compresses each 16-bit audio sample into a **4-bit nibble** (4:1 compression ratio), reducing storage and transfer bandwidth to **$16,000\text{ bytes/sec}$** ($16\text{ KB/s}$).

### Step Table (Quantizer Adaptation)
```c
static const uint16_t adpcm_step_table[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50,
    55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279,
    307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282,
    1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871,
    5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818,
    18500, 20350, 22385, 24623, 27086, 29794, 32767
};
static const int8_t adpcm_index_table[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8
};
```

### Decoding Algorithm (Fast Integer Implementation)
```c
void pam_adpcm_byte(pam_adpcm_t *s, uint8_t byte, int16_t out[2]) {
    for (int i = 0; i < 2; i++) {
        uint8_t nibble = (i == 0) ? (byte & 0x0F) : ((byte >> 4) & 0x0F);
        int32_t step = adpcm_step_table[s->step_index];
        int32_t vpdiff = step >> 3;
        if (nibble & 4) vpdiff += step;
        if (nibble & 2) vpdiff += (step >> 1);
        if (nibble & 1) vpdiff += (step >> 2);

        if (nibble & 8) s->predicted -= vpdiff;
        else            s->predicted += vpdiff;

        if (s->predicted > 32767)  s->predicted = 32767;
        if (s->predicted < -32768) s->predicted = -32768;

        s->step_index += adpcm_index_table[nibble];
        if (s->step_index < 0)  s->step_index = 0;
        if (s->step_index > 88) s->step_index = 88;

        out[i] = (int16_t)s->predicted;
    }
}
```

---

## 3. ST-Link Audio Relay Architecture (No Serial / UART Needed)

When programming a 1MB+ voice pack onto an SPI flash chip on a custom STM32 board without a USB-to-UART converter:

### Memory Map:
* `0x08000000 - 0x08005FFF`: Audio Relay Firmware ($\approx 10\text{ KB}$, permanent resident).
* `0x0800E000 - 0x0800E00F`: Descriptor Header (Magic `0x52454C59`, destination address, byte count, CRC32).
* `0x08010000 - 0x0801FFFF`: 64 KB Chunk Staging Zone in STM32 internal Flash.
* `0x20004000`: 4 Diagnostic Status Words in SRAM (`STATUS`, `JEDEC`, `ERROR`, `PROGRESS`).

### Protocol Flow:
1. PC splits the binary voice pack into 64 KB chunks.
2. PC uses OpenOCD to flash the chunk and descriptor header to the STM32 staging area.
3. PC issues `reset halt` and `resume`.
4. Relay firmware detects header, unprotects W25Qxx, erases the required 64 KB block, programs 256-byte pages via SPI, and reads back to verify CRC32.
5. Relay posts `0xDEADBEEF` to `0x20004000`.
6. PC reads `0x20004000`, confirms `0xDEADBEEF`, erases staging area, and advances to the next chunk.
7. Repeat until 100% written, then erase STM32 and flash the production application.
