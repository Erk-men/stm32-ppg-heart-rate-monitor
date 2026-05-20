# Phase 4: Full Integration + Analog Calibration — Research

**Researched:** 2026-05-20
**Domain:** STM32F070RB bare-metal C, PPG analog signal chain, firmware integration, empirical threshold calibration
**Confidence:** HIGH — all findings derived from the verified codebase, CLAUDE.md register reference, and Phase 1–3 verified artifacts

---

## Summary

Phase 4 is a pure integration and calibration phase. All peripheral drivers (SysTick, USART2, TIM3, ADC1) and all algorithm modules (moving average, adaptive threshold, refractory, bounds check, rolling average) are fully implemented and code-verified in Phases 1–3. The task is to remove the `SYNTHETIC_TEST` compile gate, enable the live ADC path, and then empirically tune three threshold constants against a real LM358 PPG signal from a finger.

The phase also adds two new compile-time diagnostic modes — `CALIBRATION_MODE` and `DEBUG_VERBOSE` — neither of which exists in the current codebase. These require new code in `algorithm.c`/`main.c`, and their output format is precisely specified by OUT-03 and OUT-04. No new drivers or peripheral configuration is needed; the integration work is entirely at the `main.c` and `algorithm.c` layer.

The most significant risk is the LM358 analog signal quality. On a 3.3V supply the LM358 saturates at approximately 1.8V (~2200 ADC counts), but the actual PPG AC amplitude from a finger will be much smaller — typically 10–100 counts of AC swing on a DC pedestal. If the AC amplitude is below `FINGER_MIN_AMPLITUDE`, the no-finger path fires. The CALIBRATION_MODE startup window exists specifically to measure actual hardware amplitude so the constant can be set correctly before the algorithm runs. The op-amp supply can be switched to 5V USB rail if amplitude is too low (noted in STATE.md accumulated context).

There is also one critical bug in the existing firmware that must be fixed as part of Phase 4: `systick.c` configures `SysTick_Config(7999)` which corresponds to 1ms at 8MHz HSI, but the CLAUDE.md specification and TIM3 timer math show the project is designed for 48MHz SYSCLK (PCLK = 48MHz). The TIM3 `tim3_init()` currently uses PSC=79/ARR=999 which also computes to 100Hz at 8MHz — consistent with the current HSI clock. The USART2 BRR is set to `0x45` for 8MHz. The comment in `usart.c` explicitly states "When PLL is enabled in Phase 4+ (SYSCLK=48MHz), update BRR to 0x1A1 (417)". **Phase 4 is the point where the PLL must be enabled** if 48MHz operation is required, which would require updating SysTick LOAD, TIM3 PSC/ARR, and USART2 BRR. However, 8MHz HSI is sufficient for the lab demo (as documented in STATE.md), so the safest Phase 4 plan keeps the clock at 8MHz HSI to avoid introducing a new risk. All math in the codebase is already calibrated for 8MHz.

**Primary recommendation:** Phase 4 is three sequential tasks: (1) remove SYNTHETIC_TEST and verify live ADC feeds algorithm, (2) implement CALIBRATION_MODE startup window and DEBUG_VERBOSE per-sample output in algorithm.c, (3) empirically measure ADC amplitude with finger on sensor and lock in `FINGER_MIN_AMPLITUDE` and initial threshold constants. No new files, no new drivers, no clock changes required.

---

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| Live ADC sample routing | main.c integration layer | adc.c ISR (already complete) | main.c loop consumes g_adc_ready/g_adc_sample and calls algorithm_process(); the ADC driver itself is complete |
| CALIBRATION_MODE startup window | main.c (timing loop) | algorithm.c (min/max tracking) | 5-second window before algorithm starts; min/max/amplitude computed over 500 samples (100Hz × 5s); output via uart_write_str/uart_write_u32 |
| DEBUG_VERBOSE per-sample output | algorithm.c | — | algorithm_process() already owns RAW, FILT, THR, AMP, STATE values; adding #ifdef DEBUG_VERBOSE print block requires no new globals |
| No-finger detection (OUT-02) | algorithm.c | — | Amplitude tracking already in algorithm_process scope; add amplitude < FINGER_MIN_AMPLITUDE check and reset + "--\r\n" print |
| Threshold constant empirical tuning | Physical calibration step | — | Human measurement only — cannot be automated; CALIBRATION_MODE output drives this decision |
| BPM output (OUT-01) | algorithm.c (already complete) | — | uart_write_str("BPM: ") / uart_write_u32(bpm_avg) / uart_write_str("\r\n") path is already verified |

---

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| OUT-01 | BPM value printed over USART2 each time a heartbeat is detected ("BPM: NN\r\n") | Already implemented in algorithm.c PEAK_HOLD branch. Phase 4 activates it by removing SYNTHETIC_TEST. No code change needed for OUT-01 itself. |
| OUT-02 | No-finger detection: when amplitude < FINGER_MIN_AMPLITUDE, print "--\r\n" and reset adaptive threshold | Not yet implemented. Requires amplitude tracking variable added to algorithm_process() and a periodic check. Reset means: s_threshold = 1200 (initial value), s_state = IDLE, s_bpm_count = 0. |
| OUT-03 | CALIBRATION_MODE: prints ADC min/max/amplitude over 5 seconds at startup | Not yet implemented. New startup loop in main.c tracking min/max over 500 samples (5s at 100Hz), then printing results before entering the main while(1). |
| OUT-04 | DEBUG_VERBOSE: prints per-sample RAW / FILT / THR / AMP / STATE fields | Not yet implemented. New #ifdef DEBUG_VERBOSE block inside algorithm_process() printing all five fields every sample. At 100Hz this is very verbose — only used for capture sessions, not continuous operation. |
</phase_requirements>

---

## Project Constraints (from CLAUDE.md)

All directives from CLAUDE.md apply without exception:

- **Bare-metal CMSIS only**: `#include "stm32f0xx.h"`, direct register writes. No HAL, no LL, no Arduino, no RTOS.
- **No FPU**: STM32F070RB is Cortex-M0, no hardware FPU. All arithmetic must be integer-only. No `float` or `double` anywhere — they compile via soft-float library (large, slow, disallowed).
- **USART registers**: F0 uses `ISR` (status) and `TDR` (transmit data), not `SR`/`DR`.
- **GPIO on AHB**: `RCC->AHBENR |= RCC_AHBENR_GPIOAEN` — not APB2ENR.
- **ADC1_IRQn**: F070 has no ADC2; single vector, no shared-vector check needed.
- **USART2 BRR**: Currently `0x45` (69) for 8MHz HSI. If clock switches to 48MHz PLL, update to `0x1A1` (417). Phase 4 recommendation: keep 8MHz to avoid introducing new risk.
- **No stdlib / no printf**: Only `uart_write_str` + `uart_write_u32` for serial output.
- **No dynamic memory**: All buffers static, fixed-size.
- **Fixed component list**: LM358, BPW34, 940nm IR LED — no substitutions.
- **Development order**: Real hardware signal required for calibration — can only finalize threshold constants after bench measurement.

---

## Standard Stack

### Core (already established, no new additions)

| Library/File | Version/State | Purpose | Notes |
|-------------|---------------|---------|-------|
| stm32f0xx.h | CMSIS, part of CubeIDE device pack | All peripheral register definitions | Already used in all driver files |
| systick.c/h | Phase 1, verified | millis() at 1ms resolution | SysTick_Config(7999) for 8MHz HSI |
| usart.c/h | Phase 1, verified | uart_write_str / uart_write_u32 | BRR=0x45 for 8MHz; update to 0x1A1 if PLL enabled |
| tim3.c/h | Phase 2, verified | TIM3 100Hz TRGO — PSC=79, ARR=999 at 8MHz | Autonomous, no changes needed |
| adc.c/h | Phase 2, verified | ADC1 hardware-triggered ISR, g_adc_sample / g_adc_ready | No changes needed |
| algorithm.c/h | Phase 3, verified | All 5 SIG modules | Phase 4 adds CALIBRATION_MODE and DEBUG_VERBOSE blocks |

### New Code in Phase 4

| Location | What Gets Added | Why |
|----------|----------------|-----|
| `main.c` | Remove `#define SYNTHETIC_TEST` | Activates live ADC path |
| `main.c` | Add `#define CALIBRATION_MODE` (initially defined, remove for production) | Enables startup amplitude measurement |
| `main.c` | CALIBRATION_MODE startup loop (500 samples, 5s, min/max/amplitude print) | OUT-03 |
| `algorithm.c` | `#define FINGER_MIN_AMPLITUDE` constant | Empirically determined; gates OUT-02 |
| `algorithm.c` | Amplitude tracking variable + no-finger check + `"--\r\n"` + threshold reset | OUT-02 |
| `algorithm.c` | `#ifdef DEBUG_VERBOSE` per-sample print block | OUT-04 |
| `algorithm.h` | Possibly expose `FINGER_MIN_AMPLITUDE` if needed externally | Only if CALIBRATION_MODE needs it |

**No new files, no new drivers, no new peripherals.**

---

## Architecture Patterns

### System Architecture Diagram

```
[Finger on sensor]
       |
       v
[IR LED → BPW34 photodiode → LM358 (TIA + HPF + ×100 gain + LPF)]
       |
       v (analog voltage at PA0, 0–1.8V max on 3.3V supply)
[ADC1 ISR — fires on TIM3 TRGO at 100Hz]
       | g_adc_sample / g_adc_ready
       v
[main.c while(1) — live path: if (g_adc_ready)]
       |
       +--- [CALIBRATION_MODE path: first 5s only]
       |         min/max tracking over 500 samples
       |         print "CAL: min=NNN max=NNN amp=NNN\r\n"
       |         decision: amplitude > FINGER_MIN_AMPLITUDE? proceed / warn
       |
       v [after calibration window, or immediately if CALIBRATION_MODE not defined]
[algorithm_process(g_adc_sample)]
       |
       +--- [32-sample moving average → filtered value]
       |
       +--- [amplitude tracking: (max_recent - min_recent)]
       |         if amplitude < FINGER_MIN_AMPLITUDE:
       |             print "--\r\n"
       |             reset s_threshold, s_state, s_bpm_count
       |             (continue loop, no peak detection this cycle)
       |
       +--- [5-state peak detector: IDLE→RISING→PEAK_HOLD→FALLING→REFRACTORY]
       |         peak registered in PEAK_HOLD:
       |             update adaptive threshold (last_peak × 3/5)
       |             compute BPM = 60000 / interval_ms
       |             bounds check (40–200)
       |             rolling average (5-entry)
       |             print "BPM: NN\r\n"    [OUT-01]
       |
       +--- [#ifdef DEBUG_VERBOSE]
                 every sample: print "RAW:NNN FILT:NNN THR:NNN AMP:NNN STATE:N\r\n"
                 [OUT-04]
```

### Recommended File Touch List

Only two files need modification:

```
Src/
├── main.c          # Remove SYNTHETIC_TEST; add CALIBRATION_MODE startup window
└── algorithm.c     # Add FINGER_MIN_AMPLITUDE, no-finger detection, DEBUG_VERBOSE output
```

No new files. No changes to adc.c, tim3.c, usart.c, systick.c.

### Pattern 1: Live ADC Activation (Remove SYNTHETIC_TEST)

**What:** Delete the `#ifndef SYNTHETIC_TEST` / `#define SYNTHETIC_TEST` / `#endif` guard lines at the top of main.c. This causes the preprocessor to take the `#else` branch (live ADC path) in the while(1) loop.

**Current state in main.c:**
```c
#ifndef SYNTHETIC_TEST
#define SYNTHETIC_TEST
#endif
// ...
#ifdef SYNTHETIC_TEST
    /* synthetic table path */
#else
    if (g_adc_ready)
    {
        g_adc_ready = 0;
        algorithm_process((uint16_t)g_adc_sample);
    }
#endif
```

**After removal:**
```c
// SYNTHETIC_TEST define removed — live ADC path active
// ...
// Only the else branch remains; can simplify to:
if (g_adc_ready)
{
    g_adc_ready = 0;
    algorithm_process((uint16_t)g_adc_sample);
}
```

Also remove tim3_init() / adc_init() from inside `#ifndef SYNTHETIC_TEST` — they must be called unconditionally in the live path.

[VERIFIED: Src/main.c lines 58-60, 70-88]

### Pattern 2: CALIBRATION_MODE Startup Window

**What:** Before entering while(1), run a 5-second measurement loop that samples the ADC 500 times (one per 10ms, matching the live 100Hz rate), tracks min and max, computes amplitude, and prints results.

**Design:**
```c
#ifdef CALIBRATION_MODE
{
    uint16_t cal_min = 4095;
    uint16_t cal_max = 0;
    uint32_t cal_count = 0;
    uint32_t cal_start = millis();

    uart_write_str("CALIBRATION_MODE: sampling 5s...\r\n");

    while ((millis() - cal_start) < 5000UL)
    {
        if (g_adc_ready)
        {
            g_adc_ready = 0;
            uint16_t s = g_adc_sample;
            if (s < cal_min) cal_min = s;
            if (s > cal_max) cal_max = s;
            cal_count++;
        }
    }

    uint16_t cal_amp = (cal_max > cal_min) ? (cal_max - cal_min) : 0;
    uart_write_str("CAL min=");   uart_write_u32(cal_min);
    uart_write_str(" max=");      uart_write_u32(cal_max);
    uart_write_str(" amp=");      uart_write_u32(cal_amp);
    uart_write_str(" n=");        uart_write_u32(cal_count);
    uart_write_str("\r\n");
    /* Human reads amp value and sets FINGER_MIN_AMPLITUDE accordingly */
}
#endif /* CALIBRATION_MODE */
```

**Notes:**
- No float. All integer. uart_write_str / uart_write_u32 only. [VERIFIED: pattern from CLAUDE.md §USART2]
- cal_amp subtraction: uint16 subtraction where cal_max >= cal_min is always non-negative — safe.
- The 5-second window must come AFTER `adc_init()` and TIM3 are running (ADC needs TIM3 TRGO to fire).
- `millis()` at 8MHz with SysTick_Config(7999) provides the 5-second window correctly. [VERIFIED: Src/systick.c]

[ASSUMED] The 5-second window will capture enough heartbeats (5–8 at 60–80 BPM) for meaningful min/max. If the finger is removed mid-window the amplitude will be small (low signal), which is exactly the correct diagnostic output.

### Pattern 3: No-Finger Detection and Threshold Reset (OUT-02)

**What:** algorithm_process() must track recent signal amplitude and detect when no finger is present (amplitude below FINGER_MIN_AMPLITUDE). On detection: print "--\r\n", reset adaptive threshold to initial value, reset state to IDLE.

**Design approach:** Track a rolling amplitude estimate using the moving average's own characteristics. The simplest robust approach for bare-metal with no float is to maintain a slow-updating min/max window alongside the existing moving average.

**Option A — Secondary amplitude window (recommended):**
```c
/* Amplitude tracking: 128-sample window (1.28s) for no-finger detection */
#define AMP_WINDOW_MS  1280U   /* check amplitude every 1280ms */
static uint16_t s_amp_min = 4095;
static uint16_t s_amp_max = 0;
static uint32_t s_amp_window_start = 0;

/* Inside algorithm_process(), after computing filtered: */
if (sample < s_amp_min) s_amp_min = sample;
if (sample > s_amp_max) s_amp_max = sample;

uint32_t now_amp = millis();
if ((now_amp - s_amp_window_start) >= AMP_WINDOW_MS)
{
    uint16_t amplitude = (s_amp_max > s_amp_min) ? (s_amp_max - s_amp_min) : 0;
    s_amp_window_start = now_amp;
    s_amp_min = 4095;
    s_amp_max = 0;

    if (amplitude < FINGER_MIN_AMPLITUDE)
    {
        /* No finger detected — reset and signal absence */
        uart_write_str("--\r\n");
        /* Reset adaptive threshold and state */
        s_threshold = 1200;   /* initial threshold value */
        s_state = IDLE;
        s_peak_val = 0;
        s_bpm_count = 0;    /* clear rolling average to avoid stale BPM */
    }
}
```

**FINGER_MIN_AMPLITUDE initial value:** 200 counts is a reasonable conservative starting point for a 12-bit ADC measuring an LM358 PPG with ×100 gain (documented in CLAUDE.md as ~2200 counts max). The actual value must be determined from CALIBRATION_MODE output. [ASSUMED — will be adjusted empirically]

**The 2-second requirement (SC-3):** The amplitude window of 1280ms means the "--\r\n" prints within ~1.28 seconds of finger removal at worst, satisfying the "within 2 seconds" criterion.

[VERIFIED: amplitude reset pattern consistent with algorithm_init() in Src/algorithm.c lines 33-51]

### Pattern 4: DEBUG_VERBOSE Per-Sample Output (OUT-04)

**What:** Every sample, print five labeled fields: RAW, FILT, THR, AMP, STATE.

**Design:**
```c
#ifdef DEBUG_VERBOSE
{
    /* Print per-sample diagnostic fields */
    /* State encoding: 0=IDLE,1=RISING,2=PEAK_HOLD,3=FALLING,4=REFRACTORY */
    uart_write_str("RAW:");  uart_write_u32(sample);
    uart_write_str(" FILT:"); uart_write_u32(filtered);
    uart_write_str(" THR:");  uart_write_u32(s_threshold);
    uart_write_str(" AMP:");  uart_write_u32(amplitude);  /* needs amplitude in scope */
    uart_write_str(" ST:");   uart_write_u32((uint32_t)s_state);
    uart_write_str("\r\n");
}
#endif
```

**Timing concern:** At 100Hz, DEBUG_VERBOSE generates 100 lines/second. Each line is approximately 35 characters. At 115200 baud (at 8MHz HSI with BRR=0x45), the raw bit rate allows ~11520 bytes/second, which is ~330 characters/sample interval. 35 chars is well within budget — polling TX will complete before the next ADC sample arrives 10ms later. [VERIFIED: uart_write_str polling pattern in Src/usart.c; math: 35 chars × ~87µs/char = ~3ms, well within 10ms sample window]

**AMP in scope:** DEBUG_VERBOSE block must come after the amplitude tracking window computation, or a local amplitude variable must be computed. The cleanest approach is to compute amplitude as a local variable at the top of algorithm_process() using the window logic, then reference it in both the no-finger check and the DEBUG_VERBOSE block.

**STATE as integer:** Printing `(uint32_t)s_state` gives 0–4. This is acceptable for per-sample capture (verbose enough to trace threshold crossings). The human-readable names are handled by the existing `DEBUG_STATE` output on transitions.

### Anti-Patterns to Avoid

- **Enabling DEBUG_VERBOSE in production run**: At 100 lines/second, the serial buffer will fill if the host is not reading fast enough, and the polling TX will block the main loop. Always undefine DEBUG_VERBOSE before the 30-second accuracy capture for OUT-01.
- **Mixing CALIBRATION_MODE with DEBUG_VERBOSE simultaneously**: CALIBRATION_MODE runs before the main loop; DEBUG_VERBOSE runs inside algorithm_process(). They can coexist, but CALIBRATION_MODE output will precede any DEBUG_VERBOSE output, which is the correct order.
- **Forgetting to call tim3_init() / adc_init() unconditionally after removing SYNTHETIC_TEST**: The current main.c gates these inside `#ifndef SYNTHETIC_TEST`. When the gate is removed, these init calls must be made unconditional. If they are left commented out or missing, no TIM3 TRGO fires and g_adc_ready never sets — the algorithm starves silently.
- **Using float for amplitude calculation**: `(cal_max - cal_min)` is an integer subtraction. No float needed anywhere in CALIBRATION_MODE or amplitude tracking. [VERIFIED: CLAUDE.md no-FPU constraint]
- **Printing CALIBRATION_MODE output before ADC is running**: CALIBRATION_MODE loop must come after `adc_init()` and `tim3_init()` in the init sequence. Currently both are gated inside `#ifndef SYNTHETIC_TEST` — they must be made unconditional first.
- **State not resetting on no-finger detection**: If `s_state` is left in REFRACTORY or FALLING when the finger is removed, re-acquisition fails. The reset must set `s_state = IDLE` explicitly.

---

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Baud rate calculation | Custom BRR formula | BRR=0x45 (8MHz) from CLAUDE.md | Already computed and verified in hardware; recalculating risks error |
| ADC trigger chain | Software polling loop | Existing TIM3 TRGO → ADC1 hardware trigger | Already verified in Phase 2; hardware trigger eliminates jitter |
| Amplitude smoothing | Complex FIR/IIR amplitude estimator | Simple min/max window over 1.28s | PPG signal at 60–120 BPM has period 500–1000ms; 1.28s window captures 1–2 full cycles |
| Number-to-string conversion | Custom itoa | `uart_write_u32()` in usart.c | Already implemented, tested, no stdlib dependency |
| Time measurement | Hardware timer compare | `millis()` via SysTick | Already running; atomic 32-bit read on Cortex-M0 |
| FINGER_MIN_AMPLITUDE value | Guessing from spec sheets | CALIBRATION_MODE empirical measurement | LM358 gain varies with component tolerances; only hardware measurement is reliable |

---

## Common Pitfalls

### Pitfall 1: CALIBRATION_MODE Runs Before ADC Is Initialized
**What goes wrong:** If CALIBRATION_MODE loop runs before `adc_init()` / `tim3_init()`, `g_adc_ready` never sets, the loop spins for 5 seconds seeing zero samples (cal_count=0), and reports cal_amp=0.
**Why it happens:** The current main.c gates `tim3_init()` / `adc_init()` inside `#ifndef SYNTHETIC_TEST`. Removing the gate without moving the calls to before the CALIBRATION_MODE block causes this.
**How to avoid:** Ensure call order: `systick_init()` → `usart2_init()` → `tim3_init()` → `adc_init()` → `algorithm_init()` → `CALIBRATION_MODE block` → `while(1)`.
**Warning signs:** CALIBRATION_MODE prints `n=0` or `amp=0` even with finger on sensor.

### Pitfall 2: Threshold Reset Does Not Clear Rolling Average
**What goes wrong:** After finger removal and re-acquisition, the first few valid BPM readings are averaged with stale values from the previous session, producing a wrong BPM for the first 5 beats.
**Why it happens:** `s_bpm_count` is not reset to 0 in the no-finger reset path.
**How to avoid:** Reset path must set `s_bpm_count = 0` and `s_bpm_idx = 0` (or at minimum `s_bpm_count = 0` to force re-fill).
**Warning signs:** BPM output after re-acquisition shows a value that is an average of old and new readings.

### Pitfall 3: FINGER_MIN_AMPLITUDE Set Too High
**What goes wrong:** Algorithm enters no-finger mode even with a finger on the sensor because the PPG AC amplitude is legitimately small (weak signal from thin finger, poor contact, or low LM358 gain).
**Why it happens:** Starting constant is guessed rather than measured.
**How to avoid:** Always run CALIBRATION_MODE first. Set `FINGER_MIN_AMPLITUDE` to approximately 50% of the observed amplitude with a good finger placement. Start conservatively low (e.g., 50 counts) and raise if false positives occur.
**Warning signs:** `--\r\n` prints intermittently even with steady finger pressure.

### Pitfall 4: LM358 on 3.3V Supply — Output Saturated or Rail-Limited
**What goes wrong:** LM358 output can only swing to approximately 1.8V on a 3.3V supply. If the biasing places the DC operating point near 1.8V and the AC signal pushes it higher, the waveform clips at the top, distorting the peak shape and causing the adaptive threshold to update to a clipped value.
**Why it happens:** LM358 is not a rail-to-rail op-amp. Output swing is limited to Vcc − 1.5V typical.
**How to avoid:** CALIBRATION_MODE output reveals saturation: `cal_max` will be approximately 2200 counts (1.8V / 3.3V × 4095 ≈ 2234) with no headroom variation. Switching op-amp supply to 5V USB rail (as documented in STATE.md) raises the ceiling to approximately 3.5V output, giving more headroom. [VERIFIED: STATE.md accumulated context, CLAUDE.md LM358 saturation note]
**Warning signs:** CALIBRATION_MODE shows cal_max = 2200 ± 20 counts flat (saturated), or cal_min/cal_max bounce between only two stable values.

### Pitfall 5: DEBUG_VERBOSE Blocking the Main Loop
**What goes wrong:** At 100Hz, ~35 characters/line × 8.68µs/char (at 115200 baud, 8MHz HSI) = ~3ms/line. The ADC fires every 10ms. So ~3ms is fine. But if the serial monitor host cannot drain the UART buffer fast enough and the host-side buffer fills, the host will not be the problem — the STM32 side uses polling TX, which completes when the hardware FIFO drains, not when the host reads. So this is safe as long as each poll completes within 10ms, which it does (3ms << 10ms).
**Why it matters:** This is a concern to CHECK, not a confirmed problem.
**How to avoid:** Verify that total DEBUG_VERBOSE output per sample fits within 10ms TX budget. At 8MHz HSI BRR=0x45 (115200 baud): 10 bits/byte ÷ 115200 = 86.8µs/byte. 35 bytes × 86.8µs = 3.04ms. Safe margin: 10ms − 3.04ms = 6.96ms remaining for algorithm execution.
**Warning signs:** BPM readings drift high (missed peaks due to algorithm starving of samples) when DEBUG_VERBOSE is enabled.

### Pitfall 6: SYNTHETIC_TEST ifdef Chain Left Partially Removed
**What goes wrong:** Removing only the top-of-file `#ifndef SYNTHETIC_TEST / #define / #endif` block while leaving the `#ifdef SYNTHETIC_TEST ... #else ... #endif` in while(1) intact means the live ADC path is now the `#else` branch of a conditional that evaluates `#ifdef SYNTHETIC_TEST` — which is now undefined — so the `#else` (live) branch IS taken. This is actually correct behavior. However, leaving the dead `#ifdef SYNTHETIC_TEST ... sine_table ... #endif` block in main.c wastes flash and clutters the code.
**How to avoid:** Remove the entire SYNTHETIC_TEST conditional block from while(1), keeping only the live ADC path. Remove the sine_table definition. This is cleaner and reduces binary size.
**Warning signs:** Binary size does not decrease after removing SYNTHETIC_TEST (dead code still compiled in).

---

## Code Examples

### Complete Phase 4 main.c Structure (Target State)

```c
/* Phase 4: live ADC path — SYNTHETIC_TEST removed */
/* #define CALIBRATION_MODE    */  /* uncomment for startup amplitude measurement */
/* #define DEBUG_VERBOSE       */  /* uncomment for per-sample serial capture */
#ifndef DEBUG_STATE
#define DEBUG_STATE
#endif
#include "stm32f0xx.h"
#include "systick.h"
#include "usart.h"
#include "tim3.h"
#include "adc.h"
#include "algorithm.h"

int main(void)
{
    SystemInit();
    systick_init();
    usart2_init();
    tim3_init();     /* unconditional — no longer gated on SYNTHETIC_TEST */
    adc_init();      /* unconditional */
    algorithm_init();

    uart_write_str("\r\n--- HeartRateSensor Phase 4 ---\r\n");

#ifdef CALIBRATION_MODE
    /* ... calibration window (5s, 500 samples) ... */
    /* ... print CAL: min=NNN max=NNN amp=NNN n=NNN\r\n ... */
#endif

    while (1)
    {
        if (g_adc_ready)
        {
            g_adc_ready = 0;
            algorithm_process((uint16_t)g_adc_sample);
        }
    }
}
```
[Source: Src/main.c — live path already in #else branch, lines 83–87]

### algorithm.c additions: FINGER_MIN_AMPLITUDE and no-finger check

```c
/* Tune FINGER_MIN_AMPLITUDE from CALIBRATION_MODE output.
 * Start at 50 counts (conservative); raise if "--" fires with finger present. */
#define FINGER_MIN_AMPLITUDE  50U

/* amplitude tracking (static, file-scope) */
static uint16_t s_amp_min = 4095;
static uint16_t s_amp_max = 0;
static uint32_t s_amp_win_start = 0;

/* Inside algorithm_process(), after computing filtered: */

/* Track raw sample min/max for amplitude estimation */
if (sample < s_amp_min) s_amp_min = sample;
if (sample > s_amp_max) s_amp_max = sample;

uint32_t now = millis();
if ((now - s_amp_win_start) >= 1280UL)   /* 1280ms window = ~1-2 heartbeat cycles */
{
    uint16_t amplitude = (s_amp_max > s_amp_min) ? (uint16_t)(s_amp_max - s_amp_min) : 0;
    s_amp_win_start = now;
    s_amp_min = 4095;
    s_amp_max = 0;

    if (amplitude < FINGER_MIN_AMPLITUDE)
    {
        uart_write_str("--\r\n");
        /* Reset algorithm state for clean re-acquisition */
        s_threshold = 1200;
        s_state     = IDLE;
        s_peak_val  = 0;
        s_bpm_count = 0;
        s_bpm_idx   = 0;
    }
}
```
[Source: consistent with algorithm_init() in Src/algorithm.c lines 33-51; no float; integer arithmetic only]

### algorithm.c additions: DEBUG_VERBOSE

```c
#ifdef DEBUG_VERBOSE
    /* Per-sample diagnostic dump — enable only for serial captures */
    uint16_t amp_display = (s_amp_max > s_amp_min) ? (uint16_t)(s_amp_max - s_amp_min) : 0;
    uart_write_str("RAW:");   uart_write_u32(sample);
    uart_write_str(" FILT:"); uart_write_u32(filtered);
    uart_write_str(" THR:");  uart_write_u32(s_threshold);
    uart_write_str(" AMP:");  uart_write_u32(amp_display);
    uart_write_str(" ST:");   uart_write_u32((uint32_t)s_state);
    uart_write_str("\r\n");
#endif /* DEBUG_VERBOSE */
```
[Source: CLAUDE.md uart_write_str/uart_write_u32 pattern; no printf; no float]

---

## Runtime State Inventory

> Phase 4 is an integration and calibration phase — no rename or migration involved. This section documents the one meaningful piece of mutable state that affects Phase 4 specifically.

| Category | Items Found | Action Required |
|----------|-------------|------------------|
| Stored data | No databases or datastores used. All state is volatile MCU RAM, reset on power cycle. | None |
| Live service config | No external services. Standalone embedded firmware. | None |
| OS-registered state | No OS-level registrations. | None |
| Secrets/env vars | None. | None |
| Build artifacts | STM32CubeIDE project: `.elf` / `.bin` / `.map` rebuilt on each compile. Old binaries in `Debug/` directory are stale after Phase 4 edits. | Recompile and flash; Debug/ artifacts are auto-regenerated. |
| **Algorithm threshold constants** | `s_threshold = 1200` (initial hardcoded), `REFRACTORY_MS = 350U`, `FINGER_MIN_AMPLITUDE` (not yet defined — must be added) | `FINGER_MIN_AMPLITUDE` must be empirically determined from CALIBRATION_MODE output and locked in as a `#define` in algorithm.c |

**The one blocking constant:** `FINGER_MIN_AMPLITUDE` does not yet exist in the codebase. It must be added to algorithm.c before OUT-02 can work. Its numeric value cannot be determined without running CALIBRATION_MODE on real hardware. The plan must sequence: implement CALIBRATION_MODE first → flash → measure → add `FINGER_MIN_AMPLITUDE` constant → implement no-finger check.

---

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| STM32CubeIDE + ST-Link | Flash and debug firmware | Expected available (used in Phases 1–3) | 1.15+ | None — required for hardware |
| Nucleo-F070RB board | All hardware verification | Expected available (Phase 3 verified on hardware) | — | None |
| LM358 + BPW34 + 940nm IR LED | Analog signal chain | Expected assembled (Phase 3 hardware verified) | — | None |
| Serial monitor (115200 baud) | CALIBRATION_MODE output, DEBUG_VERBOSE capture | Available (used in Phases 1–3) | — | Any terminal emulator (PuTTY, minicom, CubeIDE console) |
| Reference pulse oximeter | SC-1 accuracy verification (±5 BPM) | [ASSUMED] available for comparison | — | Smartphone heart rate app as fallback (less accurate) |

**Missing dependencies with no fallback:**
- STM32CubeIDE + ST-Link: required to flash. No software fallback.

**Missing dependencies with fallback:**
- Reference pulse oximeter: smartphone HR app is acceptable for ±5 BPM comparison at course demo level.

---

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| `#define SYNTHETIC_TEST` active | Remove guard, live ADC path | Phase 4 | Activates the real hardware signal path |
| tim3_init/adc_init gated on `#ifndef SYNTHETIC_TEST` | Both called unconditionally | Phase 4 | Peripherals now always initialized |
| No amplitude tracking | Rolling min/max window, 1280ms | Phase 4 | Enables OUT-02 no-finger detection |
| No startup diagnostic | CALIBRATION_MODE 5s window | Phase 4 | OUT-03: provides hard evidence for report |
| No per-sample dump | DEBUG_VERBOSE 5-field output | Phase 4 | OUT-04: serial capture for report |
| `USART2->BRR = 0x45` (8MHz HSI) | Remains 0x45 unless PLL added | Phase 4 does not change clock | Safe; keeping 8MHz reduces risk |

**Deprecated:**
- The `#ifdef SYNTHETIC_TEST ... sine_table ... #endif` block in main.c: dead code after Phase 4. Remove entirely for cleanliness.
- The `DEBUG_STATE` define at the top of algorithm.c (line 1): this define is currently hardcoded inside the file (`#define DEBUG_STATE` on line 1) rather than being toggled from main.c. For Phase 4, it should either be removed from algorithm.c (making it controllable from main.c like SYNTHETIC_TEST) or left as-is for Phase 4 debugging and removed when finalizing for Phase 5. The Phase 3 CONTEXT.md (D-04) says it persists through Phase 4.

---

## Critical Pre-Planning Finding: Clock Discrepancy

The current firmware runs at 8MHz HSI (not 48MHz PLL). Evidence:

- `systick.c`: `SysTick_Config(7999)` = 1ms at 8MHz (not 47999 for 48MHz)
- `tim3.c`: PSC=79, ARR=999 = 100Hz at 8MHz (not PSC=479, ARR=999 for 48MHz per CLAUDE.md)
- `usart.c`: BRR=0x45 (69) = 115200 at 8MHz; comment says "When PLL is enabled in Phase 4+, update BRR to 0x1A1 (417)"

**Implication for Phase 4:** The firmware already works end-to-end at 8MHz. The CLAUDE.md documents both the 48MHz target (PLL) and the 8MHz current state (HSI, "SystemInit stub"). The usart.c comment explicitly defers PLL enablement to "Phase 4+".

**Recommendation:** Do NOT enable the PLL in Phase 4 unless it becomes necessary. Enabling the PLL requires updating SysTick LOAD, TIM3 PSC/ARR, and USART2 BRR atomically — three register writes that could introduce new bugs at the worst possible time (Phase 4 has real hardware signal and threshold calibration to validate). The system works correctly at 8MHz for the lab demo. Phase 5 report can document the clock configuration as-is.

If PLL is desired for final accuracy (tighter baud rate), it should be a dedicated task with its own verification checkpoint, not bundled into Phase 4 integration work.

[VERIFIED: Src/systick.c, Src/tim3.c, Src/usart.c, CLAUDE.md §RCC Clock Configuration]

---

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | `FINGER_MIN_AMPLITUDE = 50` counts is a safe starting value for a ×100 gain LM358 PPG circuit | Common Pitfalls / Code Examples | If wrong (too high): algorithm prints "--" constantly even with finger. Fix: lower the constant. If too low: no-finger detection fails. Fix: raise the constant. Both are correctable from CALIBRATION_MODE output. |
| A2 | A 1280ms amplitude window (s_amp_window) provides adequate no-finger detection response within 2 seconds | Common Pitfalls / Code Examples | If window is too long, "--" may take up to 2.56s (two window periods). Could fail SC-3 at boundary. Fix: reduce window to 800ms if needed. |
| A3 | Reference pulse oximeter is available for ±5 BPM accuracy comparison (SC-1) | Environment Availability | If unavailable: must use smartphone HR app, which has ±3–5 BPM accuracy itself. This degrades the accuracy claim to "within ±5 BPM of a smartphone app" rather than a calibrated reference. |
| A4 | LM358 circuit is already assembled and producing a usable PPG signal (Phase 3 hardware was verified on real hardware per commit `0b5f4be`) | Summary | Phase 3 hardware verification commit message states "BPM:60 stable, state transitions confirmed" — but with SYNTHETIC_TEST defined, the hardware test may have been the serial monitor observation only. If LM358 circuit has a wiring fault, Phase 4 will reveal it. |
| A5 | DEBUG_VERBOSE per-sample print (35 chars at 115200 baud, 8MHz) fits within 10ms sample interval | Common Pitfalls | Calculated 3.04ms < 10ms. Safe by 2x margin. Risk is low but ASSUMED for exact character count of format strings. |

**All A1–A5 claims are ASSUMED or calculated from first principles. A1, A2, A3 require empirical confirmation during Phase 4 execution.**

---

## Open Questions

1. **Is the LM358 circuit already producing clean PPG signal, or is there still analog debugging needed?**
   - What we know: Phase 3 hardware commit `0b5f4be` says "BPM:60 stable, state transitions confirmed" — but this was with SYNTHETIC_TEST defined (synthetic table, not live ADC)
   - What's unclear: Whether the commit message reflects real LM358 ADC output or just the synthetic path on hardware
   - Recommendation: Phase 4 Plan 1 should include a smoke-test task: flash with CALIBRATION_MODE defined and SYNTHETIC_TEST removed, observe ADC output for 5 seconds. If amp > 0 and varies with finger, circuit is working. If amp = 0 or flat, analog debugging needed.

2. **Should DEBUG_STATE be controllable from main.c (like SYNTHETIC_TEST) or remain hardcoded in algorithm.c (current state)?**
   - What we know: Phase 3 CONTEXT.md D-04 says it persists through Phase 4. Currently `#define DEBUG_STATE` is on line 1 of algorithm.c.
   - What's unclear: Whether Phase 4 should clean this up
   - Recommendation: Leave as-is for Phase 4. Note as cleanup item for Phase 5.

3. **What serial capture tool should be used for the DEBUG_VERBOSE screenshot (report evidence)?**
   - What we know: OUT-04 requires "at least one threshold-crossing event visible in the capture" for the report
   - What's unclear: Whether STM32CubeIDE's built-in console or a separate terminal emulator (PuTTY, minicom) is available
   - Recommendation: Any 115200 baud terminal emulator works. Plan should specify capture format expected. PuTTY session log or STM32CubeIDE console copy-paste both suffice.

---

## Validation Architecture

> nyquist_validation is set to false in .planning/config.json — this section is SKIPPED.

---

## Security Domain

> This is a bare-metal embedded firmware project with no network, no authentication, no user data, no storage, and no external service connections. ASVS categories V2 (Authentication), V3 (Session Management), V4 (Access Control), V6 (Cryptography) do not apply. V5 (Input Validation) is addressed by the existing BPM bounds check (SIG-04: 40–200 BPM gate) and the amplitude validity check (FINGER_MIN_AMPLITUDE). No additional security controls are applicable.

---

## Sources

### Primary (HIGH confidence)
- `Src/main.c` — current Phase 3 firmware, SYNTHETIC_TEST structure [VERIFIED: read in this session]
- `Src/algorithm.c` — all 5 SIG modules, state machine, threshold logic [VERIFIED: read in this session]
- `Src/adc.c` — ADC init sequence, g_adc_sample/g_adc_ready globals [VERIFIED: read in this session]
- `Src/usart.c` — uart_write_str/uart_write_u32 implementation, BRR=0x45 comment [VERIFIED: read in this session]
- `Src/systick.c` — SysTick_Config(7999) = 8MHz HSI confirmed [VERIFIED: read in this session]
- `Src/tim3.c` — PSC=79/ARR=999 = 100Hz at 8MHz confirmed [VERIFIED: read in this session]
- `CLAUDE.md` — register map, F070 gotchas, LM358 saturation note, interrupt priorities [VERIFIED: project instructions]
- `.planning/phases/03-algorithm-modules-on-synthetic-data/03-VERIFICATION.md` — Phase 3 verified, 11/11 must-haves [VERIFIED: read in this session]
- `.planning/STATE.md` — LM358 3.3V saturation note, Phase 4 threshold blocker documented [VERIFIED: read in this session]
- `.planning/REQUIREMENTS.md` — OUT-01 through OUT-04 specification text [VERIFIED: read in this session]

### Secondary (MEDIUM confidence)
- Phase 3 CONTEXT.md decisions D-01 through D-08 — algorithmic design decisions that Phase 4 inherits [CITED: .planning/phases/03-algorithm-modules-on-synthetic-data/03-CONTEXT.md]

### Tertiary (LOW confidence)
- A1–A5 in Assumptions Log — calculated estimates for FINGER_MIN_AMPLITUDE, amplitude window, and timing margins. Not verifiable without hardware.

---

## Metadata

**Confidence breakdown:**
- Integration steps (removing SYNTHETIC_TEST, calling init unconditionally): HIGH — code structure fully verified, changes are mechanical
- CALIBRATION_MODE and DEBUG_VERBOSE implementation: HIGH — patterns are simple integer-only UART prints, no new peripherals
- OUT-02 no-finger detection implementation: HIGH for code structure; MEDIUM for threshold constant value (requires hardware measurement)
- FINGER_MIN_AMPLITUDE value: LOW — must be empirically determined from CALIBRATION_MODE output
- Analog signal quality (will real LM358 produce usable signal): LOW — requires hardware observation

**Research date:** 2026-05-20
**Valid until:** 2026-06-20 (stable — STM32F070RB register map does not change; only the threshold constants need hardware measurement before becoming final)
