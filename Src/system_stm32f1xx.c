/**
 * system_stm32f1xx.c — SystemInit: HSE 8MHz × PLL9 = 72MHz
 *
 * Clock tree result (matches CLAUDE.md §RCC Clock Configuration):
 *   SYSCLK = 72 MHz  (PLL output)
 *   HCLK   = 72 MHz  (AHB prescaler /1)
 *   PCLK1  = 36 MHz  (APB1 prescaler /2)
 *   PCLK2  = 72 MHz  (APB2 prescaler /1)
 *   TIM2 clock = 72 MHz (APB1 × 2 when prescaler ≠ 1)
 *
 * RM0008 §6.3, §3.1 (Flash ACR)
 */

#include "stm32f1xx.h"

void SystemInit(void)
{
    /* Flash: prefetch enable + 2 wait states (required for SYSCLK > 48 MHz) */
    FLASH->ACR = FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY_2;

    /* Enable HSE and wait for it to stabilise */
    RCC->CR |= RCC_CR_HSEON;
    while (!(RCC->CR & RCC_CR_HSERDY)) {}

    /* Configure bus prescalers before switching clock source:
     *   AHB  = SYSCLK / 1  → HCLK  = 72 MHz
     *   APB1 = HCLK  / 2  → PCLK1 = 36 MHz  (max 36 MHz for APB1)
     *   APB2 = HCLK  / 1  → PCLK2 = 72 MHz
     */
    RCC->CFGR = RCC_CFGR_HPRE_DIV1   /* AHB  /1  */
              | RCC_CFGR_PPRE1_DIV2  /* APB1 /2  */
              | RCC_CFGR_PPRE2_DIV1; /* APB2 /1  */

    /* PLL: source = HSE, multiplier = ×9  →  8 MHz × 9 = 72 MHz */
    RCC->CFGR |= RCC_CFGR_PLLSRC     /* PLL source = HSE */
              |  RCC_CFGR_PLLMULL9;  /* PLL × 9          */

    /* Enable PLL and wait for it to lock */
    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY)) {}

    /* Switch SYSCLK to PLL and wait for hardware confirmation */
    RCC->CFGR |= RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {}

    /* HSI can now be left running (default); it consumes ~80 µA and is
     * unused as a clock source after PLL locks. Disable if power matters. */
}
