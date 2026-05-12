#include "stm32f1xx.h"
#include "systick.h"

int main(void)
{
    /* SystemInit() is called from startup_stm32f103xb.s before main;
     * explicit call here is a harmless no-op that keeps intent visible. */
    SystemInit();
    systick_init();

    volatile uint32_t last = 0;

    while (1)
    {
        uint32_t now = millis();
        if (now - last >= 1000)
        {
            last = now;
            /* placeholder for Plan 02 uart_write — breakpoint anchor */
            __NOP();
        }
    }
}
