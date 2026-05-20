#define DEBUG_STATE     /* print state transitions; remove for Phase 4 silent mode */
#include "systick.h"    /* millis() — peak interval timing */
#include "usart.h"      /* uart_write_str, uart_write_u32 — output only */
#include "algorithm.h"

/* algorithm.c owns no peripheral registers — stm32f0xx.h is intentionally absent (D-01) */

/* --- Moving average (SIG-01) --- */
static uint16_t  s_ma_buf[32];        /* circular buffer, 32 samples */
static uint8_t   s_ma_idx  = 0;
static uint32_t  s_ma_sum  = 0;       /* running sum; uint32 avoids overflow (32 × 4095 = 131040) */

/* --- Adaptive threshold peak detector (SIG-02, SIG-03) --- */
#define REFRACTORY_MS  350U            /* midpoint of 300-400ms SIG-03 window */

typedef enum { IDLE, RISING, PEAK_HOLD, FALLING, REFRACTORY } peak_state_t;
static peak_state_t s_state          = IDLE;
static uint16_t     s_threshold      = 1200; /* initial ~60% of midpoint; updated after each peak */
static uint16_t     s_peak_val       = 0;    /* maximum filtered value seen in RISING */
static uint32_t     s_refractory_start = 0;  /* millis() value when refractory began */

/* --- BPM calculation (SIG-04, SIG-05) --- */
static uint32_t  s_last_peak_ms  = 0;
static uint16_t  s_bpm_buf[5];          /* rolling average ring buffer */
static uint8_t   s_bpm_idx       = 0;
static uint8_t   s_bpm_count     = 0;   /* valid readings currently in buf, 0..5 */

/* --------------------------------------------------------------------------
 * algorithm_init
 *   Zero all static state before first algorithm_process() call.
 *   Called once from main() after adc_init().
 * -------------------------------------------------------------------------- */
void algorithm_init(void)
{
    /* Zero moving-average buffer and sum */
    for (uint8_t i = 0; i < 32; i++) s_ma_buf[i] = 0;
    s_ma_sum = 0;
    s_ma_idx = 0;

    /* Reset peak detector state */
    s_state          = IDLE;
    s_threshold      = 1200;
    s_peak_val       = 0;
    s_refractory_start = 0;

    /* Reset BPM rolling average */
    for (uint8_t i = 0; i < 5; i++) s_bpm_buf[i] = 0;
    s_bpm_idx      = 0;
    s_bpm_count    = 0;
    s_last_peak_ms = 0;
}

/* --------------------------------------------------------------------------
 * algorithm_process
 *   Called from main loop once per sample (100 Hz / 10 ms interval).
 *   sample : 12-bit ADC value (or synthetic table entry in SYNTHETIC_TEST mode)
 * -------------------------------------------------------------------------- */
void algorithm_process(uint16_t sample)
{
    /* SIG-01: 32-sample integer moving average
     * Index masked with 0x1F (= modulo 32) — no division, no branch. */
    s_ma_sum -= s_ma_buf[s_ma_idx];
    s_ma_buf[s_ma_idx] = sample;
    s_ma_sum += sample;
    s_ma_idx = (s_ma_idx + 1) & 0x1F;
    uint16_t filtered = (uint16_t)(s_ma_sum >> 5);

    /* SIG-02 + SIG-03: five-state adaptive-threshold peak detector with refractory */
    peak_state_t next_state = s_state;

    switch (s_state)
    {
        case IDLE:
            /* Wait for signal to rise above threshold */
            if (filtered > s_threshold)
            {
                s_peak_val = filtered;
                next_state = RISING;
            }
            break;

        case RISING:
            /* Track maximum while signal keeps climbing */
            if (filtered > s_peak_val)
            {
                s_peak_val = filtered;
            }
            else if (s_peak_val - filtered >= 20)   /* hysteresis: 20 counts ~ 0.5% of 4095 */
            {
                /* Signal has fallen enough below peak — peak has crested; register the beat */
                next_state = PEAK_HOLD;
            }
            break;

        case PEAK_HOLD:
        {
            uint32_t now = millis();   /* single atomic snapshot for the whole case */

            /* --- Register the beat --- */

            /* SIG-02: update adaptive threshold = last_peak * 3 / 5 (integer multiply before divide) */
            s_threshold = (uint16_t)((uint32_t)s_peak_val * 3 / 5);

            /* SIG-03: record refractory start time to suppress dicrotic notch */
            s_refractory_start = now;

            /* SIG-04 + SIG-05: BPM calculation and rolling average */
            if (s_last_peak_ms == 0)
            {
                /* First beat ever — no interval exists yet; record timestamp and skip */
                s_last_peak_ms = now;
            }
            else
            {
                uint32_t interval_ms = now - s_last_peak_ms;
                s_last_peak_ms = now;

                if (interval_ms != 0)
                {
                    uint32_t bpm = 60000UL / interval_ms;

                    /* SIG-04: bounds check — reject readings outside physiological range */
                    if (bpm >= 40 && bpm <= 200)
                    {
                        /* SIG-05: insert into rolling average ring buffer */
                        s_bpm_buf[s_bpm_idx] = (uint16_t)bpm;
                        s_bpm_idx = (s_bpm_idx + 1) % 5;
                        if (s_bpm_count < 5) s_bpm_count++;

                        /* Compute rolling average over valid entries */
                        uint32_t sum = 0;
                        for (uint8_t i = 0; i < s_bpm_count; i++) sum += s_bpm_buf[i];
                        uint32_t bpm_avg = sum / s_bpm_count;

                        /* Print averaged BPM — once per valid detection, not per sample */
                        uart_write_str("BPM: ");
                        uart_write_u32(bpm_avg);
                        uart_write_str("\r\n");
                    }
                }
            }

            next_state = REFRACTORY;
            break;
        }

        case REFRACTORY:
            /* Ignore all samples until refractory window expires — suppresses dicrotic notch */
            if ((millis() - s_refractory_start) >= REFRACTORY_MS)
            {
                next_state = FALLING;
            }
            break;

        case FALLING:
            /* Wait for signal to drop back below threshold before accepting next beat */
            if (filtered <= s_threshold)
            {
                next_state = IDLE;
            }
            break;
    }

    /* Emit state name on transition only — not every sample (D-04) */
    if (next_state != s_state)
    {
        s_state = next_state;
#ifdef DEBUG_STATE
        switch (s_state)
        {
            case IDLE:
                uart_write_str("STATE: IDLE\r\n");
                break;
            case RISING:
                uart_write_str("STATE: RISING\r\n");
                break;
            case PEAK_HOLD:
                uart_write_str("STATE: PEAK_HOLD\r\n");
                break;
            case FALLING:
                uart_write_str("STATE: FALLING\r\n");
                break;
            case REFRACTORY:
                uart_write_str("STATE: REFRACTORY\r\n");
                break;
        }
#endif
    }
}
