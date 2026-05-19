#include "stm32f0xx.h"
#include "tim3.h"

/*
 * tim3_init -- configure TIM3 to generate a 100 Hz TRGO pulse.
 *
 * Clock math (HSI 8 MHz, no APB doubling on F0):
 *   PSC = 79  -> counter clock = 8 000 000 / (79+1) = 100 000 Hz
 *   ARR = 999 -> update rate   = 100 000  / (999+1) = 100 Hz exactly
 *
 * TIM3->CR2 = TIM_CR2_MMS_1 selects Master Mode "Update" (MMS[2:0]=010, bit 5).
 * This routes the Update Event to the TRGO output that hardware-triggers ADC1.
 * No TIM3 interrupt is enabled -- TIM3 runs autonomously.
 *
 * EGR.UG is intentionally NOT written.  Letting the first Update Event happen
 * naturally avoids a spurious TRGO pulse at startup.
 */
void tim3_init(void)
{
    /* Step 1: Enable TIM3 clock on APB1 */
    RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;

    /* Step 2: Prescaler -- 8 MHz / (79+1) = 100 kHz counter clock */
    TIM3->PSC = 79;

    /* Step 3: Auto-reload -- 100 kHz / (999+1) = 100 Hz update event */
    TIM3->ARR = 999;

    /* Step 4: Master mode = Update Event -> TRGO (MMS[2:0] = 010 = TIM_CR2_MMS_1) */
    TIM3->CR2 = TIM_CR2_MMS_1;

    /* Step 5: Enable counter */
    TIM3->CR1 |= TIM_CR1_CEN;
}
