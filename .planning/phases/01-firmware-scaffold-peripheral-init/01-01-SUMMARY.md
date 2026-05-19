---
phase: 01-firmware-scaffold-peripheral-init
plan: 01
subsystem: firmware
tags: [stm32f103, bare-metal, cmsis, systick, cortex-m3]

requires: []
provides:
  - SysTick millis() driver (DRV-01) — 1ms counter via HCLK at 72MHz
  - Bare-metal project scaffold with CMSIS headers (no HAL)
  - main.c stub with 1Hz non-blocking loop using millis()
affects: [02-adc-tim2-sampling, 03-peak-detection, 04-bpm-output, 05-polish]

tech-stack:
  added: [CMSIS 5 (core_cm3.h, stm32f1xx.h), arm-none-eabi-gcc]
  patterns: [driver-pair (Inc/X.h + Src/X.c), ISR-in-owner (SysTick_Handler in systick.c)]

key-files:
  created:
    - Inc/systick.h
    - Src/systick.c
    - Src/main.c
    - Src/system_stm32f1xx.c
    - Src/stm32f1xx_it.c
    - Startup/startup_stm32f103xb.s
    - STM32F103RBTx_FLASH.ld
  modified: []

key-decisions:
  - "SysTick_Config(71999) — LOAD=(72MHz/1kHz)-1 for 1ms tick at HCLK=72MHz"
  - "SysTick_Handler defined in Src/systick.c per D-02; removed from stm32f1xx_it.c to avoid duplicate definition"
  - "s_ticks declared static volatile uint32_t — file-local per D-03, not exposed via extern"
  - "NVIC_SetPriority(SysTick_IRQn, 0) called after SysTick_Config to raise to highest priority"
  - "SystemInit() called explicitly in main() as self-documenting intent; startup already calls it, second call is harmless no-op"
  - "Hardware verify (Task 4) deferred — will be confirmed together with Plan 01-02 serial terminal gate"

patterns-established:
  - "Driver pair: Inc/X.h declares public API, Src/X.c owns counter and ISR"
  - "ISR-in-owner: SysTick_Handler lives in systick.c, never in stm32f1xx_it.c"
  - "No HAL: zero HAL_ calls anywhere in Src/ or Inc/"
  - "Unsigned millis() subtraction (now - last) for rollover-safe timing in all future phases"

requirements-completed:
  - DRV-01

duration: ~45min
completed: 2026-05-19
---

# Plan 01-01: Project Scaffold + SysTick millis() Driver Summary

**Bare-metal STM32F103RB scaffold with CMSIS headers, SysTick_Config(71999) for 1ms tick at 72MHz, and driver-pair pattern established (no HAL)**

## Performance

- **Duration:** ~45 min
- **Started:** 2026-05-12T14:00:00Z
- **Completed:** 2026-05-12 (tasks 1-3); hardware verify deferred to Plan 01-02 gate
- **Tasks:** 3/4 (Task 4 hardware verify pending — will be confirmed at Plan 01-02 serial terminal check)
- **Files modified:** 7

## Accomplishments

- CMSIS headers manually added (`Drivers/CMSIS/`) — no CubeIDE GUI, bare-metal project structure established
- `Inc/systick.h` + `Src/systick.c` implement DRV-01: `systick_init()` and `millis()` with ISR in driver file
- `Src/main.c` wired to call `systick_init()`, runs 1Hz non-blocking loop via unsigned subtraction `(now - last) >= 1000`, `__NOP()` placeholder for Plan 02 uart call

## Task Commits

1. **Task 1: CubeIDE project scaffold** — `eb6f46c` (feat(phase-01): project scaffold, CMSIS headers, SysTick driver, main stub)
2. **Task 2: Implement SysTick millis() driver** — `7ede88d` (feat(01-01): implement SysTick millis() driver (DRV-01))
3. **Task 3: Wire main.c** — `5f91803` (feat(01-01): wire main.c to systick_init() and millis() loop)
4. **Task 4: Hardware verify** — DEFERRED (no commit yet; will be verified with Plan 01-02 serial terminal gate)

## Files Created/Modified

- `Inc/systick.h` — declares `systick_init()` and `millis()` (D-03 compliant: no extern counter)
- `Src/systick.c` — `static volatile uint32_t s_ticks`, `SysTick_Config(71999)`, `SysTick_Handler` increments counter
- `Src/main.c` — calls `systick_init()`, 1Hz non-blocking loop with `__NOP()` placeholder
- `Src/stm32f1xx_it.c` — `SysTick_Handler` removed (stub only, prevents duplicate definition link error)
- `Src/system_stm32f1xx.c`, `Startup/startup_stm32f103xb.s`, `STM32F103RBTx_FLASH.ld` — generated scaffold files

## Decisions Made

- `LOAD = 71999`: `(72_000_000 / 1000) - 1` — SysTick counts from LOAD to 0 inclusive, 72000 cycles = 1ms exactly at HCLK=72MHz
- Followed D-02/D-03: ISR in driver file, counter is `static`, no extern exposure
- Driver-pair pattern established as the module template for all subsequent drivers (usart, adc, tim2)
- Hardware verify (Task 4 breakpoint test) deferred — will be confirmed at end of Plan 01-02 when serial terminal output is visible, which provides equivalent proof that millis() is running

## Deviations from Plan

None for tasks 1-3. Task 4 (hardware verify) deferred by user decision at Phase 01-02 start — both hardware gates will be satisfied together when the serial terminal shows `millis: NNNN` lines.

## Issues Encountered

None.

## Next Phase Readiness

- `Inc/systick.h` exports `millis()` — ready for Plan 01-02 (uart_write main loop) and Phase 02 (ADC ISR timing)
- Driver-pair pattern established — Plan 01-02 follows same Inc/usart.h + Src/usart.c structure
- Hardware verification pending: will be confirmed when serial terminal shows `millis: NNNN\r\n` every ~1000ms at end of Plan 01-02

---
*Phase: 01-firmware-scaffold-peripheral-init*
*Completed: 2026-05-19*
