#ifndef USART_H
#define USART_H

#include <stdint.h>

void usart2_init(void);                /* configures GPIO PA2/PA3 + USART2 @ 115200 8N1, polling TX */
void uart_write_str(const char *s);    /* blocks; null-terminated; no formatting */
void uart_write_u32(uint32_t n);       /* decimal ASCII, no leading zeros, "0" for zero */

#endif /* USART_H */
