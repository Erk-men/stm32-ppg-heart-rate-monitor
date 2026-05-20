---
phase: 01-firmware-scaffold-peripheral-init
plan: 02
status: complete
completed: "2026-05-19"
---

# Plan 01-02 Summary — USART2 Driver + millis Print Loop

## What Was Built

- `Inc/usart.h` — public API: `usart2_init()`, `uart_write_str()`, `uart_write_u32()`
- `Src/usart.c` — USART2 polling TX driver (F070 register style)
- `Src/main.c` — prints `millis: NNNN\r\n` every 1000ms

## Final Register Configuration (STM32F070xB)

| Register | Value | Purpose |
|----------|-------|---------|
| RCC->AHBENR | `RCC_AHBENR_GPIOAEN` | GPIOA clock (F0: AHB, not APB2) |
| RCC->APB1ENR | `RCC_APB1ENR_USART2EN` | USART2 clock |
| GPIOA->MODER PA2 | `10` (AF) | USART2_TX |
| GPIOA->MODER PA3 | `10` (AF) | USART2_RX |
| GPIOA->AFR[0] | `0x11 << 8` | AF1 for PA2 and PA3 |
| USART2->BRR | `0x45` (69) | 115200 baud at HSI 8MHz PCLK |
| USART2->CR1 | `UE \| TE` | Enable USART2 TX, 8N1, no interrupts |
| SysTick LOAD | `7999` | 1ms tick at 8MHz HSI |

## Confirmed Serial Output

```
--- HeartRateSensor Phase 1 ---
millis: 1000
millis: 2000
millis: 3000
```

Lines appear at 1-second intervals at 115200 8N1. Board runs autonomously after reset.

## Key Decisions

- **HSI 8MHz diagnostic clock**: `SystemInit()` left empty; F070 defaults to HSI 8MHz on reset. Simple and reliable for Phase 1.
- **BRR=0x45**: 8,000,000 / 69 = 115,942 baud — 0.6% off nominal, well within UART tolerance.
- **TC poll omitted**: Not polling TC after last byte. At 1Hz output rate the next print is 1 second away — no risk of truncation.
- **No stdlib formatting**: All output composed from `uart_write_str` + `uart_write_u32` per D-04.

## Issues Encountered and Fixes

1. **CubeIDE project targeted F103** — source code had been migrated to F070 but `.cproject` still targeted F103RB. Fixed by creating a new CubeIDE project targeting STM32F070RBTx.
2. **SysTick LOAD=71999 (for 72MHz F103)** — at 8MHz HSI this gave 9ms ticks; output appeared every 9 seconds. Fixed to `SysTick_Config(7999)`.
3. **Missing `STM32F070xB` define** — new project only had `STM32F070RBTx`; `stm32f0xx.h` requires `STM32F070xB` to select the device header. Added manually in compiler preprocessor settings.

## Pattern for Phases 2–5

All serial output composed from `uart_write_str(const char *)` + `uart_write_u32(uint32_t)`. No printf, no stdlib formatting.

## Phase 1 Success Criteria — All Met

- [x] SC-1: Zero HAL dependencies
- [x] SC-2: millis() at 1ms resolution
- [x] SC-3: Serial terminal shows `millis: NNNN\r\n` every second at 115200
- [x] SC-4: Board runs autonomously after reset
