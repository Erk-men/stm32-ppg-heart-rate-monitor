#include "stm32f0xx.h"
#include "systick.h"
#include "usart.h"
#include "tim3.h"
#include "adc.h"

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

    uart_write_str("\r\n--- HeartRateSensor Phase 2 ---\r\n");

    while (1)
    {
        if (g_adc_ready)
        {
            g_adc_ready = 0;                     /* consume the flag */
            uart_write_str("adc: ");
            uart_write_u32((uint32_t)g_adc_sample);
            uart_write_str("\r\n");
        }
    }
}
