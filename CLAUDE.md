<!-- GSD:project-start source:PROJECT.md -->
## Project

**HeartRateSensor**

A bare-metal heart rate monitor built on the STM32 Nucleo-F070RB (ARM Cortex-M0) for an Advanced Microprocessors university course. It uses reflective photoplethysmography (PPG) — an IR LED and BPW34 photodiode with an LM358 analog signal chain — to measure pulse and output BPM values over USART2 serial. Deliverable is working hardware + bare-metal C source code + a technical report.

**Core Value:** Finger on sensor → stable, accurate BPM value visible on the serial monitor.

### Constraints

- **Tech stack**: Bare-metal C only — direct CMSIS register writes, no HAL, no Arduino, no RTOS
- **Hardware**: Fixed component list; LM358 op-amp, BPW34 photodiode, 940nm IR LED — no substitutions without retesting
- **IDE**: STM32CubeIDE (project auto-generates CMSIS headers via .ioc configuration)
- **Development order**: Build breadboard circuit before finalizing algorithm thresholds — real signal needed for calibration
- **Deliverable**: Working demo + technical report (not just code)
<!-- GSD:project-end -->

<!-- GSD:stack-start source:research/STACK.md -->
## Technology Stack

## Toolchain
### STM32CubeIDE Project Setup (No HAL)
| Tool | Version / Detail | Role |
|------|-----------------|------|
| STM32CubeIDE | 1.15+ (2024) | IDE, GCC ARM toolchain, ST-Link debugger |
| arm-none-eabi-gcc | bundled with CubeIDE (GCC 12+) | Cross-compiler |
| OpenOCD / ST-Link GDB server | bundled | Flash + debug over USB ST-Link |
| CMSIS 5 headers | shipped inside CubeIDE device pack | Device + core headers |
| STM32CubeMX (.ioc) | Integrated in CubeIDE | Generates clock tree + linker script only |
## CMSIS Header Dependency Tree
#define STM32F070xB
#include "stm32f0xx.h"
## RCC Clock Configuration
### Clock tree for this project
- SYSCLK = 48 MHz (HSE 8 MHz × PLLMUL6, or HSI 8 MHz used as diagnostic fallback)
- HCLK = 48 MHz (AHB prescaler /1)
- PCLK = 48 MHz (single APB on F070, prescaler /1)
- TIM3 clock = 48 MHz
### Peripheral clock enables (must precede any register write to that peripheral)
- GPIOA: RCC->AHBENR |= RCC_AHBENR_GPIOAEN (GPIO is on AHB on F0, not APB2)
- USART2: RCC->APB1ENR |= RCC_APB1ENR_USART2EN
- TIM3: RCC->APB1ENR |= RCC_APB1ENR_TIM3EN
- ADC1: RCC->APB2ENR |= RCC_APB2ENR_ADCEN
## GPIO Configuration (CMSIS, no HAL)
| Pin | Function | Register | Field | Value | Note |
|-----|----------|----------|-------|-------|------|
| PA0 | ADC_IN0 | GPIOA->MODER | MODER0 [1:0] | 11 (analog) | PUPDR default 00 |
| PA2 | USART2_TX | GPIOA->MODER | MODER2 [5:4] | 10 (AF) | AFR[0] bits [11:8] = 0001 (AF1) |
| PA3 | USART2_RX | GPIOA->MODER | MODER3 [7:6] | 10 (AF) | AFR[0] bits [15:12] = 0001 (AF1) |
## ADC1 — Bare-Metal Register Configuration
### Key registers (RM0091)
| Register | Purpose |
|----------|---------|
| ADC1->CFGR1 | External trigger source (EXTSEL), trigger edge (EXTEN), overrun mode |
| ADC1->SMPR | Sample time for all channels (SMP[2:0]) |
| ADC1->CHSELR | Channel selection bitmask |
| ADC1->IER | Interrupt enable: EOCIE (bit 2) |
| ADC1->ISR | Status: EOC (bit 2), ADRDY (bit 0) |
| ADC1->DR | 12-bit result (bits [11:0]) — reading clears EOC |
| ADC1->CR | ADEN, ADSTART, ADCAL, ADSTP |
### CFGR1 — external trigger
| EXTSEL[2:0] | F070 trigger source |
|-------------|---------------------|
| 000 | TIM1_TRGO |
| 001 | TIM1_CC4 |
| 010 | TIM2_TRGO |
| **011** | **TIM3_TRGO ← use this** |
| 100 | TIM15_TRGO |
| 101–111 | reserved |
### ADC calibration sequence (mandatory on F0)
ADC1->CR |= ADC_CR_ADCAL; while (ADC1->CR & ADC_CR_ADCAL); — must run with ADEN=0
### ADC1 interrupt
IRQ name: ADC1_IRQn (F070 has no ADC2; single vector, no shared-vector check needed)
## TIM3 — 100 Hz Hardware Trigger for ADC
### Calculation
- TIM3 clock = PCLK = 48 MHz (single APB, no doubling on F070)
- PSC = 479, ARR = 999 → period = (479+1)×(999+1)/48000000 = 10 ms = 100 Hz
### Register sequence
| MMS[2:0] | TRGO event |
|----------|-----------|
| 000 | Reset |
| 001 | Enable |
| **010** | **Update (UEV) ← use this** |
| 011–111 | OC1/2/3/4REF |
## USART2 — 115200 Baud Serial Output
### Baud rate calculation (RM0091 §25)
- BRR = PCLK / baud = 48,000,000 / 115,200 = 416.67 → **0x1A1 (417)**
### Status/data registers (F0 differs from F1)
| Register | Bit | Name | Meaning |
|----------|-----|------|---------|
| USART2->ISR | 5 | RXNE | RX Not Empty |
| USART2->ISR | 6 | TC | Transmission Complete |
| USART2->ISR | 7 | TXE | TX Data Register Empty — safe to write TDR |
| USART2->TDR | — | — | Transmit data register (write byte here, not DR) |
## SysTick — 1 ms millis()
- SysTick_Config(47999) → (48,000,000 / 1000) - 1 = 47999 at HCLK=48MHz
- Single 32-bit aligned LDR is atomic on Cortex-M0 — no IRQ disable needed to read millis()
## Interrupt Priority Plan
| IRQ | Priority | Rationale |
|-----|----------|-----------|
| SysTick | 0 (highest) | millis() must never be delayed |
| ADC1_IRQn | 1 | sample ISR must complete before next TIM3 trigger at 10ms |
| USART2 (if using IRQ TX) | 2 | output is best-effort |
## Alternatives Considered
| Category | Recommended | Alternative | Why Not |
|----------|-------------|-------------|---------|
| ADC trigger | TIM3 TRGO hardware trigger | Software polling with SysTick | Software trigger introduces ISR latency jitter; at 100Hz the peak-interval timing needs sub-millisecond accuracy |
| ADC result handling | EOC interrupt (ADC1_IRQn) | DMA channel 1 | DMA adds setup complexity; single-channel 100Hz is trivially handled by ISR with no CPU saturation (<1% duty cycle per ISR) |
| USART TX | Polling (TXE flag on ISR) | Interrupt-driven TX ring buffer | BPM output is ~15 chars at ~1 Hz — polling completes in <150µs; ring buffer overhead is not justified |
| millis() source | SysTick | TIM14 or TIM16 | SysTick is a dedicated core timer; saving TIM14/TIM16 for potential future use |
| Clock source | HSI 8MHz (SystemInit stub, 48MHz via PLL in future) | HSE 8MHz × PLL6 = 48MHz | SystemInit is currently a stub; HSI is sufficient for lab demo. Use HSE for final hardware for tighter baud accuracy. |
## F070-Specific Gotchas Summary
| Gotcha | Detail |
|--------|--------|
| ADC calibration mandatory | Run ADCAL sequence (ADEN=0 → ADCAL=1 → wait) before first conversion. Skipping gives random DC offset. |
| HSI14 dedicated ADC clock | F070 ADC has its own 14 MHz RC oscillator (HSI14). Enable via RCC->CR2 |= RCC_CR2_HSI14ON and wait HSI14RDY before ADEN. |
| Single APB — no timer clock doubling | F070 has one APB (not APB1+APB2). TIM3 clock = PCLK = 48 MHz. No ×2 multiplier. PSC/ARR calculated against 48 MHz directly. |
| GPIO on AHB, not APB2 | F0 GPIO clock: RCC->AHBENR |= RCC_AHBENR_GPIOAEN. F1 uses APB2ENR, F4 uses AHB1ENR. Wrong bus = GPIO stays input. |
| MODER/AFR GPIO config (not CRL/CRH) | F0 uses MODER[1:0] (2-bit mode) + AFR[0]/AFR[1] (4-bit AF select). F1 uses 4-bit CNF+MODE in CRL/CRH. Code is not portable. |
| USART registers: ISR/TDR not SR/DR | F0 USART uses ISR (status) and TDR (transmit data). F1 uses SR and DR. Mixing them compiles but writes wrong registers. |
| ADC1_IRQn — no shared vector | F070 has only ADC1; interrupt vector is ADC1_IRQn. F103 uses ADC1_2_IRQn (shared with ADC2). No SR->EOC check needed. |
| USART2 BRR for 48MHz PCLK | BRR = 0x1A1 (417). Many online examples assume 72MHz (F103) or 8MHz HSI — produce wrong baud rates for this project. |
| No FPU on Cortex-M0 | F070 is Cortex-M0, no hardware FPU. All arithmetic must be integer-only. float/double silently use soft-float library (large, slow). |
## Sources
- STM32F070xB Reference Manual RM0091 (ST Microelectronics) — primary register reference. Sections: §6 (RCC), §12 (ADC), §17 (TIM3), §25 (USART), §9 (GPIO)
- STM32F070RB Datasheet — pin multiplexing table, ADC channel mapping
- CMSIS 5 core_cm0.h — SysTick_Config(), NVIC_SetPriority(), NVIC_EnableIRQ() signatures
- Nucleo-F070RB User Manual — PA2/PA3 routed to ST-Link virtual COM, HSE crystal confirmed
- Confidence: HIGH — RM0091 register map is fixed silicon; no version ambiguity for STM32F070.
<!-- GSD:stack-end -->

<!-- GSD:conventions-start source:CONVENTIONS.md -->
## Conventions

Conventions not yet established. Will populate as patterns emerge during development.
<!-- GSD:conventions-end -->

<!-- GSD:architecture-start source:ARCHITECTURE.md -->
## Architecture

Architecture not yet mapped. Follow existing patterns found in the codebase.
<!-- GSD:architecture-end -->

<!-- GSD:skills-start source:skills/ -->
## Project Skills

No project skills found. Add skills to any of: `.claude/skills/`, `.agents/skills/`, `.cursor/skills/`, `.github/skills/`, or `.codex/skills/` with a `SKILL.md` index file.
<!-- GSD:skills-end -->

<!-- GSD:workflow-start source:GSD defaults -->
## GSD Workflow Enforcement

Before using Edit, Write, or other file-changing tools, start work through a GSD command so planning artifacts and execution context stay in sync.

Use these entry points:
- `/gsd-quick` for small fixes, doc updates, and ad-hoc tasks
- `/gsd-debug` for investigation and bug fixing
- `/gsd-execute-phase` for planned phase work

Do not make direct repo edits outside a GSD workflow unless the user explicitly asks to bypass it.
<!-- GSD:workflow-end -->



<!-- GSD:profile-start -->
## Developer Profile

> Profile not yet configured. Run `/gsd-profile-user` to generate your developer profile.
> This section is managed by `generate-claude-profile` -- do not edit manually.
<!-- GSD:profile-end -->
