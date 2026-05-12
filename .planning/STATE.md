# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-05-12)

**Core value:** Finger on sensor → stable, accurate BPM value visible on the serial monitor.
**Current focus:** Phase 1 — Firmware Scaffold + Peripheral Init

## Current Position

Phase: 1 of 5 (Firmware Scaffold + Peripheral Init)
Plan: 0 of TBD in current phase
Status: Ready to plan
Last activity: 2026-05-12 — Roadmap created; all 14 v1 requirements mapped across 5 phases

Progress: [░░░░░░░░░░] 0%

## Performance Metrics

**Velocity:**
- Total plans completed: 0
- Average duration: —
- Total execution time: 0h

**By Phase:**

| Phase | Plans | Total | Avg/Plan |
|-------|-------|-------|----------|
| - | - | - | - |

**Recent Trend:**
- Last 5 plans: —
- Trend: —

*Updated after each plan completion*

## Accumulated Context

### Decisions

Decisions are logged in PROJECT.md Key Decisions table.
Recent decisions affecting current work:

- [Init]: EXTSEL value for TIM2_TRGO must be resolved from RM0008 Table 68 before writing adc_init() — STACK.md and PITFALLS.md disagree
- [Init]: ADCPRE=/6 mandatory; /4 gives 18MHz which exceeds 14MHz ADC clock limit with no error flag
- [Init]: LM358 on 3.3V supply saturates at ~1.8V max output — switch to 5V USB rail if ADC amplitude is insufficient during Phase 4 calibration
- [Init]: All algorithm math must use integer arithmetic; STM32F103 has no FPU

### Pending Todos

None yet.

### Blockers/Concerns

- [Phase 2]: EXTSEL value for TIM2_TRGO is unresolved — must be read from RM0008 Table 68 before implementation; wrong value = ADC never fires
- [Phase 4]: All threshold constants (FINGER_MIN_AMPLITUDE, threshold fraction 0.6, refractory period) require empirical bench measurement with real LM358 signal

## Deferred Items

| Category | Item | Status | Deferred At |
|----------|------|--------|-------------|
| Display | DSP-01: I2C LCD BPM display | v2 | Init |
| Display | DSP-02: LCD no-finger indicator | v2 | Init |

## Session Continuity

Last session: 2026-05-12
Stopped at: Roadmap and STATE created; next action is /gsd-plan-phase 1
Resume file: None
