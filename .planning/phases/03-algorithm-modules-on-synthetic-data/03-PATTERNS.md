# Phase 3: Algorithm Modules on Synthetic Data - Pattern Map

**Mapped:** 2026-05-20
**Files analyzed:** 3 (2 new, 1 modified)
**Analogs found:** 3 / 3

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|-------------------|------|-----------|----------------|---------------|
| `Src/algorithm.c` | service/utility | transform (streaming samples → BPM) | `Src/adc.c` | role-partial (state, global flags, ISR-free module pattern) |
| `Inc/algorithm.h` | header/config | — | `Inc/adc.h` | exact (header guard + extern + init signature) |
| `Src/main.c` (modify) | controller | request-response (poll loop) | `Src/main.c` itself | exact (modify in place) |

---

## Pattern Assignments

### `Inc/algorithm.h` (header, config)

**Analog:** `Inc/adc.h` (lines 1–11)

**Header guard + includes pattern** (adc.h lines 1–11):
```c
#ifndef ADC_H
#define ADC_H

#include <stdint.h>

extern volatile uint16_t g_adc_sample;  /* latest 12-bit ADC reading */
extern volatile uint8_t  g_adc_ready;   /* set to 1 by ISR; cleared by consumer */

void adc_init(void);

#endif /* ADC_H */
```

**Apply to `Inc/algorithm.h` as:**
```c
#ifndef ALGORITHM_H
#define ALGORITHM_H

#include <stdint.h>

/*
 * Compile-time feature switches — define before #include or in build flags.
 *   SYNTHETIC_TEST  — feed sine_table[] instead of g_adc_sample/g_adc_ready
 *   DEBUG_STATE     — print state transitions via uart_write_str
 */

void algorithm_init(void);
void algorithm_process(uint16_t sample);  /* called from main loop per sample */

#endif /* ALGORITHM_H */
```

Key points:
- No `extern` globals exported from algorithm — all state is `static` inside `algorithm.c` (see core pattern below).
- `#define` guards follow the `FILENAME_H` convention used in every existing header.
- `SYNTHETIC_TEST` and `DEBUG_STATE` are NOT defined in this header; they live at the top of `main.c` so the caller controls them without touching the module.

---

### `Src/algorithm.c` (service, transform)

**Analog:** `Src/adc.c` — the closest structural analog for a bare-metal C module with file-local state, a single init function, and a processing function called from main loop.

**Imports pattern** (adc.c lines 1–2):
```c
#include "stm32f0xx.h"
#include "adc.h"
```

**Apply to `Src/algorithm.c` as:**
```c
#include "systick.h"   /* millis() — refractory period timing */
#include "usart.h"     /* uart_write_str, uart_write_u32 — output only */
#include "algorithm.h"
```

Note: `stm32f0xx.h` is NOT included — `algorithm.c` owns no peripheral registers. This is intentional and mirrors the D-01 decision that algorithm is a pure-software module.

**File-local state pattern** (adc.c lines 18–19, systick.c line 5):
```c
/* adc.c */
volatile uint16_t g_adc_sample = 0;
volatile uint8_t  g_adc_ready  = 0;

/* systick.c */
static volatile uint32_t s_ticks = 0;
```

**Apply to `Src/algorithm.c` — all state is static (no extern), no volatile needed (only touched from main loop, not ISR):**
```c
/* --- Moving average (SIG-01) --- */
static uint16_t  s_ma_buf[32];        /* circular buffer, 32 samples */
static uint8_t   s_ma_idx  = 0;
static uint32_t  s_ma_sum  = 0;       /* running sum; uint32 avoids overflow (32 × 4095 = 131040) */

/* --- Adaptive threshold peak detector (SIG-02, SIG-03) --- */
typedef enum { IDLE, RISING, PEAK_HOLD, FALLING, REFRACTORY } peak_state_t;
static peak_state_t s_state          = IDLE;
static uint16_t     s_threshold      = 1200; /* initial ~60% of midpoint; updated after each peak */
static uint16_t     s_peak_val       = 0;    /* maximum filtered value seen in RISING/PEAK_HOLD */
static uint32_t     s_refractory_end = 0;    /* millis() value when refractory ends */

/* --- BPM calculation (SIG-04, SIG-05) --- */
static uint32_t  s_last_peak_ms  = 0;
static uint16_t  s_bpm_buf[5];          /* rolling average buffer */
static uint8_t   s_bpm_idx       = 0;
static uint8_t   s_bpm_count     = 0;   /* how many valid readings in buf (0–5) */
```

**Init function pattern** (adc.c lines 21–61, systick.c lines 7–13):
```c
/* adc.c — sequential numbered steps with inline comments */
void adc_init(void)
{
    /* Step 1 -- PA0 analog mode ... */
    /* Step N -- ... */
}

/* systick.c — minimal, single responsibility */
void systick_init(void)
{
    SysTick_Config(71999);
    NVIC_SetPriority(SysTick_IRQn, 0);
}
```

**Apply to `Src/algorithm.c`:**
```c
void algorithm_init(void)
{
    /* Zero moving-average buffer and sum */
    for (uint8_t i = 0; i < 32; i++) s_ma_buf[i] = 0;
    s_ma_sum = 0;
    s_ma_idx = 0;

    /* Reset peak detector state */
    s_state     = IDLE;
    s_threshold = 1200;
    s_peak_val  = 0;
    s_refractory_end = 0;

    /* Reset BPM rolling average */
    for (uint8_t i = 0; i < 5; i++) s_bpm_buf[i] = 0;
    s_bpm_idx   = 0;
    s_bpm_count = 0;
    s_last_peak_ms = 0;
}
```

**Core processing pattern — no analog exists; derive from requirements:**

The five sub-modules (SIG-01 through SIG-05) chain linearly inside `algorithm_process(uint16_t sample)`:

```c
void algorithm_process(uint16_t sample)
{
    /* SIG-01: 32-sample integer moving average */
    s_ma_sum -= s_ma_buf[s_ma_idx];
    s_ma_buf[s_ma_idx] = sample;
    s_ma_sum += sample;
    s_ma_idx = (s_ma_idx + 1) & 0x1F;   /* & 0x1F = modulo 32 without division */
    uint16_t filtered = (uint16_t)(s_ma_sum >> 5);

    /* SIG-02 + SIG-03: state machine peak detector with refractory */
    /* ... state transitions using filtered, s_threshold, millis() ... */

    /* SIG-04 + SIG-05 + OUT-01: BPM bounds check, rolling average, print */
    /* ... on peak detected: compute interval_ms, bpm = 60000/interval_ms ... */
    /* ... if bpm in [40,200]: insert into s_bpm_buf, print rolling average ... */
}
```

**State transition debug output pattern** (D-04, mirrors uart output in main.c lines 32–34):
```c
/* main.c existing pattern */
uart_write_str("adc: ");
uart_write_u32((uint32_t)g_adc_sample);
uart_write_str("\r\n");

/* Apply to DEBUG_STATE transitions in algorithm.c */
#ifdef DEBUG_STATE
    uart_write_str("STATE: RISING\r\n");
#endif
```

Only emit the string on transition (when `new_state != s_state`), not on every call. Use literal strings per state name — no dynamic string construction.

**BPM output pattern** (mirrors main.c lines 32–34, CONTEXT.md §code_context):
```c
uart_write_str("BPM: ");
uart_write_u32(bpm_avg);
uart_write_str("\r\n");
```

**No error handling** — bare-metal module with no error paths. Mirrors all existing drivers (adc.c, systick.c, tim3.c — none have error returns or error branches). All edge cases are handled by algorithm bounds (SIG-04) silently discarding out-of-range readings.

---

### `Src/main.c` (modify — add SYNTHETIC_TEST path)

**Analog:** `Src/main.c` itself (modify in place).

**Existing poll loop** (main.c lines 27–36):
```c
while (1)
{
    if (g_adc_ready)
    {
        g_adc_ready = 0;                     /* consume the flag */
        uart_write_str("adc: ");
        uart_write_u32((uint32_t)g_adc_sample);
        uart_write_str("\r\n");
    }
}
```

**Apply — wrap existing branch, add synthetic branch (D-02, D-03):**
```c
/* Define at top of main.c, above all #includes, for easy Phase 4 removal */
#define SYNTHETIC_TEST
#define DEBUG_STATE

/* Synthetic table — file-local, not exported (D-06, D-07, D-08) */
#ifdef SYNTHETIC_TEST
static const uint16_t sine_table[100] = {
    /* 100 entries; PPG shape with dicrotic notch */
    /* baseline ~200, main peak ~2000 at idx ~30, notch ~800 at idx ~50 */
    /* ... values TBD during implementation ... */
};
#endif

/* Inside while(1): */
while (1)
{
#ifdef SYNTHETIC_TEST
    static uint32_t s_last_sample_ms = 0;
    static uint8_t  s_table_idx      = 0;
    uint32_t now = millis();
    if ((now - s_last_sample_ms) >= 10)   /* 10ms = 100 Hz */
    {
        s_last_sample_ms = now;
        algorithm_process(sine_table[s_table_idx]);
        s_table_idx = (s_table_idx + 1 >= 100) ? 0 : s_table_idx + 1;
    }
#else
    if (g_adc_ready)
    {
        g_adc_ready = 0;
        algorithm_process((uint16_t)g_adc_sample);
    }
#endif
}
```

**Init call pattern** (main.c lines 12–25 — ordered calls, comment explains dependency):
```c
SystemInit();
systick_init();
usart2_init();
tim3_init();
adc_init();
uart_write_str("\r\n--- HeartRateSensor Phase 2 ---\r\n");
```

Add `algorithm_init()` after `adc_init()`, update banner string to `"Phase 3"`.

**Header include pattern** (main.c lines 1–5):
```c
#include "stm32f0xx.h"
#include "systick.h"
#include "usart.h"
#include "tim3.h"
#include "adc.h"
```

Add `#include "algorithm.h"` after `adc.h`.

---

## Shared Patterns

### File-Local State (apply to `algorithm.c`)
**Source:** `Src/systick.c` line 5 and `Src/adc.c` lines 18–19
**Apply to:** `Src/algorithm.c` — all module state declared `static` at file scope, never exported via `extern`. Matches the established driver pattern.
```c
static volatile uint32_t s_ticks = 0;   /* systick.c pattern */
```

### Serial Output (apply to `algorithm.c`)
**Source:** `Src/usart.c` lines 49–79, `Inc/usart.h` lines 7–8
**Apply to:** `Src/algorithm.c` BPM print and state debug output
```c
void uart_write_str(const char *s);   /* blocks; null-terminated; no formatting */
void uart_write_u32(uint32_t n);      /* decimal ASCII, no leading zeros */
```
No `printf`, no format strings, no stdlib. Two-call pattern: `uart_write_str("BPM: "); uart_write_u32(bpm); uart_write_str("\r\n");`

### Integer-Only Math (apply to `algorithm.c`)
**Source:** CLAUDE.md §F103-Specific Gotchas, REQUIREMENTS.md SIG-01/SIG-02
**Apply to:** All arithmetic in `algorithm.c`
- Moving average: `sum >> 5` (not `sum / 32.0f`)
- Threshold: `last_peak * 3 / 5` (not `last_peak * 0.6f`) — integer multiply before divide avoids truncation to zero
- BPM: `60000UL / interval_ms` — use `UL` suffix; interval_ms is `uint32_t` from `millis()` subtraction

### Compile-Time Feature Gates (apply to `main.c` and `algorithm.c`)
**Source:** CONTEXT.md D-02, D-04; no existing analog in codebase
**Apply to:** `main.c` (define site) and `algorithm.c` (use site)
```c
/* Define at top of main.c */
#define SYNTHETIC_TEST
#define DEBUG_STATE

/* Use in algorithm.c */
#ifdef DEBUG_STATE
    uart_write_str("STATE: RISING\r\n");
#endif
```

### Naming Convention
**Source:** All existing files (adc.c, systick.c, tim3.c, usart.c)
- Static file-local variables: `s_` prefix (e.g., `s_ticks`, `s_ma_buf`)
- Global shared variables: `g_` prefix (e.g., `g_adc_sample`, `g_adc_ready`)
- Init functions: `<module>_init(void)` (e.g., `adc_init`, `systick_init`)
- ISR handlers: `<Peripheral>_IRQHandler(void)` — not applicable to `algorithm.c` (no ISR)

---

## No Analog Found

| File | Role | Data Flow | Reason |
|------|------|-----------|--------|
| `Src/algorithm.c` (core logic) | service | transform/state-machine | No signal processing or state machine exists in codebase yet; `adc.c` is structural analog only. Core algorithm logic (moving average, peak detector, BPM rolling average) has no precedent — planner must derive from REQUIREMENTS.md SIG-01 through SIG-05 and CONTEXT.md §Specific Ideas. |

---

## Metadata

**Analog search scope:** `Src/`, `Inc/`
**Files scanned:** 9 source/header files
**Pattern extraction date:** 2026-05-20
