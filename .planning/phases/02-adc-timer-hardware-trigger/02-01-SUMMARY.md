---
phase: 02-adc-timer-hardware-trigger
plan: 01
subsystem: firmware-drivers
tags: [stm32f070, adc, tim3, trgo, hardware-trigger, bare-metal, cortex-m0, hsi14]

# Dependency graph
requires:
  - phase: 01-firmware-scaffold-peripheral-init
    provides: GPIOA clock enabled via usart2_init, stm32f0xx.h include pattern, NVIC API confirmed working

provides:
  - tim3.h / tim3.c: TIM3 driver generating 100 Hz TRGO at HSI 8 MHz (PSC=79, ARR=999)
  - adc.h / adc.c: ADC1 hardware-triggered driver sampling PA0 via TIM3_TRGO, EOC ISR storing 12-bit result in g_adc_sample
  - g_adc_sample / g_adc_ready volatile globals for ISR-to-main communication

affects:
  - 02-02: main.c integration of tim3_init() and adc_init() calls
  - 04-ppg-algorithm: consumes g_adc_sample and g_adc_ready from this driver

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "HSI14 async ADC clock path: RCC->CR2 HSI14ON + CFGR2 CKMODE=00 (default)"
    - "F070 single-step ADC calibration: ADC_CR_ADCAL set + poll, before ADEN"
    - "TIM3 TRGO trigger chain: TIM_CR2_MMS_1 (MMS=010) -> EXTSEL=011 -> ADC1"
    - "CFGR1 configured before ADEN to avoid write-lock"

key-files:
  created:
    - Inc/tim3.h
    - Src/tim3.c
    - Inc/adc.h
    - Src/adc.c
  modified: []

key-decisions:
  - "TIM3 used instead of TIM2 (TIM2 absent on STM32F070xB silicon)"
  - "PSC=79 (not PSC=719) because F070 has no APB timer clock doubling at 8 MHz"
  - "HSI14 async ADC clock (CKMODE=00) chosen over PCLK/N for maximum 14 MHz ADC speed"
  - "EXTSEL=011 (ADC_CFGR1_EXTSEL_0|EXTSEL_1) for TIM3_TRGO; EXTSEL=010 would select absent TIM2"
  - "SMP=111 (239.5 cycles) for maximum SNR; PPG bandwidth is <10 Hz so timing headroom is large"
  - "Reading ADC1->DR in ISR clears EOC automatically; explicit w1c not needed"

patterns-established:
  - "New peripheral headers go in Inc/ (not Src/ anomaly from Phase 1 systick.h)"
  - "ISR lives in same .c file as its init function (matches systick.c pattern)"
  - "Init function handles NVIC priority + enable internally"
  - "0x0FFF mask on ADC1->DR read to isolate 12-bit result from reserved bits"

requirements-completed:
  - DRV-03
  - DRV-04

# Metrics
duration: 20min
completed: 2026-05-19
---

# Phase 2 Plan 01: ADC + TIM3 Hardware Trigger Drivers Summary

**TIM3 100 Hz TRGO driver and ADC1 hardware-triggered ISR driver for STM32F070xB, implementing the deterministic TIM3 -> ADC1 -> ISR sampling chain at 100 Hz via HSI14 clock and EXTSEL=011**

## Performance

- **Duration:** ~20 min
- **Started:** 2026-05-19T00:00:00Z
- **Completed:** 2026-05-19
- **Tasks:** 2
- **Files modified:** 4 (all created new)

## Accomplishments

- TIM3 driver: PSC=79, ARR=999, CR2=TIM_CR2_MMS_1 generates exactly 100 Hz TRGO from HSI 8 MHz with zero software involvement after init
- ADC1 driver: 11-step initialization sequence (PA0 analog, HSI14, ADCAL before ADEN, CFGR1 before ADEN, CHSELR, IER, NVIC, ADEN, ADSTART) strictly following F070 initialization order requirements
- ADC1_IRQHandler fires on EOC, stores 12-bit DR result in g_adc_sample, sets g_adc_ready=1 for main loop consumption

## Task Commits

Each task was committed atomically:

1. **Task 1: Create tim3.h and tim3.c -- 100 Hz TRGO driver** - `22a6d49` (feat)
2. **Task 2: Create adc.h and adc.c -- ADC1 hardware-triggered ISR driver** - `bbcd988` (feat)

**Plan metadata:** (docs commit follows)

## Files Created/Modified

- `Inc/tim3.h` - Header guard + `void tim3_init(void)` declaration
- `Src/tim3.c` - TIM3 driver: RCC enable, PSC=79, ARR=999, CR2=TIM_CR2_MMS_1, CR1=TIM_CR1_CEN
- `Inc/adc.h` - Header guard, stdint.h, extern volatile g_adc_sample/g_adc_ready, adc_init() declaration
- `Src/adc.c` - ADC1 init (11-step sequence) + ADC1_IRQHandler

All four files mirrored to CubeIDE workspace at:
- `/home/erkmen/STM32CubeIDE/workspace_2.1.1/HeartRateSensor1/Inc/` (tim3.h, adc.h)
- `/home/erkmen/STM32CubeIDE/workspace_2.1.1/HeartRateSensor1/Src/` (tim3.c, adc.c)

## Decisions Made

- TIM3 selected as TIM2 replacement: STM32F070xB has no TIM2 in silicon. TIM3 is the only general-purpose timer with CR2.MMS TRGO capability on this part.
- PSC=79 (not legacy PSC=719): F070 has a single APB bus with no timer clock doubling. Timer clock = 8 MHz flat. PSC=79 gives 100 kHz counter; PSC=719 would give 11.1 Hz (wrong by 9x).
- HSI14 async clock (CKMODE=00): Dedicated 14 MHz RC oscillator for ADC; avoids ADCPRE=/6 F103 pattern which does not apply to F070.
- EXTSEL=011 verified: Two independent StdPeriph sources confirm TIM3_TRGO = EXTSEL[2:0]=011 on F0. EXTSEL=010 would select TIM2_TRGO (absent device).
- No EGR.UG at TIM3 startup: Writing EGR.UG would cause a spurious first TRGO pulse; letting counter run from zero avoids this.

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

During initial execution, Task 1 was accidentally committed to the `master` branch instead of the `worktree-agent-*` branch (cwd drift to main repo). The commit was immediately reverted via `git reset --hard HEAD~1` on master, and the files were re-created and committed correctly on the `worktree-agent-a3b90aee8b08ab2c8` branch.

## Threat Model Coverage

All mitigations from the plan's threat register were implemented:
- T-02-01: `0x0FFF` mask on ADC1->DR read prevents reserved bit bleed into g_adc_sample
- T-02-02: HSI14 polled via `while (!(RCC->CR2 & RCC_CR2_HSI14RDY))` before ADC enable
- T-02-03: ADCAL at step 4, ADEN at step 10 -- code order enforces calibration-before-enable
- T-02-04: Reading ADC1->DR in ISR clears EOC automatically; no runaway ISR possible

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- tim3_init() and adc_init() are ready to be called from main.c (Plan 02-02)
- g_adc_sample and g_adc_ready globals are declared extern in adc.h for main loop access
- No blockers; CubeIDE project will compile once tim3.c and adc.c are added to the Sources build list (right-click Src folder -> "Add to Build" if not auto-discovered)

---
*Phase: 02-adc-timer-hardware-trigger*
*Completed: 2026-05-19*
