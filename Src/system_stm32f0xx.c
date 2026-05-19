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
    /* Flash: prefetch enable + 1 wait state (required for HCLK > 24 MHz) */
    FLASH->ACR = FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY;  /* LATENCY=1 */

    /* Enable HSE and wait for it to stabilise (provided by ST-Link MCO) */
    RCC->CR |= RCC_CR_HSEON;
    uint32_t timeout = 50000;
    while (!(RCC->CR & RCC_CR_HSERDY) && --timeout) {}

    if (timeout == 0)
    {
        /* HSE failed — stay on HSI 8MHz (board still boots, UART baud wrong) */
        return;
    }

    /* PREDIV = /1: HSE 8MHz direct into PLL */
    RCC->CFGR2 = RCC_CFGR2_PREDIV_DIV1;

    /* PLL: source = HSE/PREDIV = 8MHz, multiplier = ×6 → 48MHz */
    RCC->CFGR |= RCC_CFGR_PLLSRC_HSE_PREDIV | RCC_CFGR_PLLMUL6;

    /* Enable PLL and wait for lock */
    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY)) {}

    /* Switch SYSCLK to PLL */
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {}
}
