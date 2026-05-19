# Phase 3: Algorithm Modules on Synthetic Data - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-05-19
**Phase:** 03-algorithm-modules-on-synthetic-data
**Areas discussed:** Module file structure, Synthetic test integration, State machine debug output, Sine table shape

---

## Module File Structure

| Option | Description | Selected |
|--------|-------------|----------|
| Single algorithm.c/h | All 5 modules in one file pair; avoids cross-include overhead for tightly-coupled modules | ✓ |
| Three files: filter.c, peak_detect.c, bpm_calc.c | Mirrors hardware driver separation; cleaner if algorithm grows, but awkward cross-include dependencies | |

**User's choice:** Single algorithm.c/h
**Notes:** None beyond selection.

---

## Synthetic Test Integration

| Option | Description | Selected |
|--------|-------------|----------|
| #define SYNTHETIC_TEST switch | ADC path stays; while(1) reads table when defined; Phase 4 undefines — no rewrite | ✓ |
| Replace ADC loop entirely | main.c Phase 3 = table-only; Phase 4 must re-integrate ADC manually | |

**User's choice:** #define SYNTHETIC_TEST switch

| Option | Description | Selected |
|--------|-------------|----------|
| millis() timer, one sample every 10ms | Matches 100Hz ADC rate; timing-dependent logic stays valid | ✓ |
| Tight loop, no timing | Instant; timing-dependent logic (refractory ms) behaves differently | |

**User's choice:** millis() timer at 10ms
**Notes:** None beyond selection.

---

## State Machine Debug Output

| Option | Description | Selected |
|--------|-------------|----------|
| #define DEBUG_STATE persists to Phase 4 | SC-2 (Ph3) and SC-4 (Ph4) both need state visibility; one define covers both | ✓ |
| Always-on in Phase 3 only | Unconditional now; requires cleanup step before Phase 4 | |

**User's choice:** #define DEBUG_STATE persists to Phase 4

| Option | Description | Selected |
|--------|-------------|----------|
| State name only: "STATE: RISING\r\n" | uart_write_str-compatible; SC-2 just requires transitions visible | ✓ |
| Full diagnostic: "STATE: RISING \| filt=2187 thr=1845\r\n" | More useful for calibration; requires 5+ uart calls per transition; noisy at 100Hz | |

**User's choice:** State name only
**Notes:** None beyond selection.

---

## Sine Table Shape

| Option | Description | Selected |
|--------|-------------|----------|
| PPG-shaped with dicrotic notch | Main peak ~sample 30; notch ~sample 50 (~40% amplitude); exercises SC-3 refractory suppression | ✓ |
| Pure mathematical sine (0–4095) | Simpler; SC-3 never triggered — refractory logic untested | |

**User's choice:** PPG-shaped with dicrotic notch

| Option | Description | Selected |
|--------|-------------|----------|
| 0–2200 counts | Matches real LM358-on-3.3V range; thresholds work unmodified in Phase 4 | ✓ |
| 0–4095 counts (full 12-bit) | Easier to reason about; Phase 4 needs threshold re-tuning | |

**User's choice:** 0–2200 counts
**Notes:** None beyond selection.

---

## Claude's Discretion

None — user selected all options explicitly.

## Deferred Ideas

None — discussion stayed within phase scope.
