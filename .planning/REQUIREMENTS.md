# Requirements: HeartRateSensor

**Defined:** 2026-05-12
**Core Value:** Finger on sensor → stable, accurate BPM value visible on the serial monitor.

## v1 Requirements

### Peripheral Drivers

- [ ] **DRV-01**: System tick provides millis() via SysTick at 1ms resolution (bare-metal CMSIS, no HAL)
- [ ] **DRV-02**: USART2 transmits ASCII strings at 115200 baud over USB virtual COM port (bare-metal register access)
- [ ] **DRV-03**: ADC1 samples PA0 at exactly 100Hz via TIM2 hardware trigger (no polling jitter); ADCPRE=/6 (12MHz ADC clock); SMPR2 ≥ 28.5 cycles; RSTCAL+CAL mandatory at startup
- [ ] **DRV-04**: TIM2 generates Update TRGO at 100Hz (PSC=719, ARR=999 from 72MHz timer clock); EXTSEL resolved from RM0008 Table 68

### Signal Processing

- [ ] **SIG-01**: 32-sample integer moving average filter smooths raw ADC samples (power-of-2 window; `sum >> 5`; no floating point)
- [ ] **SIG-02**: Adaptive threshold peak detector identifies heartbeat peaks (threshold = last_peak_value × 0.6, updated each beat; integer arithmetic only)
- [ ] **SIG-03**: Refractory period of 300–400ms suppresses dicrotic notch false positives
- [ ] **SIG-04**: BPM sanity bounds reject readings outside 40–200 BPM before insertion into rolling average
- [ ] **SIG-05**: 5-reading BPM rolling average produces stable, jitter-free output

### Output & Safety

- [ ] **OUT-01**: BPM value printed over USART2 each time a heartbeat is detected (`"BPM: NN\r\n"`)
- [ ] **OUT-02**: No-finger detection: when signal amplitude < FINGER_MIN_AMPLITUDE, print `"--\r\n"` and reset adaptive threshold to initial value
- [ ] **OUT-03**: CALIBRATION_MODE (compile-time `#define`): prints ADC min/max/amplitude over 5 seconds at startup for analog circuit validation and report evidence
- [ ] **OUT-04**: DEBUG_VERBOSE mode (compile-time `#define`): prints per-sample labeled fields (`RAW`, `FILT`, `THR`, `AMP`, `STATE`) for algorithm transparency and serial screenshot evidence

### Report Deliverable

- [ ] **RPT-01**: Technical report includes: circuit schematic with component values, register configuration table, BPM algorithm flowchart, CALIBRATION_MODE serial output screenshot, DEBUG_VERBOSE serial screenshot showing threshold crossing event, BPM accuracy comparison vs. reference over 60 seconds

## v2 Requirements

### Display

- **DSP-01**: BPM value shown on I2C LCD display (16×2 character)
- **DSP-02**: LCD shows `"-- BPM"` when no finger detected

## Out of Scope

| Feature | Reason |
|---------|--------|
| HAL / LL drivers | Course requires bare-metal register access throughout |
| I2C LCD display (v1) | Deferred to v2; serial monitor is sufficient for course deliverable |
| Bluetooth / wireless | Not required for course; adds hardware complexity |
| SpO2 measurement | Hardware supports single wavelength only — physically impossible |
| FFT-based HR detection | Worse frequency resolution at 100Hz; complex for zero benefit |
| DMA for ADC | Not needed at 100Hz; ISR is sufficient and simpler |
| RTOS / FreeRTOS | Anti-feature for this project; course context prefers bare-metal |
| Float math anywhere | STM32F103 has no FPU; all algorithm math expressible as integer arithmetic |
| Dynamic memory allocation | No heap in bare-metal embedded; all buffers are static fixed-size |

## Traceability

| Requirement | Phase | Status |
|-------------|-------|--------|
| DRV-01 | Phase 1 — Firmware Scaffold + Peripheral Init | Pending |
| DRV-02 | Phase 1 — Firmware Scaffold + Peripheral Init | Pending |
| DRV-03 | Phase 2 — ADC + Timer Hardware Trigger | Pending |
| DRV-04 | Phase 2 — ADC + Timer Hardware Trigger | Pending |
| SIG-01 | Phase 3 — Algorithm Modules on Synthetic Data | Pending |
| SIG-02 | Phase 3 — Algorithm Modules on Synthetic Data | Pending |
| SIG-03 | Phase 3 — Algorithm Modules on Synthetic Data | Pending |
| SIG-04 | Phase 3 — Algorithm Modules on Synthetic Data | Pending |
| SIG-05 | Phase 3 — Algorithm Modules on Synthetic Data | Pending |
| OUT-01 | Phase 4 — Full Integration + Analog Calibration | Pending |
| OUT-02 | Phase 4 — Full Integration + Analog Calibration | Pending |
| OUT-03 | Phase 4 — Full Integration + Analog Calibration | Pending |
| OUT-04 | Phase 4 — Full Integration + Analog Calibration | Pending |
| RPT-01 | Phase 5 — Report Evidence + Polish | Pending |

**Coverage:**
- v1 requirements: 14 total
- Mapped to phases: 14
- Unmapped: 0 ✓

---
*Requirements defined: 2026-05-12*
*Last updated: 2026-05-12 after roadmap creation*
