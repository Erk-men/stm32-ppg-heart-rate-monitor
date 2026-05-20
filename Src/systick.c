#include "stm32f0xx.h"
#include "systick.h"

/* D-03: counter is file-local, not exposed via extern */
static volatile uint32_t s_ticks = 0;

void systick_init(void)
{
    /* LOAD = 47999 → (48,000,000 Hz / 1000 Hz) - 1 = 1 ms at HCLK=48MHz */
    SysTick_Config(47999);

    /* Raise SysTick to highest priority per CLAUDE.md §Interrupt Priority Plan */
    NVIC_SetPriority(SysTick_IRQn, 0);
}

uint32_t millis(void)
{
    /* Single 32-bit aligned LDR is atomic on Cortex-M0 — no need to disable interrupts */
    return s_ticks;
}

/* D-02: ISR lives in the driver file that owns the counter */
void SysTick_Handler(void)
{
    s_ticks++;
}
