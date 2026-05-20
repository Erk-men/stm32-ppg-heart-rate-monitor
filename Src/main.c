#define SYNTHETIC_TEST
#define DEBUG_STATE
#include "stm32f0xx.h"
#include "systick.h"
#include "usart.h"
#include "tim3.h"
#include "adc.h"
#include "algorithm.h"

/* PPG-shaped synthetic sine table (D-06, D-07, D-08):
 *   100 entries at 10ms/sample = 1.0s per loop = 60 BPM when looped continuously.
 *   ADC count range: 0–2200 (matches real LM358-on-3.3V hardware, D-07).
 *   Shape: baseline ~200 → main systolic peak ~2000 at idx ~16 → descent to ~200
 *          by idx ~44 → dicrotic notch ~800 at idx ~48 → baseline ~200 by idx ~64
 *          → flat baseline ~200 for indices 64–99 (D-06). */
#ifdef SYNTHETIC_TEST
static const uint16_t sine_table[100] = {
    /* 100 entries total; 10 per row; PPG shape (D-06 D-07 D-08)           */
    /* idx  0.. 9 — baseline rising toward systolic upstroke               */
     200,  210,  230,  260,  310,  380,  480,  620,  800, 1020,
    /* idx 10..19 — rapid systolic upstroke, peak at idx 16 (~2040 cts)   */
    1270, 1510, 1720, 1880, 1970, 2020, 2040, 2030, 1990, 1930,
    /* idx 20..29 — post-peak descent                                      */
    1850, 1760, 1660, 1560, 1450, 1350, 1250, 1160, 1080, 1010,
    /* idx 30..39 — continued descent toward notch                         */
     950,  900,  860,  820,  790,  760,  740,  720,  700,  680,
    /* idx 40..49 — approaching and rising through dicrotic notch          */
     660,  640,  620,  600,  580,  570,  580,  620,  700,  800,
    /* idx 50..59 — dicrotic notch bump descending                         */
     820,  780,  720,  650,  580,  520,  470,  430,  400,  370,
    /* idx 60..69 — return to baseline                                      */
     340,  320,  300,  280,  260,  240,  230,  220,  210,  205,
    /* idx 70..79 — flat baseline                                           */
     200,  200,  200,  200,  200,  200,  200,  200,  200,  200,
    /* idx 80..89 — flat baseline                                           */
     200,  200,  200,  200,  200,  200,  200,  200,  200,  200,
    /* idx 90..99 — flat baseline                                           */
     200,  200,  200,  200,  200,  200,  200,  200,  200,  200
};
#endif /* SYNTHETIC_TEST */

int main(void)
{
    /* SystemInit() is called from startup_stm32f070xb.s before main;
     * explicit call here is a harmless no-op that keeps intent visible. */
    SystemInit();
    systick_init();

    /* usart2_init() enables GPIOA clock (RCC_AHBENR_GPIOAEN).
     * adc_init() depends on GPIOA being clocked for PA0 MODER config.
     * Call order: systick_init -> usart2_init -> tim3_init -> adc_init is mandatory. */
    usart2_init();

    /* Start TIM3: TRGO begins firing at 100 Hz immediately after this call. */
    tim3_init();

    /* Calibrate and arm ADC1: first conversion fires on next TIM3 TRGO pulse. */
    adc_init();

    /* Zero algorithm state before first sample arrives. */
    algorithm_init();

    uart_write_str("\r\n--- HeartRateSensor Phase 3 ---\r\n");

    while (1)
    {
#ifdef SYNTHETIC_TEST
        static uint32_t s_last_sample_ms = 0;
        static uint8_t  s_table_idx      = 0;
        uint32_t now = millis();
        if ((now - s_last_sample_ms) >= 10)   /* 10ms = 100 Hz */
        {
            s_last_sample_ms = now;
            algorithm_process(sine_table[s_table_idx]);
            s_table_idx = (s_table_idx + 1 >= 100) ? 0 : s_table_idx + 1;
        }
#else
        if (g_adc_ready)
        {
            g_adc_ready = 0;                     /* consume the flag */
            algorithm_process((uint16_t)g_adc_sample);
        }
#endif /* SYNTHETIC_TEST */
    }
}
