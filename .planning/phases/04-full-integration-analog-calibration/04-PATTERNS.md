# Phase 4: Full Integration + Analog Calibration — Pattern Map

**Mapped:** 2026-05-20
**Files analyzed:** 3 (main.c, algorithm.c, algorithm.h)
**Analogs found:** 3 / 3 (all modifications target existing files — no new files)

---

## File Classification

| Modified File | Role | Data Flow | Closest Analog Section | Match Quality |
|---------------|------|-----------|------------------------|---------------|
| `Src/main.c` | integration / top-level loop | event-driven (g_adc_ready flag) | Same file — existing `#else` live-ADC branch (lines 82–88) | exact — already present, just gated behind `#ifdef SYNTHETIC_TEST` |
| `Src/algorithm.c` | signal processing | transform (streaming per-sample) | Same file — `algorithm_init()` reset block (lines 33–51) and `PEAK_HOLD` UART output block (lines 96–141) | exact |
| `Src/algorithm.h` | header / constants | N/A | Same file — `REFRACTORY_MS` constant pattern | exact |

---

## Pattern Assignments

### `Src/main.c` — Remove SYNTHETIC_TEST, add CALIBRATION_MODE

**Analog:** Same file, existing structure.

#### Pattern 1A: The SYNTHETIC_TEST guard to remove (lines 1–3)

```c
#ifndef SYNTHETIC_TEST
#define SYNTHETIC_TEST
#endif
```

**Action:** Delete these three lines entirely. After deletion the macro is undefined, so every `#ifdef SYNTHETIC_TEST` block below evaluates false and falls through to the `#else` (live ADC) branch. This is the minimal, correct change.

#### Pattern 1B: The ifdef block in while(1) to collapse (lines 71–88)

```c
#ifdef SYNTHETIC_TEST
    static uint32_t s_last_sample_ms = 0;
    static uint8_t  s_table_idx      = 0;
    uint32_t now = millis();
    if ((now - s_last_sample_ms) >= 10)   /* 10ms = 100 Hz */
    {
        s_last_sample_ms = now;
        algorithm_process(sine_table[s_table_idx]);
        s_table_idx++;
        if (s_table_idx >= 100) s_table_idx = 0;
    }
#else
    if (g_adc_ready)
    {
        g_adc_ready = 0;                     /* consume the flag */
        algorithm_process((uint16_t)g_adc_sample);
    }
#endif /* SYNTHETIC_TEST */
```

**Action:** Replace the entire block with only the live ADC body. Also delete the `sine_table[100]` array (lines 21–44) and its `#ifdef SYNTHETIC_TEST` / `#endif` wrapper.

**Target state for while(1):**
```c
    while (1)
    {
        if (g_adc_ready)
        {
            g_adc_ready = 0;
            algorithm_process((uint16_t)g_adc_sample);
        }
    }
```

#### Pattern 1C: The gated init calls to make unconditional (lines 58–62)

```c
#ifndef SYNTHETIC_TEST
    /* TIM3 and ADC only needed for real hardware — synthetic test uses millis() only. */
    tim3_init();
    adc_init();
#endif
```

**Action:** Remove the `#ifndef SYNTHETIC_TEST` / `#endif` wrapper. The two calls become unconditional, placed in the same position in the init sequence:

```c
    systick_init();
    usart2_init();
    tim3_init();     /* unconditional after SYNTHETIC_TEST guard removed */
    adc_init();      /* unconditional */
    algorithm_init();
```

This call order is mandatory: `usart2_init()` enables GPIOA clock (comment at line 53–55), which `adc_init()` depends on for PA0 MODER configuration.

#### Pattern 1D: CALIBRATION_MODE startup window — insert after algorithm_init(), before while(1)

**UART output pattern** (from `uart_write_str` / `uart_write_u32` in usart.c lines 49–79):
```c
uart_write_str("CAL min=");   uart_write_u32(cal_min);
uart_write_str(" max=");      uart_write_u32(cal_max);
uart_write_str(" amp=");      uart_write_u32(cal_amp);
uart_write_str(" n=");        uart_write_u32(cal_count);
uart_write_str("\r\n");
```

**g_adc_ready consumption pattern** (from main.c lines 83–87, the existing live ADC branch):
```c
if (g_adc_ready)
{
    g_adc_ready = 0;
    /* consume g_adc_sample here */
}
```

**millis() timing pattern** (same as algorithm.c PEAK_HOLD case line 97 and REFRACTORY case line 149):
```c
uint32_t now = millis();
if ((now - s_refractory_start) >= REFRACTORY_MS)
```

**Complete CALIBRATION_MODE block to insert:**
```c
#ifdef CALIBRATION_MODE
{
    uint16_t cal_min   = 4095;
    uint16_t cal_max   = 0;
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

    uint16_t cal_amp = (cal_max > cal_min) ? (uint16_t)(cal_max - cal_min) : 0;
    uart_write_str("CAL min=");   uart_write_u32(cal_min);
    uart_write_str(" max=");      uart_write_u32(cal_max);
    uart_write_str(" amp=");      uart_write_u32(cal_amp);
    uart_write_str(" n=");        uart_write_u32(cal_count);
    uart_write_str("\r\n");
}
#endif /* CALIBRATION_MODE */
```

**Constraints:**
- No `float`. Subtraction `cal_max - cal_min` is safe because the ternary guards against underflow.
- `g_adc_sample` is declared `volatile uint16_t` in adc.c — copy to a local `uint16_t s` before use (avoids re-read race on Cortex-M0).
- Block runs after `adc_init()` / `tim3_init()` — TIM3 TRGO must be active or `g_adc_ready` never sets.

#### Pattern 1E: Compile-time define placement (lines 1–6 of current main.c)

Current top-of-file guard pattern:
```c
#ifndef SYNTHETIC_TEST
#define SYNTHETIC_TEST
#endif
#ifndef DEBUG_STATE
#define DEBUG_STATE
#endif
```

**Target state after Phase 4:** Replace with commented-out toggle defines:
```c
/* #define CALIBRATION_MODE */   /* uncomment for 5s startup amplitude measurement */
/* #define DEBUG_VERBOSE     */  /* uncomment for per-sample serial capture */
#ifndef DEBUG_STATE
#define DEBUG_STATE
#endif
```

`DEBUG_STATE` stays in main.c (as it is now) and in algorithm.c (line 1). Leave both as-is per RESEARCH.md recommendation (cleanup deferred to Phase 5).

---

### `Src/algorithm.c` — Add FINGER_MIN_AMPLITUDE, no-finger detection, DEBUG_VERBOSE

**Analog:** Same file — existing static variable declarations (lines 9–27), `algorithm_init()` reset block (lines 33–51), and the `PEAK_HOLD` UART output block (lines 96–141).

#### Pattern 2A: Static variable declaration style (lines 9–27)

```c
/* --- Moving average (SIG-01) --- */
static uint16_t  s_ma_buf[32];        /* circular buffer, 32 samples */
static uint8_t   s_ma_idx  = 0;
static uint32_t  s_ma_sum  = 0;       /* running sum; uint32 avoids overflow (32 × 4095 = 131040) */

/* --- Adaptive threshold peak detector (SIG-02, SIG-03) --- */
#define REFRACTORY_MS  350U            /* midpoint of 300-400ms SIG-03 window */

typedef enum { IDLE, RISING, PEAK_HOLD, FALLING, REFRACTORY } peak_state_t;
static peak_state_t s_state          = IDLE;
static uint16_t     s_threshold      = 1200;
static uint16_t     s_peak_val       = 0;
static uint32_t     s_refractory_start = 0;
```

**New statics to add in same style** (insert after line 27, before the `/* ----------` separator):
```c
/* --- No-finger detection (OUT-02) --- */
#define FINGER_MIN_AMPLITUDE  50U      /* counts; tune from CALIBRATION_MODE output */
#define AMP_WINDOW_MS         1280U    /* 1.28s window ≈ 1-2 heartbeat cycles at 60-80 BPM */

static uint16_t  s_amp_min       = 4095;
static uint16_t  s_amp_max       = 0;
static uint32_t  s_amp_win_start = 0;
```

#### Pattern 2B: algorithm_init() reset pattern (lines 33–51) — used for no-finger reset

The full init function shows exactly which fields to reset and their reset values:
```c
void algorithm_init(void)
{
    /* Zero moving-average buffer and sum */
    for (uint8_t i = 0; i < 32; i++) s_ma_buf[i] = 0;
    s_ma_sum = 0;
    s_ma_idx = 0;

    /* Reset peak detector state */
    s_state          = IDLE;
    s_threshold      = 1200;
    s_peak_val       = 0;
    s_refractory_start = 0;

    /* Reset BPM rolling average */
    for (uint8_t i = 0; i < 5; i++) s_bpm_buf[i] = 0;
    s_bpm_idx      = 0;
    s_bpm_count    = 0;
    s_last_peak_ms = 0;
}
```

**No-finger partial reset** (copy subset; do NOT zero the moving-average buffer — that would cause startup artifact; only reset the peak detector and BPM state):
```c
if (amplitude < FINGER_MIN_AMPLITUDE)
{
    uart_write_str("--\r\n");
    s_threshold    = 1200;   /* reset to initial value from algorithm_init() */
    s_state        = IDLE;   /* reset to initial value from algorithm_init() */
    s_peak_val     = 0;      /* reset to initial value from algorithm_init() */
    s_bpm_count    = 0;      /* clear rolling average — avoids stale BPM on re-acquisition */
    s_bpm_idx      = 0;      /* reset write pointer */
}
```

#### Pattern 2C: millis() timing window pattern (from REFRACTORY case, lines 147–150)

```c
case REFRACTORY:
    if ((millis() - s_refractory_start) >= REFRACTORY_MS)
    {
        next_state = FALLING;
    }
    break;
```

**Amplitude window check** (same pattern, different variable names):
```c
uint32_t now_amp = millis();
if ((now_amp - s_amp_win_start) >= AMP_WINDOW_MS)
{
    uint16_t amplitude = (s_amp_max > s_amp_min) ? (uint16_t)(s_amp_max - s_amp_min) : 0;
    s_amp_win_start = now_amp;
    s_amp_min = 4095;
    s_amp_max = 0;

    if (amplitude < FINGER_MIN_AMPLITUDE)
    {
        /* no-finger reset block (Pattern 2B above) */
    }
}
```

#### Pattern 2D: UART output pattern (from PEAK_HOLD case, lines 135–139)

```c
uart_write_str("BPM: ");
uart_write_u32(bpm_avg);
uart_write_str("\r\n");
```

**"--\r\n" follows the same pattern** (single uart_write_str call):
```c
uart_write_str("--\r\n");
```

#### Pattern 2E: #ifdef DEBUG_STATE conditional output (lines 165–188)

```c
if (next_state != s_state)
{
    s_state = next_state;
#ifdef DEBUG_STATE
    switch (s_state)
    {
        case IDLE:
            uart_write_str("STATE: IDLE\r\n");
            break;
        /* ... */
    }
#endif
}
```

**DEBUG_VERBOSE block follows identical #ifdef pattern** (insert at bottom of `algorithm_process()`, after state transition block, before closing brace):
```c
#ifdef DEBUG_VERBOSE
{
    /* Per-sample diagnostic dump — at 100Hz produces ~35 chars/sample (~3ms TX at 8MHz HSI)
     * which fits within the 10ms sample interval. Disable for production BPM capture. */
    /* Use current window running max/min as AMP proxy — actual amplitude rolls over per window */
    uint16_t amp_display = (s_amp_max > s_amp_min) ? (uint16_t)(s_amp_max - s_amp_min) : 0;
    uart_write_str("RAW:");   uart_write_u32(sample);
    uart_write_str(" FILT:"); uart_write_u32(filtered);
    uart_write_str(" THR:");  uart_write_u32(s_threshold);
    uart_write_str(" AMP:");  uart_write_u32(amp_display);
    uart_write_str(" ST:");   uart_write_u32((uint32_t)s_state);
    uart_write_str("\r\n");
}
#endif /* DEBUG_VERBOSE */
```

**Placement constraint:** `filtered` is declared on line 66. `sample` is the function parameter. `s_threshold` and `s_state` are file-scope statics. `s_amp_max` / `s_amp_min` are new statics added in Pattern 2A. All are in scope at the bottom of `algorithm_process()`.

#### Pattern 2F: algorithm_init() additions for new statics

Add three lines to `algorithm_init()` to reset the amplitude tracking state (insert after `s_last_peak_ms = 0;` at line 50, before closing brace):
```c
    /* Reset amplitude tracking window */
    s_amp_min       = 4095;
    s_amp_max       = 0;
    s_amp_win_start = 0;
```

#### Pattern 2G: Insertion point for amplitude tracking in algorithm_process()

The tracking update must occur on every sample, after `filtered` is computed (line 66) and before the state machine switch (line 71). This ensures the amplitude window sees raw sample values, not filtered values (raw has larger swing for detection sensitivity).

Insert between lines 66 and 69:
```c
    /* OUT-02: Track raw sample min/max for no-finger amplitude detection */
    if (sample < s_amp_min) s_amp_min = sample;
    if (sample > s_amp_max) s_amp_max = sample;

    uint32_t now_amp = millis();
    if ((now_amp - s_amp_win_start) >= AMP_WINDOW_MS)
    {
        uint16_t amplitude = (s_amp_max > s_amp_min) ? (uint16_t)(s_amp_max - s_amp_min) : 0;
        s_amp_win_start = now_amp;
        s_amp_min = 4095;
        s_amp_max = 0;

        if (amplitude < FINGER_MIN_AMPLITUDE)
        {
            uart_write_str("--\r\n");
            s_threshold = 1200;
            s_state     = IDLE;
            s_peak_val  = 0;
            s_bpm_count = 0;
            s_bpm_idx   = 0;
            return;   /* skip state machine this cycle — clean re-entry next sample */
        }
    }
```

**Note on `return` vs `continue`:** Using `return` early-exits `algorithm_process()` cleanly, which avoids running the state machine with a just-reset s_state in the same call. The `filtered` local is abandoned (no side effects). This is correct.

---

### `Src/algorithm.h` — Add FINGER_MIN_AMPLITUDE if needed externally

The existing header exports only the two function signatures:
```c
void algorithm_init(void);
void algorithm_process(uint16_t sample);
```

**FINGER_MIN_AMPLITUDE** is defined inside algorithm.c as a file-scope `#define`. It does not need to be in the header unless CALIBRATION_MODE in main.c needs to compare against it. Per the architecture: CALIBRATION_MODE only prints the observed amplitude — the human then sets `FINGER_MIN_AMPLITUDE` in algorithm.c manually. No cross-file dependency. **No header change required.**

If a future plan exposes FINGER_MIN_AMPLITUDE to main.c, follow the `REFRACTORY_MS` pattern: keep the `#define` in algorithm.c (not in the header), because it is an implementation constant, not an interface constant.

---

## Shared Patterns

### UART Output
**Source:** `Src/usart.c` lines 49–79
**Apply to:** All new print statements in main.c (CALIBRATION_MODE) and algorithm.c (no-finger, DEBUG_VERBOSE)

Rules extracted from the implementation:
- Always `uart_write_str(literal)` for labels and separators
- Always `uart_write_u32(uint32_t)` for numeric values — never printf, never itoa
- No `float` — convert all numeric output to `uint32_t` first
- Use `(uint32_t)cast` for enum values (`(uint32_t)s_state`) to satisfy `uart_write_u32` signature
- Terminate every output line with `uart_write_str("\r\n")`

```c
/* Polling TX — always safe on STM32F070 with 8MHz HSI at 115200 baud */
void uart_write_str(const char *s) { while (*s) { while (!(USART2->ISR & USART_ISR_TXE)); USART2->TDR = (uint8_t)*s++; } }
void uart_write_u32(uint32_t n)    { /* itoa-style reverse loop, no stdlib */ }
```

### millis() Timing
**Source:** `Src/systick.c` lines 16–19; used in `algorithm.c` lines 97, 149
**Apply to:** CALIBRATION_MODE 5s window (main.c) and amplitude window check (algorithm.c)

```c
uint32_t millis(void)
{
    return s_ticks;   /* single 32-bit read — atomic on Cortex-M0 */
}
```

Pattern for elapsed-time checks:
```c
uint32_t start = millis();
while ((millis() - start) < DURATION_MS) { /* ... */ }
/* OR */
if ((millis() - s_win_start) >= WINDOW_MS) { /* ... */ }
```

**Critical:** `millis()` is configured for 8MHz HSI (`SysTick_Config(7999)`). Do not change SysTick_Config in Phase 4 — 5-second window computed as `5000UL` milliseconds is correct at this clock rate.

### g_adc_ready / g_adc_sample Consumption
**Source:** `Src/main.c` lines 83–87 (existing live ADC branch), `Src/adc.c` lines 18–19, 71–74

```c
/* Globals declared in adc.c: */
volatile uint16_t g_adc_sample = 0;
volatile uint8_t  g_adc_ready  = 0;

/* Consumption pattern — main loop: */
if (g_adc_ready)
{
    g_adc_ready = 0;                     /* consume the flag atomically before use */
    algorithm_process((uint16_t)g_adc_sample);
}
```

**Rule:** Always clear `g_adc_ready = 0` before reading `g_adc_sample` into a local copy. Copy `g_adc_sample` to a local `uint16_t` before arithmetic — avoids re-read of volatile in multi-step expressions.

### #ifdef Conditional Compilation Toggle
**Source:** `Src/algorithm.c` lines 165–188 (`#ifdef DEBUG_STATE` block)
**Apply to:** `#ifdef CALIBRATION_MODE` (main.c), `#ifdef DEBUG_VERBOSE` (algorithm.c)

```c
#ifdef DEBUG_STATE
    /* conditional code — enabled by #define at top of file or from compiler -D flag */
    /* ... */
#endif
```

Pattern: define toggles at the top of the file that owns them (not in headers). Comment explains purpose and removal condition.

### Integer-Only Arithmetic
**Source:** Established throughout — `algorithm.c` line 102, all UART output, adc.c line 72
**Apply to:** All arithmetic in CALIBRATION_MODE and amplitude tracking

```c
/* Correct — integer multiply before divide to preserve precision */
s_threshold = (uint16_t)((uint32_t)s_peak_val * 3 / 5);

/* Correct — guarded subtraction avoids unsigned underflow */
uint16_t amplitude = (s_amp_max > s_amp_min) ? (uint16_t)(s_amp_max - s_amp_min) : 0;

/* WRONG — no float anywhere */
/* float amplitude = (float)(s_amp_max - s_amp_min); */  /* FORBIDDEN: no FPU on M0 */
```

---

## No Analog Found

No files in Phase 4 have no analog — all three touched files already exist and provide their own patterns.

| File | Situation |
|------|-----------|
| `Src/main.c` | Modifying existing file; live ADC path already present at lines 82–88 |
| `Src/algorithm.c` | Modifying existing file; all new patterns extrapolated from existing code style |
| `Src/algorithm.h` | No change required (see Pattern Assignment above) |

---

## Exact Insertion Map

| Change | File | Insert Position | Action |
|--------|------|-----------------|--------|
| Remove 3-line SYNTHETIC_TEST guard | main.c | Lines 1–3 | Delete |
| Remove sine_table array | main.c | Lines 20–44 | Delete |
| Make tim3_init/adc_init unconditional | main.c | Lines 58–62 | Remove `#ifndef`/`#endif`, keep the two calls |
| Add CALIBRATION_MODE/DEBUG_VERBOSE toggle comments | main.c | Lines 1–3 (after deletion) | Insert commented `#define` lines |
| Add CALIBRATION_MODE startup block | main.c | After `algorithm_init();`, before `uart_write_str(phase banner)` | Insert Pattern 1D |
| Collapse SYNTHETIC_TEST ifdef in while(1) | main.c | Lines 71–88 | Replace with Pattern 1B target state |
| Add FINGER_MIN_AMPLITUDE, AMP statics | algorithm.c | After line 27 (after `s_refractory_start` declaration) | Insert Pattern 2A |
| Add amplitude tracking + no-finger check | algorithm.c | After line 66 (`filtered` computed), before line 69 (`next_state` declaration) | Insert Pattern 2G |
| Add s_amp_min/max/win_start reset to algorithm_init() | algorithm.c | After `s_last_peak_ms = 0;` (line 50), before closing brace | Insert Pattern 2F |
| Add DEBUG_VERBOSE block | algorithm.c | After `s_state = next_state;` transition block (after line 188 `}`), before function closing brace | Insert Pattern 2E |

---

## Metadata

**Analog search scope:** `/home/erkmen/STM32CubeIDE/workspace_2.1.1/HeartRateSensor1/Src/` — all 6 source files read in full
**Files scanned:** 6 (main.c, algorithm.c, usart.c, adc.c, tim3.c, systick.c) + 04-RESEARCH.md
**Pattern extraction date:** 2026-05-20
