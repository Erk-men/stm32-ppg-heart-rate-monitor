#include "stm32f0xx.h"
#include "adc.h"

/*
 * ADC1 hardware-triggered driver for STM32F070xB.
 *
 * Trigger chain: TIM3 TRGO (100 Hz) -> ADC1 EXTSEL=011, EXTEN=01 (rising edge)
 * Clock:         HSI14 async RC oscillator (14 MHz), CKMODE=00 (reset default)
 * Channel:       ADC_IN0 = PA0 (analog mode)
 * ISR:           ADC_IRQHandler fires on EOC, stores result in g_adc_sample
 *
 * Initialization order (critical -- see pitfalls in 02-RESEARCH.md):
 *   PA0 analog -> ADC clock -> HSI14 start -> ADCAL (ADEN must be 0) ->
 *   CFGR1/SMPR/CHSELR/IER (all before ADEN) -> NVIC -> ADEN -> wait ADRDY ->
 *   ADSTART (arms triggered mode)
 */

volatile uint16_t g_adc_sample = 0;
volatile uint8_t  g_adc_ready  = 0;

void adc_init(void)
{
    /* Step 1 -- PA0 analog mode (GPIOA clock enabled by usart2_init, no duplicate) */
    GPIOA->MODER |= GPIO_MODER_MODER0;   /* MODER[1:0] = 11 (analog); PUPDR default 00 */

    /* Step 2 -- Enable ADC1 clock on APB2 */
    RCC->APB2ENR |= RCC_APB2ENR_ADCEN;

    /* Step 3 -- Start HSI14 dedicated ADC oscillator and wait for stabilisation */
    RCC->CR2 |= RCC_CR2_HSI14ON;
    while (!(RCC->CR2 & RCC_CR2_HSI14RDY));

    /* Step 4 -- Calibrate ADC (ADEN must be 0 -- ADCAL with ADEN=1 is undefined) */
    ADC1->CR |= ADC_CR_ADCAL;
    while (ADC1->CR & ADC_CR_ADCAL);   /* self-clears when calibration completes (~6 us) */

    /* Step 5 -- Configure CFGR1 before enabling ADC (write-locked after ADEN=1) */
    ADC1->CFGR1 = ADC_CFGR1_EXTSEL_0 | ADC_CFGR1_EXTSEL_1  /* EXTSEL=011 -> TIM3_TRGO */
                | ADC_CFGR1_EXTEN_0                           /* EXTEN=01   -> rising edge */
                | ADC_CFGR1_OVRMOD;                           /* overwrite DR on overrun  */

    /* Step 6 -- Maximum sample time (SMP=111 = 239.5 cycles) for best SNR */
    ADC1->SMPR = ADC_SMPR_SMP;   /* = 0x7 (all three SMP bits) */

    /* Step 7 -- Select channel 0 (PA0 = ADC_IN0) */
    ADC1->CHSELR = ADC_CHSELR_CHSEL0;   /* = 0x00000001 */

    /* Step 8 -- Enable EOC interrupt */
    ADC1->IER = ADC_IER_EOCIE;

    /* Step 9 -- Set NVIC priority for ADC1_IRQn (priority 1, below SysTick at 0) */
    NVIC_SetPriority(ADC1_IRQn, 1);
    NVIC_EnableIRQ(ADC1_IRQn);

    /* Step 10 -- Enable ADC and wait for voltage reference to settle */
    ADC1->CR |= ADC_CR_ADEN;
    while (!(ADC1->ISR & ADC_ISR_ADRDY));

    /* Step 11 -- Arm triggered conversion mode (waits for next TIM3 TRGO rising edge) */
    ADC1->CR |= ADC_CR_ADSTART;
}

/*
 * ADC_IRQHandler -- fires on EOC (end of conversion) at 100 Hz.
 *
 * Reading ADC1->DR clears the EOC flag automatically on F0 (prevents runaway ISR).
 * The 0x0FFF mask captures only the 12-bit result (mitigates T-02-01: reserved bit bleed).
 */
void ADC1_IRQHandler(void)
{
    if (ADC1->ISR & ADC_ISR_EOC) {
        g_adc_sample = (uint16_t)(ADC1->DR & 0x0FFF);  /* reading DR clears EOC */
        g_adc_ready  = 1;
    }
}
