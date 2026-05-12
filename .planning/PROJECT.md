# HeartRateSensor

## What This Is

A bare-metal heart rate monitor built on the STM32 Nucleo-F103RB (ARM Cortex-M3) for an Advanced Microprocessors university course. It uses reflective photoplethysmography (PPG) — an IR LED and BPW34 photodiode with an LM358 analog signal chain — to measure pulse and output BPM values over USART2 serial. Deliverable is working hardware + bare-metal C source code + a technical report.

## Core Value

Finger on sensor → stable, accurate BPM value visible on the serial monitor.

## Requirements

### Validated

(None yet — ship to validate)

### Active

- [ ] ADC1 configured at 12-bit, 100Hz sampling rate via TIM2 hardware trigger (no jitter)
- [ ] Bare-metal ADC driver (register-level CMSIS, no HAL or Arduino libraries)
- [ ] Bare-metal USART2 driver at 115200 baud for serial debug output
- [ ] SysTick-based millis() for timing measurements
- [ ] Moving average filter (16–32 sample window) for noise reduction
- [ ] Adaptive threshold peak detection (threshold = avg_peak × 0.6, recalculated per beat)
- [ ] BPM calculation: 60000 / peak-to-peak interval (ms)
- [ ] BPM sanity bounds: reject readings outside 40–200 BPM before averaging
- [ ] 5-reading BPM rolling average for stable output
- [ ] "No finger" detection: print `"--"` when signal amplitude < minimum threshold
- [ ] Serial output: BPM value printed each detected heartbeat
- [ ] Technical report: circuit schematic + calculations, register config, algorithm flowchart, serial monitor screenshots, calibration results

### Out of Scope

- I2C LCD display — explicitly deferred; serial monitor is the only output for v1
- HAL / STM32 LL drivers — course requires bare-metal register access throughout
- Bluetooth / wireless transmission — not required for course deliverable
- Android/PC companion app — out of scope

## Context

- **Course**: İleri Mikro İşlemciler (Advanced Microprocessors)
- **Platform**: STM32 Nucleo-F103RB — STM32F103RBT6, ARM Cortex-M3, 72MHz, 12-bit ADC
- **Toolchain**: STM32CubeIDE, CMSIS headers, ST-Link USB programmer
- **Signal chain**: IR LED (940nm) → finger tissue → BPW34 photodiode → TIA (LM358) → HPF (fc≈0.5Hz) → ×100 amplifier (LM358) → LPF (fc≈15.9Hz using 1µF + 10KΩ) → PA0 ADC input
- **PPG geometry**: Reflective mode (LED and photodiode on the same side of the finger)
- **LM358 supply note**: At 3.3V supply, LM358 output saturates ~1.5V below rail (~1.8V max). ADC range is effectively 0–~2200 counts out of 4095. Adaptive threshold compensates; if signal is too weak during bench testing, power op-amp from 5V USB rail instead.
- **Capacitor note**: LPF uses 1µF (from existing component list) giving fc≈15.9Hz — adequate for PPG (signal <3Hz, sampling at 100Hz)
- **LED drive current**: 10KΩ current-limiting resistor (~330µA) is conservative; reduce to 1KΩ if signal amplitude is insufficient during calibration

## Constraints

- **Tech stack**: Bare-metal C only — direct CMSIS register writes, no HAL, no Arduino, no RTOS
- **Hardware**: Fixed component list; LM358 op-amp, BPW34 photodiode, 940nm IR LED — no substitutions without retesting
- **IDE**: STM32CubeIDE (project auto-generates CMSIS headers via .ioc configuration)
- **Development order**: Build breadboard circuit before finalizing algorithm thresholds — real signal needed for calibration
- **Deliverable**: Working demo + technical report (not just code)

## Key Decisions

| Decision | Rationale | Outcome |
|----------|-----------|---------|
| Bare-metal CMSIS over HAL | Course requirement; demonstrates register-level understanding | — Pending |
| TIM2 hardware trigger for ADC sampling | Eliminates jitter in 100Hz sampling; jitter corrupts peak-interval timing | — Pending |
| Adaptive threshold instead of fixed | PPG amplitude varies with skin tone, pressure, ambient light | — Pending |
| 16–32 sample moving average (up from 8) | 8 samples = 80ms — insufficient to smooth LM358 noise at 3.3V supply | — Pending |
| Reflective PPG geometry | Standard for wearable sensors; easier finger placement than transmissive | — Pending |
| LPF at 15.9Hz (1µF) vs 10Hz (1.6µF) | 1.6µF is non-standard; 1µF already in component list, fc still adequate | — Pending |
| 5V op-amp supply (if needed) | LM358 output swing limited at 3.3V — to be tested on bench | — Pending |

## Evolution

This document evolves at phase transitions and milestone boundaries.

**After each phase transition** (via `/gsd-transition`):
1. Requirements invalidated? → Move to Out of Scope with reason
2. Requirements validated? → Move to Validated with phase reference
3. New requirements emerged? → Add to Active
4. Decisions to log? → Add to Key Decisions
5. "What This Is" still accurate? → Update if drifted

**After each milestone** (via `/gsd-complete-milestone`):
1. Full review of all sections
2. Core Value check — still the right priority?
3. Audit Out of Scope — reasons still valid?
4. Update Context with current state

---
*Last updated: 2026-05-12 after initialization*
