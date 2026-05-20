/**
 * system_stm32f0xx.c — SystemInit: HSI 8 MHz (STM32F070xB)
 *
 * Clock tree result:
 *   SYSCLK = 8 MHz  (HSI internal RC, default after reset)
 *   HCLK   = 8 MHz  (AHB prescaler /1)
 *   PCLK   = 8 MHz  (APB prescaler /1 — F070 has single APB)
 *
 * USART2 BRR for 115200 at 8 MHz PCLK : 8000000/115200 = 69.4 → 0x45 (69)
 * SysTick LOAD for 1 ms at 8 MHz HCLK : 8000 - 1 = 7999
 *
 * The startup file (startup_stm32f070xb.s) calls SystemInit() before main.
 * main() also calls SystemInit() — this is a harmless double-call since
 * the function does nothing and HSI is already the active clock.
 */

#include "stm32f0xx.h"

void SystemInit(void)
{
    /* HSI 8 MHz is the reset default — no configuration needed. */
}
