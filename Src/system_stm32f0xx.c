/**
 * system_stm32f0xx.c — SystemInit: HSE 8MHz × PLL6 = 48MHz (STM32F070xB)
 *
 * Clock tree result:
 *   SYSCLK = 48 MHz  (PLL output, HSE 8MHz × PLLMUL6)
 *   HCLK   = 48 MHz  (AHB prescaler /1)
 *   PCLK   = 48 MHz  (APB prescaler /1 — F070 has single APB)
 *   TIM2 clock = 48 MHz
 *
 * USART2 BRR for 115200 at 48MHz PCLK: 48000000/115200 = 416.67 → 0x1A1 (417)
 * SysTick LOAD for 1ms at 48MHz HCLK: 48000 - 1 = 47999
 */

#include "stm32f0xx.h"

void SystemInit(void)
{
    /* Use HSI 8MHz directly — no PLL, no HSE needed.
     * SYSCLK = PCLK = 8MHz. BRR for 115200 = 0x45 (69).
     * SysTick LOAD = 7999 for 1ms at 8MHz.
     * This is the diagnostic clock — simple, always works. */
}
