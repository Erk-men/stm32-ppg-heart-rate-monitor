# Roadmap: HeartRateSensor

## Overview

Five sequential phases deliver a bare-metal STM32F103RBT6 PPG heart rate monitor from an empty project to a working hardware demo with a complete technical report. Phases 1–3 are entirely software-verifiable and can be completed before the breadboard circuit is assembled. Phase 4 requires real hardware and empirical threshold calibration. Phase 5 captures all report evidence.

## Phases

**Phase Numbering:**
- Integer phases (1, 2, 3): Planned milestone work
- Decimal phases (2.1, 2.2): Urgent insertions (marked with INSERTED)

Decimal phases appear between their surrounding integers in numeric order.

- [ ] **Phase 1: Firmware Scaffold + Peripheral Init** - SysTick millis() and USART2 serial driver running; toolchain and ST-Link confirmed
- [ ] **Phase 2: ADC + Timer Hardware Trigger** - TIM3 hardware-triggers ADC1 at exactly 100Hz; raw values stream over serial
- [ ] **Phase 3: Algorithm Modules on Synthetic Data** - All signal-processing modules proven correct on a synthetic sine table before real hardware
- [ ] **Phase 4: Full Integration + Analog Calibration** - Real finger produces stable BPM; all thresholds empirically tuned
- [ ] **Phase 5: Report Evidence + Polish** - Complete report package assembled and verified

## Phase Details

### Phase 1: Firmware Scaffold + Peripheral Init
**Goal**: The project builds, flashes, and prints a live timestamp over serial — confirming the entire toolchain, ST-Link programmer, and USB virtual COM port work before any peripheral complexity is added
**Mode**: mvp
**Depends on**: Nothing (first phase)
**Requirements**: DRV-01, DRV-02
**Success Criteria** (what must be TRUE):
  1. STM32CubeIDE project compiles cleanly with bare-metal CMSIS headers and zero HAL dependencies
  2. `millis()` increments at 1ms resolution (SysTick LOAD=71999, HCLK source) and is readable in the debugger
  3. Serial terminal (115200 baud) shows `"millis: NNNN\r\n"` updating every second — confirming ST-Link USB COM port end-to-end
  4. Flashing succeeds via ST-Link without error; board runs autonomously after power cycle
**Plans**: 2 plans
Plans:
- [ ] 01-01-PLAN.md — Project scaffold + SysTick millis() driver (DRV-01)
- [ ] 01-02-PLAN.md — USART2 polling driver + main loop "millis: NNNN" output (DRV-02)

### Phase 2: ADC + Timer Hardware Trigger
**Goal**: ADC1 samples PA0 at exactly 100Hz driven by TIM3 TRGO — no polling jitter — and streams raw 12-bit values over serial, proving the hardware trigger chain is correctly wired before any algorithm work begins (STM32F070RB: uses TIM3 + HSI14 ADC clock + single-step ADCAL)
**Mode**: mvp
**Depends on**: Phase 1
**Requirements**: DRV-03, DRV-04
**Success Criteria** (what must be TRUE):
  1. Raw ADC values print over serial at 100Hz (one line per sample, no bursts or gaps)
  2. PA0 tied to 3.3V (VDDA) reads ≈ 4095; PA0 tied to GND reads ≈ 0 — confirming 12-bit range and HSI14 ADC clock (14MHz)
  3. EXTSEL bits in ADC1->CFGR1 = 011 (TIM3_TRGO); ADC fires only when TIM3 update event fires
  4. ADC calibration (single ADCAL step) completes at startup with no hang
**Plans**: 2 plans
Plans:
- [x] 02-01-PLAN.md — TIM3 100Hz TRGO driver + ADC1 hardware-triggered ISR driver (DRV-03, DRV-04)
- [ ] 02-02-PLAN.md — Wire drivers into main.c, stream "adc: NNNN\r\n" at 100Hz, human-verify range + rate (DRV-03, DRV-04)

### Phase 3: Algorithm Modules on Synthetic Data
**Goal**: All five signal-processing modules — moving average, adaptive threshold peak detector, refractory suppression, BPM bounds check, and rolling average — are implemented as pure-C functions and produce stable 60 BPM output from a 100-step synthetic sine table in main.c, proving algorithmic correctness before any real signal is involved
**Mode**: mvp
**Depends on**: Phase 2
**Requirements**: SIG-01, SIG-02, SIG-03, SIG-04, SIG-05
**Success Criteria** (what must be TRUE):
  1. A 100-entry integer sine table (one full PPG cycle) fed at 100Hz produces a steady `"BPM: 60\r\n"` reading on the serial terminal
  2. Serial output shows state machine transitions (IDLE → RISING → PEAK_HOLD → FALLING → REFRACTORY) making algorithm internals visible
  3. Dicrotic-notch double-counting does not occur: refractory period (300–400ms) suppresses any secondary peak within the same cycle
  4. BPM values outside 40–200 are silently rejected and do not corrupt the 5-reading rolling average
**Plans**: TBD

### Phase 4: Full Integration + Analog Calibration
**Goal**: The live ADC sample stream replaces the synthetic sine table; the full firmware stack runs on real LM358 PPG signal; all threshold constants are empirically measured and locked in; stable BPM output is confirmed from a real finger
**Mode**: mvp
**Depends on**: Phase 3
**Requirements**: OUT-01, OUT-02, OUT-03, OUT-04
**Success Criteria** (what must be TRUE):
  1. Real finger on sensor produces `"BPM: NN\r\n"` output that stays within ±5 BPM of a reference pulse oximeter reading over 30 seconds
  2. `CALIBRATION_MODE` printout at startup shows ADC min/max/amplitude values confirming LM358 signal is usable (amplitude > FINGER_MIN_AMPLITUDE counts); if LM358 is saturated at 3.3V, op-amp supply switched to 5V and amplitude re-verified
  3. Removing the finger causes `"--\r\n"` within 2 seconds and adaptive threshold resets to initial value so re-acquisition succeeds on next finger placement
  4. `DEBUG_VERBOSE` serial output shows labeled `RAW / FILT / THR / AMP / STATE` fields; at least one threshold-crossing event is visible in the capture
**Plans**: TBD

### Phase 5: Report Evidence + Polish
**Goal**: All deliverable evidence is collected, formatted, and assembled into the technical report — circuit schematic, register configuration table, algorithm flowchart, annotated serial screenshots, and a 60-second BPM accuracy table
**Mode**: mvp
**Depends on**: Phase 4
**Requirements**: RPT-01
**Success Criteria** (what must be TRUE):
  1. Circuit schematic is drawn with all component values labeled (IR LED + 10KΩ, BPW34, LM358 TIA, HPF 0.5Hz, ×100 gain stage, LPF 15.9Hz, PA0 connection)
  2. Register configuration table documents every peripheral register written (RCC, GPIO, TIM3, ADC1, USART2, SysTick) with the hex value and plain-English rationale
  3. BPM accuracy table covers 60 seconds of capture and reports mean BPM, standard deviation, and reference pulse oximeter value
  4. Report includes CALIBRATION_MODE screenshot showing ADC amplitude and DEBUG_VERBOSE screenshot showing at least one clean threshold-crossing event
**Plans**: TBD

## Progress

**Execution Order:**
Phases execute in numeric order: 1 → 2 → 3 → 4 → 5

| Phase | Plans Complete | Status | Completed |
|-------|----------------|--------|-----------|
| 1. Firmware Scaffold + Peripheral Init | 0/2 | Planned | - |
| 2. ADC + Timer Hardware Trigger | 0/2 | Planned | - |
| 3. Algorithm Modules on Synthetic Data | 0/TBD | Not started | - |
| 4. Full Integration + Analog Calibration | 0/TBD | Not started | - |
| 5. Report Evidence + Polish | 0/TBD | Not started | - |
