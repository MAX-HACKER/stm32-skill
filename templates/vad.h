/*******************************************************************************
 * vad.h - Adaptive Energy Voice Activity Detection (VAD) for STM32
 *
 * Calibrations:
 *   - Continuous asymmetric noise floor tracking in silence
 *   - +13 dB Speech threshold margin above ambient noise
 *   - +5.5 dB Silence hysteresis
 *   - 3 consecutive onset frames (30 ms) to reject phone notification pings
 *   - 10 hangover frames (100 ms) to bridge natural syllable pauses
 *   - 1.5s max speech duration cap to reject fan/AC hum
 ******************************************************************************/
#ifndef VAD_H
#define VAD_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VAD_MIN_SPEECH_FRAMES     12    // 120 ms min duration (matches enrollment)
#define VAD_MAX_SPEECH_FRAMES     150   // 1.5 seconds max speech duration
#define VAD_ONSET_CONFIRM_FRAMES  3     // 30 ms consecutive speech to trigger onset (rejects clicks/chirps)
#define VAD_HANGOVER_FRAMES       10    // 100 ms silence to detect end-of-speech

typedef enum {
    VAD_STATE_SILENCE = 0,
    VAD_STATE_ONSET,
    VAD_STATE_SPEECH,
    VAD_STATE_END
} VadState;

typedef struct {
    float    noise_floor_energy;
    float    speech_threshold_energy;
    float    silence_threshold_energy;
    VadState state;
    uint16_t speech_frame_count;
    uint16_t silence_frame_count;
    uint16_t onset_frame_count;
    float    peak_energy;
    float    sum_energy;
    bool     max_frames_hit;
} VadContext;

void vad_init(VadContext *vad);
void vad_calibrate(VadContext *vad, float ambient_energy);
void vad_reset(VadContext *vad);
VadState vad_process_frame(VadContext *vad, const float *frame_samples, uint16_t frame_len, float *frame_energy_out);
bool vad_is_valid_utterance(const VadContext *vad);

#ifdef __cplusplus
}
#endif
#endif
