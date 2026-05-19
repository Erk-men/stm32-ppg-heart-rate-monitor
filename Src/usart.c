#include "stm32f1xx.h"
#include "usart.h"
#include <stddef.h>

/*
 * usart2_init — configure PA2/PA3 GPIO and USART2 for 115200 8N1 polling TX.
 *
 * Clock tree (from CLAUDE.md §RCC Clock Configuration):
 *   HCLK = 72 MHz (HSE 8 MHz x PLL9)
 *   APB1 prescaler = /2  -> PCLK1 = 36 MHz  -> USART2 clock = 36 MHz
 *   APB2 prescaler = /1  -> PCLK2 = 72 MHz  -> GPIOA clock = 72 MHz
 *
 * BRR = 0x139 (313 decimal) = 36 000 000 / 115200 (RM0008 §27.3.4, CLAUDE.md §USART2)
 *
 * PA2 (TX): CNF=10 (AF push-pull), MODE=11 (50 MHz output) -> nibble 0xB at CRL[11:8]
 * PA3 (RX): CNF=01 (floating input), MODE=00 (input)       -> nibble 0x4 at CRL[15:12]
 *
 * TC poll: omitted per-byte to avoid ~87µs/byte overhead; single TC poll added after
 * the last byte of uart_write_str/uart_write_u32 is not implemented here — TXE-only
 * polling is sufficient for Phase 1 correctness at 1 Hz output rate.
 */
void usart2_init(void)
{
    /* 1. Enable GPIOA clock (GPIOA is on APB2 — F103 gotcha; NOT AHB like F4) */
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN;

    /* 2. Enable USART2 clock (USART2 is on APB1, PCLK1 = 36 MHz) */
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;

    /* 3. Configure PA2 (TX): AF push-pull 50 MHz — nibble 0xB at CRL bits [11:8]
     *    CNF[1:0] = 10 (AF push-pull), MODE[1:0] = 11 (50 MHz output)
     *    Clear then set — read-modify-write to preserve other pins in CRL */
    GPIOA->CRL &= ~(GPIO_CRL_CNF2 | GPIO_CRL_MODE2);
    GPIOA->CRL |=  (0x0B << 8);

    /* 4. Configure PA3 (RX): floating input — nibble 0x4 at CRL bits [15:12]
     *    CNF[1:0] = 01 (floating input), MODE[1:0] = 00 (input)  */
    GPIOA->CRL &= ~(GPIO_CRL_CNF3 | GPIO_CRL_MODE3);
    GPIOA->CRL |=  (0x04 << 12);

    /* 5. Set baud rate: 115200 at PCLK1=36 MHz -> BRR=0x139 (313 decimal)
     *    Value from CLAUDE.md §USART2; do NOT compute at runtime. */
    USART2->BRR = 0x139;

    /* 6. Enable USART2 and TX (8N1 defaults apply; no RX enable, no interrupts) */
    USART2->CR1 = USART_CR1_UE | USART_CR1_TE;
}

/*
 * uart_write_str — transmit a null-terminated string over USART2.
 * Polls TXE (SR bit 7) before each byte write.
 * NULL-safe: returns immediately if s == NULL (bare-metal nullptr deref = HardFault).
 */
void uart_write_str(const char *s)
{
    if (s == NULL)
        return;

    while (*s)
    {
        while (!(USART2->SR & USART_SR_TXE))
            ;
        USART2->DR = (uint8_t)*s++;
    }
}

/*
 * uart_write_u32 — emit decimal ASCII representation of n, no leading zeros.
 * Special-case: n==0 emits exactly "0".
 * Uses a fixed 10-char stack buffer (max 10 digits for UINT32_MAX = 4294967295).
 * No stdlib formatting functions used.
 */
void uart_write_u32(uint32_t n)
{
    if (n == 0)
    {
        while (!(USART2->SR & USART_SR_TXE))
            ;
        USART2->DR = '0';
        return;
    }

    char buf[10];
    int i = 0;

    while (n > 0)
    {
        buf[i++] = '0' + (char)(n % 10);
        n /= 10;
    }

    /* buf holds digits in reverse order; emit most-significant first */
    while (i > 0)
    {
        while (!(USART2->SR & USART_SR_TXE))
            ;
        USART2->DR = (uint8_t)buf[--i];
    }
}
