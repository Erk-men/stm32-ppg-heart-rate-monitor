---
phase: 02-adc-timer-hardware-trigger
plan: 02
subsystem: firmware-integration

key-files:
  modified:
    - Src/main.c
    - Src/adc.c

key-decisions:
  - "ADC_IRQHandler used (not ADC1_IRQHandler) — F070 startup vector table uses ADC_IRQHandler"
---

# Plan 02-02 Summary

## What was built
main.c updated to call tim3_init() and adc_init() in correct order, then stream "adc: NNNN\r\n" at 100 Hz via USART2.

## Hardware verification results
- Serial output: "--- HeartRateSensor Phase 2 ---" banner + continuous adc: lines ✓
- PA0 = 3.3V → 4095 ✓
- PA0 = GND → 0 ✓
- ADC ISR firing at 100 Hz ✓

## Issues encountered
ADC1_IRQHandler renamed to ADC_IRQHandler to match F070 startup vector table (commit a9a3b10). The CMSIS header defines ADC1_IRQHandler as an alias but the startup .s file uses ADC_IRQHandler as the weak symbol name.

## Self-Check: PASSED
