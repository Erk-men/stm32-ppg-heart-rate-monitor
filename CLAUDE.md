<!-- GSD:project-start source:PROJECT.md -->
## Project

**HeartRateSensor**

A bare-metal heart rate monitor built on the STM32 Nucleo-F103RB (ARM Cortex-M3) for an Advanced Microprocessors university course. It uses reflective photoplethysmography (PPG) — an IR LED and BPW34 photodiode with an LM358 analog signal chain — to measure pulse and output BPM values over USART2 serial. Deliverable is working hardware + bare-metal C source code + a technical report.

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
#define STM32F103xB
#include "stm32f1xx.h"
## RCC Clock Configuration
### Clock tree for this project
### Register sequence (RM0008 §6.3)
### Peripheral clock enables (must precede any register write to that peripheral)
## GPIO Configuration (CMSIS, no HAL)
| Pin | Function | CRL/CRH bits | CNF | MODE | Note |
|-----|----------|--------------|-----|------|------|
| PA0 | ADC1_IN0 | CRL bits [3:0] | 00 (analog) | 00 (input) | analog input mode |
| PA2 | USART2_TX | CRL bits [11:8] | 10 (AF push-pull) | 11 (50 MHz) | |
| PA3 | USART2_RX | CRL bits [15:12] | 01 (float input) | 00 (input) | |
## ADC1 — Bare-Metal Register Configuration
### Key registers (RM0008 §11)
| Register | Purpose |
|----------|---------|
| ADC1->CR1 | Mode, interrupt enable, resolution control |
| ADC1->CR2 | ADON, external trigger source, trigger enable, start |
| ADC1->SQR3 | Channel sequence for regular group (first conversion) |
| ADC1->SMPR2 | Sample time for channels 0–9 |
| ADC1->SR | Status: EOC (bit 1), STRT (bit 4) |
| ADC1->DR | 12-bit result (bits [11:0]) |
### CR1 configuration
### CR2 configuration
| EXTSEL[2:0] | F103 trigger source (regular group) |
|-------------|--------------------------------------|
| 000 | TIM1_CC1 |
| 001 | TIM1_CC2 |
| 010 | TIM1_CC3 |
| 011 | **TIM2_TRGO** ← use this |
| 100 | TIM2_CC2 |
| 101 | TIM3_TRGO |
| 110 | TIM3_CC1 |
| 111 | EXTI11 / TIM8_TRGO (software trigger when SWSTART used) |
### SQR3 — channel selection
### SMPR2 — sample time
### ADC1 interrupt
## TIM2 — 100 Hz Hardware Trigger for ADC
### Calculation (RM0008 §14)
### Register sequence
| MMS[2:0] | TRGO event |
|----------|-----------|
| 000 | Reset |
| 001 | Enable |
| **010** | **Update (UEV) ← use this** |
| 011 | Compare Pulse (OC1) |
| 100–111 | OC1/2/3/4REF |
## USART2 — 115200 Baud Serial Output
### Baud rate calculation (RM0008 §27.3.4)
| Bit | Name | Meaning |
|-----|------|---------|
| 5 | RXNE | RX Not Empty — data ready to read |
| 6 | TC | Transmission Complete |
| 7 | TXE | TX Data Register Empty — safe to write DR |
## SysTick — 1 ms millis()
## Interrupt Priority Plan
| IRQ | Priority | Rationale |
|-----|----------|-----------|
| SysTick | 0 (highest) | millis() must never be delayed |
| ADC1_2_IRQn | 1 | sample ISR must complete before next TIM2 trigger at 10ms |
| USART2 (if using IRQ TX) | 2 | output is best-effort |
## Alternatives Considered
| Category | Recommended | Alternative | Why Not |
|----------|-------------|-------------|---------|
| ADC trigger | TIM2 TRGO hardware trigger | Software polling with SysTick | Software trigger introduces ISR latency jitter; at 100Hz the peak-interval timing needs sub-millisecond accuracy |
| ADC result handling | EOC interrupt (ADC1_2_IRQn) | DMA channel 1 | DMA adds setup complexity; single-channel 100Hz is trivially handled by ISR with no CPU saturation (<1% duty cycle per ISR) |
| USART TX | Polling (TXE flag) | Interrupt-driven TX ring buffer | BPM output is ~15 chars at ~1 Hz — polling completes in <150µs; ring buffer overhead is not justified |
| millis() source | SysTick | TIM3 or TIM4 | SysTick is a dedicated OS tick timer; saving TIM3/TIM4 for potential future use |
| Clock source | HSE 8MHz × PLL9 = 72MHz | HSI 8MHz × PLL9 = 72MHz | HSI is ±1% accurate; HSE uses the Nucleo onboard crystal at ±50ppm — more accurate baud rate and ADC timing |
## F103-Specific Gotchas Summary
| Gotcha | Detail |
|--------|--------|
| ADC calibration mandatory | Run RSTCAL + CAL sequence after every ADON. Not required on F4/F7. Skipping gives random DC offset. |
| EXTSEL encoding differs from F4 | TIM2_TRGO = 011 on F103; on F4 ADC the EXTSEL map is completely different. Never copy F4 ADC examples. |
| No ADC resolution bits in CR1 | F103 ADC is always 12-bit. Bits [25:24] of CR1 are reserved. F4 code that sets RES[1:0] is silently incompatible. |
| Timer clock doubling on APB | When APB1 prescaler ≠ 1 (ours = /2), TIM2 clock = 2×PCLK1 = 72 MHz, not 36 MHz. This affects PSC/ARR calculation. |
| GPIOA clock on APB2, not AHB | STM32F1 GPIO is on APB2 (RCC_APB2ENR). STM32F4 GPIO is on AHB1 (RCC_AHB1ENR). Wrong enable = GPIO stays input. |
| CRL/CRH GPIO config (not MODER) | F1 uses 4-bit packed CNF+MODE fields in CRL/CRH. F4 uses MODER/OTYPER/OSPEEDR/PUPDR. Code is not portable. |
| ADC1_2_IRQn shared vector | ADC1 and ADC2 share one interrupt vector (ADC1_2_IRQn). Check ADC1->SR->EOC inside the ISR. |
| USART2 BRR for 36MHz PCLK1 | BRR = 0x139 (313). Many online examples assume 72MHz APB or 8MHz HSI — use wrong BRR values for this project. |
## Sources
- STM32F103xB Reference Manual RM0008 Rev 21 (ST Microelectronics) — primary register reference. Sections: §6 (RCC), §11 (ADC), §14 (TIM2), §27 (USART), §3 (Flash ACR), §9 (GPIO)
- STM32F103RBT6 Datasheet DS5319 — pin multiplexing table, ADC channel mapping
- CMSIS 5 core_cm3.h — SysTick_Config(), NVIC_SetPriority(), NVIC_EnableIRQ() signatures
- Nucleo-F103RB User Manual UM1724 — PA2/PA3 routed to ST-Link virtual COM, 8MHz HSE crystal confirmed
- Confidence: HIGH — RM0008 register map is fixed silicon; no version ambiguity for STM32F103.
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
