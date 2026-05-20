#ifndef ALGORITHM_H
#define ALGORITHM_H

#include <stdint.h>

/*
 * Compile-time feature switches — define before #include or at top of main.c.
 *   SYNTHETIC_TEST  — feed sine_table[] instead of g_adc_sample/g_adc_ready
 *   DEBUG_STATE     — print state transitions via uart_write_str
 *
 * These switches are NOT defined in this header; they live at the top of main.c
 * so the caller controls them without touching the module.
 */

void algorithm_init(void);
void algorithm_process(uint16_t sample);  /* called from main loop per sample */

#endif /* ALGORITHM_H */
