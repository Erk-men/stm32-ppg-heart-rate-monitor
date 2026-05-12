# Architecture Patterns

**Domain:** Bare-metal STM32 PPG heart rate sensor firmware
**Platform:** STM32F103RBT6, ARM Cortex-M3, 72MHz (Nucleo-F103RB)
**Toolchain:** STM32CubeIDE, CMSIS headers, bare-metal register access
**Researched:** 2026-05-12
**Confidence:** HIGH — derived from project spec, CMSIS documentation, and established
bare-metal firmware conventions for Cortex-M3 PPG systems.

---

## Recommended Architecture

Three strict horizontal layers. Data flows strictly downward (hardware → algorithm →
application). No layer reaches up. No layer skips a layer.

```
┌─────────────────────────────────────────────────────────┐
│                   APPLICATION LAYER                     │
│  main.c  ·  bpm.c  ·  finger_detect.c  ·  uart_print.c │
│  Owns: BPM averaging, "no finger" logic, serial output  │
├─────────────────────────────────────────────────────────┤
│                   ALGORITHM LAYER                       │
│         filter.c  ·  peak_detect.c                      │
│  Owns: moving-average filter, peak-detect state machine │
├─────────────────────────────────────────────────────────┤
│                   PERIPHERAL DRIVER LAYER               │
│      adc.c  ·  tim.c  ·  usart.c  ·  systick.c         │
│  Owns: register-level CMSIS writes, ISR, ring buffer    │
└─────────────────────────────────────────────────────────┘
                           ▲
                    Hardware / CMSIS
```

---

## Module Inventory and Component Boundaries

### Peripheral Driver Layer

Each driver owns exactly one peripheral. It exposes an init function and — where
relevant — a non-blocking read or write primitive. No driver knows about PPG or BPM.

#### `adc.c` / `adc.h`

Responsibility: Configure ADC1 in single-channel, hardware-triggered mode. Provide
one function that returns the latest 12-bit sample from the circular sample buffer.

Owns:
- RCC clock enable for ADC1 and GPIOA
- PA0 analog pin mode configuration (CR configuration — input floating/analog)
- ADC1 register setup: SMPR, SQR, CR1, CR2
- TIM2 TRGO as external trigger source (ADC CR2 EXTSEL bits)
- ADC EOC interrupt enable; ISR writes sample into `adc_buf[]` ring buffer
- `adc_init(void)` — sets up ADC1 and arms first conversion
- `adc_get_sample(uint16_t *out)` — copies latest sample from ring buffer; returns 1
  if new data available, 0 otherwise (non-blocking)

Does NOT own: filter, peak detection, timing math.

#### `tim.c` / `tim.h`

Responsibility: Configure TIM2 to generate a TRGO update event at exactly 100Hz, which
hardware-triggers ADC1. This eliminates sampling jitter that would corrupt
peak-interval timing.

Owns:
- RCC clock enable for TIM2
- TIM2 PSC and ARR calculation: `PSC=719, ARR=999` → 72MHz / 720 / 1000 = 100Hz
- TIM2 CR2 MMS bits set to `010` (Update → TRGO)
- `tim2_init(void)` — call before `adc_init()`

Does NOT own: ADC configuration, interrupt handling.

#### `usart.c` / `usart.h`

Responsibility: Initialize USART2 at 115200 baud (the ST-Link VCP channel on Nucleo).
Provide a blocking single-byte transmit and a blocking string transmit. Keep it simple
— no DMA, no TX ring buffer needed at 100Hz data rates.

Owns:
- RCC clock enable for USART2 and GPIOA
- PA2 (TX) alternate function push-pull configuration
- USART2 BRR = 72MHz / (16 × 115200) = 39.0625 → BRR = 0x271
- `usart2_init(void)`
- `usart2_write_char(char c)`
- `usart2_write_str(const char *s)` — loops over write_char
- `usart2_write_int(int32_t n)` — integer-to-ASCII conversion, no printf, no stdlib

Does NOT own: formatting of BPM messages (that belongs to application layer).

#### `systick.c` / `systick.h`

Responsibility: Provide millisecond wall-clock time using the Cortex-M3 SysTick timer.
This is the sole time source used throughout the firmware.

Owns:
- SysTick LOAD = 72000 - 1 (1ms tick at 72MHz)
- SysTick ISR increments `volatile uint32_t tick_ms`
- `systick_init(void)`
- `millis(void)` → returns `tick_ms`

Does NOT own: delays, timeouts, application logic.

---

### Algorithm Layer

Algorithm modules are pure C — no peripheral access, no global register reads. They
take values in and return values out via function parameters or small structs. This
makes them unit-testable with synthetic data without any hardware present.

#### `filter.c` / `filter.h`

Responsibility: Implement a fixed-size moving average filter over ADC samples. Reduces
LM358 noise and 50Hz mains interference.

Owns:
- `#define FILTER_SIZE 32` — window length (power of 2 preferred; 32 samples = 320ms
  at 100Hz, well within one heartbeat cycle)
- Fixed-size internal array `uint16_t buf[FILTER_SIZE]` — no malloc, no heap
- Head index and running sum maintained as module-static variables
- `filter_init(void)` — zeros buffer
- `filter_push(uint16_t sample)` → `uint16_t` — inserts sample, returns new average

Data flow: ADC sample (producer) → `filter_push()` → filtered value (consumer:
`peak_detect_update()`)

Implementation note: Use integer sum with uint32_t accumulator. Divide by FILTER_SIZE
using right-shift when FILTER_SIZE is a power of 2: `sum >> 5` for 32, `sum >> 4` for
16. Avoids division instruction on Cortex-M3.

#### `peak_detect.c` / `peak_detect.h`

Responsibility: Identify heartbeat peaks in the filtered PPG waveform and compute
peak-to-peak interval in milliseconds.

Owns:
- State machine (see State Machine section below)
- Adaptive threshold: `threshold = last_peak_amplitude * 0.6` — recalculated per beat
- Refractory period: minimum 300ms between valid peaks (rejects double-triggers at 200
  BPM, where beat interval = 300ms)
- `peak_detect_init(void)`
- `peak_detect_update(uint16_t filtered_sample, uint32_t timestamp_ms)` → returns
  `int32_t` — beat-to-beat interval in ms if a peak was just confirmed, -1 otherwise

Data flow: filtered value + timestamp (producer: filter.c + systick.c) →
`peak_detect_update()` → interval ms (consumer: `bpm.c`)

Does NOT own: BPM calculation, BPM averaging, UART output.

---

### Application Layer

Modules that compose peripheral drivers and algorithm modules into the final behaviour.
This is the only layer that may call across multiple lower-layer modules in sequence.

#### `bpm.c` / `bpm.h`

Responsibility: Convert beat-to-beat interval to BPM, apply sanity bounds, and maintain
a 5-reading rolling average.

Owns:
- `#define BPM_HISTORY 5`
- Rolling BPM array `uint16_t bpm_history[BPM_HISTORY]`, no heap
- `bpm_push(int32_t interval_ms)` — converts interval, bounds-checks (40–200 BPM),
  inserts into rolling array, returns averaged BPM or 0 if interval rejected
- BPM = 60000 / interval_ms (integer division, no floating point)

Data flow: interval ms (producer: `peak_detect_update()`) → `bpm_push()` → averaged
BPM (consumer: `main.c` print logic)

#### `finger_detect.c` / `finger_detect.h`

Responsibility: Determine whether a finger is present by examining filtered signal
amplitude. If amplitude is below a minimum threshold, no finger is on the sensor.

Owns:
- `#define FINGER_MIN_AMPLITUDE 100` — tuned during hardware calibration; represents
  minimum ADC count swing expected from a real pulse (starting value; adjust on bench)
- Tracks rolling min and max of last N filtered samples (small window, ~10 samples)
- `finger_present(void)` → `bool` — returns 1 if amplitude >= threshold, 0 otherwise

Data flow: filtered sample (producer: filter.c) → `finger_detect_update()` →
`finger_present()` → main.c decision

#### `main.c`

Responsibility: System entry point and main loop. Calls init functions, polls for new
ADC data, drives the pipeline, handles serial output.

Owns:
- Peripheral init sequence (see Build Order section)
- Main polling loop
- Calls `adc_get_sample()` → `filter_push()` → `peak_detect_update()` → `bpm_push()`
  and `finger_detect_update()` on each new sample
- Serial output: `"BPM: 72\r\n"` per confirmed beat, or `"--\r\n"` when no finger

Does NOT own: any algorithm logic. It is a sequencer, not a processor.

---

## ISR vs Main Loop: What Belongs Where

The single most important architectural boundary in bare-metal Cortex-M firmware.

### What Lives in ISR Context

| ISR | Trigger | Responsibility | Maximum Work Allowed |
|-----|---------|---------------|----------------------|
| ADC1 EOC ISR | ADC1 end-of-conversion | Read ADC DR register, write to ring buffer, set `new_sample` flag | < 50 CPU cycles |
| SysTick ISR | 1ms timer tick | Increment `tick_ms` counter | < 10 CPU cycles |

Rule: ISRs do nothing except capture data and set a flag. No filtering, no math, no
UART writes, no function calls into algorithm layer.

Rationale: At 72MHz, one 100Hz ADC sample fires every 720,000 cycles. The ISR does
~10 cycles of work. All remaining 719,990 cycles belong to the main loop. There is
no need to put signal processing in the ISR — the timing budget is extremely generous.

### What Lives in Main Loop

Everything else. The main loop polls `adc_get_sample()` (which checks the flag set by
the ISR). On a new sample:

```
adc_get_sample() → filter_push() → peak_detect_update() → bpm_push() / finger_detect()
→ usart print (when beat detected or state changes)
```

The loop runs at whatever speed the CPU allows between samples (far faster than
100Hz). It stays in a tight polling loop — no blocking delays inside the loop body.

### Why Not Full-ISR Processing?

Tempting to run the entire pipeline from the ADC ISR. Reject this approach because:
1. UART write from ISR blocks interrupts during transmission (~87µs at 115200 baud)
2. State machine bugs inside ISR are hard to debug (no printf-style tracing)
3. There is no timing pressure justifying ISR processing at 100Hz on a 72MHz CPU

---

## Data Flow: End to End

```
[Analog signal: LM358 output]
         |
         v
[ADC1, 12-bit, hardware triggered by TIM2 at 100Hz]
         |   (ADC EOC ISR fires)
         v
[adc_buf[] ring buffer] ←──── write in ISR
         |   (main loop polls adc_get_sample())
         v
[filter_push(raw_sample)] ──→ uint16_t filtered
         |
         ├──→ [finger_detect_update(filtered)] → finger_present() bool
         |
         v
[peak_detect_update(filtered, millis())] → int32_t interval_ms (-1 = no peak yet)
         |
         v (interval_ms > 0 only)
[bpm_push(interval_ms)] → uint16_t averaged_bpm (0 = rejected)
         |
         v
[usart2_write_str("BPM: NN\r\n")]  or  [usart2_write_str("--\r\n")]
```

Producers always push to the next stage. Consumers never reach back into producers.
The timestamp (`millis()`) is injected by main.c at the point of calling
`peak_detect_update()` — the peak detector does not call `millis()` directly
(makes it testable).

---

## State Machine: Peak Detection

State machine lives entirely inside `peak_detect.c`. Main loop calls
`peak_detect_update()` once per sample and reads the return value.

```
States: IDLE → RISING → PEAK_HOLD → FALLING → REFRACTORY → IDLE
```

### Transitions

```
IDLE
  ├─ sample > threshold? → RISING (record entry_time, reset peak_val)
  └─ sample ≤ threshold? → IDLE (stay)

RISING
  ├─ sample > peak_val? → RISING (update peak_val = sample, peak_time = now)
  ├─ sample < peak_val AND (now - peak_time > 20ms)? → PEAK_HOLD
  └─ sample < threshold AND (now - entry_time < 100ms)? → IDLE (noise reject)

PEAK_HOLD
  ├─ sample < peak_val × 0.9? → FALLING (compute interval, emit)
  └─ sample ≥ peak_val × 0.9? → PEAK_HOLD (stay — flat top)

FALLING
  ├─ sample < threshold? → REFRACTORY (arm refractory timer)
  └─ sample ≥ threshold? → FALLING (still descending)

REFRACTORY
  ├─ (now - last_peak_time) ≥ REFRACTORY_MS (300ms)? → IDLE
  └─ otherwise? → REFRACTORY (suppress retriggering)
```

### Threshold Update Rule

On each confirmed peak (PEAK_HOLD → FALLING transition):
```c
threshold = (uint16_t)(peak_val * 0.6f);
```
Use integer arithmetic: `threshold = (peak_val * 3) / 5;` to avoid FPU dependency.

### No-Finger Interaction

If `finger_present()` returns false, `peak_detect_init()` is called to reset the
state machine to IDLE. This prevents stale state when the finger is re-placed.

---

## Circular Buffer Pattern (No Malloc, Cortex-M3 Safe)

Used in two places: ADC sample ring buffer (in adc.c) and filter window (in filter.c).

```c
// Pattern: power-of-2 size → mask-based wrap (cheaper than modulo on M3)
#define BUF_SIZE  32          // must be power of 2
#define BUF_MASK  (BUF_SIZE - 1)

typedef struct {
    uint16_t  data[BUF_SIZE];
    uint8_t   head;           // write index
    uint8_t   count;          // valid entries (saturates at BUF_SIZE)
} RingBuf;

// Write (called from ISR — keep minimal):
static inline void ring_push(RingBuf *b, uint16_t val) {
    b->data[b->head & BUF_MASK] = val;
    b->head++;
    if (b->count < BUF_SIZE) b->count++;
}

// Read (called from main loop):
static inline uint16_t ring_peek_latest(RingBuf *b) {
    return b->data[(b->head - 1) & BUF_MASK];
}
```

ISR/main-loop safety: On Cortex-M3, 32-bit aligned reads/writes are atomic. `head`
is uint8_t — 8-bit write is also atomic on M3. No critical section needed for a
single-producer single-consumer ring buffer where ISR writes and main reads.

For the filter's moving average, `filter.c` maintains its own flat array + head index
(it does not use the ring buffer struct — it only needs a running sum, not arbitrary
access).

---

## STM32CubeIDE File / Module Structure

Standard CubeIDE layout. Project name: `HeartRateSensor`.

```
HeartRateSensor/
├── Core/
│   ├── Inc/
│   │   ├── adc.h
│   │   ├── tim.h
│   │   ├── usart.h
│   │   ├── systick.h
│   │   ├── filter.h
│   │   ├── peak_detect.h
│   │   ├── bpm.h
│   │   ├── finger_detect.h
│   │   └── main.h          ← include guards + global typedefs only
│   └── Src/
│       ├── adc.c
│       ├── tim.c
│       ├── usart.c
│       ├── systick.c
│       ├── filter.c
│       ├── peak_detect.c
│       ├── bpm.c
│       ├── finger_detect.c
│       ├── main.c
│       └── stm32f1xx_it.c  ← ISR bodies (ADC1_IRQHandler, SysTick_Handler)
├── Drivers/
│   └── CMSIS/              ← auto-generated by .ioc; do not edit
├── HeartRateSensor.ioc     ← CubeMX config (clock tree only — no HAL codegen)
└── HeartRateSensor.ld      ← linker script (auto-generated)
```

Key decisions:
- ISR bodies live in `stm32f1xx_it.c` (CubeIDE convention) but call thin shim
  functions declared in `adc.h` / `systick.h` — keeps ISR code reviewable alongside
  peripheral drivers.
- Algorithm files (`filter.c`, `peak_detect.c`, `bpm.c`) have zero `#include`s of
  CMSIS headers — enforces the hardware abstraction boundary.
- `main.h` contains only include guards and `#include "stm32f1xx.h"` — never
  algorithm logic.

---

## Build Order (Bottom-Up, with Rationale)

Each stage is testable before the next begins. Hardware is not required until Stage 4.

### Stage 1: SysTick + USART (Day 1)

Modules: `systick.c`, `usart.c`

Why first: Every subsequent stage needs a time source and a way to print debug output.
`millis()` and `usart2_write_str()` are the firmware's stdout. Build these, flash to
board, verify `millis()` increments and characters appear in the serial monitor.

Test: Print `millis()` every 1000ms. Verify count advances at ~1Hz.

### Stage 2: Algorithm Modules on Synthetic Data (Day 1-2)

Modules: `filter.c`, `peak_detect.c`, `bpm.c`, `finger_detect.c`

Why second: These are pure C with no hardware dependency. Feed synthetic sine-like
values via a lookup table defined in main.c. Verify filter smooths correctly, peak
detector fires at the right count, BPM averages correctly. Use USART to print
intermediate state.

Synthetic test signal in main.c (no ADC needed):
```c
// 100-step sine approximation table for 60 BPM (100 samples/beat)
static const uint16_t SINE_TABLE[100] = { /* precomputed */ };
static uint8_t sine_idx = 0;

// In the 100Hz tick handler:
uint16_t synthetic = SINE_TABLE[sine_idx++ % 100];
uint16_t filtered  = filter_push(synthetic);
int32_t  interval  = peak_detect_update(filtered, millis());
if (interval > 0) bpm_push(interval);
```

Expected output: stable 60 BPM printed every beat.

Test without ADC: Replace `adc_get_sample()` with the sine table read. The rest of
the call chain is identical to production code.

### Stage 3: TIM2 + ADC (Day 2-3)

Modules: `tim.c`, `adc.c`

Why third: Add hardware triggers only after the algorithm chain is validated. Connect
PA0 to 3.3V or a potentiometer for a static DC value. Verify ADC samples arrive at
100Hz (measure with SysTick: print `millis()` each ISR fire, expect ~10ms intervals).
Verify ring buffer fills without corruption.

Test: Print raw ADC value every sample. Confirm value matches potentiometer voltage
(ADC_count = Vin × 4095 / 3.3).

### Stage 4: Full Integration with Real PPG Signal (Day 3-4)

Assemble breadboard circuit. Connect LM358 output to PA0. Place finger. Observe raw
ADC waveform on serial monitor first (print raw value before filter). Tune `FILTER_SIZE`
and `FINGER_MIN_AMPLITUDE` to the real signal before enabling peak detection. Then
enable the full pipeline.

Calibration checkpoints:
1. Raw ADC oscillates visibly with pulse → LM358 gain is sufficient
2. After filter, waveform is smoother → filter window size is correct
3. Peak detector fires once per beat, not double-triggering → refractory period and
   threshold are correct
4. BPM output matches reference (phone camera PPG app) within ±5 BPM

### Stage 5: Stability and Edge Cases (Day 4-5)

- Confirm `"--\r\n"` prints when finger removed
- Confirm BPM stabilises within 5 beats after placement
- Test at low signal (dim room / loose placement) — adaptive threshold should
  maintain tracking where a fixed threshold would fail
- Test motion artifacts: brief finger shift should not cause persistent wrong BPM

---

## Architecture Anti-Patterns to Avoid

### Anti-Pattern 1: Processing Inside the ADC ISR

What: Running `filter_push()` or `peak_detect_update()` from `ADC1_IRQHandler`.
Why bad: Any bug in those functions corrupts ISR stack; UART writes from ISR block
interrupts; debugging is far harder.
Instead: Set a flag in the ISR, do all processing in the main loop.

### Anti-Pattern 2: Using `HAL_Delay()` or Any Blocking Delay in the Main Loop

What: `HAL_Delay(10)` between samples to pace the loop.
Why bad: Blocks the CPU; the ADC ring buffer overflows; SysTick ISR still fires but
loop misses samples.
Instead: The TIM2 hardware trigger paces ADC at exactly 100Hz. The main loop polls
`adc_get_sample()` and processes only when new data is available.

### Anti-Pattern 3: Algorithm State in `main.c`

What: Putting `peak_val`, `state`, `bpm_history[]` as local variables or file-statics
in `main.c`.
Why bad: Cannot unit test independently; main.c becomes a god file.
Instead: Each algorithm module owns its own state as file-static variables. `main.c`
calls the module's API functions only.

### Anti-Pattern 4: Floating-Point in Algorithm Layer

What: Using `float` or `double` for the moving average or BPM calculation.
Why bad: STM32F103 has no FPU — all float ops are software-emulated and take ~10x
more cycles. Integer arithmetic is sufficient for all operations here.
Instead: Use integer arithmetic throughout. Threshold = `(peak_val * 3) / 5`.
BPM = `60000 / interval_ms`. Filter average = `sum >> shift`.

### Anti-Pattern 5: Single Monolithic Source File

What: All code in `main.c`.
Why bad: Violates separation of concerns; cannot swap algorithm without touching driver
code; extremely difficult to test; course report cannot clearly explain system structure.
Instead: One file per module, one header per module. The layered structure described
above is the expected deliverable for an Advanced Microprocessors course report.

---

## Scalability Considerations

This is an embedded system with fixed constraints. "Scalability" means staying within
flash and RAM budgets.

| Concern | Current Approach | Limit / Note |
|---------|-----------------|--------------|
| Flash | All algorithm in plain C, no stdlib | STM32F103RB has 128KB flash; this firmware will use < 8KB |
| RAM | No malloc; all buffers statically sized | 20KB SRAM; filter(32×2) + bpm(5×2) + ring(32×2) < 200 bytes total |
| CPU | Main loop polling at ~72MHz; processing per sample < 1000 cycles | 720,000 cycles/sample budget; < 0.2% utilization |
| Stack | ISR stack depth minimal (< 10 words) | Default 512-word stack (2KB) is more than sufficient |

---

## Sources

- STM32F103RBT6 Reference Manual (RM0008) — ADC trigger, TIM2 TRGO, USART BRR calculation
- ARM Cortex-M3 Technical Reference Manual — SysTick, atomic access rules for 8/32-bit
- STM32 Nucleo-F103RB User Manual (UM1724) — PA2 USART2 TX pin mapping, ST-Link VCP
- Project spec (`.planning/PROJECT.md`) — algorithm parameters, signal chain, constraints
- Confidence: HIGH for layer structure and ISR/main boundary (universal bare-metal
  convention). HIGH for register values (derived from datasheets). MEDIUM for threshold
  initial values — require hardware calibration to confirm.
