#ifndef ADC_H
#define ADC_H

#include <stdint.h>

extern volatile uint16_t g_adc_sample;  /* latest 12-bit ADC reading */
extern volatile uint8_t  g_adc_ready;   /* set to 1 by ISR; cleared by consumer */

void adc_init(void);

#endif /* ADC_H */
