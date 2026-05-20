---
phase: 03-algorithm-modules-on-synthetic-data
reviewed: 2026-05-20T00:00:00Z
depth: standard
files_reviewed: 3
files_reviewed_list:
  - Inc/algorithm.h
  - Src/algorithm.c
  - Src/main.c
findings:
  critical: 2
  warning: 6
  info: 2
  total: 10
status: fixed
---

# Phase 3: Code Review Report

**Reviewed:** 2026-05-20
**Depth:** standard
**Files Reviewed:** 3
**Status:** issues_found

## Summary

Three files were reviewed: the algorithm header, the algorithm implementation, and main.c with its synthetic test harness. The algorithm logic structure (moving average, 5-state peak detector, refractory window, BPM bounds, rolling average) is sound in its overall design. However, two critical defects were found that will produce incorrect BPM readings in production: a non-atomic multi-call `millis()` pattern inside PEAK_HOLD that causes systematic interval measurement error, and a USART BRR register value that contradicts its own comment and is numerically wrong. Six warnings cover a systick/clock frequency mismatch that silently breaks all timing, the refractory overflow edge case, and several robustness issues. Two informational items address magic numbers and unconditional debug defines.

---

## Critical Issues

### CR-01: USART2 BRR register value is wrong — serial output is corrupt

**File:** `Src/usart.c:43`
**Issue:** The comment on line 43 states `BRR = 0x1A1 (417) → 115200 baud at PCLK=48MHz`, but the value actually written to the register is `0x45` (decimal 69). At 48 MHz, `48,000,000 / 69 ≈ 695,652 baud` — approximately 6× the intended rate. All serial output (`BPM: …` lines, `STATE: …` transitions) will be garbled on any connected terminal. This bug is in a support file outside the review scope but it directly breaks the observable output of algorithm.c's `uart_write_str`/`uart_write_u32` calls and is the most impactful defect in the system.

**Fix:**
```c
/* BRR = PCLK / baud = 48,000,000 / 115,200 = 416.67 → round to 417 = 0x1A1 */
USART2->BRR = 0x1A1;
```

---

### CR-02: Three separate `millis()` calls in one PEAK_HOLD visit — interval measurement is non-atomic

**File:** `Src/algorithm.c:102-113`
**Issue:** `millis()` is called three separate times within a single `PEAK_HOLD` case execution: at line 102 (set refractory end), at line 108 (record first-beat timestamp), and at line 113 (record subsequent-beat timestamp). Because `millis()` reads a `volatile` counter that SysTick increments asynchronously, each call may return a different value. In the `else` branch (line 111–138), `interval_ms` is computed as `millis() - s_last_peak_ms` (line 112) and then `s_last_peak_ms` is immediately updated with yet another `millis()` call (line 113). If SysTick fires between these two calls, `s_last_peak_ms` is set 1 ms later than the start of the measured interval, causing the *next* interval to be computed as 1 ms shorter than it truly is. Over several beats this accumulates as a systematic BPM over-read.

The fix is to snapshot `millis()` once at the top of the case and reuse the snapshot:

```c
case PEAK_HOLD:
{
    uint32_t now = millis();   /* single atomic snapshot for the whole case */

    s_threshold      = (uint16_t)((uint32_t)s_peak_val * 3 / 5);
    s_refractory_end = now + REFRACTORY_MS;

    if (s_last_peak_ms == 0)
    {
        s_last_peak_ms = now;
    }
    else
    {
        uint32_t interval_ms = now - s_last_peak_ms;
        s_last_peak_ms = now;   /* both sides use the same 'now' */

        if (interval_ms != 0)
        {
            uint32_t bpm = 60000UL / interval_ms;
            if (bpm >= 40 && bpm <= 200)
            {
                /* ... rolling average and UART output unchanged ... */
            }
        }
    }

    next_state = REFRACTORY;
    break;
}
```

---

## Warnings

### WR-01: SysTick LOAD value assumes 72 MHz but usart.c confirms clock is 48 MHz — all timing is wrong

**File:** `Src/systick.c:9`
**Issue:** `systick_init()` calls `SysTick_Config(71999)` with the comment "72,000,000 Hz / 1000 Hz) - 1 = 1 ms at HCLK=72MHz". But `usart.c` explicitly documents "SYSCLK = PCLK = 48 MHz (HSE 8MHz × PLL6, APB /1)". With HCLK=48MHz and LOAD=71999, SysTick fires every 72,000 / 48,000,000 s = 1.5 ms, not 1 ms. Every `millis()` value is therefore 1.5× the true elapsed time. Consequences:
- The 350 ms refractory window becomes 233 ms (may not suppress the dicrotic notch).
- The 10 ms sample gate in the SYNTHETIC_TEST loop becomes a ~6.7 ms gate (samples arrive at ~150 Hz instead of 100 Hz), distorting BPM calculation.
- `60000UL / interval_ms` produces BPM values scaled by 1/1.5 = 0.667× — readings will read ~40% lower than true BPM.

**Fix:** Match LOAD to the actual 48 MHz system clock:
```c
/* LOAD = (48,000,000 / 1000) - 1 = 47999 → 1 ms at HCLK=48MHz */
SysTick_Config(47999);
```

---

### WR-02: `s_refractory_end` addition will silently fail on `millis()` rollover

**File:** `Src/algorithm.c:102`
**Issue:** `s_refractory_end = millis() + REFRACTORY_MS` performs unsigned 32-bit addition. When `millis()` is within 350 ms of wrapping (i.e., within ~49.7 days of power-on), `s_refractory_end` wraps to a small value near 0. The guard on line 146, `millis() >= s_refractory_end`, then evaluates `TRUE` immediately because the freshly-wrapped `millis()` (a large value close to UINT32_MAX) is greater than the wrapped `s_refractory_end` (a value near 0) — but only for the ~350 ms window around rollover. Immediately after the rollover `millis()` also wraps to 0, and `0 >= s_refractory_end` is `FALSE`, so the state machine can get stuck in REFRACTORY until `millis()` catches up — potentially 350 ms of missed beats. For a university demo this is a non-issue, but for production code the elapsed-time idiom is correct:

**Fix:**
```c
/* Store the start time, not the end time */
static uint32_t s_refractory_start = 0;

/* In PEAK_HOLD: */
s_refractory_start = now;   /* using the 'now' snapshot from CR-02 fix */

/* In REFRACTORY: */
case REFRACTORY:
    if ((millis() - s_refractory_start) >= REFRACTORY_MS)
    {
        next_state = FALLING;
    }
    break;
```

---

### WR-03: `SYNTHETIC_TEST` and `DEBUG_STATE` are unconditionally defined in main.c — cannot build for hardware without source edits

**File:** `Src/main.c:1-2`
**Issue:** Lines 1 and 2 hard-code `#define SYNTHETIC_TEST` and `#define DEBUG_STATE` with no compiler-flag guard. Building for real hardware requires remembering to delete or comment out both lines. `DEBUG_STATE` in particular causes `uart_write_str` to be called inside `algorithm_process()` on every state transition — this polling UART call adds up to ~150 µs of blocking time that is completely absent from the production (non-DEBUG_STATE) build. Timing-sensitive behavior therefore differs between test and production builds.

**Fix:** Replace the bare defines with makefile/IDE preprocessor-flag guards so the defines can be toggled at build time without touching source:
```c
/* Remove lines 1-2 from main.c entirely.
   In STM32CubeIDE: Project → Properties → C/C++ Build → Settings
   → MCU GCC Compiler → Preprocessor → add SYNTHETIC_TEST and DEBUG_STATE
   as optional build-configuration symbols.
   In algorithm.c / main.c, no source change is needed — #ifdef already guards them. */
```

---

### WR-04: `s_table_idx` increment evaluates `s_table_idx + 1` twice — latent mutation hazard

**File:** `Src/main.c:75`
**Issue:** `s_table_idx = (s_table_idx + 1 >= 100) ? 0 : s_table_idx + 1;`
The expression `s_table_idx + 1` is computed twice. While `s_table_idx` is a `static uint8_t` local (not volatile, not modified concurrently), the idiom is unnecessarily fragile — any future change that makes the variable volatile or that passes it by pointer would silently introduce a bug. The standard modulo form is unambiguous.

**Fix:**
```c
s_table_idx++;
if (s_table_idx >= 100) s_table_idx = 0;
/* or equivalently: s_table_idx = (s_table_idx + 1) % 100; */
```

---

### WR-05: `systick.c` atomicity comment cites Cortex-M3 but target silicon is Cortex-M0

**File:** `Src/systick.c:17`
**Issue:** Comment reads "Single 32-bit read is atomic on Cortex-M3 — no need to disable interrupts". The review context and all driver register choices (MODER, AFR, ISR, TDR, AHBENR) confirm this is STM32F070RB (Cortex-M0). On Cortex-M0, a 32-bit `LDR` from a naturally aligned address is also a single instruction and is atomic — so the conclusion is accidentally correct, but citing the wrong core is misleading and inconsistent with the rest of the codebase.

**Fix:**
```c
/* Single 32-bit aligned LDR is atomic on Cortex-M0 — no need to disable interrupts */
return s_ticks;
```

---

### WR-06: RISING state will not detect a flat-topped peak — drops straight to PEAK_HOLD on first non-increasing sample

**File:** `Src/algorithm.c:83-91`
**Issue:** The RISING case transitions to PEAK_HOLD the first time `filtered` is not strictly greater than `s_peak_val`. A real PPG signal (and even the synthetic table) can have two consecutive equal samples near the peak (e.g., idx 16 = 2040, idx 17 = 2030 — this pair works, but any digitization that produces two equal maximums would trigger early). More significantly, a single ADC noise spike above the true peak sets `s_peak_val` to the spike value, then the very next sample (which returns to the true peak level) is `<= s_peak_val` and prematurely fires PEAK_HOLD with an inflated `s_peak_val`. The inflated value propagates into the adaptive threshold (`s_peak_val * 3 / 5`), raising the threshold above the true peak and suppressing the next beat entirely.

**Fix:** Require the signal to fall by a minimum hysteresis margin before declaring peak:
```c
case RISING:
    if (filtered > s_peak_val)
    {
        s_peak_val = filtered;
    }
    else if (s_peak_val - filtered >= 20)   /* hysteresis: 20 counts ~ 0.5% of 4095 */
    {
        next_state = PEAK_HOLD;
    }
    break;
```

---

## Info

### IN-01: Initial threshold magic number 1200 is unexplained

**File:** `Src/algorithm.c:17` and `Src/algorithm.c:41`
**Issue:** `s_threshold = 1200` appears twice (declaration and `algorithm_init`). The comment says "~60% of midpoint" but does not define "midpoint". The synthetic table peaks at 2040 ADC counts; 60% of 2040 = 1224, so 1200 is plausible. The relationship to the hardware signal range (documented elsewhere as 0–2200) should be explicit.

**Fix:** Replace with a named constant and add a units comment:
```c
/* Initial threshold: ~55% of expected systolic peak (2200 cts max hardware range).
   After the first beat the adaptive threshold takes over. */
#define THRESHOLD_INITIAL  1200U
```

---

### IN-02: `g_adc_ready` and `g_adc_sample` declared `extern volatile` in adc.h but their defining `adc.c` is never reviewed

**File:** `Src/main.c:79-82`
**Issue:** In the non-SYNTHETIC_TEST path, `g_adc_ready` is cleared and `g_adc_sample` is read without a memory barrier or `__disable_irq()` guard. The ISR sets both variables. On Cortex-M0 this is safe for naturally-aligned accesses (single instruction), but `g_adc_ready` (uint8_t) and `g_adc_sample` (uint16_t) reads are each single instructions and the `volatile` keyword prevents compiler reordering — so the existing code is correct. Flagged as informational to ensure the adc.c ISR is reviewed when that file is in scope.

---

_Reviewed: 2026-05-20_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
