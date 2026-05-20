#include "systick.h"    /* millis() — peak interval timing */
#include "usart.h"      /* uart_write_str, uart_write_u32 — output only */
#include "algorithm.h"

/* algorithm.c owns no peripheral registers — stm32f0xx.h is intentionally absent (D-01) */

/* --- Moving average (SIG-01) --- */
static uint16_t  s_ma_buf[32];        /* circular buffer, 32 samples */
static uint8_t   s_ma_idx  = 0;
static uint32_t  s_ma_sum  = 0;       /* running sum; uint32 avoids overflow (32 × 4095 = 131040) */

/* --- Minimal peak detector (Plan 01 walking slice; replaced by full state machine in Plan 02) --- */
static uint16_t  s_prev_filtered  = 0;
static uint32_t  s_last_peak_ms   = 0;

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

    /* Reset minimal detector state */
    s_prev_filtered = 0;
    s_last_peak_ms  = 0;
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

    /* SIG-02..SIG-05: full detector added in Plan 02 */

    /* Plan 01 minimal peak-to-BPM stage (walking slice only):
     * Detect rising-edge crossing of a fixed threshold of 1000 counts.
     * No refractory, no bounds check, no rolling average — those are Plan 02. */
    if (s_prev_filtered < 1000 && filtered >= 1000)
    {
        /* Rising edge through threshold — treat as peak */
        if (s_last_peak_ms != 0)
        {
            uint32_t interval_ms = millis() - s_last_peak_ms;
            if (interval_ms != 0)
            {
                uint32_t bpm = 60000UL / interval_ms;
                uart_write_str("BPM: ");
                uart_write_u32(bpm);
                uart_write_str("\r\n");
            }
        }
        s_last_peak_ms = millis();
    }

    s_prev_filtered = filtered;
}
