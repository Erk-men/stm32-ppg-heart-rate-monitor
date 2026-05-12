/**
 * stm32f1xx_it.c — Default interrupt handler stubs.
 *
 * SysTick_Handler is intentionally absent here — it is defined in
 * Src/systick.c per driver ownership rule D-02. The startup file
 * declares SysTick_Handler as a weak alias to Default_Handler, so
 * the strong definition in systick.c will override it at link time.
 */

#include "stm32f1xx_it.h"

void NMI_Handler(void) {}
void HardFault_Handler(void) { while (1) {} }
void MemManage_Handler(void) { while (1) {} }
void BusFault_Handler(void)  { while (1) {} }
void UsageFault_Handler(void){ while (1) {} }
void SVC_Handler(void) {}
void DebugMon_Handler(void) {}
void PendSV_Handler(void) {}
