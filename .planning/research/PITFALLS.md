# Domain Pitfalls: Bare-Metal STM32F103 PPG Heart Rate Sensor

**Domain:** Bare-metal embedded C, analog signal chain, physiological signal processing
**Platform:** STM32F103RBT6 (Nucleo-F103RB), LM358 op-amp, BPW34 photodiode, 940nm IR LED
**Researched:** 2026-05-12
**Confidence:** HIGH for hardware/silicon facts (stable reference manual data); MEDIUM for algorithm tuning ranges

---

## Critical Pitfalls

Mistakes in this category cause reworks, wrong BPM readings, or complete signal loss.

---

### Pitfall C1: ADC Clock Exceeds 14 MHz Maximum

**Phase:** Phase 1 (Clock + ADC peripheral init)

**What goes wrong:** The STM32F103 ADC has a hard maximum input clock of 14 MHz (Reference Manual, section on ADC clock). At 72 MHz system clock with default APB2 prescaler of /1, PCLK2 = 72 MHz. The ADC clock is derived from PCLK2 via `RCC_CFGR` bits `ADCPRE[1:0]`. The default after reset is `/2`, giving 36 MHz — more than double the allowed maximum. Students copy a working RCC init that sets HSE + PLL to 72 MHz without touching `ADCPRE`, then wonder why ADC reads are garbage or non-deterministic.

**Why it happens:** The ADC prescaler field is in `RCC_CFGR` bits [15:14], separate from APB1/APB2 prescalers. It is easy to miss because it is not near the PLL configuration fields conceptually, and STM32CubeIDE's clock tree GUI makes it easy to confirm system clock without scrolling to the ADC clock derived value.

**Consequences:** ADC conversions complete but values are wrong, noisy, or stuck at rail values. The error is intermittent and temperature-dependent, making it very hard to debug without a logic analyzer.

**Prevention:**
- Set `RCC_CFGR` `ADCPRE` to `/6` (bits = `10`) giving 72/6 = 12 MHz, safely under 14 MHz.
- At 72 MHz, `/4` gives 18 MHz — still over the limit. `/6` is the minimum safe divider.
- Verify in code: `RCC->CFGR |= RCC_CFGR_ADCPRE_DIV6;` before enabling the ADC.
- Read back `ADC1->DR` a few times after a known input voltage (tie PA0 to 3.3V) and confirm you get ~4095 ± 5.

**Detection warning signs:** ADC reads are plausible but slightly wrong; readings are not repeatable when input voltage is held constant; ADC values drift upward over time as chip warms.

---

### Pitfall C2: Wrong Sample Time → Insufficient Source Impedance Settling

**Phase:** Phase 1 (ADC configuration)

**What goes wrong:** The LM358 op-amp output impedance, combined with the 10KΩ LPF resistor, creates a source impedance that the ADC's internal sample-and-hold capacitor must charge against. With too short a sample time, the capacitor does not fully charge, and the ADC measures an intermediate voltage — lower than the true signal. The error is proportional to source impedance and inversely proportional to sample time.

**Specific numbers:** The STM32F103 ADC sample-and-hold capacitor is ~8 pF. With a 10KΩ source impedance, the RC time constant is 80 ns. To settle to 12-bit accuracy (0.02%), you need ~9 time constants = 720 ns minimum. At 12 MHz ADC clock, one ADC clock cycle = 83 ns. The minimum sample time register value (`SMP[2:0] = 000`) gives 1.5 cycles = 125 ns — far too short. You need at minimum `SMP = 011` (13.5 cycles = 1.125 µs) for a 10KΩ source. Use `SMP = 100` (28.5 cycles) for margin.

**Register:** `ADC1->SMPR2` for channels 0–9. PA0 = channel 0. Set bits [2:0] of `SMPR2` to `100` (binary) for 28.5 cycles:
```c
ADC1->SMPR2 &= ~ADC_SMPR2_SMP0;          // clear SMP0 field
ADC1->SMPR2 |= (4U << ADC_SMPR2_SMP0_Pos); // 28.5 cycles
```

**Phase surfaced:** Phase 1 — silent error that makes calibration in Phase 2 impossible to complete.

**Detection warning signs:** ADC values are consistently 5–15% lower than expected. Connecting the same signal directly to PA0 (bypassing the LPF resistor) gives correct readings — this isolates source impedance as the cause.

---

### Pitfall C3: VDDA Not Properly Decoupled — ADC Reference Noise

**Phase:** Phase 1 (hardware breadboard construction)

**What goes wrong:** On the Nucleo-F103RB board, the STM32 VDDA pin is the ADC reference. It is powered from the same 3.3V rail as the digital logic. Every GPIO state change and timer interrupt injects switching noise into VDDA, which appears directly as ADC reading noise. On a breadboard with no local bypass capacitors, this noise can be 20–50 mV peak-to-peak, which is 25–62 LSBs at 12-bit/3.3V — completely masking a small PPG signal.

**Prevention:**
- Place a 100nF ceramic capacitor as close as physically possible to the VDDA/VSSA pins of the STM32 on the breadboard. On the Nucleo board these are accessible on the morpho connector.
- Add a 10µF bulk electrolytic capacitor in parallel for low-frequency decoupling.
- Run a separate ground wire for the analog signal chain, connecting to AGND/GND at a single point near the STM32 (star ground topology).
- If using the Nucleo board's CN7 morpho connector, check whether VDDA is tied to VDD internally — it is on F103RB, so decoupling the STM32 chip directly is more effective than decoupling at the connector.

**Detection warning signs:** With no signal connected (PA0 tied to GND via 1KΩ), ADC reads spread over 10–30 counts instead of being stable within ±2. Oscilloscope on VDDA shows spikes coincident with GPIO toggles or TIM2 trigger events.

---

### Pitfall C4: TIM2 Trigger Does Not Start ADC Conversion — Wrong EXTTRIG/EXTSEL Bits

**Phase:** Phase 1 (TIM2 + ADC trigger configuration)

**What goes wrong:** Configuring TIM2 to trigger ADC1 requires setting two fields correctly simultaneously. Students set `ADC1->CR2` `EXTTRIG` (bit 20, enable external trigger) but use the wrong `EXTSEL[2:0]` value for TIM2. For ADC1 on STM32F103, `EXTSEL = 000` is TIM1_CC1, `EXTSEL = 001` is TIM1_CC2, `EXTSEL = 010` is TIM1_CC3, `EXTSEL = 011` is TIM2_CC2, `EXTSEL = 100` is TIM3_TRGO, **`EXTSEL = 110` is TIM2_TRGO** (the one you want for a clean trigger). Using TIM2_CC2 (`011`) instead of TIM2_TRGO (`110`) means the trigger fires at a different moment, or not at all if CC2 is not configured.

**Correct configuration for TIM2 TRGO:**
```c
// In TIM2 init: set MMS to Update event → TRGO
TIM2->CR2 &= ~TIM_CR2_MMS;
TIM2->CR2 |= TIM_CR2_MMS_1;   // MMS = 010 = Update event

// In ADC1 init:
ADC1->CR2 &= ~ADC_CR2_EXTSEL;
ADC1->CR2 |= (6U << ADC_CR2_EXTSEL_Pos);  // EXTSEL = 110 = TIM2 TRGO
ADC1->CR2 |= ADC_CR2_EXTTRIG;             // enable external trigger
```

**Consequences:** ADC runs at wrong rate or never triggers, so you fall back to software-triggered ADC in a polling loop — which introduces variable jitter up to the main loop execution time (potentially ±5ms at 100Hz), corrupting peak interval timing and therefore BPM.

**Phase surfaced:** Phase 1 configuration, but the jitter consequence is invisible until Phase 2 when BPM calculations start drifting.

---

### Pitfall C5: LM358 Output Saturation at 3.3V Supply — Signal Clipped Before ADC

**Phase:** Phase 1 (hardware build) and Phase 2 (calibration)

**What goes wrong:** The LM358 is a legacy op-amp designed for single-supply operation from 3V to 32V, but its output swing is NOT rail-to-rail. At a 3.3V supply, the output can swing from approximately 0V (near ground, within ~20mV) to approximately 1.5–1.8V maximum (roughly 1.5V below the positive rail). This means:

- The amplifier's usable output range is only 0 to ~1.8V.
- In 12-bit ADC terms: 0 to approximately 2232 counts (out of 4095).
- If the DC operating point of the amplifier output is set to 1.5V (mid-supply of 3V), you are already at the LM358's saturation ceiling at 3.3V supply.
- The ×100 gain stage will clip the AC PPG signal against the 1.8V ceiling before the signal reaches PA0, destroying the waveform shape.

**Prevention (priority order):**
1. Power the LM358 from the 5V USB rail (Nucleo CN6 pin, or USB pin on morpho connector). The LM358 output then swings to ~3.5V max at 5V supply, and the ADC still sees 0–3.3V correctly (the output is clamped by the ADC's internal protection diodes if it exceeds 3.3V+0.3V — but a simple resistor divider or the LPF resistor already in the chain limits current).
2. Alternatively: redesign the DC operating point of the gain stage so the output sits at ~0.9V DC, maximizing available headroom on the low side.
3. Use a rail-to-rail op-amp as a drop-in substitute — but the project specifies LM358 with no substitutions.

**Detection warning signs:** Oscilloscope on the LM358 output shows the PPG waveform with a flat top — the positive peaks are clipped. ADC reads never exceed ~2200 counts regardless of signal amplitude. Moving average output has a ceiling artifact.

---

### Pitfall C6: HPF AC-Coupling Capacitor — Wrong DC Operating Point After Stage

**Phase:** Phase 1 (hardware) and Phase 2 (calibration)

**What goes wrong:** The HPF between the TIA stage and the gain stage (fc ≈ 0.5 Hz) removes the DC component of the photodiode current. After the HPF, the signal is AC-only centered near 0V differential. When this 0V-centered signal drives the non-inverting input of the gain stage (configured as a non-inverting amplifier), the LM358 needs a bias resistor to set the DC operating point of the output to mid-supply (~1.5V). Without a proper DC bias (e.g., a voltage divider on the non-inverting input), the op-amp output sits near 0V, and only the positive half of the AC PPG wave is amplified — the negative half is clipped at ground.

**Prevention:** Add a DC bias voltage to the non-inverting input of the gain stage op-amp. A simple voltage divider (two equal resistors, e.g., 100KΩ each) from 3.3V to GND, with the midpoint connected via a 10KΩ resistor to the non-inverting input, sets the operating point at 1.65V. The AC PPG signal (small, <100mV before gain) superimposes on this bias.

**Detection warning signs:** Gain stage output shows only half-wave rectified PPG — only the positive peaks are visible, the signal never goes below the DC bias level on the negative excursions. BPM detection works intermittently (only on alternate beats corresponding to the half-wave).

---

## Moderate Pitfalls

---

### Pitfall M1: SysTick vs HAL_Delay — Blocking Delay Breaks Sampling Loop

**Phase:** Phase 1 (firmware scaffold) and Phase 2 (algorithm timing)

**What goes wrong:** Student muscle memory calls `HAL_Delay(10)` because it works in every example project. In a bare-metal project with no HAL included, this either fails to compile (missing symbol) or, if HAL is accidentally included, blocks SysTick-driven execution while the TIM2-triggered ADC fires and writes new values to `ADC1->DR` — which are silently overwritten before reading because there is no interrupt pending flag check outside the ISR. Additionally, if `HAL_Delay` is called inside any ISR context, it deadlocks (SysTick cannot preempt itself at equal priority).

**Prevention:** Implement a 32-bit millisecond counter in the SysTick handler:
```c
// In SysTick_Handler (1ms period):
static volatile uint32_t ms_ticks = 0;
void SysTick_Handler(void) { ms_ticks++; }
uint32_t millis(void) { return ms_ticks; }

// Non-blocking delay pattern:
uint32_t t0 = millis();
while (millis() - t0 < 500U);  // wait 500ms, no blocking
```
Never call any delay inside an ISR. Use a flag set in the ISR and handle in main loop.

**Phase surfaced:** Phase 1 if copying from HAL examples. Phase 2 if using delays in the BPM output loop.

---

### Pitfall M2: Forgetting to Clear ADC End-of-Conversion Flag — ISR Fires Once Then Stops

**Phase:** Phase 1 (ADC interrupt configuration)

**What goes wrong:** When using ADC in interrupt mode (EOC interrupt), the `ADC1->SR` `EOC` flag must be cleared inside `ADC1_2_IRQHandler`. If it is not cleared, the interrupt fires once, reads `DR` (which hardware-clears `EOC` on F103 when `DR` is read), but if the student reads `DR` outside the ISR (polling `EOC` flag after enabling interrupt), the flag stays set and the interrupt fires continuously, starving the CPU. Alternatively, if the student never reads `DR` and just checks the flag — `DR` is never read, the DMA channel (if enabled) stalls, or the EOC flag is never cleared by hardware.

**The F103-specific behavior:** On STM32F103, reading `ADC1->DR` clears the `EOC` flag in hardware. So the correct pattern is: inside the ISR, read `ADC1->DR` into a variable — this both captures the value and clears the flag. Do NOT clear `EOC` by writing to `SR` separately; do NOT read `DR` outside the ISR.

```c
void ADC1_2_IRQHandler(void) {
    if (ADC1->SR & ADC_SR_EOC) {
        adc_raw = ADC1->DR;  // reading DR clears EOC flag — this IS the flag clear
        adc_ready = 1;       // signal flag for main loop
    }
}
```

**Detection warning signs:** First ADC reading arrives correctly, then no further interrupts fire. Or: CPU load goes to 100% (interrupt firing continuously) and main loop never runs.

---

### Pitfall M3: TIM2 Period Calculation for 100Hz — Off-by-One on ARR

**Phase:** Phase 1 (TIM2 configuration)

**What goes wrong:** To generate 100Hz update events from TIM2 at 72 MHz system clock:
- TIM2 is on APB1. If APB1 prescaler is /2 (PCLK1 = 36 MHz), the timer clock is doubled by hardware to 72 MHz (this APB1 timer doubling is a common surprise).
- TIM2 prescaler (PSC) and auto-reload register (ARR) must be set so that: f_timer = f_TIM_CLK / (PSC+1) / (ARR+1) = 100 Hz.
- Common mistake: PSC=719, ARR=999 gives 72,000,000 / 720 / 1000 = 100 Hz. This is correct.
- Off-by-one error: PSC=720, ARR=999 gives 72,000,000 / 721 / 1000 = 99.86 Hz. Over time this drifts BPM calculations.
- The +1 is mandatory because the counter counts from 0 to ARR inclusive (ARR+1 counts total).

**APB1 timer clock rule (F103):** If `RCC_CFGR` `PPRE1[2:0]` prescaler divides APB1 by more than 1, the timer input clock is PCLK1 × 2. If APB1 is not divided (prescaler = /1), timer clock = PCLK1. At 72 MHz system clock with APB1 /2, PCLK1 = 36 MHz, TIM2 clock = 72 MHz.

**Correct register writes:**
```c
TIM2->PSC = 719;    // prescale 72MHz → 100kHz (divides by 720)
TIM2->ARR = 999;    // count 0..999 → reload at 100Hz
TIM2->CNT = 0;
TIM2->CR1 |= TIM_CR1_CEN;
```

**Phase surfaced:** Phase 1, but the 0.14% frequency error only manifests as BPM drift in Phase 2.

---

### Pitfall M4: Moving Average Ring Buffer Index Wrap — Uninitialized or Modulo Bug

**Phase:** Phase 2 (algorithm implementation)

**What goes wrong:** A 32-element ring buffer for the moving average requires correct modulo indexing. Two common bugs:
1. Index incremented past the buffer size before the modulo operation, accessing `buf[32]` (one past end) on the first wrap — stack corruption.
2. Buffer not initialized to zero before first 32 samples arrive. The running sum includes garbage values, producing a moving average that is wildly wrong for the first 320ms. This causes a false peak detection that seeds a wrong adaptive threshold, after which the threshold is never recalibrated correctly.

**Prevention:**
```c
#define MA_SIZE 32
static int32_t ma_buf[MA_SIZE];  // zero-initialized by C static storage rules
static uint32_t ma_idx = 0;
static int32_t ma_sum = 0;

int32_t moving_average_update(int32_t new_val) {
    ma_sum -= ma_buf[ma_idx];
    ma_buf[ma_idx] = new_val;
    ma_sum += new_val;
    ma_idx = (ma_idx + 1) % MA_SIZE;  // safe modulo
    return ma_sum / MA_SIZE;
}
```
Use `static` local or global arrays — C guarantees zero-initialization for objects with static storage duration. Do not use `malloc` or stack-allocated arrays.

**Phase surfaced:** Phase 2. Bug may be invisible on the first run if the algorithm happens to not detect false peaks in the garbage-filled window.

---

### Pitfall M5: Adaptive Threshold Sticks After Signal Loss — "No Finger" Re-Acquisition Fails

**Phase:** Phase 2 (peak detection algorithm)

**What goes wrong:** The adaptive threshold (avg_peak × 0.6) is computed from the last N detected peaks. When the finger is removed, the signal collapses to near-zero, but the threshold variable retains the last valid value (e.g., threshold = 800 counts). When the finger is reapplied, the PPG signal builds up slowly as tissue perfuses. During this ramp-up, peaks start below the stale threshold — no peaks are detected — so the threshold is never updated. The system is stuck: signal is too low to cross the threshold, threshold never drops because it requires peak detection to recalibrate.

**Prevention:** Add a decay mechanism: if no peak is detected for more than 3 seconds, halve the threshold. Also implement the "no finger" amplitude check independently of the peak detector — if the signal amplitude (max − min over the last 32 samples) is below an absolute minimum (e.g., 50 counts), print `"--"` and reset threshold to a low starting value (e.g., 100 counts):
```c
if (signal_amplitude < NO_FINGER_THRESHOLD) {
    print_no_finger();
    peak_threshold = THRESHOLD_INIT;  // reset to safe low value
    bpm_avg_reset();
}
```

**Phase surfaced:** Phase 2 algorithm, surfaces during Phase 3 calibration/demo.

---

### Pitfall M6: Double-Peak Detection on Dicrotic Notch — False Double BPM

**Phase:** Phase 2 (peak detection)

**What goes wrong:** The PPG waveform has a primary systolic peak followed by a smaller secondary peak (dicrotic notch artifact) at roughly 60–70% amplitude. If the adaptive threshold is set too low (e.g., avg_peak × 0.3 instead of × 0.6), the secondary peak crosses the threshold and is detected as a second heartbeat, doubling the apparent BPM. At 72 BPM real heart rate, the system reports 140–150 BPM.

**Prevention:** Two complementary defenses:
1. Keep the threshold ratio at 0.6 or higher. Below 0.5, dicrotic notch false-positives become likely.
2. Enforce a minimum refractory period: after detecting a peak, ignore all subsequent peaks for at least 400ms (150 BPM maximum rate → minimum interval = 400ms). This is the standard physiological refractory period approach:
```c
if ((current_time - last_peak_time) < REFRACTORY_MS) continue;  // REFRACTORY_MS = 400
```

**Phase surfaced:** Phase 2; visible during Phase 3 testing when heart rate appears doubled on high-activity fingers.

---

### Pitfall M7: UART printf Buffering — Output Never Appears Without Newline

**Phase:** Phase 1 (UART driver) and Phase 2 (debug output)

**What goes wrong:** In STM32CubeIDE with semihosting or a retargeted `_write`, `printf` output is buffered by the C library until a `\n` is encountered (line buffering) or the buffer fills. Students who print partial lines or forget `\n` see no output on the serial monitor for minutes, then assume the UART driver is broken and start debugging the wrong thing.

**Secondary issue:** If `printf` is called inside an ISR (e.g., ADC interrupt handler) with a blocking UART transmit implementation (polling `TXE` flag in a spin loop), the ISR takes 87µs per character at 115200 baud — printing "BPM: 72\n" (9 characters) takes 783µs, which is nearly a full 10ms ADC sample period. This causes ADC triggers to be missed.

**Prevention:**
1. Always terminate `printf` format strings with `\n`.
2. Alternatively, call `fflush(stdout)` after every print (inefficient but safe during development).
3. Never call `printf` or any UART transmit function inside an ISR. Use a flag:
```c
// In ADC ISR:
if (peak_detected) print_bpm_flag = 1;  // signal only

// In main loop:
if (print_bpm_flag) {
    print_bpm_flag = 0;
    printf("BPM: %lu\n", current_bpm);
}
```

**Phase surfaced:** Phase 1 UART setup and Phase 2 debug integration.

---

### Pitfall M8: BPM Sanity Bounds Applied Before Rolling Average — Outlier Contaminates Average

**Phase:** Phase 2 (BPM calculation)

**What goes wrong:** If the sanity bounds check (40–200 BPM valid range) is applied after adding a raw BPM value to the rolling average, a single 600 BPM spike (from noise) poisons the 5-reading average. The average recovers only after 5 valid readings replace all poisoned slots. At one reading per beat (~60 BPM = 1 per second), recovery takes 5 seconds.

**Prevention:** Apply the sanity check before inserting into the rolling average:
```c
uint32_t raw_bpm = 60000UL / interval_ms;
if (raw_bpm >= 40 && raw_bpm <= 200) {
    bpm_ring[bpm_idx] = raw_bpm;
    bpm_idx = (bpm_idx + 1) % BPM_AVG_SIZE;
    bpm_valid_count = MIN(bpm_valid_count + 1, BPM_AVG_SIZE);
}
// compute average only from valid_count entries, not always 5
```

**Phase surfaced:** Phase 2 algorithm. Surfaces as erratic reported BPM during Phase 3 bench testing.

---

## Minor Pitfalls

---

### Pitfall m1: ADC Not Calibrated Before First Use

**Phase:** Phase 1

**What goes wrong:** STM32F103 ADC requires a self-calibration cycle after powerup before it can produce accurate results. Skipping the calibration adds an offset error of up to ±2 LSB systematic error across all readings. For a signal that may only span 200–500 counts (small PPG amplitude), 2 LSB systematic error is negligible, but calibration is a one-time 2-line addition and should not be skipped for a course project.

**Prevention:**
```c
ADC1->CR2 |= ADC_CR2_RSTCAL;                    // reset calibration
while (ADC1->CR2 & ADC_CR2_RSTCAL);             // wait
ADC1->CR2 |= ADC_CR2_CAL;                       // start calibration
while (ADC1->CR2 & ADC_CR2_CAL);                // wait for completion
// Now enable continuous/triggered mode
```
This must happen before `ADON` triggers the first conversion.

---

### Pitfall m2: Ambient Light — IR Interference and 50Hz Mains Flicker

**Phase:** Phase 1 (hardware), Phase 3 (calibration)

**What goes wrong:** Fluorescent and LED lights flicker at 100Hz (50Hz mains × 2 half-cycles). With a 100Hz ADC sampling rate, this aliases to DC — a constant additive offset on the photodiode current. This shifts the DC operating point of the TIA output, which then shifts the gain stage output, potentially pushing it into saturation. Under incandescent (no flicker) or outdoors lighting the system works fine; under office fluorescent lights it fails.

**Prevention:**
- Shield the photodiode and finger from ambient light during testing. Black electrical tape wrapped around the sensor + finger junction is standard practice.
- Alternatively, set ADC sampling rate to a non-harmonic of 100Hz (e.g., 103Hz) to spread aliased mains noise across spectrum rather than folding it to DC. Minor implementation complexity.
- The 15.9Hz LPF already in the signal chain provides significant attenuation of 100Hz flicker but does not eliminate DC aliasing from the 100Hz sampling coincidence.

---

### Pitfall m3: Integer Overflow in BPM Interval Calculation

**Phase:** Phase 2

**What goes wrong:** `60000 / interval_ms` where both variables are `uint16_t`. At 40 BPM, interval_ms = 1500, 60000 / 1500 = 40 — fine. But 60000 does not fit in a 16-bit unsigned integer (max 65535 — actually it does, 60000 < 65536, so this specific case is safe). The overflow risk is in intermediate calculations: `interval_ms * bpm` or comparisons involving large millisecond timestamps stored in `uint16_t` that wrap at 65535ms (65.5 seconds). A `uint16_t` timestamp wraps every 65 seconds, corrupting the interval calculation.

**Prevention:** Use `uint32_t` for all timestamps and intervals. The SysTick `millis()` counter should be `volatile uint32_t` and wraps at ~49 days — safe for this project. Interval subtraction is safe across wrap: `uint32_t interval = current_ms - last_peak_ms;` works correctly even when `millis()` wraps, because unsigned subtraction wraps correctly in C.

---

### Pitfall m4: NVIC Priority Not Set for ADC Interrupt — Priority Inversion with SysTick

**Phase:** Phase 1

**What goes wrong:** If the ADC interrupt priority is not explicitly set, it defaults to 0 (highest priority) on Cortex-M3. If SysTick also runs at priority 0, neither can preempt the other. If an ADC conversion completes while SysTick is executing its millisecond increment, the ADC interrupt is held pending until SysTick returns. With the SysTick handler doing only `ms_ticks++`, this delay is negligible. But if SysTick priority is inadvertently lower than ADC, the millis counter stalls during long ADC ISR execution. Set explicit priorities:

```c
NVIC_SetPriority(SysTick_IRQn, 0);      // SysTick highest
NVIC_SetPriority(ADC1_2_IRQn, 1);       // ADC slightly lower
NVIC_EnableIRQ(ADC1_2_IRQn);
```

---

## Phase-Specific Warning Matrix

| Phase | Topic | Likely Pitfall | Mitigation |
|-------|-------|----------------|------------|
| Phase 1: Clock + ADC init | RCC configuration | ADC clock > 14 MHz (C1) | Set ADCPRE to /6 explicitly |
| Phase 1: ADC configuration | Sample time | Source impedance settling failure (C2) | Use SMP = 28.5 cycles minimum |
| Phase 1: Hardware build | Analog supply | VDDA decoupling insufficient (C3) | 100nF + 10µF at STM32 VDDA pin |
| Phase 1: TIM2 trigger | EXTSEL bits | Wrong trigger source, ADC never fires (C4) | Verify EXTSEL=110, MMS=010 in TIM2 |
| Phase 1: Hardware build | LM358 supply | Output saturation at 3.3V (C5) | Power LM358 from 5V USB rail |
| Phase 1: Hardware build | Signal chain bias | HPF removes DC, gain stage clips (C6) | Add bias resistor divider at gain input |
| Phase 1: UART setup | printf buffering | Output never appears (M7) | Always append \n; never print in ISR |
| Phase 1: ADC init | Calibration | Systematic offset without calibration (m1) | Add RSTCAL + CAL sequence before first conversion |
| Phase 2: Algorithm | Moving average | Uninitialized buffer poisons threshold (M4) | Use static arrays (C zero-init) |
| Phase 2: Algorithm | Peak detection | Double peaks on dicrotic notch (M6) | Threshold ≥ 0.6×peak; 400ms refractory |
| Phase 2: Algorithm | BPM averaging | Outlier poisons rolling average (M8) | Sanity check before insertion |
| Phase 2: Algorithm | Signal re-acquisition | Stale threshold after finger removal (M5) | Amplitude-based "no finger" reset |
| Phase 3: Calibration | Ambient light | 100Hz flicker aliases to DC (m2) | Shield sensor from ambient light during demo |
| Phase 3: Demo | Timing | SysTick delays or HAL_Delay in ISR (M1) | Non-blocking millis() pattern throughout |

---

## Course-Specific: Report Evidence Planning

The technical report requires serial monitor screenshots, calibration results, and waveform captures. These are impossible to retroactively recreate — plan for them during development.

### Evidence to capture during Phase 2 (algorithm development):

1. **ADC raw waveform printout:** Log raw ADC values at 100Hz to serial for 10 seconds. Copy to spreadsheet and plot. This is your "raw PPG waveform" figure. Without this, your report has no signal chain verification.

2. **Moving average vs raw overlay:** Print both raw and filtered values simultaneously: `printf("%lu,%lu\n", raw_adc, filtered);`. Import as CSV into Excel/MATLAB to generate the before/after filter comparison figure.

3. **Peak detection trace:** Print a marker when a peak is detected: `printf("RAW:%lu PEAK\n", raw_adc);` — visible in serial monitor as a column of numbers with occasional "PEAK" labels. Screenshot this for the algorithm flowchart verification section.

4. **BPM stability test:** Run for 60 seconds, capture all BPM outputs. Calculate mean and standard deviation. Report this as your calibration result: "Mean BPM = 72.3, σ = 1.8 BPM over 60s."

5. **No-finger state:** Screenshot the `"--"` output when no finger is present. Required to demonstrate the detection logic works.

### Evidence that is hard to capture if not planned:

- **Oscilloscope waveforms:** If you do not have a personal oscilloscope, borrow the lab one during a scheduled session and capture: (a) LM358 TIA output, (b) HPF output, (c) gain stage output, (d) LPF output (final signal to ADC). Without these four captures, the report circuit section is incomplete.
- **Power supply measurements:** Note the voltage at VDDA and at the LM358 supply pin under load. If you power LM358 from 5V USB, measure the actual voltage (it may be 4.7–4.85V, not 5.0V). Report the actual measured value.
- **Current consumption:** Not required for the course deliverable but trivially measured with a multimeter in series with the IR LED. Demonstrates you understood the drive current calculation.

---

## Sources

- STM32F103 Reference Manual (RM0008), Rev 21 — ADC chapter (sections 11.3–11.5), RCC chapter (section 7.3.2 ADCPRE), TIM2 chapter (section 15.3)
- LM358 Datasheet (Texas Instruments SNOSC16E) — Output voltage swing vs. supply voltage, input common-mode range
- Cortex-M3 Technical Reference Manual — NVIC priority configuration
- PPG signal processing literature — Refractory period approach to dicrotic notch rejection (standard 400ms minimum inter-beat interval at 150 BPM limit)
- Confidence: HIGH for STM32F103 register-level facts (stable silicon specification); MEDIUM for PPG algorithm tuning parameters (empirically derived, device-specific variation exists)
