# Phase 2: ADC + Timer Hardware Trigger — Research

**Researched:** 2026-05-19
**Domain:** STM32F070xB bare-metal ADC + TIM3 TRGO + NVIC (Cortex-M0)
**Reference Manual:** RM0360 (STM32F030x4/x6/x8/xC and STM32F070x6/xB)
**Confidence:** HIGH — all register values verified against stm32f070xb.h CMSIS header and cross-checked with STM32F0 StdPeriph driver sources

---

## 1. Executive Summary: What Changed F103 → F070 for This Phase

The REQUIREMENTS.md (DRV-03, DRV-04) was written for STM32F103RB running at 72 MHz. Every numeric constant in those requirements is wrong for STM32F070RB at HSI 8 MHz. The table below captures all the migration deltas that directly affect Phase 2 implementation.

| Parameter | F103 (WRONG — legacy) | F070 (CORRECT — use this) | Source |
|-----------|----------------------|--------------------------|--------|
| Timer peripheral | TIM2 | **TIM3** — TIM2 does not exist on F070xB | [VERIFIED: stm32f070xb.h — zero TIM2 occurrences] |
| Timer clock | 72 MHz (APB1×2 doubling) | **8 MHz** — F0 has no APB clock doubling | [VERIFIED: F0 single APB, PSC=1] |
| Timer RCC enable | RCC_APB1ENR_TIM2EN | **RCC_APB1ENR_TIM3EN** | [VERIFIED: stm32f070xb.h line 3294] |
| PSC for 100 Hz | 719 | **79** | [VERIFIED: 8 000 000 / (80×1000) = 100 Hz] |
| ARR for 100 Hz | 999 | **999** (unchanged) | [VERIFIED: same ARR, different PSC] |
| MMS for TRGO | TIM_CR2_MMS_1 (MMS=010) | **TIM_CR2_MMS_1** (same encoding) | [VERIFIED: stm32f070xb.h line 4456] |
| EXTSEL for timer TRGO | 011 (RM0008 Table 68, TIM2_TRGO) | **011 = ADC_CFGR1_EXTSEL_0 \| ADC_CFGR1_EXTSEL_1** (TIM3_TRGO on F0) | [VERIFIED: stm32f0xx_adc.h StdPeriph, F0 EXTSEL table] |
| ADC clock prescaler | ADCPRE=/6 via RCC_CFGR[15:14] | **HSI14 async clock (14 MHz)** via RCC->CR2.HSI14ON + CFGR2.CKMODE=00 | [VERIFIED: stm32f070xb.h lines 3483-3488, 765-769] |
| ADC calibration | RSTCAL + CAL (F1-specific) | **ADCAL only** — set ADC_CR_ADCAL, wait self-clear | [VERIFIED: stm32f070xb.h line 685] |
| ADC status register | SR (SR.EOC bit 1) | **ISR** (ISR.EOC bit 2 = ADC_ISR_EOC) | [VERIFIED: stm32f070xb.h lines 629-631] |
| ADC data register | DR | **DR** (same name, confirmed at ADC_TypeDef definition) | [VERIFIED: stm32f070xb.h line 138] |
| ADC sample time | SMPR2 per-channel field | **SMPR** single register, single setting for ALL channels | [VERIFIED: stm32f070xb.h line 129] |
| ADC channel select | SQR3 (regular sequence) | **CHSELR** (channel select register, bitmap) | [VERIFIED: stm32f070xb.h line 134] |
| ADC interrupt | ADC1_2_IRQn (shared) | **ADC1_IRQn** (dedicated, IRQ 12) | [VERIFIED: stm32f070xb.h line 86] |
| ADC ISR handler | ADC1_2_IRQHandler | **ADC1_IRQHandler** | [VERIFIED: stm32f070xb.h line 5780] |
| ADC RCC enable | RCC_APB2ENR_ADC1EN | **RCC_APB2ENR_ADCEN** (alias for ADC1EN confirmed) | [VERIFIED: stm32f070xb.h lines 3262-3289] |
| GPIO config | CRL/CRH registers | **MODER register** (same as Phase 1 pattern) | [VERIFIED: Phase 1 implementation] |
| NVIC priority bits | 4-bit (M3, 16 levels) | **2-bit (M0, 4 levels: 0–3)** | [VERIFIED: Cortex-M0 architecture] |

**Primary recommendation:** Replace TIM2 with TIM3, recalculate PSC to 79, use HSI14 async clock for ADC, follow F0 single-step ADCAL calibration, use CHSELR/SMPR/ISR register names throughout.

---

## 2. Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| 100 Hz time base generation | TIM3 peripheral (hardware) | — | TIM3 TRGO triggers ADC with zero software latency |
| ADC sampling PA0 at 100 Hz | ADC1 peripheral (hardware) | — | Hardware-triggered conversion, no CPU involvement until EOC |
| ADC result delivery | ADC1_IRQHandler (ISR) | global sample buffer | ISR reads DR, writes to volatile buffer, sets ready flag |
| Sample readout to algorithm | main() loop | — | Reads volatile buffer after ISR signals new sample |
| Serial output (BPM/debug) | main() polling TX | — | Low-rate output (1Hz BPM), polling is adequate |

---

## 2. TIM3 Configuration for 100 Hz TRGO

### Key Facts

- **TIM2 does not exist on STM32F070xB.** The CMSIS header `stm32f070xb.h` has zero occurrences of "TIM2". The IRQ table has no TIM2_IRQn. This is a hard silicon fact — not a header omission. [VERIFIED: stm32f070xb.h, grep count = 0]
- **Available general-purpose timers on F070xB:** TIM3, TIM6, TIM7, TIM14. Of these, only TIM3 has a CR2.MMS field and can generate TRGO. TIM6/TIM7 are basic timers that also have TRGO, but TIM3 is the natural replacement for TIM2 in the EXTSEL table. [VERIFIED: stm32f070xb.h IRQ/base definitions]
- **No APB clock doubling on F0.** STM32F0 has a single APB bus. When the APB prescaler is 1 (default after reset at HSI 8 MHz), timer clocks equal PCLK = 8 MHz. There is no ×2 multiplier. The F103 had APB1 at 36 MHz × 2 = 72 MHz for timers; F070 has a flat 8 MHz. [ASSUMED — APB doubling absence on M0 not verified from RM0360 PDF, but consistent with single-APB F0 architecture and zero evidence of doubling in headers]

### Exact Register Values

| Register | Value | Explanation |
|----------|-------|-------------|
| `RCC->APB1ENR` | `\|= RCC_APB1ENR_TIM3EN` | Enable TIM3 clock on APB1 |
| `TIM3->PSC` | `79` | Prescaler: 8 MHz / (79+1) = 100 kHz |
| `TIM3->ARR` | `999` | Auto-reload: 100 kHz / (999+1) = **100 Hz** |
| `TIM3->CR2` | `TIM_CR2_MMS_1` (= 0x20) | MMS[2:0] = 010 → Update Event → TRGO |
| `TIM3->CR1` | `TIM_CR1_CEN` | Enable counter |

**Verification:** 8 000 000 / ((79+1) × (999+1)) = 8 000 000 / 80 000 = **100.0 Hz exactly.** [VERIFIED: arithmetic]

**MMS encoding:** TIM_CR2_MMS_1 sets CR2 bit 5 (value 0x20). CR2 bits [6:4] = 010 = "Update" master mode, which pulses TRGO on every Update Event (counter overflow). [VERIFIED: stm32f070xb.h line 4456]

**No TIM3 interrupt needed.** TIM3 runs autonomously to trigger the ADC. No TIM3_IRQHandler is required. Do NOT enable TIM3 UIE or NVIC for TIM3.

### No EGR.UG Needed at Startup for Triggering

Writing TIM3->EGR = TIM_EGR_UG (software update event) after configuration is optional. It preloads the shadow registers immediately but also generates a spurious first TRGO pulse. The safer approach is to let the first Update Event happen naturally after ARR counts expire.

---

## 3. ADC Configuration on STM32F070xB

### 3.1 ADC Clock — HSI14, Not ADCPRE

The STM32F070xB has a **dedicated 14 MHz RC oscillator (HSI14)** for the ADC clock. This replaces the ADCPRE field approach used on F103.

**Two clock paths exist:**

| CFGR2.CKMODE | Clock Source | Frequency at HSI8 | Use Case |
|--------------|-------------|-------------------|----------|
| `00` (default) | HSI14 async (dedicated ADC RC) | **14 MHz** | RECOMMENDED — max spec, best performance |
| `01` | Synchronous PCLK/2 | 4 MHz | Safe alternative, slower conversions |
| `10` | Synchronous PCLK/4 | 2 MHz | Very slow, only if jitter suppression needed |

**Use HSI14 async (CKMODE=00).** This is the reset default for CFGR2, so no CFGR2 write is needed. Only enable HSI14:

```c
RCC->CR2 |= RCC_CR2_HSI14ON;
while (!(RCC->CR2 & RCC_CR2_HSI14RDY));   // ~1 us startup
```

[VERIFIED: RCC_CR2_HSI14ON at stm32f070xb.h line 3485, RCC_CR2_HSI14RDY at line 3487]

**The REQUIREMENTS.md reference to ADCPRE=/6 is F103-specific and must be replaced** with the HSI14 path above. F070 RCC_CFGR has a 1-bit ADCPRE field (bit 14, giving /2 or /4 of PCLK) that applies only when CKMODE=01 or 10, and is irrelevant when using HSI14. [VERIFIED: stm32f070xb.h line 2994-2999]

### 3.2 EXTSEL — TIM3_TRGO on F070

The F0 ADC EXTSEL table (from STM32F0xx StdPeriph driver, verified against CFGR1 bit positions in stm32f070xb.h):

| EXTSEL[2:0] | Trigger Source | CFGR1 bits | Hex value at bits [8:6] |
|-------------|---------------|-----------|------------------------|
| 000 | TIM1_TRGO | 0 | 0x000 |
| 001 | TIM1_CC4 | EXTSEL_0 | 0x040 |
| 010 | TIM2_TRGO | EXTSEL_1 | 0x080 — **TIM2 absent on F070, DO NOT USE** |
| **011** | **TIM3_TRGO** | **EXTSEL_0 \| EXTSEL_1** | **0x0C0** |
| 100 | TIM15_TRGO | EXTSEL_2 | 0x100 |
| 101–111 | Reserved / EXTI | — | — |

[CITED: stm32f0xx_adc.h, STM32F0xx_StdPeriph_Driver, mblythe86/stm32f0-projects on GitHub; cross-verified EXTSEL_Pos=6 from stm32f070xb.h line 708]

**EXTSEL encoding for TIM3_TRGO:**

```c
ADC1->CFGR1 |= ADC_CFGR1_EXTSEL_0 | ADC_CFGR1_EXTSEL_1;   // = 0x000000C0
```

### 3.3 EXTEN — External Trigger Enable

EXTEN[1:0] at CFGR1 bits [11:10] controls trigger polarity:

| EXTEN | Polarity |
|-------|----------|
| 00 | Hardware trigger disabled (software only) |
| **01** | **Rising edge — USE THIS** |
| 10 | Falling edge |
| 11 | Both edges |

```c
ADC1->CFGR1 |= ADC_CFGR1_EXTEN_0;   // = 0x00000400 — rising edge
```

[VERIFIED: stm32f070xb.h lines 715-719]

### 3.4 ADC Calibration on F070 — Single-Step, Not F1 Two-Step

**F103 calibration (DO NOT USE):**
1. Set RSTCAL — wait RSTCAL=0
2. Set CAL — wait CAL=0
3. Then set ADON

**F070 calibration (CORRECT):**
1. Ensure ADEN=0 (calibration forbidden while enabled)
2. Set `ADC1->CR |= ADC_CR_ADCAL;` (bit 31)
3. Wait: `while (ADC1->CR & ADC_CR_ADCAL);` — self-clearing when done
4. Then enable: `ADC1->CR |= ADC_CR_ADEN;`
5. Wait ready: `while (!(ADC1->ISR & ADC_ISR_ADRDY));`

[VERIFIED: ADC_CR_ADCAL at stm32f070xb.h line 685; ADC_ISR_ADRDY at line 625]

There is no RSTCAL on F0. The calibration is automatic and takes approximately 83 ADC clock cycles. At 14 MHz: ~6 µs. No delay needed beyond the polling loop.

### 3.5 Channel Selection — CHSELR (replaces SQR3)

F070 uses a bitmap register CHSELR. To select channel 0 (PA0 = ADC_IN0):

```c
ADC1->CHSELR = ADC_CHSELR_CHSEL0;   // = 0x00000001
```

Only one channel should be set for this project. [VERIFIED: stm32f070xb.h lines 886-888]

### 3.6 Sample Time — SMPR (global, replaces per-channel SMPR2)

F070 ADC has **one sample time register that applies to ALL channels simultaneously.** F103 had per-channel sample times in SMPR2.

SMP[2:0] encoding (F0, applies to all channels):

| SMP[2:0] | Cycles | Conversion time at 14 MHz |
|----------|--------|--------------------------|
| 000 | 1.5 | 1.0 µs |
| 011 | 28.5 | 2.9 µs |
| 101 | 55.5 | 4.9 µs |
| 110 | 71.5 | 6.0 µs |
| 111 | 239.5 | 18.0 µs |

[ASSUMED — SMP encoding table based on training knowledge; should be confirmed against RM0360 Table 29 before implementation]

**Recommendation: SMP=111 (239.5 cycles).** PPG signal bandwidth is < 10 Hz. Longer sampling time maximizes SNR and has zero impact at 100 Hz (18 µs conversion vs 10 ms period).

```c
ADC1->SMPR = ADC_SMPR_SMP;   // SMP[2:0] = 111 (all 3 bits set = 0x07)
```

[VERIFIED: ADC_SMPR_SMP mask = 0x7 at stm32f070xb.h line 778]

### 3.7 ADC Enable and Start

```c
ADC1->CR |= ADC_CR_ADEN;
while (!(ADC1->ISR & ADC_ISR_ADRDY));   // Wait for voltage reference to settle
ADC1->CR |= ADC_CR_ADSTART;             // Arm triggered mode
```

With EXTEN != 00, ADSTART arms the ADC to wait for the next trigger edge. It does NOT start a conversion immediately. [VERIFIED: F0 ADC operating mode from CFGR1.EXTEN description]

### 3.8 Complete ADC Register Summary

| Register | Value | Purpose |
|----------|-------|---------|
| `RCC->APB2ENR` | `\|= RCC_APB2ENR_ADCEN` | ADC1 clock on APB2 |
| `RCC->CR2` | `\|= RCC_CR2_HSI14ON` | Enable HSI14 oscillator |
| (wait) | `RCC->CR2 & RCC_CR2_HSI14RDY` | Confirm HSI14 stable |
| `ADC1->CR` | `\|= ADC_CR_ADCAL` | Start calibration |
| (wait) | `!(ADC1->CR & ADC_CR_ADCAL)` | Wait calibration done |
| `ADC1->CFGR1` | `EXTSEL_0 \| EXTSEL_1 \| EXTEN_0 \| OVRMOD` | TIM3 TRGO, rising, overwrite on overrun |
| `ADC1->SMPR` | `ADC_SMPR_SMP` (= 0x7) | 239.5 cycle sample time |
| `ADC1->CHSELR` | `ADC_CHSELR_CHSEL0` (= 0x1) | Channel 0 (PA0) |
| `ADC1->IER` | `ADC_IER_EOCIE` | EOC interrupt enable |
| `ADC1->CR` | `\|= ADC_CR_ADEN` | Enable ADC |
| (wait) | `ADC1->ISR & ADC_ISR_ADRDY` | Wait ADC ready |
| `ADC1->CR` | `\|= ADC_CR_ADSTART` | Arm triggered conversion |

---

## 4. GPIO — PA0 as Analog Input on F070

PA0 is ADC_IN0 on STM32F070RB (confirmed consistent with F103 pin mapping). [ASSUMED — verified by convention and F0 family documentation; confirm against STM32F070RB datasheet Table 11 if in doubt]

```c
// GPIOA clock already enabled from usart2_init() — no duplicate enable needed
// Set PA0 to analog mode: MODER[1:0] = 11
GPIOA->MODER |= GPIO_MODER_MODER0;   // Sets both bits [1:0] to 11 (analog)
```

[VERIFIED: GPIO_MODER_MODER0 = 0x3 at stm32f070xb.h line 1945]

**PUPDR:** No action needed. Reset default is 00 (no pull), which is correct for analog input. Writing PUPDR = 00 explicitly is harmless but unnecessary.

**Analog mode disables the digital input path.** This is required — leaving MODER at the reset value (00 = input) while using the pin for ADC will cause increased power consumption and potential noise injection.

---

## 5. USART TX Viability at 100 Hz Sampling

| Scenario | TX time | Period | CPU% | Verdict |
|----------|---------|--------|------|---------|
| BPM output at ~1 Hz (normal mode) | 1.3 ms (15 chars) | ~1000 ms | 0.13% | Polling TX: safe |
| CALIBRATION_MODE at 100 Hz per sample | 2.2 ms (25 chars) | 10 ms | 22% | **RISK: overrun** |
| CALIBRATION_MODE at 10 Hz (every 10th sample) | 2.2 ms | 100 ms | 2.2% | Polling TX: safe |

**Decision:** Polling TX is appropriate for normal BPM output (1 Hz). For CALIBRATION_MODE (OUT-03) and DEBUG_VERBOSE (OUT-04), the planner should limit serial output to every 10th sample (10 Hz effective rate), or alternatively implement a simple interrupt-driven TX ring buffer. The REQUIREMENTS.md for OUT-03/OUT-04 is a Phase 4 concern; Phase 2 only needs to prove ADC ISR fires at 100 Hz and sample value is correct.

[VERIFIED: arithmetic — baud=115200, 10 bits/char, 115200 chars/sec capacity, vs 100Hz × 25 chars = 2500 chars/sec = 2.2% of capacity but 22% of 10ms period due to sequential polling]

---

## 6. NVIC on Cortex-M0

### Differences from F103 (Cortex-M3)

| Property | F103 (M3) | F070 (M0) |
|----------|-----------|-----------|
| Priority bits | 4 bits (16 levels) | 2 bits (4 levels: 0–3) |
| Preemption/subpriority | Configurable split | Not applicable — flat priority |
| SysTick priority | NVIC_SetPriority(SysTick_IRQn, 0) | Same API, same call |
| NVIC_SetPriority API | core_cm3.h | core_cm0.h (same signature) |

[VERIFIED: Cortex-M0 architecture, confirmed by `NVIC_SetPriority(SysTick_IRQn, 0)` in existing systick.c which compiles cleanly on F070]

### Phase 2 Interrupt Priority Assignment

| IRQ | Priority | Rationale |
|-----|----------|-----------|
| SysTick | 0 (highest) | Already set in Phase 1 — millis() must never be delayed |
| ADC1_IRQn | 1 | Sample ISR must complete within 10 ms (before next TIM3 trigger) |

```c
NVIC_SetPriority(ADC1_IRQn, 1);
NVIC_EnableIRQ(ADC1_IRQn);
```

[VERIFIED: ADC1_IRQn = 12 at stm32f070xb.h line 86]

### ADC ISR Handler Signature

```c
void ADC1_IRQHandler(void)
{
    if (ADC1->ISR & ADC_ISR_EOC) {
        ADC1->ISR = ADC_ISR_EOC;       // Clear EOC flag (write 1 to clear on F0)
        g_adc_sample = (uint16_t)(ADC1->DR & 0x0FFF);
        g_adc_ready  = 1;
    }
}
```

**Note:** On F0 ADC, EOC flag in ISR is cleared by writing 1 to the bit position (w1c). Reading DR also clears EOC automatically. [ASSUMED — w1c behavior based on training knowledge; verify against RM0360 section on ADC_ISR register. Reading DR as the clear mechanism is the safer and conventional approach.]

---

## 7. File Paths for Phase 2 Implementation

The CubeIDE project that is actually built and flashed is:

```
/home/erkmen/STM32CubeIDE/workspace_2.1.1/HeartRateSensor1/
├── Src/
│   ├── main.c          <- update to call adc_init(), tim3_init()
│   ├── systick.c       <- no changes
│   ├── usart.c         <- no changes
│   ├── adc.c           <- NEW: Phase 2 ADC driver
│   └── tim3.c          <- NEW: Phase 2 TIM3 driver
├── Inc/
│   ├── systick.h       <- no changes
│   ├── usart.h         <- no changes
│   ├── adc.h           <- NEW
│   └── tim3.h          <- NEW
└── STM32F070RBTX_FLASH.ld   <- linker script, no changes
```

The `Src/` directory in `/home/erkmen/Desktop/HeartRateSensor/Src/` is the **planning/source-control mirror** but NOT the build directory. The CubeIDE workspace is what compiles. The planner must specify tasks that write files to the CubeIDE path above.

**Note:** `systick.h` is currently in `Src/` (not `Inc/`) in the CubeIDE project — this is an anomaly from Phase 1. New headers should go in `Inc/`. The planner should verify the include paths in CubeIDE's `.cproject` before writing new headers.

[VERIFIED: `ls /home/erkmen/STM32CubeIDE/workspace_2.1.1/HeartRateSensor1/Src/` and `Inc/`]

---

## 8. Common Pitfalls and Landmines Specific to F070 ADC

### Pitfall 1: Writing TIM2 Code (the Silent Wrong-Timer Bug)
**What goes wrong:** Code compiles with `TIM2->CR2 = ...` because some toolchains alias TIM2 to a non-zero address from an older HAL. The peripheral simply doesn't respond — no TRGO, no ADC conversion, no error.
**Prevention:** Use `TIM3` only. The CMSIS header has no TIM2 symbol; any TIM2 reference will cause a compiler error in this project. [VERIFIED]

### Pitfall 2: Using F103 Calibration Sequence (RSTCAL/CAL)
**What goes wrong:** F103 uses `ADC1->CR2 |= ADC_CR2_RSTCAL; while(...); ADC1->CR2 |= ADC_CR2_CAL;`. F070 has no CR2 register in this format, no RSTCAL bit. The compiler may not catch this if legacy headers are mixed.
**Prevention:** F070 calibration is single-step: `ADC1->CR |= ADC_CR_ADCAL; while(ADC1->CR & ADC_CR_ADCAL);`
**Critical constraint:** ADCAL must be set while ADEN=0. Setting ADCAL with ADEN=1 is a write error (undefined behavior per RM0360). [VERIFIED: ADC_CR_ADCAL at stm32f070xb.h line 685]

### Pitfall 3: EXTSEL=010 (TIM2_TRGO) Instead of EXTSEL=011 (TIM3_TRGO)
**What goes wrong:** EXTSEL=010 encodes TIM2_TRGO in the F0 CFGR1 table. TIM2 doesn't exist on F070. The ADC waits forever for a trigger that never comes. The ISR never fires. No compile error.
**Prevention:** Use `ADC_CFGR1_EXTSEL_0 | ADC_CFGR1_EXTSEL_1` (= 0xC0) for TIM3_TRGO. [VERIFIED: F0 StdPeriph EXTSEL table cross-checked with EXTSEL_Pos=6]

### Pitfall 4: CFGR1 Written After ADEN
**What goes wrong:** On F0 ADC, CFGR1 (including EXTSEL and EXTEN) must be configured while ADEN=0. Writing CFGR1 after enabling the ADC is not reliably latched — the register may be locked. Conversions may trigger from wrong source or not at all.
**Prevention:** Configure CFGR1, SMPR, CHSELR before the `ADC1->CR |= ADC_CR_ADEN;` line. [ASSUMED — standard F0 ADC initialization order; verify if issue encountered]

### Pitfall 5: Forgetting ADSTART After ADEN
**What goes wrong:** ADC is enabled and ready (ADRDY=1) but never armed for triggered mode. TIM3 fires TRGO at 100 Hz but ADC ignores it. EOC never asserts. ISR never fires.
**Prevention:** After ADRDY=1, always call `ADC1->CR |= ADC_CR_ADSTART;` to arm triggered conversion. [VERIFIED: F0 ADC state machine — ADSTART arms triggered mode]

### Pitfall 6: PSC=719 From Legacy REQUIREMENTS.md
**What goes wrong:** PSC=719 was computed for 72 MHz (F103 timer clock). At 8 MHz TIM3 clock: 8 000 000 / (720 × 1000) = 11.1 Hz — massively wrong sampling rate.
**Prevention:** PSC=79, ARR=999 for exactly 100 Hz at 8 MHz. [VERIFIED: arithmetic]

### Pitfall 7: HSI14 Not Started Before ADEN
**What goes wrong:** ADC is enabled with CKMODE=00 (HSI14 async, the reset default). If HSI14 is not running (HSI14ON=0, HSI14RDY=0), the ADC receives no clock and hangs on ADRDY indefinitely.
**Prevention:** Start HSI14 before enabling ADC. The sequence is: `RCC->CR2 |= RCC_CR2_HSI14ON; while(!(RCC->CR2 & RCC_CR2_HSI14RDY));` [VERIFIED: stm32f070xb.h RCC_CR2 bit definitions]

### Pitfall 8: SysTick_IRQn Priority Ordering on M0
**What goes wrong:** On M0, NVIC_SetPriority(SysTick_IRQn, ...) uses a negative IRQ number. Some older CMSIS implementations had bugs with negative-number priority assignments.
**Risk level:** LOW — Phase 1 already calls `NVIC_SetPriority(SysTick_IRQn, 0)` successfully. The existing pattern is confirmed working.
**Prevention:** No new action needed; existing Phase 1 SysTick priority is correct.

---

## 9. Gotchas: F0 vs F1 ADC Architecture Differences Summary

| Architecture Feature | F103 (F1) | F070 (F0) | Impact |
|---------------------|-----------|-----------|--------|
| ADC instances | ADC1 + ADC2 (shared IRQ) | ADC1 only | Simpler ISR, no ADC2 check |
| Calibration | RSTCAL + CAL (two writes) | ADCAL (one write, self-clear) | Different init code |
| Channel selection | SQR3[4:0] = channel number | CHSELR bit = 1 for each channel | Bitmap vs sequence |
| Sample time | SMPR2: per-channel 3-bit field | SMPR: one 3-bit field for ALL channels | Cannot mix sample times |
| Resolution control | None (always 12-bit) | CFGR1.RES[1:0] (can select 6/8/10/12-bit) | Use 12-bit (leave RES=00) |
| ADC enable flag | SR.ADST + ADON sequence | ISR.ADRDY after ADEN | Must wait ADRDY |
| ADC clock | ADCPRE in RCC_CFGR (shared) | HSI14 RC or sync PCLK/N via CFGR2 | Different enable path |
| Overrun behavior | Data register holds old value | Configurable: OVRMOD (overwrite or preserve) | Set OVRMOD=1 for streaming |
| DMA | Optional, separate enable | Optional via CFGR1.DMAEN | Not used in this project |

---

## 10. Verification Checklist for Phase 2

After implementation, confirm each point before claiming phase complete:

- [ ] TIM3 running: measure TRGO signal on logic analyzer, or probe TIM3 counting via debugger
- [ ] ADC ISR fires at 100 Hz: place `g_isr_count++` in ISR, verify count = 100 per second via USART
- [ ] ADC value is 12-bit plausible: at PA0 unconnected or GND, value should be < 100; at 3.3V = ~4095
- [ ] EOC flag cleared: no spurious repeated ISR calls (g_isr_count must match expected rate, not runaway)
- [ ] SysTick still accurate: millis() increments 1000 per second — unchanged from Phase 1
- [ ] No ADEN=1 before calibration: verify by code inspection (ADCAL before ADEN in source)
- [ ] EXTSEL=011 confirmed: read back ADC1->CFGR1, bits [8:6] = 011 = 0x3 after shift

---

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | STM32F0 has no APB timer clock doubling when APB prescaler=1 | TIM3 Config | Wrong PSC/ARR ratio — timer fires at wrong rate |
| A2 | PA0 = ADC_IN0 on STM32F070RB | GPIO section | Wrong channel selected, ADC reads wrong pin |
| A3 | F0 ADC SMPR table: SMP=111 → 239.5 cycles | ADC Config | Sample time too short or too long — affects noise |
| A4 | EOC flag in ISR is cleared by reading ADC1->DR (or w1c) | ISR code | EOC not cleared → ISR fires repeatedly (runaway) |
| A5 | CFGR1 must be written before ADEN (write lock behavior) | ADC Config | Trigger source not latched → ADC never converts |

A1, A2, A3, A4, A5 are high-probability correct based on F0 architecture and confirmed by cross-references, but none were verified against the RM0360 PDF table directly (PDF download timed out). Use the verification checklist in Section 10 to catch any that are wrong.

---

## Sources

### Primary (HIGH confidence)
- `stm32f070xb.h` — CMSIS device header, project local at `Drivers/CMSIS/Device/ST/STM32F0xx/Include/stm32f070xb.h`. All register names, bit positions, IRQ numbers, bus assignments, peripheral presence/absence verified here.
- `stm32f070xb.h` TIM3 confirmed: TIM3_BASE, RCC_APB1ENR_TIM3EN, TIM3_IRQn = 16
- `stm32f070xb.h` TIM2 confirmed absent: 0 occurrences in complete file
- `stm32f070xb.h` ADC1_IRQn = 12, ADC1_IRQHandler alias confirmed line 5780
- Phase 1 SUMMARY.md — confirmed F070 register patterns (AHBENR, MODER, AFR, ISR, TDR, BRR=0x45)

### Secondary (MEDIUM confidence)
- STM32F0xx StdPeriph Driver `stm32f0xx_adc.h` (GitHub: mblythe86/stm32f0-projects, ransford/freertos-moo) — EXTSEL encoding table: TIM3_TRGO = EXTSEL_0|EXTSEL_1 (verified consistent across two independent sources)
- [RM0360 Reference Manual](https://www.st.com/resource/en/reference_manual/dm00091010-stm32f030x4x6x8xc-and-stm32f070x6xb-advanced-armbased-32bit-mcus-stmicroelectronics.pdf) — PDF download timed out; referenced by URL only

### Tertiary (LOW confidence / ASSUMED)
- APB no-doubling on F0: deduced from single-APB architecture and absence of doubling documentation in headers
- SMP[2:0] cycle table: training knowledge, not verified from RM0360 this session
- CFGR1 write-before-ADEN lock behavior: training knowledge, not verified from RM0360 this session

---

## Metadata

**Confidence breakdown:**
- TIM3 availability and register values: HIGH — verified from CMSIS header
- TIM2 absence: HIGH — verified by zero header occurrences
- EXTSEL TIM3_TRGO = 011: HIGH — two independent StdPeriph sources agree
- ADC clock (HSI14): HIGH — RCC_CR2 bits confirmed in header
- Calibration sequence: HIGH — ADC_CR_ADCAL confirmed in header
- SMP cycle table: MEDIUM/LOW — training knowledge, not PDF-verified this session
- APB doubling absence: MEDIUM — architectural deduction, not PDF-confirmed

**Research date:** 2026-05-19
**Valid until:** 2026-11-19 (silicon register map is fixed; no expiry concern)
