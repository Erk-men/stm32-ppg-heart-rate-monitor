# Phase 1: Firmware Scaffold + Peripheral Init - Context

**Gathered:** 2026-05-12
**Status:** Ready for planning

<domain>
## Phase Boundary

STM32CubeIDE project compiles and flashes cleanly with bare-metal CMSIS headers (zero HAL). SysTick millis() increments at 1ms resolution. USART2 transmits "millis: NNNN\r\n" over the ST-Link USB virtual COM port at 115200 baud. Proves the full toolchain (compiler → ST-Link flasher → serial terminal) end-to-end before any peripheral complexity is added.

**Requirements in scope:** DRV-01 (SysTick millis()), DRV-02 (USART2 serial output)
**Out of scope:** ADC, TIM2, signal processing, algorithm code — all in later phases

</domain>

<decisions>
## Implementation Decisions

### File / Module Organization
- **D-01:** Separate driver files from day 1 — `usart.c` / `usart.h` and `systick.c` / `systick.h` in Src/ and Inc/. `main.c` calls driver init functions and the main loop. This module boundary is the pattern Phases 2–4 extend (adc.c/h, tim.c/h, algorithm.c/h) — no refactor needed between phases.
- **D-02:** ISRs live in the driver .c file that owns the peripheral. `SysTick_Handler` in `systick.c`. `ADC1_2_IRQHandler` (Phase 2) in `adc.c`. Each driver owns its full interrupt path — no separate `interrupt_handlers.c`.

### millis() API
- **D-03:** `millis()` is declared as `uint32_t millis(void)` in `systick.h` and defined in `systick.c`. Returns a copy of the volatile internal counter. Any .c file that includes `systick.h` calls `millis()`. The raw counter variable is not exposed via extern — no accidental writes from other files.

### UART Output API
- **D-04:** `uart_write_str(const char *s)` + `uart_write_u32(uint32_t n)` in `usart.c/h`. No stdlib / newlib dependency. No printf retarget. Phases 2–4 build serial output from these two primitives (e.g., `uart_write_str("BPM: "); uart_write_u32(bpm); uart_write_str("\r\n");`). Keeps flash footprint minimal and aligns with bare-metal course spirit.

### Clock Initialization
- **D-05 (Claude's discretion):** Use the CubeIDE-generated `system_stm32f1xx.c` and `SystemInit()` for HSE + PLL clock setup (72MHz). This file uses CMSIS register access internally and is reliable. Phase 1 focus is proving SysTick + USART2, not RCC init from scratch. If the course grader requires visible RCC register writes in student code, promote the RCC sequence from `system_stm32f1xx.c` into `main()` before Phase 5 report freeze.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Project Requirements
- `.planning/REQUIREMENTS.md` — DRV-01 (SysTick, LOAD=71999, HCLK source, 1ms resolution) and DRV-02 (USART2 115200 baud, bare-metal register access) are the acceptance criteria for this phase
- `.planning/ROADMAP.md` §Phase 1 — Four success criteria that define "done"

### Register Configuration (in CLAUDE.md)
- `CLAUDE.md` §GPIO Configuration — PA2 (USART2_TX: CNF=10 AF push-pull, MODE=11 50MHz), PA3 (USART2_RX: CNF=01 float input, MODE=00 input); GPIOA clock on APB2 (RCC_APB2ENR)
- `CLAUDE.md` §USART2 — 115200 Baud Serial Output — BRR=0x139 (313 decimal) for 36MHz PCLK1; polling on TXE flag (bit 7 of SR)
- `CLAUDE.md` §SysTick — 1ms millis() — SysTick_Config(71999) or manual LOAD=71999, HCLK source
- `CLAUDE.md` §RCC Clock Configuration — HSE 8MHz × PLL9 = 72MHz; APB1 prescaler /2 → PCLK1=36MHz; APB2 = 72MHz
- `CLAUDE.md` §F103-Specific Gotchas — GPIOA clock on APB2 not AHB; CRL/CRH GPIO config (not MODER)

### Source References
- RM0008 §27 (USART), §9 (GPIO), §6 (RCC), §3 (Flash ACR) — primary register reference (physical copy or PDF)
- CMSIS `core_cm3.h` — `SysTick_Config()` signature

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- None — empty project. All files created from scratch in this phase.

### Established Patterns
- None yet — this phase establishes the pattern.

### Integration Points
- `main.c` calls `systick_init()` and `usart2_init()` on startup, then loops calling `millis()` and `uart_write_str()` / `uart_write_u32()`.
- CubeIDE-generated `startup_stm32f103xb.s` and linker script `STM32F103RBTx_FLASH.ld` are kept as-is (generated from .ioc). `system_stm32f1xx.c` / `SystemInit()` handles clock setup.

</code_context>

<specifics>
## Specific Ideas

- Output format for the main loop: `"millis: NNNN\r\n"` — matches Phase 1 success criterion SC-3
- Print interval: every 1000ms (compare `millis()` delta, not delay loop)
- `uart_write_u32` should output decimal ASCII digits without leading zeros for readability

</specifics>

<deferred>
## Deferred Ideas

- Printf retarget via `syscalls.c` — not needed; `uart_write_str` + `uart_write_u32` cover all use cases through Phase 5
- Explicit bare-metal RCC init in `main()` — deferred; `system_stm32f1xx.c` covers Phase 1–4. Revisit in Phase 5 if report evidence requires visible student-written RCC code.
- `interrupt_handlers.c` as a dedicated file — rejected in favor of ISR-in-owner-driver pattern

</deferred>

---

*Phase: 1 — Firmware Scaffold + Peripheral Init*
*Context gathered: 2026-05-12*
