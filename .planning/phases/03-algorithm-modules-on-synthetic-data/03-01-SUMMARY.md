---
phase: 03-algorithm-modules-on-synthetic-data
plan: "01"
subsystem: algorithm
tags: [algorithm, moving-average, synthetic-test, ppg, bpm, uart]
dependency_graph:
  requires: [Inc/adc.h, Inc/systick.h, Inc/usart.h, Src/main.c]
  provides: [Inc/algorithm.h, Src/algorithm.c]
  affects: [Src/main.c]
tech_stack:
  added: []
  patterns:
    - "32-sample integer moving average with circular buffer (s_ma_sum >> 5)"
    - "SYNTHETIC_TEST compile-time gate for hardware-free algorithm validation"
    - "Two-call UART output pattern: uart_write_str + uart_write_u32"
key_files:
  created:
    - Inc/algorithm.h
    - Src/algorithm.c
  modified:
    - Src/main.c
decisions:
  - "D-01: algorithm.c excludes stm32f0xx.h — pure software module with no peripheral register ownership"
  - "D-06/D-07/D-08: 100-entry PPG-shaped sine table at 10ms/sample = 60 BPM loop, ADC range 0-2200 matching LM358-on-3.3V"
  - "Plan 01 uses fixed 1000-count threshold crossing for minimal walking-slice detector; Plan 02 replaces with full state machine"
metrics:
  duration: "~15 minutes"
  completed: "2026-05-20T11:21:44Z"
  tasks_completed: 2
  tasks_total: 2
  files_created: 2
  files_modified: 1
---

# Phase 03 Plan 01: Algorithm Module Contract and Synthetic Data Vertical Slice Summary

**One-liner:** 32-sample integer moving average (SIG-01) + minimal fixed-threshold peak detector in algorithm.c/h, fed by a 100-entry PPG-shaped sine table in main.c under a SYNTHETIC_TEST compile gate that proves the full sample→BPM→UART pipeline without hardware.

## Tasks Completed

| Task | Name | Commit | Files |
|------|------|--------|-------|
| 1 | Define algorithm module contract and 32-sample moving average | 44cc83a | Inc/algorithm.h, Src/algorithm.c |
| 2 | Wire SYNTHETIC_TEST sine table and algorithm into main.c | b4d2ae7 | Src/main.c |

## What Was Built

### Inc/algorithm.h
Header guard `ALGORITHM_H`, `#include <stdint.h>`, two-function contract with no extern globals:
- `void algorithm_init(void)` — zeroes all static state before first sample
- `void algorithm_process(uint16_t sample)` — called from main loop per 10ms sample

Comment block documents that `SYNTHETIC_TEST` and `DEBUG_STATE` are defined in main.c, not in this header.

### Src/algorithm.c
Implements SIG-01 and a Plan 01 minimal detector:

**SIG-01 — 32-sample integer moving average:**
- Circular buffer `s_ma_buf[32]` with running sum `s_ma_sum` (uint32_t, overflow-safe for 32×4095=131040)
- Index advance via `(s_ma_idx + 1) & 0x1F` (modulo 32 without division)
- Divide via `s_ma_sum >> 5` (power-of-2, integer-only, no float)

**Minimal Plan 01 peak detector (walking slice):**
- Detects rising-edge crossing of fixed threshold 1000 counts (`s_prev_filtered < 1000 && filtered >= 1000`)
- BPM: `60000UL / interval_ms` with `interval_ms != 0` guard (T-03-03 mitigation)
- Prints via two-call pattern: `uart_write_str("BPM: "); uart_write_u32(bpm); uart_write_str("\r\n");`
- `s_last_peak_ms == 0` sentinel skips first computation (T-03-04 mitigation)

No float, no double, no stdlib, no stm32f0xx.h — pure software module.

### Src/main.c (modified)
- First two lines: `#define SYNTHETIC_TEST` and `#define DEBUG_STATE`
- Added `#include "algorithm.h"` after `#include "adc.h"`
- 100-entry `sine_table[100]` under `#ifdef SYNTHETIC_TEST`:
  - Shape: baseline ~200 → systolic peak 2040 at idx 16 → descent → dicrotic notch ~820 at idx 50 → flat baseline from idx 70
  - Loop period: 100 × 10ms = 1.0s = 60 BPM when the table loops continuously (D-08)
  - ADC range: 200–2040 (matches LM358-on-3.3V hardware range 0–2200, D-07)
- SYNTHETIC_TEST path in while(1): advances `s_table_idx` every 10ms via `millis()` delta, calls `algorithm_process(sine_table[s_table_idx])`, wraps at 100
- Live ADC path preserved in `#else` branch: `algorithm_process((uint16_t)g_adc_sample)`
- `algorithm_init()` added to init sequence after `adc_init()`
- Banner updated: `"--- HeartRateSensor Phase 3 ---"`

## Threat Mitigations Applied

| Threat | Mitigation |
|--------|-----------|
| T-03-01: s_ma_buf index out of bounds | `(s_ma_idx + 1) & 0x1F` — mask guarantees 0..31 always |
| T-03-03: divide by zero in BPM calc | `interval_ms != 0` guard before `60000UL / interval_ms` |
| T-03-04: uninitialized static state | `algorithm_init()` explicitly zeroes all static vars; called in main before first sample |

## Deviations from Plan

None — plan executed exactly as written. The sine table comment structure was adjusted once to ensure exactly 100 numeric entries (minor formatting deviation during implementation, no logic change).

## Known Stubs

The Plan 01 minimal detector is an intentional stub — a fixed-threshold rising-edge crossing that produces a BPM line to prove the end-to-end pipeline. It does NOT implement:
- SIG-02: adaptive threshold (last_peak × 0.6)
- SIG-03: refractory suppression (300–400ms)
- SIG-04: 40–200 BPM bounds check
- SIG-05: 5-reading rolling average

These are explicitly deferred to Plan 02 per the plan objective ("Plan 02 replaces the minimal detector with the full state machine"). The stub does not block the plan goal (proving the sample→algorithm→BPM→UART slice works).

## Threat Flags

None — no new security-relevant surface beyond what is specified in the plan threat model.

## Self-Check

Files exist:
- `Inc/algorithm.h`: FOUND
- `Src/algorithm.c`: FOUND
- `Src/main.c`: FOUND (modified)
- `03-01-SUMMARY.md`: this file

Commits exist:
- `44cc83a`: feat(03-01): define algorithm module contract and 32-sample moving average — FOUND
- `b4d2ae7`: feat(03-01): wire SYNTHETIC_TEST sine table and algorithm into main.c — FOUND

## Self-Check: PASSED
