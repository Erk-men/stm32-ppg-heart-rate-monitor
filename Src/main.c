#include "stm32f1xx.h"
#include "systick.h"
#include "usart.h"

int main(void)
{
    /* SystemInit() is called from startup_stm32f103xb.s before main;
     * explicit call here is a harmless no-op that keeps intent visible. */
    SystemInit();
    systick_init();
    usart2_init();

    uart_write_str("\r\n--- HeartRateSensor Phase 1 ---\r\n");

    volatile uint32_t last = 0;

    while (1)
    {
        uint32_t now = millis();
        if (now - last >= 1000)
        {
            last = now;
            uart_write_str("millis: ");
            uart_write_u32(now);
            uart_write_str("\r\n");
        }
    }
}
