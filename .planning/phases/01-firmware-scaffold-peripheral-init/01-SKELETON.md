---
phase: 01-firmware-scaffold-peripheral-init
type: skeleton
created: 2026-05-12
---

# Walking Skeleton: HeartRateSensor Firmware

This document records the architectural decisions made in Phase 1 that subsequent phases (2–5) will build on without renegotiating.

## Capability Proven

After Phase 1, the following end-to-end pipeline is provably working:

```
arm-none-eabi-gcc compiles Src/*.c → ELF/HEX
  → ST-Link flashes Nucleo-F103RB
  → board runs autonomously
  → USART2 (PA2/PA3) → ST-Link virtual COM port
  → host serial terminal at 115200 baud
  → "millis: NNNN\r\n" updating every 1000ms
```

This is the thinnest possible end-to-end stack for the project.

## Architectural Decisions (locked for Phases 2–5)

### Toolchain
- **IDE / Build:** STM32CubeIDE 1.15+ with bundled arm-none-eabi-gcc
- **Flasher:** ST-Link GDB server (bundled with CubeIDE) over USB ST-Link/V2 onboard the Nucleo
- **Device:** STM32F103RBT6 (Nucleo-F103RB), Cortex-M3 @ 72MHz HSE+PLL
- **Headers:** CMSIS only (`stm32f1xx.h`, `core_cm3.h`). No HAL, no LL, no Arduino.

### Project Structure
```
HeartRateSensor/
├── HeartRateSensor.ioc           # CubeMX config (clock tree + linker only — no peripheral init)
├── Inc/
│   ├── systick.h                 # Phase 1 — millis() API (D-03)
│   ├── usart.h                   # Phase 1 — uart_write_str / uart_write_u32 (D-04)
│   ├── adc.h                     # Phase 2
│   ├── tim.h                     # Phase 2
│   └── algorithm.h               # Phase 3
├── Src/
│   ├── main.c                    # Application entry; calls *_init() and loops
│   ├── systick.c                 # SysTick_Handler ISR lives here (D-02)
│   ├── usart.c                   # USART2 polling TX
│   ├── adc.c                     # Phase 2 — ADC1_2_IRQHandler lives here (D-02)
│   ├── tim.c                     # Phase 2
│   ├── algorithm.c               # Phase 3
│   ├── system_stm32f1xx.c        # CubeIDE-generated SystemInit() — HSE+PLL → 72MHz (D-05)
│   └── stm32f1xx_it.c            # ONLY default handler stubs; real ISRs live in their driver
├── Startup/
│   └── startup_stm32f103xb.s     # CubeIDE-generated
└── STM32F103RBTx_FLASH.ld        # CubeIDE-generated linker script
```

### Driver Module Pattern (D-01, D-02)
- One driver = one .c + one .h pair.
- Driver exposes a small `*_init()` function and a handful of public functions.
- ISRs for the driver's peripheral live INSIDE the driver's .c file (e.g. `SysTick_Handler` in `systick.c`).
- No central `interrupt_handlers.c`.
- Phases 2–4 extend this pattern (adc.c, tim.c, algorithm.c) — no refactor needed.

### Clock Tree (locked)
- HSE 8MHz crystal (Nucleo onboard) × PLL9 = SYSCLK 72MHz = HCLK 72MHz
- APB1 prescaler /2 → PCLK1 = 36MHz (USART2, TIM2 base; timer clock doubled to 72MHz)
- APB2 prescaler /1 → PCLK2 = 72MHz (GPIO, ADC source)
- Flash latency: 2 wait states (required for HCLK > 48MHz, RM0008 §3.3.2)
- ADC prescaler /6 → ADCCLK = 12MHz (locked for Phase 2; do NOT use /4 → 18MHz exceeds 14MHz limit)
- Init handled by CubeIDE-generated `SystemInit()` in `system_stm32f1xx.c` (D-05). Promote into student code only if Phase 5 report requires.

### Serial Output API (D-04 — locked for Phases 2–5)
Two primitives, no stdlib, no printf:
- `void uart_write_str(const char *s);` — null-terminated ASCII
- `void uart_write_u32(uint32_t n);` — decimal ASCII, no leading zeros

All later output (`"BPM: NN\r\n"`, `"--\r\n"`, `"RAW=NNN FILT=NNN ..."`) is composed from these two functions. No newlib `_write` retarget.

### Timing API (D-03 — locked for Phases 2–5)
- `uint32_t millis(void);` declared in `systick.h`
- Returns volatile internal counter copy; no extern of the raw counter
- 1ms tick, SysTick LOAD = 71999, HCLK source, interrupt-driven increment
- All time-based logic (1Hz print, refractory period, finger-loss timeout) compares `millis()` deltas — never delay loops

### Interrupt Priority Plan
- SysTick: priority 0 (highest) — millis() must never be delayed
- ADC1_2_IRQn (Phase 2): priority 1
- USART2 (if ever IRQ-driven, currently polling): priority 2

## Verification Procedure (Phase 1 acceptance)
1. CubeIDE build produces ELF + HEX with zero warnings, zero errors
2. ST-Link "Run" flashes board successfully
3. Host opens serial terminal (PuTTY / minicom / CubeIDE terminal) on ST-Link COM port, 115200 8N1
4. Terminal shows `millis: 1000\r\nmillis: 2000\r\nmillis: 3000\r\n...` advancing every 1s ±1%
5. Press Nucleo reset button — output restarts from `millis: 1000`
6. Disconnect+reconnect USB — board boots autonomously, output resumes

## What is NOT in the skeleton (locked deferrals)
- ADC1, TIM2, GPIO PA0 analog mode — Phase 2
- Signal processing modules — Phase 3
- Analog calibration constants — Phase 4
- printf retarget — never (deferred indefinitely; `uart_write_str/u32` covers all needs)
- I2C LCD — v2 only
- DMA, RTOS, FPU math, dynamic allocation — out of scope

## Sources
- `.planning/phases/01-firmware-scaffold-peripheral-init/01-CONTEXT.md` decisions D-01..D-05
- `CLAUDE.md` §RCC Clock Configuration, §GPIO Configuration, §USART2, §SysTick, §F103-Specific Gotchas
- RM0008 (STM32F103xB Reference Manual) §3, §6, §9, §27
