# Phase 1: Firmware Scaffold + Peripheral Init - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-05-12
**Phase:** 1 — Firmware Scaffold + Peripheral Init
**Areas discussed:** File organization

---

## File Organization

### Q1: Where should driver code live?

| Option | Description | Selected |
|--------|-------------|----------|
| Separate driver files | usart.c/h + systick.c/h in Src/Inc; clean module pattern for all phases | ✓ |
| Single main.c | Everything in main.c; simplest scaffold; refactor before Phase 2 | |

**User's choice:** Separate driver files (Recommended)

---

### Q2: Where should interrupt handlers go?

| Option | Description | Selected |
|--------|-------------|----------|
| In the driver .c file that owns the peripheral | SysTick_Handler in systick.c; each driver owns its ISR | ✓ |
| Dedicated interrupt_handlers.c | All ISRs in one place; HAL-generated code pattern | |
| In main.c | All handlers alongside main loop; gets messy by Phase 3 | |

**User's choice:** In the driver .c file that owns the peripheral (Recommended)

---

### Q3: How should millis() be exposed?

| Option | Description | Selected |
|--------|-------------|----------|
| Declared in systick.h, defined in systick.c | `uint32_t millis(void)` as a public function; returns a copy | ✓ |
| Extern volatile uint32_t in a shared header | Exposes raw counter; faster but leaks internal state | |

**User's choice:** Declared in systick.h, defined in systick.c (Recommended)

---

### Q4: How should USART2 output be exposed?

| Option | Description | Selected |
|--------|-------------|----------|
| uart_write_str() + uart_write_u32() helpers | No stdlib; bare-metal spirit; two primitives cover all output needs | ✓ |
| Printf retarget via syscalls.c | More ergonomic; pulls in newlib; harder to explain in register-level report | |

**User's choice:** uart_write_str(const char*) + uart_write_u32(uint32_t) helpers (Recommended)

---

## Claude's Discretion

- **Clock initialization:** Use CubeIDE-generated `system_stm32f1xx.c` / `SystemInit()`. User did not select "Clock init ownership" for discussion — applied sensible default. May be revisited in Phase 5 if course report requires visible student-written RCC code.

## Deferred Ideas

- Printf retarget — not needed; uart_write_str + uart_write_u32 sufficient through Phase 5
- Explicit bare-metal RCC init in main() — `system_stm32f1xx.c` covers Phases 1–4; revisit for Phase 5 report if needed
- `interrupt_handlers.c` as separate file — rejected; ISR-in-owner-driver pattern chosen instead
