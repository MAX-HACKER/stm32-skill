/*******************************************************************************
 * pam8403_dac.h - Clean, Low-Noise DAC Audio Driver for STM32 + PAM8403
 *
 * Signal Path:
 *   TIM6 (Hardware Sample Clock) --TRGO--> DAC1_CH1 (PA4) <--DMA1_CH3-- Ring
 *
 * Key Electrical Calibrations:
 *   - Midpoint: 2048 (1.65V DC bias)
 *   - Swing Limit: 420 counts (clamps peak output below 0.34Vpk, ensuring
 *     PAM8403 5V amplifier rails never saturate or clip into square waves)
 *   - 5-Level Scaled Volume: { 140, 190, 250, 310, 380 }
 *   - 256-sample Cosine Anti-Pop Fade In & Fade Out ramps
 ******************************************************************************/
#ifndef PAM8403_DAC_H
#define PAM8403_DAC_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PAM_RING_SAMPLES      512u
#define PAM_DAC_MID           2048
#define PAM_SWING_LIMIT       750     /* Maximum peak swing limit counts (250..750) */
#define PAM_FADE_IN_SAMPLES   256u    /* ~8 ms ramp @ 32 kHz */
#define PAM_FADE_OUT_SAMPLES  256u    /* ~8 ms ramp to midpoint */

#define PAM_VOLUME_LEVELS     5

typedef uint32_t (*pam_source_fn)(void *ctx, int16_t *dst, uint32_t n);

void     pam_init(void);
void     pam_set_volume(uint8_t level); // 1..5
uint8_t  pam_get_volume(void);
void     pam_stop(void);
uint32_t pam_play(pam_source_fn source, void *ctx, uint32_t sample_rate_hz);
uint32_t pam_last_underruns(void);

// 4-bit IMA ADPCM Decoder
typedef struct {
    int32_t predicted;
    int8_t  step_index;
} pam_adpcm_t;

void pam_adpcm_reset(pam_adpcm_t *s);
void pam_adpcm_byte(pam_adpcm_t *s, uint8_t byte, int16_t out[2]);

#ifdef __cplusplus
}
#endif
#endif
