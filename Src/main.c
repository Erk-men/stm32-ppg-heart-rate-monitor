/* #define CALIBRATION_MODE */      /* uncomment for 5s startup amplitude
  measurement */
/* #define DEBUG_VERBOSE     */  /* uncomment for per-sample serial capture */
#ifndef DEBUG_STATE
#define DEBUG_STATE
#endif
#include "stm32f0xx.h"
#include "systick.h"
#include "usart.h"
#include "tim3.h"
#include "adc.h"
#include "algorithm.h"

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

    /* TIM3 and ADC unconditional — live ADC path is always active. */
    tim3_init();
    adc_init();

    /* Zero algorithm state before first sample arrives. */
    algorithm_init();

    uart_write_str("\r\n--- HeartRateSensor Phase 4 ---\r\n");

#ifdef CALIBRATION_MODE
{
    uint16_t cal_min   = 4095;
    uint16_t cal_max   = 0;
    uint32_t cal_count = 0;
    uint32_t cal_start = millis();

    uart_write_str("CALIBRATION_MODE: sampling 5s...\r\n");

    while ((millis() - cal_start) < 5000UL)
    {
        if (g_adc_ready)
        {
            g_adc_ready = 0;
            uint16_t s = g_adc_sample;
            if (s < cal_min) cal_min = s;
            if (s > cal_max) cal_max = s;
            cal_count++;
        }
    }

    uint16_t cal_amp = (cal_max > cal_min) ? (uint16_t)(cal_max - cal_min) : 0;
    uart_write_str("CAL min=");   uart_write_u32(cal_min);
    uart_write_str(" max=");      uart_write_u32(cal_max);
    uart_write_str(" amp=");      uart_write_u32(cal_amp);
    uart_write_str(" n=");        uart_write_u32(cal_count);
    uart_write_str("\r\n");
}
#endif /* CALIBRATION_MODE */

    while (1)
    {
        if (g_adc_ready)
        {
            g_adc_ready = 0;
            algorithm_process((uint16_t)g_adc_sample);
        }
    }
}
