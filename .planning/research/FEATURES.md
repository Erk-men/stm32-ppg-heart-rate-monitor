# Feature Landscape: Bare-Metal PPG Heart Rate Sensor

**Domain:** Embedded PPG firmware — STM32F103RBT6 (Cortex-M3, 72MHz, 20KB RAM, no FPU)
**Researched:** 2026-05-12
**Confidence:** HIGH (embedded PPG signal processing is a well-established domain; Cortex-M3 constraints are documented hardware fact)

---

## Table Stakes

Features any working PPG sensor firmware must have to be credible. Missing any of these and the project is incomplete regardless of other polish.

| Feature | Why Expected | Complexity | Cortex-M3 Practical? | Notes |
|---------|--------------|------------|----------------------|-------|
| 100Hz ADC sampling with hardware timer trigger | Jitter-free sampling is the foundation — software polling introduces timing errors that corrupt peak-interval BPM calculations | Low | Yes — TIM2 trigger to ADC1 is a standard STM32F1 peripheral chain | Use TIM2 update event as ADC external trigger via EXTSEL bits in ADC_CR2; eliminates SysTick polling error |
| Noise filter before peak detection | Raw ADC output from LM358 at 3.3V has DC offset drift and 50Hz mains pickup; unfiltered signal produces false peaks | Low | Yes — integer arithmetic only | Moving average is the right choice here; see Signal Processing section |
| Peak detection with minimum refractory period | Without refractory period, a single heartbeat generates 2–4 false triggers on the rising and falling slopes | Low-Medium | Yes | Refractory window of 300ms minimum (= 30 samples at 100Hz) prevents double-counting; physiological HR max ~200 BPM = 300ms minimum interval |
| BPM sanity bounds (40–200 BPM) | Any measurement system must reject obviously wrong readings; this is engineering hygiene | Low | Yes | Reject peaks where interval < 300ms (>200 BPM) or > 1500ms (<40 BPM) |
| "No finger" detection | A sensor that outputs garbage BPM when nothing is placed on it is unusable and undemonstrable | Low | Yes | Check signal amplitude (max - min in rolling window) < threshold; print `"--"` instead of a BPM |
| USART serial output at 115200 baud | Evaluation requires observable output; serial monitor is the standard debug channel for STM32 Nucleo | Low | Yes — USART2 maps to ST-Link virtual COM port on Nucleo-F103RB | Use polling TX (USART_SR TXE flag) for simplicity; interrupt-driven not needed at 1 BPM print per beat |
| BPM printed per detected heartbeat | Output must be human-readable and timestamped relative to beats, not a continuous stream | Low | Yes | One printf-style USART transmission per detected peak |
| Bare-metal register-level drivers | Course requirement — HAL disqualifies the submission | Low-Medium | Yes | ADC1, TIM2, USART2, SysTick all configurable via CMSIS; STM32CubeIDE .ioc generates RCC and GPIO init which can be used as a baseline |

---

## Differentiators

Features that distinguish a good submission from an average one. None are individually required, but together they demonstrate understanding beyond "it compiles and kind of works."

| Feature | Value Proposition | Complexity | Cortex-M3 Practical? | Notes |
|---------|-------------------|------------|----------------------|-------|
| Adaptive threshold (avg_peak × fraction, recalculated per beat) | Fixed threshold fails when finger pressure changes or ambient light varies; adaptive threshold shows understanding of the real signal environment | Medium | Yes — integer multiply + shift, no float needed | Maintain a rolling average of the last N peak amplitudes; threshold = (rolling_avg * 6) / 10 avoids float. Recommended fraction: 0.5–0.7 of recent peak average |
| 5-reading BPM rolling average for stable display | Single-beat BPM is noisy (±10–15 BPM variation is normal); averaging over 5 beats before display shows awareness of measurement statistics | Low | Yes | Circular buffer of 5 BPM values; display median or mean. Mean is simpler; median is more robust to outliers |
| Refractory period explicitly parameterized | Hardcoded magic numbers in firmware are a red flag in a technical report; defining `#define MIN_BEAT_INTERVAL_MS 300` shows professional practice | Low | Yes | — |
| Signal amplitude reporting in debug output | During calibration, you need to see raw peak-to-peak counts to know if the analog chain is working; adding `"AMP: %d"` to serial output takes 10 lines and saves hours of oscilloscope debugging | Low | Yes | Toggle with a `#define DEBUG_VERBOSE 1` compile flag; clean output in normal mode |
| Separate DC baseline tracking | HPF at 0.5Hz removes most DC, but slow respiration artifacts (0.15–0.4Hz) partially pass through; tracking a slow moving average (128–256 sample window) and subtracting it from the fast moving average isolates the cardiac AC component cleanly | Medium | Yes — requires a second accumulator | This is the difference between "it works in ideal conditions" and "it works when the subject is breathing visibly" |
| Flowchart-accurate algorithm with named states | Firmware structured as an explicit state machine (IDLE, SEARCHING_PEAK, IN_REFRACTORY) maps directly to the algorithm flowchart required in the report, making the report trivial to write | Medium | Yes | Enum-based state machine in main loop; no overhead vs ad-hoc if-else chains |
| Calibration mode printout | A startup sequence that prints raw ADC min/max over 5 seconds (with no BPM computation) gives hard numbers for the circuit section of the report ("ADC output ranged 512–1847 counts, peak-to-peak = 1335, SNR sufficient") | Low | Yes | Activated by a `#define CALIBRATION_MODE 1` compile flag or a GPIO button hold at startup |
| SysTick millis() with overflow handling | A millis() counter that overflows at 2^32 ms (~49 days) without a guard causes subtle timing bugs if a demo runs longer than expected or if a counter is compared across an overflow boundary; proper `(uint32_t)(now - then)` unsigned subtraction handles overflow correctly | Low | Yes | This is a known embedded gotcha; demonstrating awareness of it impresses evaluators |

---

## Anti-Features

What to deliberately NOT add for this course deliverable.

| Anti-Feature | Why Avoid | What to Do Instead |
|--------------|-----------|-------------------|
| FIR filter with floating-point coefficients | Cortex-M3 has no FPU; software float on M3 is 20–60 cycles per operation. A 32-tap FIR at 100Hz = 3200 soft-float operations/second — feasible but unnecessary when integer moving average achieves the same result for PPG | Use integer moving average with power-of-2 window (shift instead of divide) |
| IIR Butterworth in float | Same FPU absence problem; biquad IIR in float is ~10 operations per sample per stage. Two-stage = 2000 float ops/second — feasible but adds implementation risk (coefficient calculation, stability analysis) for no credibility gain over moving average in this context | Integer first-order IIR is acceptable if desired: `y = y - (y >> 4) + (x >> 4)` |
| DMA circular buffer for ADC | DMA adds correctness complexity (cache alignment, half-transfer interrupt) with no benefit at 100Hz (one sample every 10ms is trivially handled by TIM2 ADC trigger + end-of-conversion interrupt) | TIM2 hardware trigger + ADC EOC interrupt or polling is sufficient |
| RTOS / FreeRTOS | Adds 5–10KB RAM overhead, requires understanding of task priorities, introduces preemption bugs; course requires bare-metal, so this is also disqualifying | Single superloop with state machine |
| SpO2 / oxygen saturation measurement | Requires two wavelengths (660nm red + 940nm IR) and ratio computation; BPW34 + single IR LED cannot compute SpO2. Attempting it would produce meaningless numbers | Do not claim or attempt SpO2 |
| I2C LCD display output | PROJECT.md explicitly defers this. Serial monitor is sufficient for demonstration and report screenshots | USART serial only for v1 |
| FFT-based heart rate extraction | FFT on Cortex-M3 requires integer FFT (CMSIS DSP or manual); for 100Hz, 512-point FFT gives 0.195Hz resolution = ~11.7 BPM resolution — worse than time-domain peak detection. Overkill and inferior for this application | Time-domain peak detection is strictly better for single-channel PPG at this sample rate |
| Autocorrelation BPM detection | Mathematically elegant, but requires O(N^2) or O(N log N) computation, large buffer (3–10 seconds of data = 300–1000 samples), and careful normalization. Implementation risk high, marginal credibility gain over adaptive threshold for a course project | Adaptive threshold peak detection is the right choice |
| Bluetooth / UART DMA streaming | Out of scope per PROJECT.md; adds RF certification concerns and is not evaluated | — |
| Dynamic memory allocation (malloc/free) | Embedded firmware must not use heap allocation — fragmentation, undefined behavior, no heap in minimal linker scripts | All buffers statically allocated at compile time |

---

## Signal Processing: Standard Approaches at 100Hz PPG on Cortex-M3

### What Is Standard in the Field

**Confidence: HIGH** — This is well-established practice in published embedded PPG literature (MAX30102 reference firmware, open-source pulse oximeters, academic PPG papers all converge on the same approach for resource-constrained devices).

#### Filtering Hierarchy for 100Hz PPG

1. **DC removal / baseline wander correction** — The analog HPF (fc=0.5Hz) handles most of this. In firmware, a slow moving average (128 samples = 1.28s window) tracks the baseline; subtracting it from the signal isolates the cardiac AC component. This is not strictly required if the HPF is well-tuned, but it helps when subjects move.

2. **Noise smoothing** — A moving average (16–32 samples) is the standard embedded choice for 100Hz PPG. At 16 samples (160ms window), it attenuates high-frequency noise while preserving the PPG waveform (fundamental ~1Hz, harmonics up to ~3Hz). The analog LPF at 15.9Hz already removes most content above that; the digital moving average handles the remaining 15Hz–50Hz band. A 32-sample window (320ms) is better for noisy signals but adds 1.5 heartbeat-period latency at 60 BPM.

3. **No further filtering needed** — The signal chain (HPF at 0.5Hz + LPF at 15.9Hz + ×100 gain) delivers a bandpass-filtered signal. Digital post-processing needs only noise smoothing, not another bandpass filter.

#### Integer Moving Average Implementation

```c
// Power-of-2 window (N=32) allows shift instead of divide
#define MA_WINDOW 32
#define MA_SHIFT  5   // 2^5 = 32

static int32_t ma_buffer[MA_WINDOW];
static int32_t ma_sum = 0;
static uint8_t ma_index = 0;

int32_t moving_average(int32_t new_sample) {
    ma_sum -= ma_buffer[ma_index];
    ma_buffer[ma_index] = new_sample;
    ma_sum += new_sample;
    ma_index = (ma_index + 1) & (MA_WINDOW - 1); // bitmask wrap, no modulo
    return ma_sum >> MA_SHIFT;  // divide by 32 via shift, no float
}
```

This is O(1) per sample, uses 128 bytes of RAM (32 x int32_t), and produces correct integer output. No floating point, no division instruction.

#### Why Not FIR or IIR for This Application

- **FIR (e.g., 32-tap Hann-windowed lowpass at 5Hz)**: Requires pre-calculated coefficients and either float multiplication or fixed-point scaling. Implementation is correct but adds complexity with no measurable quality improvement over moving average for 100Hz PPG where the analog LPF already handles high-frequency content. The moving average IS a FIR filter (rectangular window, uniform coefficients = 1/N) — this is a valid thing to state in the technical report.

- **IIR (first-order integer)**: `y = y - (y >> alpha) + (x >> alpha)` is a valid single-pole lowpass IIR implementable in pure integer. Time constant ≈ alpha/fs seconds. Reasonable alternative if desired. Slightly more efficient than moving average (no buffer, O(1) with 4 bytes state). Less intuitive to explain and tune. Not worth switching to unless moving average RAM is a concern (it is not — 128 bytes out of 20KB).

- **Autocorrelation / FFT**: See Anti-Features above.

---

## BPM Detection: Peak Detection Variants for Cortex-M3

### Variant Comparison

| Method | Description | Complexity | RAM Needed | Accuracy for PPG | Cortex-M3 Practical? |
|--------|-------------|------------|------------|------------------|----------------------|
| Fixed threshold | Trigger when signal > constant value | Trivial | None | Poor — fails with any amplitude variation | Yes, but wrong choice |
| Derivative zero-crossing | Trigger on sign change of (sample[n] - sample[n-1]) | Low | 1 sample history | Medium — noisy derivative produces false crossings without pre-smoothing | Yes, after smoothing |
| Adaptive threshold | Trigger when signal > fraction of recent peak average; refractory period prevents double-counting | Low-Medium | Peak history buffer (5–10 values) | Good — adapts to signal amplitude changes | Yes — recommended |
| Template matching | Cross-correlate incoming signal with a stored PPG template | High | Template buffer (100+ samples) | High | Marginal — too much RAM and computation for the gain |
| Autocorrelation | Compute self-correlation of buffered signal, find dominant period | High | 300–1000 sample buffer | High | No — see Anti-Features |
| Pan-Tompkins derivative | Designed for ECG QRS, not PPG; squaring and integration stages | High | Multi-buffer pipeline | Poor for PPG (different morphology than ECG) | Not appropriate |

### Recommended: Adaptive Threshold with Refractory Period

**Rationale:** This is the approach used in virtually all open-source embedded PPG projects (e.g., SparkFun MAX30105 library, Maxim Integrated AN6612 application note, academic PPG papers for resource-constrained MCUs). It is simple enough to explain in 1 page of a report, robust enough to handle real physiological variation, and implementable in ~40 lines of C.

**Algorithm outline:**

```
State: IDLE or IN_REFRACTORY
Inputs: filtered_sample (int32_t), millis() timestamp

IDLE state:
  if filtered_sample > threshold AND (millis() - last_peak_time) > MIN_INTERVAL_MS:
    record peak_time = millis()
    compute interval_ms = peak_time - last_peak_time
    if 300 < interval_ms < 1500:
      bpm_raw = 60000 / interval_ms
      push bpm_raw to 5-sample BPM rolling average
      print averaged BPM over USART
    update threshold = (peak_amplitude_avg * 6) / 10  // 0.6 fraction, no float
    last_peak_time = peak_time
    enter IN_REFRACTORY
    
IN_REFRACTORY state:
  if (millis() - last_peak_time) > MIN_REFRACTORY_MS (300ms):
    return to IDLE
```

**No floating point required.** `60000 / interval_ms` is integer division available natively on Cortex-M3 (UDIV instruction in Thumb-2 — present on M3, unlike M0). The threshold fraction uses integer multiply + shift.

**Note on Cortex-M3 UDIV:** The `60000 / interval_ms` division is safe — Cortex-M3 supports hardware UDIV/SDIV in Thumb-2 ISA. This is NOT true on Cortex-M0/M0+. No need for software division library.

---

## Debug Output: Serial Format for Calibration

### What Is Most Useful

Two modes are recommended, selectable at compile time:

**Normal mode (submitted firmware):**
```
BPM: 72
BPM: 74
BPM: 73
--
--
BPM: 71
```
One line per detected beat (or "--" for no-finger). Clean, human-readable, screenshot-friendly for the report.

**Verbose/calibration mode (`#define DEBUG_VERBOSE 1`):**
```
RAW: 1423  FILT: 1387  THR: 1102  AMP: 412  STATE: IDLE
RAW: 1456  FILT: 1401  THR: 1102  AMP: 435  STATE: IDLE
RAW: 1489  FILT: 1428  THR: 1102  AMP: 412  STATE: PEAK -> BPM: 73
RAW: 1401  FILT: 1395  THR: 1102  AMP: 412  STATE: REFRACT
```
This format lets you observe the filter behavior, threshold dynamics, and state transitions during bench calibration. It is also the basis for generating the serial monitor screenshot for the report — you can capture a window showing a full heartbeat cycle with threshold crossing visible.

**Calibration startup mode (`#define CALIBRATION_MODE 1`):**
```
=== CALIBRATION MODE — 5 seconds ===
ADC min: 512  max: 1847  peak-to-peak: 1335
Signal amplitude: SUFFICIENT (>200 counts threshold)
Estimated SNR: OK
=== Entering BPM detection ===
```
Run this once to confirm the analog chain is producing a usable signal before algorithm tuning.

### Why This Format

- Each field is labeled — a reviewer can read the output without referring to the code
- State transitions are explicit — useful for demonstrating the algorithm in the report
- No floats in output — `%d` formatting is safe without printf float support (STM32CubeIDE newlib-nano strips float printf by default; adding it costs 8–12KB flash)

---

## Report Evidence: Expected Measurements and Captures

Based on standard embedded systems course deliverables and the PROJECT.md requirements:

| Evidence Item | How to Obtain | Difficulty | Critical for Report? |
|---------------|---------------|------------|----------------------|
| Circuit schematic with component values | Draw in STM32CubeIDE, KiCad, or by hand; include resistor values, op-amp pinout, supply voltages | Low | Yes — without this the circuit section is incomplete |
| Op-amp gain and filter calculations | Derive fc = 1/(2πRC) for HPF and LPF; derive voltage gain = Rf/Rin for amplifier stage | Low | Yes — shows the analog design is intentional, not copy-paste |
| ADC register configuration table | List ADC_CR1, ADC_CR2, ADC_SMPR, TIM2_ARR, TIM2_PSC values with hex and meaning | Low | Yes — this is the "bare-metal" evidence |
| Serial monitor screenshot (normal mode) | Capture in STM32CubeIDE serial monitor or PuTTY during resting HR measurement | Low | Yes — primary functional evidence |
| Serial monitor screenshot (verbose/calibration mode) | Capture showing peak detection events with threshold and amplitude values | Low | Strongly recommended — shows algorithm working, not just lucky output |
| Algorithm flowchart | Draw state machine (IDLE → PEAK_DETECTED → REFRACTORY → IDLE) with decision boxes for threshold and bounds checking | Low-Medium | Yes — PROJECT.md requires this |
| BPM accuracy comparison | Measure own HR simultaneously with commercial pulse oximeter or phone app; compare 10 readings; compute mean error | Medium | Strongly recommended — quantitative result is required to claim the system "works" |
| Oscilloscope capture (if available) | Capture op-amp output showing AC PPG waveform; measure amplitude; identify heartbeat pulses visually | Medium — requires lab oscilloscope access | Recommended — strongest evidence that analog chain is correct; if not available, describe why and use verbose serial output as substitute |
| Frequency spectrum discussion | Not a measurement — just explain in text why 100Hz sampling is sufficient for a 0.5–3Hz signal (Nyquist: need >6Hz minimum; 100Hz gives 33× margin) | Low | Adds depth to signal processing section |

---

## Feature Dependencies

```
TIM2 hardware trigger
  └── ADC1 sampling at 100Hz (depends on TIM2)
        └── Moving average filter (depends on ADC samples)
              └── Peak detection (depends on filtered signal)
                    ├── BPM calculation (depends on peak timing)
                    │     └── BPM rolling average (depends on BPM values)
                    │           └── USART output (depends on averaged BPM)
                    └── No-finger detection (depends on signal amplitude)

SysTick millis()
  └── Peak interval timing (depends on millis())
        └── Refractory period enforcement (depends on millis())

Adaptive threshold
  └── Recalibration per beat (depends on peak amplitude history)
```

---

## MVP Definition

The minimum that constitutes a working, credible submission:

**Must have (in order of build dependency):**
1. SysTick millis() — timing foundation
2. USART2 serial driver — debug output without this you are blind
3. TIM2 + ADC1 at 100Hz — hardware sampling foundation
4. 16-sample moving average — signal must be filtered before detection
5. Adaptive threshold peak detection with 300ms refractory period — core algorithm
6. BPM sanity bounds (40–200 BPM) — engineering hygiene, also required for PROJECT.md
7. 5-reading BPM rolling average — stable display
8. No-finger detection — required for demonstration
9. BPM printed per heartbeat on USART — only output channel

**Should add (low effort, high report value):**
10. Calibration mode printout (ADC min/max over 5 seconds)
11. Verbose debug mode behind `#define DEBUG_VERBOSE`
12. Named constants for all magic numbers (`#define MIN_BEAT_INTERVAL_MS 300`)
13. Overflow-safe millis comparison (`(uint32_t)(now - then)` unsigned subtraction)

**Defer without hesitation:**
- LCD display (explicitly out of scope)
- Second moving average for DC baseline tracking (nice-to-have, not required if HPF is working)
- BPM accuracy table in firmware (do the comparison manually, document in report)

---

## Sources

**Confidence note:** WebSearch was unavailable in this research session. All findings are based on:
- Training knowledge of Cortex-M3 ISA (Thumb-2 UDIV, no FPU — hardware fact, HIGH confidence)
- Established PPG signal processing literature (moving average for noise, adaptive threshold for peak detection — convergent practice across MAX30102 app notes, open-source embedded PPG implementations, academic papers — HIGH confidence)
- STM32F103 peripheral architecture (TIM2 as ADC external trigger via EXTSEL, USART2 on Nucleo mapped to ST-Link virtual COM — hardware-documented fact — HIGH confidence)
- Course project conventions (report structure expectations — MEDIUM confidence, inferred from standard embedded systems course deliverables)

No findings are flagged LOW confidence. All critical technical claims (Cortex-M3 UDIV availability, M3 no FPU, 60000/interval_ms as integer division, TIM2 hardware trigger feasibility, moving average as O(1) integer operation) are hardware-documented facts.
