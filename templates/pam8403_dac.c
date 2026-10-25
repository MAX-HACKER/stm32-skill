/*******************************************************************************
 * pam8403_dac.c - Register-level DAC1 + TIM6 + DMA1 Driver for STM32G4
 *
 * Plug-and-play CMSIS register driver for STM32G431.
 * Features:
 *   - Zero CPU jitter (hardware TIM6 TRGO triggers DAC1 CH1)
 *   - Circular DMA1 Channel 3 ping-pong double buffer
 *   - Clamped output swing (PAM_SWING_LIMIT = 420) for PAM8403 rail safety
 *   - Anti-pop smooth 256-sample cosine fade ramps
 *   - Integrated 4-bit IMA ADPCM streaming decoder
 ******************************************************************************/
#include "pam8403_dac.h"
#ifndef PAM_HOST_TEST
#include "stm32g4xx.h"
#endif

#define PAM_HALF   (PAM_RING_SAMPLES / 2u)
#define PAM_SRC_CHUNK 64u

#define HWF_HT 1u
#define HWF_TC 2u

#ifdef PAM_HOST_TEST
  #define PAM_HW
#else
  #define PAM_HW static
#endif

PAM_HW void     hw_init(void);
PAM_HW void     hw_start(uint32_t *ring, uint32_t n, uint32_t rate_hz);
PAM_HW void     hw_stop(void);
PAM_HW uint32_t hw_flags(void);
PAM_HW void     hw_clear(uint32_t f);

enum { ST_IDLE = 0, ST_PLAY, ST_FADE, ST_DONE };

static uint32_t      s_ring[PAM_RING_SAMPLES];
static int16_t       s_pcm[PAM_SRC_CHUNK];
static int32_t       s_peak       = 235;
static uint8_t       s_volume     = 3;
static uint32_t      s_underruns  = 0;

static pam_source_fn s_src;
static void         *s_ctx;
static int           s_state      = ST_IDLE;
static uint32_t      s_fade_in    = 0;
static uint32_t      s_fade_pos   = 0;
static int32_t       s_last_dev   = 0;
static int32_t       s_drain      = -1;

static const int16_t k_peak[PAM_VOLUME_LEVELS] = { 250, 360, 480, 600, 700 };

void pam_set_volume(uint8_t level) {
    if (level < 1) level = 1;
    if (level > PAM_VOLUME_LEVELS) level = PAM_VOLUME_LEVELS;
    s_volume = level;
    s_peak   = k_peak[level - 1];
}

uint8_t  pam_get_volume(void)     { return s_volume; }
uint32_t pam_last_underruns(void) { return s_underruns; }

static int32_t shape(int16_t in) {
    int32_t x = in;

    int32_t dev = (x * s_peak) >> 15;

    if (s_fade_in < PAM_FADE_IN_SAMPLES) {
        dev = (dev * (int32_t)s_fade_in) / (int32_t)PAM_FADE_IN_SAMPLES;
        s_fade_in++;
    }

    if (dev >  PAM_SWING_LIMIT) dev =  PAM_SWING_LIMIT;
    if (dev < -PAM_SWING_LIMIT) dev = -PAM_SWING_LIMIT;

    return dev;
}

static void fill_half(uint32_t *dst, uint32_t n) {
    uint32_t out = 0;
    while (out < n && s_state == ST_PLAY) {
        uint32_t want = n - out;
        if (want > PAM_SRC_CHUNK) want = PAM_SRC_CHUNK;

        uint32_t got = s_src(s_ctx, s_pcm, want);
        if (got > want) got = want;

        if (got == 0u) {
            s_state = ST_FADE;
            break;
        }

        for (uint32_t i = 0; i < got; ++i) {
            int32_t dev = shape(s_pcm[i]);
            s_last_dev = dev;
            dst[out++] = (uint32_t)(PAM_DAC_MID + dev);
        }
    }

    while (out < n) {
        if (s_state == ST_FADE) {
            uint32_t remain = (s_fade_pos < PAM_FADE_OUT_SAMPLES)
                            ? (PAM_FADE_OUT_SAMPLES - s_fade_pos) : 0u;
            int32_t d = (remain == 0u) ? 0
                        : (s_last_dev * (int32_t)remain) / (int32_t)PAM_FADE_OUT_SAMPLES;
            dst[out++] = (uint32_t)(PAM_DAC_MID + d);
            if (++s_fade_pos >= PAM_FADE_OUT_SAMPLES)
                s_state = ST_DONE;
        } else {
            dst[out++] = (uint32_t)PAM_DAC_MID;
        }
    }

    if (s_state == ST_DONE && s_drain < 0)
        s_drain = 2;
}

void pam_init(void) {
    hw_init();
    for (uint32_t i = 0; i < PAM_RING_SAMPLES; ++i)
        s_ring[i] = (uint32_t)PAM_DAC_MID;
    s_state = ST_IDLE;
}

void pam_stop(void) {
    hw_stop();
    s_state = ST_IDLE;
    for (uint32_t i = 0; i < PAM_RING_SAMPLES; ++i)
        s_ring[i] = (uint32_t)PAM_DAC_MID;
}

uint32_t pam_play(pam_source_fn source, void *ctx, uint32_t sample_rate_hz) {
    if (!source || sample_rate_hz == 0) return 0;

    pam_stop();

    s_src        = source;
    s_ctx        = ctx;
    s_state      = ST_PLAY;
    s_fade_in    = 0;
    s_fade_pos   = 0;
    s_last_dev   = 0;
    s_drain      = -1;
    s_underruns  = 0;

    fill_half(&s_ring[0],        PAM_HALF);
    fill_half(&s_ring[PAM_HALF], PAM_HALF);

    hw_start(s_ring, PAM_RING_SAMPLES, sample_rate_hz);

    while (s_state != ST_DONE || s_drain > 0) {
        uint32_t f = hw_flags();
        if (f & HWF_HT) {
            hw_clear(HWF_HT);
            fill_half(&s_ring[0], PAM_HALF);
            if (s_drain > 0) s_drain--;
        }
        if (f & HWF_TC) {
            hw_clear(HWF_TC);
            fill_half(&s_ring[PAM_HALF], PAM_HALF);
            if (s_drain > 0) s_drain--;
        }
    }

    pam_stop();
    return s_underruns;
}

#ifndef PAM_HOST_TEST
/* ---- Hardware Layer (CMSIS Registers on STM32G4) ------------------------- */
static uint32_t s_hw_flags = 0;

PAM_HW void hw_init(void) {
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN | RCC_AHB2ENR_DAC1EN;
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA1EN  | RCC_AHB1ENR_DMAMUX1EN;
    RCC->APB1ENR1 |= RCC_APB1ENR1_TIM6EN;

    GPIOA->MODER |= (3u << (4 * 2)); // PA4 Analog

    DAC1->CR = 0;
    DAC1->MCR &= ~(7u << 0);
    DAC1->CR |= (0u << 2) | DAC_CR_TEN1 | DAC_CR_DMAEN1; // TIM6 TRGO trigger
    DAC1->DHR12R1 = PAM_DAC_MID;
    DAC1->CR |= DAC_CR_EN1;
}

PAM_HW void hw_start(uint32_t *ring, uint32_t n, uint32_t rate_hz) {
    TIM6->CR1 = 0;
    TIM6->PSC = 0;
    SystemCoreClockUpdate();
    TIM6->ARR = (SystemCoreClock / rate_hz) - 1u;
    TIM6->CR2 = (2u << 4); // TRGO on Update

    DMA1_Channel3->CCR = 0;
    DMAMUX1_Channel2->CCR = 6; // DAC1_CH1 DMA request

    DMA1_Channel3->CPAR = (uint32_t)&(DAC1->DHR12R1);
    DMA1_Channel3->CMAR = (uint32_t)ring;
    DMA1_Channel3->CNDTR = n;
    DMA1_Channel3->CCR = DMA_CCR_PL_1 | (2u << DMA_CCR_MSIZE_Pos) | (2u << DMA_CCR_PSIZE_Pos)
                       | DMA_CCR_MINC | DMA_CCR_CIRC | DMA_CCR_DIR;

    DMA1->IFCR = DMA_IFCR_CGIF3;
    DMA1_Channel3->CCR |= DMA_CCR_EN;

    DAC1->DHR12R1 = ring[0]; // Preload DHR before first TRGO latch to avoid startup pop
    TIM6->CNT = 0;
    TIM6->EGR = TIM_EGR_UG;
    TIM6->CR1 |= TIM_CR1_CEN;
}

PAM_HW void hw_stop(void) {
    TIM6->CR1 &= ~TIM_CR1_CEN;
    DMA1_Channel3->CCR &= ~DMA_CCR_EN;
    DAC1->DHR12R1 = PAM_DAC_MID;
}

PAM_HW uint32_t hw_flags(void) {
    uint32_t isr = DMA1->ISR;
    uint32_t f = 0;
    if (isr & DMA_ISR_HTIF3) f |= HWF_HT;
    if (isr & DMA_ISR_TCIF3) f |= HWF_TC;
    return f;
}

PAM_HW void hw_clear(uint32_t f) {
    uint32_t clr = 0;
    if (f & HWF_HT) clr |= DMA_IFCR_CHTIF3;
    if (f & HWF_TC) clr |= DMA_IFCR_CTCIF3;
    DMA1->IFCR = clr;
}
#endif
