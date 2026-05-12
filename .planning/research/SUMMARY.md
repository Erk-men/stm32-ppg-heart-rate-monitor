# Research Summary — HeartRateSensor

**Project:** Bare-metal STM32F103RBT6 PPG heart rate monitor
**Researched:** 2026-05-12
**Overall Confidence:** HIGH

---

## Executive Summary

Reflective PPG firmware on STM32F103 (Cortex-M3, 72MHz, 20KB RAM, no FPU). Three-layer architecture: peripheral drivers → integer algorithm modules → thin main loop. TIM2 hardware-triggers ADC1 at exactly 100Hz. ISR only captures sample + sets flag. All filtering, detection, and output happen in main loop. Adaptive threshold peak detection (threshold = last_peak × 0.6, integer arithmetic), 32-sample moving average, 5-reading BPM rolling average.

The two highest-risk areas: (1) analog hardware — LM358 saturates at ~1.8V on 3.3V supply; missing DC bias half-wave clips the PPG signal; (2) peripheral trigger chain — wrong ADC prescaler drives ADC at 36MHz (2.5× over limit) with no error flag.

---

## Recommended Stack

| Peripheral | Key Config | Value |
|---|---|---|
| ADCPRE | /6 → ADCCLK = 12MHz | `RCC_CFGR_ADCPRE_DIV6` — mandatory |
| TIM2 | 100Hz TRGO | PSC=719, ARR=999; MMS=010 in CR2 |
| ADC1 | 12-bit, HW-triggered | SMPR2 SMP0 ≥ 28.5 cycles; RSTCAL+CAL before use |
| USART2 | 115200 baud | BRR=0x139 (313) for PCLK1=36MHz |
| SysTick | 1ms millis() | LOAD=71999, HCLK source |

**⚠ Open question:** EXTSEL value for TIM2_TRGO — STACK.md and PITFALLS.md disagree. Resolve from RM0008 Table 68 directly before writing `adc_init()`.

**Key F103 traps:**
- GPIO via 4-bit CRL/CRH (not MODER) — PA0 needs CNF=00, MODE=00
- APB1 timer clock doubles when prescaler ≠ 1: PCLK1=36MHz → TIM2=72MHz
- ADC calibration (RSTCAL+CAL) is mandatory — skipping causes random DC offset
- ADC1+ADC2 share one NVIC vector (ADC1_2_IRQn); read ADC1->SR->EOC in ISR

---

## Table Stakes vs Differentiators

**Must have (dependency order):**
1. SysTick millis()
2. USART2 serial driver
3. TIM2 + ADC1 at 100Hz (hardware trigger)
4. 32-sample integer moving average (`sum >> 5`, no float)
5. Adaptive threshold peak detect + 300–400ms refractory
6. BPM sanity bounds 40–200 (applied before rolling average)
7. 5-reading BPM rolling average
8. No-finger detection (print `"--"`, reset threshold)
9. BPM printed per heartbeat

**Should have (high report value, low effort):**
- `#define CALIBRATION_MODE 1` — 5-second ADC min/max printout for report
- `#define DEBUG_VERBOSE 1` — per-sample RAW/FILT/THR/AMP/STATE output
- Named constants for all magic numbers
- Overflow-safe millis: `(uint32_t)(now - then)`
- State machine enum: IDLE / RISING / PEAK_HOLD / FALLING / REFRACTORY

**Anti-features (do not add):** FFT, autocorrelation, DMA, RTOS, SpO2, Bluetooth, float printf.

---

## Firmware Architecture

```
APPLICATION     main.c, bpm.c, finger_detect.c
                Owns: BPM averaging, no-finger logic, serial output

ALGORITHM       filter.c, peak_detect.c
                Pure C — zero CMSIS includes — testable with synthetic data

DRIVERS         adc.c, tim.c, usart.c, systick.c
                Owns: register writes, ISRs, ring buffers
```

**ISR rule:** ADC ISR reads DR (clears EOC), writes ring buffer, sets flag. SysTick ISR increments tick_ms. Nothing else in ISR context — no UART, no filtering, no math.

**Data flow:**
```
LM358 → ADC1 (TIM2 trigger, 100Hz)
→ ISR: ring buffer + adc_ready flag
→ main: filter_push() → peak_detect_update(filtered, millis()) → bpm_push() → uart_write()
```

---

## Top 5 Critical Pitfalls

1. **ADC clock > 14MHz** — Set `RCC_CFGR_ADCPRE_DIV6`; /4 still gives 18MHz (over limit)
2. **LM358 saturation at 3.3V** — Power op-amp from 5V USB rail; 3.3V gives max ~1.8V output
3. **Missing DC bias at gain stage input** — Add 100KΩ/100KΩ divider + 10KΩ to non-inverting input; without it, HPF kills DC and negative half clips
4. **Wrong EXTSEL for TIM2_TRGO** — Verify from RM0008 Table 68; wrong value = ADC never fires from TIM2
5. **Stale adaptive threshold after finger removal** — No-finger detect must reset `peak_threshold` to low value; otherwise re-acquisition is impossible

---

## Bench Calibration Checklist (Phase 4)

| Question | Measurement | Action |
|---|---|---|
| EXTSEL value | RM0008 Table 68 | Resolve before coding |
| LM358 output range at 3.3V | Oscilloscope on op-amp output | Switch to 5V if flat-topped |
| LED drive current | ADC peak-to-peak counts | Swap 10KΩ → 1KΩ if < 200 counts |
| FINGER_MIN_AMPLITUDE | ADC amplitude finger vs. no-finger | Set threshold between the two |
| VDDA noise floor | PA0 tied to GND; ADC spread | Add 100nF + 10µF at VDDA if > ±2 counts |
| Threshold fraction (0.6) | Verbose mode; watch for dicrotic false peaks | Increase if false positives |

---

## Suggested Phase Structure

| Phase | Name | Gate |
|---|---|---|
| 1 | Firmware Scaffold + Peripheral Init | millis() printing over serial |
| 2 | TIM2 + ADC1 Hardware Trigger | 100Hz raw ADC values on serial; PA0 to 3.3V reads ≈ 4095 |
| 3 | Algorithm Modules on Synthetic Data | Stable 60 BPM from sine table; state machine visible in verbose mode |
| 4 | Full Integration + Analog Calibration | Stable BPM from real finger; all thresholds empirically tuned |
| 5 | Report Evidence + Polish | Complete report package: schematic, register config, flowchart, screenshots |

Phases 1–3 complete before breadboard circuit is assembled. Phase 4 requires real hardware.
