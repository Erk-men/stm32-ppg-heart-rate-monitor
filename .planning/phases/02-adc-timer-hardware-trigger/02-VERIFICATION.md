---
phase: 02-adc-timer-hardware-trigger
verified: 2026-05-19T00:00:00Z
status: passed
score: 9/9 must-haves verified
overrides_applied: 0
---

# Phase 2: ADC + Timer Hardware Trigger Verification Report

**Phase Goal:** Establish hardware trigger chain TIM3 → ADC1 → ISR delivering deterministic 100 Hz sampling, with raw ADC values streaming over USART2.
**Verified:** 2026-05-19
**Status:** PASSED
**Re-verification:** No — initial verification

---

## Goal Achievement

### Observable Truths

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | Raw ADC values print over serial at 100 Hz (one line per sample, no bursts or gaps) | VERIFIED | `main.c:29-35` polls `g_adc_ready` in `while(1)` and prints `"adc: NNNN\r\n"` per sample; human hardware confirmation: ~100 lines/sec observed |
| 2 | PA0 tied to 3.3V reads ≈ 4095; PA0 tied to GND reads ≈ 0 | VERIFIED | Human hardware confirmation: PA0=3.3V → 4095, PA0=GND → 0 (exact full-scale and zero); 12-bit range confirmed on silicon |
| 3 | EXTSEL bits in ADC1->CFGR1 = 011 (TIM3_TRGO); ADC fires only on TIM3 update event | VERIFIED | `adc.c:38-40`: `ADC_CFGR1_EXTSEL_0 \| ADC_CFGR1_EXTSEL_1` = 011; `ADC_CFGR1_EXTEN_0` = rising edge; no software trigger path |
| 4 | ADC calibration (single ADCAL step) completes at startup with no hang | VERIFIED | `adc.c:34-35`: `ADC_CR_ADCAL` set and polled before `ADEN`; board ran and produced output confirming no startup hang |
| 5 | TIM3 counts at 100 kHz (PSC=79) and generates TRGO at 100 Hz (ARR=999) | VERIFIED | `tim3.c:24,27`: `TIM3->PSC = 79`, `TIM3->ARR = 999`; `CR2 = TIM_CR2_MMS_1` (MMS=010 = Update→TRGO) |
| 6 | ADC uses HSI14 (14 MHz) as its clock source, not PCLK | VERIFIED | `adc.c:30-31`: `RCC_CR2_HSI14ON` set and `HSI14RDY` polled; `CFGR2` left at reset (CKMODE=00 = async HSI14) |
| 7 | ADC calibration (ADCAL) completes before ADEN is asserted | VERIFIED | `adc.c` steps 4 then 10: ADCAL at line 34-35, ADEN at line 56 — correct order enforced by code sequence |
| 8 | ADC_IRQHandler fires on EOC, stores 12-bit result in g_adc_sample, sets g_adc_ready=1 | VERIFIED | `adc.c:69-75`: ISR name is `ADC_IRQHandler` (correct F070 vector table symbol, fixed in commit a9a3b10); reads `ADC1->DR & 0x0FFF` into `g_adc_sample`, sets `g_adc_ready = 1` |
| 9 | PA0 is configured in analog mode (MODER=11) with no pull resistors | VERIFIED | `adc.c:24`: `GPIOA->MODER \|= GPIO_MODER_MODER0` (sets bits [1:0]=11); PUPDR not written (reset default = no pull) |

**Score:** 9/9 truths verified

---

## Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `Inc/tim3.h` | `tim3_init()` declaration + header guard | VERIFIED | Header guard present; `void tim3_init(void)` declared |
| `Src/tim3.c` | TIM3 100 Hz TRGO driver | VERIFIED | PSC=79, ARR=999, CR2=TIM_CR2_MMS_1, CR1=TIM_CR1_CEN; 34 lines, substantive |
| `Inc/adc.h` | `adc_init()` + extern volatile globals | VERIFIED | `g_adc_sample`, `g_adc_ready` declared `extern volatile`; `adc_init()` declared |
| `Src/adc.c` | ADC1 hardware-triggered driver + ISR | VERIFIED | 75 lines; 11-step init sequence + `ADC_IRQHandler`; substantive implementation |
| `Src/main.c` | Main loop consuming `g_adc_ready`/`g_adc_sample` | VERIFIED | Calls `tim3_init()` and `adc_init()` in correct order; polls `g_adc_ready`; prints via `uart_write_str`/`uart_write_u32`; no printf/stdlib |

---

## Key Link Verification

| From | To | Via | Status | Details |
|------|----|-----|--------|---------|
| `TIM3->CR2` | ADC1 CFGR1 trigger | `TIM_CR2_MMS_1` (hardware TRGO line) | WIRED | `tim3.c:30` sets `TIM_CR2_MMS_1`; `adc.c:38-40` sets `EXTSEL_0\|EXTSEL_1` (EXTSEL=011=TIM3_TRGO) |
| `ADC_IRQHandler` | `g_adc_ready` | EOC interrupt stores DR into volatile globals | WIRED | `adc.c:69-74`: ISR sets `g_adc_sample` and `g_adc_ready=1` on EOC |
| `ADC_IRQHandler` → `g_adc_ready` | `main.c` print loop | `g_adc_ready` polled in `while(1)` | WIRED | `main.c:29-35`: `if (g_adc_ready)` → clear flag → `uart_write_str` + `uart_write_u32` |
| `main.c` | `uart_write_str` / `uart_write_u32` | Polling TX on TXE flag (existing usart.c API) | WIRED | `main.c:32-34`: both functions called; `#include "usart.h"` present |

---

## Data-Flow Trace (Level 4)

| Artifact | Data Variable | Source | Produces Real Data | Status |
|----------|---------------|--------|--------------------|--------|
| `Src/main.c` (print loop) | `g_adc_sample` | `ADC_IRQHandler` ← `ADC1->DR` ← PA0 hardware | Yes — hardware ADC reading confirmed at full-scale (4095) and zero (0) on silicon | FLOWING |
| `ADC_IRQHandler` | `ADC1->DR` | TIM3 TRGO hardware trigger at 100 Hz | Yes — trigger chain confirmed by 100 Hz output rate on hardware | FLOWING |

---

## Behavioral Spot-Checks

Hardware verification was performed directly on silicon (not emulation). Results provided by developer as human checkpoint pass.

| Behavior | Method | Result | Status |
|----------|--------|--------|--------|
| Serial banner appears on reset | Serial terminal at 115200 baud | "--- HeartRateSensor Phase 2 ---" visible | PASS |
| 100 Hz output rate | Count "adc:" lines per second | ~100 lines/sec, no bursts or gaps | PASS |
| 12-bit high-end range | PA0 jumpered to 3.3V | 4095 (exact full scale) | PASS |
| 12-bit low-end range | PA0 jumpered to GND | 0 (exact zero) | PASS |
| ISR vector resolved correctly | Commit a9a3b10 renamed to `ADC_IRQHandler` | ISR fires — confirmed by output | PASS |

---

## Requirements Coverage

| Requirement | Source Plan | Description (from REQUIREMENTS.md) | Status | Evidence |
|-------------|-------------|--------------------------------------|--------|---------|
| DRV-03 | 02-01, 02-02 | ADC1 samples PA0 at exactly 100 Hz via hardware trigger; HSI14 ADC clock; mandatory ADCAL at startup | SATISFIED | `adc.c` implements HSI14, ADCAL, EXTSEL=011 (TIM3_TRGO), EOC ISR; human verification confirms 100 Hz rate |
| DRV-04 | 02-01, 02-02 | TIM3 generates Update TRGO at 100 Hz (PSC=79, ARR=999 from 8 MHz HSI on F070) | SATISFIED | `tim3.c:24,27,30`: PSC=79, ARR=999, TIM_CR2_MMS_1; 100 Hz output confirmed on hardware |

**Note on REQUIREMENTS.md wording:** DRV-03 and DRV-04 still reference F103 values (TIM2, ADCPRE=/6, PSC=719, RSTCAL+CAL). The actual implementation correctly uses F070 equivalents: TIM3, HSI14, PSC=79, single-step ADCAL. This deviation is correct — the part was migrated to STM32F070RB in Phase 1 and the REQUIREMENTS.md was not updated to reflect F070 register values. The functional intent (100 Hz hardware-triggered ADC with mandatory calibration) is fully satisfied.

---

## Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| `Src/adc.c` | 10, 64 | Comment says `ADC1_IRQHandler` but function is `ADC_IRQHandler` | INFO | Stale comment only — function name is correct (the fix in commit a9a3b10 updated the function but not all comments); no runtime impact |

No TBD, FIXME, XXX, or placeholder markers found in phase files. No F103 register names (TIM2, SQR3, SMPR2, ADC1_2_IRQn, RSTCAL, ADC_SR_EOC) found in any new or modified file. No stdlib, printf, or sprintf in any modified file.

---

## Human Verification Required

None. All human verification was completed at the 02-02 checkpoint gate before this verification was requested. Results are recorded above under Behavioral Spot-Checks.

---

## Gaps Summary

No gaps. All 9 observable truths verified, all 5 required artifacts are substantive and wired, all key links confirmed, both requirements satisfied. Hardware confirmed on real silicon (STM32F070RB Nucleo).

The only notable item is stale comments in `adc.c` (lines 10, 64) that still say `ADC1_IRQHandler` after the function was renamed to `ADC_IRQHandler`. This is documentation drift with zero runtime impact and does not block the phase goal.

---

_Verified: 2026-05-19_
_Verifier: Claude (gsd-verifier)_
