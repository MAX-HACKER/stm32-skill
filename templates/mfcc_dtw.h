/*******************************************************************************
 * mfcc_dtw.h - 24-Filter Mel-FFT, MFCC & DTW Speech Recognition for STM32
 *
 * Mathematical Parameters:
 *   - Frame: 400 samples (25 ms @ 16 kHz), Hop: 320 samples (20 ms stride)
 *   - FFT: 512-point Real FFT via 256 complex FFT
 *   - Mel Bank: 24 triangular filters spanning 100 Hz to 7500 Hz
 *   - Lifter: L = 22
 *   - Distance: Integer squared Euclidean distance across c1..c12 (pure timbre,
 *     skipping c0 energy for complete volume-invariance)
 *   - DTW: Sakoe-Chiba constraint (+/- 25%), two-row DP (648 bytes RAM)
 ******************************************************************************/
#ifndef MFCC_DTW_H
#define MFCC_DTW_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MFCC_FRAME_LEN          400
#define MFCC_FFT_SIZE           512
#define MFCC_NUM_MEL_FILTERS    24
#define MFCC_NUM_COEFFS         13
#define MFCC_SCALE_QUANT        8.0f

#define DTW_MAX_FRAMES          80
#define DTW_WINDOW_PERCENT      25

// Center frequency FFT bin boundaries (100 Hz to 7500 Hz @ 16 kHz)
extern const uint16_t g_mel_bin_centers[26];

void     mfcc_init(void);
void     mfcc_compute_frame(const float *frame_samples, float *mfcc_out);
uint16_t mfcc_process_utterance_cms_quant(const int16_t raw_mfcc_in[][MFCC_NUM_COEFFS],
                                          uint16_t num_frames,
                                          int8_t quant_out[][MFCC_NUM_COEFFS]);

float    dtw_compute_distance_quant(const int8_t candidate_quant[][MFCC_NUM_COEFFS], uint16_t len_candidate,
                                    const int8_t template_quant[][MFCC_NUM_COEFFS],  uint16_t len_template);

#ifdef __cplusplus
}
#endif
#endif
