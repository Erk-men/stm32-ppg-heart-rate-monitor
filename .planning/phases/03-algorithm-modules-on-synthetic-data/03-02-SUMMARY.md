---
phase: 03-algorithm-modules-on-synthetic-data
plan: "02"
subsystem: algorithm
tags: [algorithm, state-machine, adaptive-threshold, refractory, rolling-average, bpm, ppg]
dependency_graph:
  requires: [Inc/systick.h, Inc/usart.h, Inc/algorithm.h, Src/algorithm.c (plan-01 SIG-01 base)]
  provides: [Src/algorithm.c (SIG-02 through SIG-05)]
  affects: []
tech_stack:
  added: []
  patterns:
    - "Five-state peak detector: IDLE/RISING/PEAK_HOLD/FALLING/REFRACTORY with transition-only debug output"
    - "Adaptive threshold update: last_peak * 3 / 5 (integer multiply-before-divide)"
    - "350ms refractory window to suppress dicrotic-notch secondary peak"
    - "5-entry rolling average ring buffer with s_bpm_count clamped at 5"
    - "40-200 BPM bounds gate protecting the rolling-average buffer"
key_files:
  created: []
  modified:
    - Src/algorithm.c
decisions:
  - "Both tasks implemented in a single file write since PEAK_HOLD is the shared beat-registration site for Task 1 (state machine) and Task 2 (BPM computation) — committing as one atomic unit"
  - "s_state update deferred to end of algorithm_process via next_state local variable, enabling transition detection without a two-step compare"
metrics:
  duration: "~3 minutes"
  completed: "2026-05-20T11:37:39Z"
  tasks_completed: 2
  tasks_total: 2
  files_created: 0
  files_modified: 1
---

# Phase 03 Plan 02: Full Signal-Processing Chain (SIG-02..SIG-05) Summary

**One-liner:** Five-state adaptive-threshold peak detector (IDLE/RISING/PEAK_HOLD/FALLING/REFRACTORY) with 350ms refractory suppression of the dicrotic notch, 40-200 BPM bounds check, and 5-reading rolling average — replacing the Plan 01 minimal fixed-threshold detector to produce a stable BPM: 60 from the synthetic sine table.

## Tasks Completed

| Task | Name | Commit | Files |
|------|------|--------|-------|
| 1 | Implement the five-state adaptive-threshold peak detector with refractory suppression | f1d29e9 | Src/algorithm.c |
| 2 | Add BPM bounds check and 5-reading rolling average | f1d29e9 | Src/algorithm.c |

## What Was Built

### Src/algorithm.c (replaced Plan 01 minimal detector)

**Removed (Plan 01 walking slice):**
- `static uint16_t s_prev_filtered` — fixed-1000 threshold crossing state
- Rising-edge crossing detector with hardcoded threshold 1000
- BPM print in the minimal detector block

**Added (SIG-02..SIG-05):**

**State machine type and constants:**
- `#define REFRACTORY_MS 350U` — midpoint of SIG-03 300-400ms window
- `typedef enum { IDLE, RISING, PEAK_HOLD, FALLING, REFRACTORY } peak_state_t`
- File-scope statics: `s_state`, `s_threshold` (init 1200), `s_peak_val`, `s_refractory_end`

**SIG-02 — Adaptive threshold (PEAK_HOLD branch):**
- `s_threshold = (uint16_t)((uint32_t)s_peak_val * 3 / 5)` — integer multiply before divide, token `* 3 / 5` present, no `0.6` literal, no float/double

**SIG-03 — Refractory suppression:**
- PEAK_HOLD sets `s_refractory_end = millis() + REFRACTORY_MS`
- REFRACTORY state ignores all samples until `millis() >= s_refractory_end`
- After refractory: transitions to FALLING (wait for signal to drop below threshold before next IDLE)

**State transitions:**
- `next_state` local variable tracks desired next state; s_state only updated once at the end of `algorithm_process` when `next_state != s_state`
- On transition, `#ifdef DEBUG_STATE` emits the literal `"STATE: <NAME>\r\n"` for each of the five names — one literal per state, no dynamic string construction (D-04)

**SIG-04 — Bounds check:**
- BPM accepted only if `bpm >= 40 && bpm <= 200`
- Out-of-range readings silently discarded, not inserted into `s_bpm_buf`, not printed

**SIG-05 — Rolling average:**
- `static uint16_t s_bpm_buf[5]`, `s_bpm_idx`, `s_bpm_count` (clamped at 5)
- Index advanced via `(s_bpm_idx + 1) % 5` — ring semantics, index always 0..4
- Average computed over `s_bpm_count` entries (guaranteed >= 1 at divide site)
- `uart_write_str("BPM: "); uart_write_u32(bpm_avg); uart_write_str("\r\n");` — exactly one print site

**algorithm_init() extensions:**
- Resets all new statics: `s_state = IDLE`, `s_threshold = 1200`, `s_peak_val = 0`, `s_refractory_end = 0`
- Zeros `s_bpm_buf[0..4]`, `s_bpm_idx = 0`, `s_bpm_count = 0`, `s_last_peak_ms = 0`

## Threat Mitigations Applied

| Threat | Mitigation |
|--------|-----------|
| T-03-06: divide by zero in BPM calc | `interval_ms != 0` guard; `s_last_peak_ms == 0` sentinel skips first beat entirely |
| T-03-07: s_bpm_buf ring index overflow | `s_bpm_idx = (s_bpm_idx + 1) % 5` — index always 0..4 |
| T-03-08: rolling average out-of-range read | Loop bounded by `s_bpm_count`; `s_bpm_count >= 1` guaranteed at divide site |
| T-03-09: uninitialized state | `algorithm_init()` explicitly resets every static variable |
| T-03-10: false-positive dicrotic notch beat | 350ms REFRACTORY state ignores all samples after peak detection |

## Deviations from Plan

**Implementation note (not a deviation):** Tasks 1 and 2 share the PEAK_HOLD state as their beat-registration site — the state machine (Task 1) and BPM computation (Task 2) are tightly coupled at that single branch point. Both were implemented and committed atomically as a single write to `Src/algorithm.c`. The plan separated them conceptually for specification clarity; the implementation correctly integrates them.

No logic deviations — plan executed exactly as specified.

## Known Stubs

None. All five SIG modules (SIG-01 through SIG-05) are now fully implemented. The Plan 01 intentional stub (minimal fixed-threshold detector) has been completely replaced.

## Threat Flags

None — no new security-relevant surface introduced beyond what is in the plan's threat model.

## Self-Check

Files exist:
- `Src/algorithm.c`: FOUND (modified)
- `03-02-SUMMARY.md`: this file

Commits exist:
- `f1d29e9`: feat(03-02): implement five-state adaptive-threshold peak detector with refractory and rolling BPM — FOUND

## Self-Check: PASSED
