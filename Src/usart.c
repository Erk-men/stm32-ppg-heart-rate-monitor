#include "stm32f0xx.h"
#include "usart.h"
#include <stddef.h>

/*
 * usart2_init — configure PA2/PA3 GPIO and USART2 for 115200 8N1 polling TX.
 *
 * STM32F070xB clock tree (system_stm32f0xx.c):
 *   SYSCLK = PCLK = 48 MHz (HSE 8MHz × PLL6, APB /1)
 *   BRR = 48 000 000 / 115 200 = 416.67 → 0x1A1 (417)
 *
 * PA2 (USART2_TX): MODER=10 (AF), AF1 in AFRL bits[11:8]
 * PA3 (USART2_RX): MODER=10 (AF), AF1 in AFRL bits[15:12]
 *
 * F0 differences from F1:
 *   GPIOA clock: RCC->AHBENR (not APB2ENR)
 *   GPIO config: MODER/AFR (not CRL/CRH)
 *   USART status: ISR (not SR); data: TDR (not DR)
 */
void usart2_init(void)
{
    /* 1. Enable GPIOA clock — F0 GPIO is on AHB (not APB2 like F1) */
    RCC->AHBENR |= RCC_AHBENR_GPIOAEN;

    /* 2. Enable USART2 clock — still on APB1 */
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;

    /* 3. PA2: alternate function mode (MODER bits [5:4] = 10) */
    GPIOA->MODER &= ~GPIO_MODER_MODER2;
    GPIOA->MODER |=  GPIO_MODER_MODER2_1;   /* 10 = AF */

    /* 4. PA3: alternate function mode (MODER bits [7:6] = 10) */
    GPIOA->MODER &= ~GPIO_MODER_MODER3;
    GPIOA->MODER |=  GPIO_MODER_MODER3_1;   /* 10 = AF */

    /* 5. Set AF1 (USART2) for PA2 and PA3 in AFRL
     *    PA2: AFRL bits [11:8] = 0001
     *    PA3: AFRL bits [15:12] = 0001 */
    GPIOA->AFR[0] &= ~(0xFFUL << 8);
    GPIOA->AFR[0] |=  (0x11UL << 8);   /* AF1 for PA2, AF1 for PA3 */

    /* 6. BRR = 0x1A1 (417) → 115200 baud at PCLK=48MHz */
    USART2->BRR = 0x45;

    /* 7. Enable USART2 with TX (8N1 defaults, no RX enable, no interrupts) */
    USART2->CR1 = USART_CR1_UE | USART_CR1_TE;
}

void uart_write_str(const char *s)
{
    if (s == NULL)
        return;
    while (*s)
    {
        while (!(USART2->ISR & USART_ISR_TXE))
            ;
        USART2->TDR = (uint8_t)*s++;
    }
}

void uart_write_u32(uint32_t n)
{
    if (n == 0)
    {
        while (!(USART2->ISR & USART_ISR_TXE))
            ;
        USART2->TDR = '0';
        return;
    }
    char buf[10];
    int i = 0;
    while (n > 0) { buf[i++] = '0' + (char)(n % 10); n /= 10; }
    while (i > 0)
    {
        while (!(USART2->ISR & USART_ISR_TXE))
            ;
        USART2->TDR = (uint8_t)buf[--i];
    }
}
