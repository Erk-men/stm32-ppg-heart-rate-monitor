#ifndef SYSTICK_H
#define SYSTICK_H

#include <stdint.h>

void systick_init(void);  /* call once in main() before any millis() use */
uint32_t millis(void);    /* monotonically increasing 1ms counter; wraps ~49.7 days */

#endif /* SYSTICK_H */
