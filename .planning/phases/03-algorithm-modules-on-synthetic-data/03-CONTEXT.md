# Phase 3: Algorithm Modules on Synthetic Data - Context

**Gathered:** 2026-05-19
**Status:** Ready for planning

<domain>
## Phase Boundary

All five signal-processing modules — moving average, adaptive threshold peak detector, refractory suppression, BPM bounds check, and rolling average — implemented as pure-C functions in a single `algorithm.c/h` pair and validated against a 100-entry PPG-shaped synthetic sine table that produces a stable `"BPM: 60\r\n"` output. No live ADC or real hardware required in this phase.

**Requirements in scope:** SIG-01, SIG-02, SIG-03, SIG-04, SIG-05
**Out of scope:** live ADC signal path, LM358 calibration, Phase 4 threshold tuning

</domain>

<decisions>
## Implementation Decisions

### Module File Structure
- **D-01:** All five algorithm modules live in a single `algorithm.c` / `algorithm.h` pair in `Src/` and `Inc/`. Phase 3 is ~300–400 lines; splitting into filter/peak_detect/bpm_calc creates cross-include overhead for tightly-coupled modules that share internal state. The Phase 1 per-peripheral pattern applies to hardware drivers, not pure software modules.

### Synthetic Test Integration
- **D-02:** Use a `#define SYNTHETIC_TEST` compile-time switch. When defined, the `while(1)` loop in `main.c` reads from a table index (advanced every 10ms via `millis()` delta) instead of consuming `g_adc_sample` / `g_adc_ready`. The live ADC path stays intact behind `#ifndef SYNTHETIC_TEST` — Phase 4 simply undefines the macro, no main.c rewrite needed.
- **D-03:** Table timing: one sample every 10ms using `millis()` delta comparison (same as live 100Hz ADC rate). Timing-dependent logic (refractory period in ms) stays valid with real hardware because the algorithm sees identical inter-sample intervals.

### State Machine Debug Output
- **D-04:** State transition logging is gated behind `#define DEBUG_STATE`. This define persists through Phase 4 — both Phase 3 SC-2 and Phase 4 SC-4 require state visibility. Log state name only on each transition: `"STATE: RISING\r\n"` etc., using `uart_write_str`. No per-sample field dump (too noisy at 100Hz).
- **D-05:** State names: `IDLE`, `RISING`, `PEAK_HOLD`, `FALLING`, `REFRACTORY` — matches Phase 3 SC-2 exactly.

### Synthetic Sine Table
- **D-06:** Table shape: PPG-shaped with a dicrotic notch. Main peak at approximately sample 30 (range 0–2200 counts peak). Secondary notch at approximately sample 50 (~40% amplitude of main peak). This is the only shape that exercises SC-3 — a pure mathematical sine never triggers double-counting so the refractory suppression logic is never tested.
- **D-07:** ADC range: 0–2200 counts (not 0–4095). Matches the real LM358-on-3.3V hardware range documented in PROJECT.md (~1.8V max output → ~2200 ADC counts). Algorithm thresholds written for this range work unmodified in Phase 4.
- **D-08:** Table length: 100 entries at 10ms/sample = 1 full second = 60 BPM when looped continuously. Directly satisfies Phase 3 SC-1.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Phase Requirements
- `.planning/REQUIREMENTS.md` — SIG-01 (32-sample integer moving average, `sum >> 5`), SIG-02 (adaptive threshold = `last_peak × 0.6`, integer arithmetic), SIG-03 (refractory 300–400ms), SIG-04 (40–200 BPM bounds), SIG-05 (5-reading rolling average)
- `.planning/ROADMAP.md` §Phase 3 — four success criteria (SC-1 through SC-4) define "done"

### Register and API Reference (CLAUDE.md)
- `CLAUDE.md` §USART2 — uart_write_str / uart_write_u32 signatures; polling TX; no printf
- `CLAUDE.md` §SysTick — millis() source for 10ms sample timing
- `CLAUDE.md` §F103-Specific Gotchas — integer-only math constraint (no FPU on F103)

### Existing Driver Headers (integration points)
- `Inc/adc.h` — `volatile uint16_t g_adc_sample` and `volatile uint8_t g_adc_ready` (consumed by algorithm in live mode)
- `Inc/systick.h` — `uint32_t millis(void)` — used for 10ms synthetic sample timing and refractory period measurement
- `Inc/usart.h` — `uart_write_str` / `uart_write_u32` — the only serial output API

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `millis()` (`Src/systick.c`): Use for 10ms table-advance timing in `SYNTHETIC_TEST` path and for refractory period measurement (elapsed ms since last peak).
- `uart_write_str` + `uart_write_u32` (`Src/usart.c`): All serial output — BPM print, state transitions under `DEBUG_STATE`, startup banner.

### Established Patterns
- Driver file pattern (Phase 1 D-01): `algorithm.c` in `Src/`, `algorithm.h` in `Inc/`, ISR-free (no peripheral ownership).
- Global flag pattern (Phase 2): `g_adc_ready` / `g_adc_sample` are the live ADC handoff; `SYNTHETIC_TEST` replaces this handoff with a table read.
- No stdlib / no printf: `uart_write_str` + `uart_write_u32` only. Format BPM output as: `uart_write_str("BPM: "); uart_write_u32(bpm); uart_write_str("\r\n");`

### Integration Points
- `Src/main.c` Phase 2 loop (`if (g_adc_ready) { ... }`) is the wrap point. Phase 3 wraps the ADC branch in `#ifndef SYNTHETIC_TEST` and adds a `#ifdef SYNTHETIC_TEST` branch that reads `sine_table[idx]`, advances idx every 10ms, wraps at 100.
- `algorithm_process(uint16_t sample)` (or similar) is the single function called from both paths — same API regardless of sample source.

</code_context>

<specifics>
## Specific Ideas

- `#define SYNTHETIC_TEST` and `#define DEBUG_STATE` both defined at the top of `main.c` (or in `algorithm.h`) for easy toggling before Phase 4.
- Synthetic sine table defined as `static const uint16_t sine_table[100]` in `main.c` (local to this phase, not exported).
- Dicrotic notch design: main peak ~2000 counts at index ~30; falls to baseline ~200 by index ~45; secondary notch ~800 counts at index ~50; back to baseline by ~65; remains at baseline ~200 for indices 65–99.
- Refractory period constant: 350ms (midpoint of 300–400ms spec). At 100Hz = 35 samples suppressed after a detected peak.
- BPM printed on each valid heartbeat detection (not on every sample).

</specifics>

<deferred>
## Deferred Ideas

None — discussion stayed within phase scope.

</deferred>

---

*Phase: 3 — Algorithm Modules on Synthetic Data*
*Context gathered: 2026-05-19*
