# Technology Stack

**Project:** HeartRateSensor — bare-metal STM32F103RBT6 PPG
**Researched:** 2026-05-12
**Confidence:** HIGH — STM32F103 RM0008 register map is frozen silicon; these values are directly from the reference manual, not inferred.

---

## Toolchain

### STM32CubeIDE Project Setup (No HAL)

| Tool | Version / Detail | Role |
|------|-----------------|------|
| STM32CubeIDE | 1.15+ (2024) | IDE, GCC ARM toolchain, ST-Link debugger |
| arm-none-eabi-gcc | bundled with CubeIDE (GCC 12+) | Cross-compiler |
| OpenOCD / ST-Link GDB server | bundled | Flash + debug over USB ST-Link |
| CMSIS 5 headers | shipped inside CubeIDE device pack | Device + core headers |
| STM32CubeMX (.ioc) | Integrated in CubeIDE | Generates clock tree + linker script only |

**Setup discipline — no HAL:** In the .ioc file set "Project Manager → Project → Firmware Library Package" to "No Library". CubeIDE will still generate `startup_stm32f103rbtx.s`, `STM32F103RBTx_FLASH.ld`, and the CMSIS device headers without generating any `stm32f1xx_hal_*` source. Delete `Core/Src/stm32f1xx_it.c` HAL callbacks and `stm32f1xx_hal_conf.h` if generated; keep only CMSIS.

---

## CMSIS Header Dependency Tree

These are the only headers needed for bare-metal register access on Cortex-M3 STM32F103:

```
stm32f1xx.h                  ← top-level, include this one only
  └─ stm32f103xb.h           ← device-specific peripheral structs + base addresses
       └─ core_cm3.h         ← NVIC, SCB, SysTick, ITM register structs
            └─ cmsis_compiler.h   ← __DSB, __ISB, __NOP etc.
            └─ cmsis_gcc.h        ← GCC-specific intrinsics
```

**Key device selection macro** — must be defined before any include, either in `Makefile`/project settings or at top of every source file:

```c
#define STM32F103xB
#include "stm32f1xx.h"
```

CubeIDE places `STM32F103xB` in the project-wide C defines automatically when you select the device; verify it is present under Project → Properties → C/C++ Build → Settings → MCU GCC Compiler → Preprocessor.

The macro selects `stm32f103xb.h` (128 KB flash variant — correct for RBT6) rather than `stm32f103xe.h` (256/512 KB). Using the wrong variant shifts some peripheral base addresses incorrectly.

---

## RCC Clock Configuration

**Target:** 72 MHz SYSCLK from 8 MHz HSE (Nucleo onboard crystal) via PLL × 9.

### Clock tree for this project

```
HSE (8 MHz onboard)
  → PLL source = HSE
  → PLLMUL = ×9   → PLL output = 72 MHz = SYSCLK
  → AHB  prescaler = /1 → HCLK  = 72 MHz
  → APB1 prescaler = /2 → PCLK1 = 36 MHz  (TIM2, USART2)
  → APB2 prescaler = /1 → PCLK2 = 72 MHz  (ADC1, GPIOA)
  → ADC  prescaler = /6 → ADCCLK = 12 MHz  (max for F103 is 14 MHz)
```

### Register sequence (RM0008 §6.3)

```c
// 1. Enable HSE, wait ready
RCC->CR |= RCC_CR_HSEON;
while (!(RCC->CR & RCC_CR_HSERDY));

// 2. Flash latency for 72 MHz (2 wait states, RM0008 §3.1)
FLASH->ACR = FLASH_ACR_LATENCY_2 | FLASH_ACR_PRFTBE;

// 3. AHB=SYSCLK, APB1=SYSCLK/2, APB2=SYSCLK/1
RCC->CFGR = RCC_CFGR_HPRE_DIV1      // AHB /1
           | RCC_CFGR_PPRE1_DIV2     // APB1 /2  → 36 MHz
           | RCC_CFGR_PPRE2_DIV1     // APB2 /1  → 72 MHz
           | RCC_CFGR_ADCPRE_DIV6    // ADC /6   → 12 MHz
           | RCC_CFGR_PLLSRC         // PLL source = HSE
           | RCC_CFGR_PLLMULL9;      // PLL ×9

// 4. Enable PLL, wait ready
RCC->CR |= RCC_CR_PLLON;
while (!(RCC->CR & RCC_CR_PLLRDY));

// 5. Switch SYSCLK to PLL
RCC->CFGR |= RCC_CFGR_SW_PLL;
while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL);
```

### Peripheral clock enables (must precede any register write to that peripheral)

```c
// APB2: GPIOA (PA0=ADC_IN0, PA2=USART2_TX, PA3=USART2_RX), AFIO, ADC1
RCC->APB2ENR |= RCC_APB2ENR_IOPAEN    // GPIOA
              | RCC_APB2ENR_AFIOEN     // Alternate function I/O
              | RCC_APB2ENR_ADC1EN;    // ADC1

// APB1: TIM2, USART2
RCC->APB1ENR |= RCC_APB1ENR_TIM2EN
              | RCC_APB1ENR_USART2EN;
```

**F103 gotcha:** GPIOA and ADC1 are on APB2 (not APB1). TIM2 and USART2 are on APB1. This is different from STM32F4 where ADC is on APB2 and some GPIO is on AHB1. Enabling the wrong bus register does nothing and the peripheral stays dead with no error.

---

## GPIO Configuration (CMSIS, no HAL)

STM32F103 uses `GPIOx->CRL` (pins 0–7) and `GPIOx->CRH` (pins 8–15), 4 bits per pin.

| Pin | Function | CRL/CRH bits | CNF | MODE | Note |
|-----|----------|--------------|-----|------|------|
| PA0 | ADC1_IN0 | CRL bits [3:0] | 00 (analog) | 00 (input) | analog input mode |
| PA2 | USART2_TX | CRL bits [11:8] | 10 (AF push-pull) | 11 (50 MHz) | |
| PA3 | USART2_RX | CRL bits [15:12] | 01 (float input) | 00 (input) | |

```c
// PA0 → analog input (ADC1_IN0)
// CRL[3:0] = 0000: MODE=00(input), CNF=00(analog)
GPIOA->CRL &= ~(GPIO_CRL_CNF0 | GPIO_CRL_MODE0);  // clear → 0000

// PA2 → USART2_TX alternate function push-pull 50MHz
// CRL[11:8]: MODE=11(50MHz output), CNF=10(AF push-pull)
GPIOA->CRL &= ~(GPIO_CRL_CNF2 | GPIO_CRL_MODE2);
GPIOA->CRL |=  (GPIO_CRL_MODE2 | GPIO_CRL_CNF2_1); // 1011

// PA3 → USART2_RX floating input
// CRL[15:12]: MODE=00(input), CNF=01(floating)
GPIOA->CRL &= ~(GPIO_CRL_CNF3 | GPIO_CRL_MODE3);
GPIOA->CRL |=  GPIO_CRL_CNF3_0; // 0100
```

**F103 gotcha:** There is no per-pin direction bit like in F4 MODER. Mode and output type are packed into 4-bit fields in CRL/CRH. Forgetting to clear before OR-ing produces wrong modes silently.

---

## ADC1 — Bare-Metal Register Configuration

**Goal:** Single-conversion mode, triggered by TIM2 TRGO (Update event), 12-bit, channel 0 (PA0), 100 Hz.

### Key registers (RM0008 §11)

| Register | Purpose |
|----------|---------|
| ADC1->CR1 | Mode, interrupt enable, resolution control |
| ADC1->CR2 | ADON, external trigger source, trigger enable, start |
| ADC1->SQR3 | Channel sequence for regular group (first conversion) |
| ADC1->SMPR2 | Sample time for channels 0–9 |
| ADC1->SR | Status: EOC (bit 1), STRT (bit 4) |
| ADC1->DR | 12-bit result (bits [11:0]) |

### CR1 configuration

```c
ADC1->CR1 = 0;  // start clean
// Bits used:
//   EOCIE [5] = 1  → interrupt on EOC (End Of Conversion)
//   SCAN  [8] = 0  → single channel (no scan mode needed for one channel)
//   JAUTO [10]= 0  → no auto-injected
// Everything else = 0 (12-bit by default in F103; no resolution bits — F103 ADC is always 12-bit)

ADC1->CR1 = ADC_CR1_EOCIE;  // enable EOC interrupt
```

**F103-specific:** There is NO resolution control field in CR1 on the F103. The F4 series added ARES[1:0] at bits [25:24]. On F103, writing anything to bits [25:24] of CR1 has no effect — but assuming they exist and trying to set 12-bit via a F4 code snippet will silently do nothing harmful; however F4 HAL code ported to F103 that reads resolution from those bits will read 0 and calculate wrong conversion times. Always verify the ADC is F103-class when using examples.

### CR2 configuration

```c
// EXTSEL[2:0] bits [19:17]: selects external trigger for regular group
// For F103: TIM2_TRGO = 011 (binary)   ← RM0008 Table 68
// EXTTRIG [20] = 1: enable external trigger
// ADON    [0]  = 1: power on ADC (write twice — first write just powers on)
// CONT    [1]  = 0: single conversion (not continuous)
// ALIGN   [11] = 0: right-aligned data in DR

ADC1->CR2 = ADC_CR2_EXTSEL_1 | ADC_CR2_EXTSEL_0  // EXTSEL = 011 = TIM2_TRGO
           | ADC_CR2_EXTTRIG                        // enable external trigger
           | ADC_CR2_ADON;                          // power on

// Calibration sequence (mandatory after power-on, RM0008 §11.4)
// Wait for ADC stabilisation: minimum 1µs after ADON
for (volatile uint32_t i = 0; i < 72; i++);  // ~1µs at 72MHz

ADC1->CR2 |= ADC_CR2_RSTCAL;    // reset calibration registers
while (ADC1->CR2 & ADC_CR2_RSTCAL);  // wait

ADC1->CR2 |= ADC_CR2_CAL;       // start calibration
while (ADC1->CR2 & ADC_CR2_CAL);     // wait until done (~83 ADC cycles)
```

**F103-specific gotcha — EXTSEL values differ from F4:**

| EXTSEL[2:0] | F103 trigger source (regular group) |
|-------------|--------------------------------------|
| 000 | TIM1_CC1 |
| 001 | TIM1_CC2 |
| 010 | TIM1_CC3 |
| 011 | **TIM2_TRGO** ← use this |
| 100 | TIM2_CC2 |
| 101 | TIM3_TRGO |
| 110 | TIM3_CC1 |
| 111 | EXTI11 / TIM8_TRGO (software trigger when SWSTART used) |

CMSIS defines `ADC_CR2_EXTSEL_0` = bit 17, `ADC_CR2_EXTSEL_1` = bit 18, `ADC_CR2_EXTSEL_2` = bit 19. For TIM2_TRGO = 0b011, set bits 17 and 18 (EXTSEL_0 | EXTSEL_1).

**F103-specific gotcha — calibration is mandatory.** The F103 ADC requires a calibration cycle after power-on. Skipping it produces readings with ±2 LSB systematic offset that is random across power cycles. The F4 and F7 have a different (self-calibrating) ADC architecture and do not require the same sequence.

### SQR3 — channel selection

```c
// SQR3[4:0] = SQ1: channel number for the first (and only) conversion
// Channel 0 = PA0 → write 0
ADC1->SQR3 = 0;   // SQ1 = channel 0
// SQR1[23:20] = L: number of conversions - 1. L=0 → 1 conversion.
ADC1->SQR1 = 0;   // already 0 reset value; explicit for clarity
```

### SMPR2 — sample time

```c
// SMPR2[2:0] = SMP0: sample time for channel 0
// Options (RM0008 Table 65):
//   000 = 1.5 cycles
//   001 = 7.5 cycles
//   010 = 13.5 cycles   ← use this for stable PPG (LM358 has ~1MHz GBW, signal well-settled)
//   011 = 28.5 cycles
// Total conversion time = sample_cycles + 12.5 = 26 cycles at 12 MHz → 2.17µs
// At 100Hz the ADC has 10ms between triggers — sample time choice is irrelevant for timing;
// choose 13.5 for best noise rejection without being unnecessary slow.
ADC1->SMPR2 = ADC_SMPR2_SMP0_1;  // SMP0 = 010 = 13.5 cycles
```

### ADC1 interrupt

```c
// Enable ADC1 IRQ in NVIC (core_cm3.h NVIC functions)
NVIC_SetPriority(ADC1_2_IRQn, 1);  // priority 1 (0=highest)
NVIC_EnableIRQ(ADC1_2_IRQn);

// ISR name (must match startup_stm32f103rbtx.s vector table entry)
void ADC1_2_IRQHandler(void) {
    if (ADC1->SR & ADC_SR_EOC) {
        ADC1->SR &= ~ADC_SR_EOC;       // clear EOC flag
        uint16_t sample = ADC1->DR;    // reading DR also clears EOC on F103
        // push sample into processing buffer
    }
}
```

**F103 note:** On F103, reading ADC1->DR clears the EOC flag automatically (RM0008 §11.10). Writing 0 to SR->EOC before reading DR is safe but redundant. Do not clear EOC before reading DR — read DR first or clear after.

---

## TIM2 — 100 Hz Hardware Trigger for ADC

**Goal:** TIM2 generates an Update Event (UEV) at exactly 100 Hz, which drives TRGO to trigger ADC1 conversion. No jitter because the timer runs independently of software.

### Calculation (RM0008 §14)

```
TIM2 clock = APB1 timer clock
APB1 prescaler = /2 → PCLK1 = 36 MHz
BUT: when APB prescaler ≠ 1, timer clock = 2 × PCLK1 = 72 MHz  (RM0008 §6.2.2)

Target: 100 Hz update rate
TIM2_CLK / ((PSC+1) × (ARR+1)) = 100

Choose PSC = 719, ARR = 999:
72,000,000 / (720 × 1000) = 100.000 Hz  ✓

Alternatively PSC = 7199, ARR = 99:
72,000,000 / (7200 × 100) = 100.000 Hz  ✓
(First option preferred: larger ARR gives finer duty cycle if PWM ever needed)
```

### Register sequence

```c
TIM2->PSC = 719;       // prescaler: divide timer clock by 720
TIM2->ARR = 999;       // auto-reload: count 0..999 = 1000 ticks → 100 Hz

// CR2: MMS[2:0] bits [6:4] — Master Mode Selection
// MMS = 010 → Update event as TRGO output   (RM0008 Table 40)
TIM2->CR2 = TIM_CR2_MMS_1;  // MMS = 010: Update → TRGO

// CR1: CEN [0] = 1 to start
// ARPE [7] = 1: buffer ARR (safe practice for changing ARR at runtime)
TIM2->CR1 = TIM_CR1_ARPE | TIM_CR1_CEN;
```

**MMS bit mapping (CR2 bits [6:4]):**

| MMS[2:0] | TRGO event |
|----------|-----------|
| 000 | Reset |
| 001 | Enable |
| **010** | **Update (UEV) ← use this** |
| 011 | Compare Pulse (OC1) |
| 100–111 | OC1/2/3/4REF |

`TIM_CR2_MMS_1` in CMSIS = bit 5 = 0b010 for MMS. Verify: `TIM_CR2_MMS_1` is defined as `(0x2UL << TIM_CR2_MMS_Pos)` where `TIM_CR2_MMS_Pos = 4`, giving bit 5 set → MMS = 010. Correct.

**No TIM2 interrupt needed** — the ADC interrupt fires after each conversion. TIM2 runs silently as a free-running trigger source.

---

## USART2 — 115200 Baud Serial Output

**Pins:** PA2 = USART2_TX, PA3 = USART2_RX (Nucleo routes USART2 to the ST-Link virtual COM port).

### Baud rate calculation (RM0008 §27.3.4)

```
USART2 clock = PCLK1 = 36 MHz  (APB1)

BRR = f_CK / (16 × baud_rate) for oversampling-by-16 (OVER8=0, default)
BRR = 36,000,000 / (16 × 115200)
BRR = 36,000,000 / 1,843,200
BRR = 19.53125

Integer part (DIV_Mantissa) = 19     → bits [15:4] = 19  → 19 << 4 = 0x130
Fractional part: 0.53125 × 16 = 8.5 → round to 9        → bits [3:0] = 9
BRR = (19 << 4) | 9 = 0x130 | 0x9 = 0x139 = 313 decimal

Actual baud = 36,000,000 / (16 × (19 + 9/16))
            = 36,000,000 / (16 × 19.5625)
            = 36,000,000 / 313
            = 115,015 baud  (error = 0.016% — within UART spec of ±1.5%)
```

**Register sequence:**

```c
USART2->BRR = 0x139;   // 313 decimal — for 115200 at PCLK1=36MHz

// CR1: TE [3]=1 (transmit enable), RE [2]=1 (receive enable), UE [13]=1 (USART enable)
// 8-bit data (M=0), 1 stop bit (in CR2 STOP[1:0]=00), no parity (PCE=0)
USART2->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
// CR2 reset value = 0x0000 → 1 stop bit, correct
// CR3 reset value = 0x0000 → no flow control, correct
```

**Transmit function (polling — sufficient for BPM output at ~1 Hz):**

```c
void usart2_send_char(char c) {
    while (!(USART2->SR & USART_SR_TXE));  // wait for TX register empty
    USART2->DR = (uint8_t)c;
}

void usart2_send_string(const char *s) {
    while (*s) usart2_send_char(*s++);
}
```

**SR flag mapping (F103 USART, RM0008 §27.6.1):**

| Bit | Name | Meaning |
|-----|------|---------|
| 5 | RXNE | RX Not Empty — data ready to read |
| 6 | TC | Transmission Complete |
| 7 | TXE | TX Data Register Empty — safe to write DR |

Do NOT use TC to pace writes (it goes low momentarily during transmit and requires explicit clear). Use TXE for each byte.

---

## SysTick — 1 ms millis()

SysTick is a Cortex-M3 core peripheral, configured via CMSIS `core_cm3.h`. It is independent of STM32 RCC peripheral clocks.

```c
// SysTick->LOAD: reload value for 1ms at 72MHz
// SysTick clock = HCLK = 72 MHz (when CLKSOURCE bit set)
// Ticks per ms = 72,000,000 / 1000 = 72,000
// LOAD = ticks - 1 = 71,999  (counter counts from LOAD down to 0, then reloads)

SysTick->LOAD = 71999;
SysTick->VAL  = 0;          // clear current value to force reload immediately
SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk  // use processor clock (HCLK), not /8
              | SysTick_CTRL_TICKINT_Msk     // enable SysTick exception
              | SysTick_CTRL_ENABLE_Msk;     // start counting

// Global counter
static volatile uint32_t ms_ticks = 0;

void SysTick_Handler(void) {
    ms_ticks++;
}

uint32_t millis(void) {
    return ms_ticks;
}
```

**CLKSOURCE gotcha:** If CLKSOURCE=0 (AHB/8 = 9 MHz), LOAD would be 8999 for 1ms. Both work, but using HCLK directly (CLKSOURCE=1) avoids an invisible /8 divider that trips up anyone reading the code later. Use HCLK.

**CMSIS convenience:** `SysTick_Config(SystemCoreClock / 1000)` from `core_cm3.h` does exactly the above in one call. It sets LOAD, VAL, CTRL with HCLK source. Use it if you want, but showing the explicit register write demonstrates bare-metal understanding for the course report.

---

## Interrupt Priority Plan

| IRQ | Priority | Rationale |
|-----|----------|-----------|
| SysTick | 0 (highest) | millis() must never be delayed |
| ADC1_2_IRQn | 1 | sample ISR must complete before next TIM2 trigger at 10ms |
| USART2 (if using IRQ TX) | 2 | output is best-effort |

Set with `NVIC_SetPriority(IRQn, priority)` before `NVIC_EnableIRQ(IRQn)`. Enable global interrupts with `__enable_irq()` (CMSIS intrinsic).

---

## Alternatives Considered

| Category | Recommended | Alternative | Why Not |
|----------|-------------|-------------|---------|
| ADC trigger | TIM2 TRGO hardware trigger | Software polling with SysTick | Software trigger introduces ISR latency jitter; at 100Hz the peak-interval timing needs sub-millisecond accuracy |
| ADC result handling | EOC interrupt (ADC1_2_IRQn) | DMA channel 1 | DMA adds setup complexity; single-channel 100Hz is trivially handled by ISR with no CPU saturation (<1% duty cycle per ISR) |
| USART TX | Polling (TXE flag) | Interrupt-driven TX ring buffer | BPM output is ~15 chars at ~1 Hz — polling completes in <150µs; ring buffer overhead is not justified |
| millis() source | SysTick | TIM3 or TIM4 | SysTick is a dedicated OS tick timer; saving TIM3/TIM4 for potential future use |
| Clock source | HSE 8MHz × PLL9 = 72MHz | HSI 8MHz × PLL9 = 72MHz | HSI is ±1% accurate; HSE uses the Nucleo onboard crystal at ±50ppm — more accurate baud rate and ADC timing |

---

## F103-Specific Gotchas Summary

| Gotcha | Detail |
|--------|--------|
| ADC calibration mandatory | Run RSTCAL + CAL sequence after every ADON. Not required on F4/F7. Skipping gives random DC offset. |
| EXTSEL encoding differs from F4 | TIM2_TRGO = 011 on F103; on F4 ADC the EXTSEL map is completely different. Never copy F4 ADC examples. |
| No ADC resolution bits in CR1 | F103 ADC is always 12-bit. Bits [25:24] of CR1 are reserved. F4 code that sets RES[1:0] is silently incompatible. |
| Timer clock doubling on APB | When APB1 prescaler ≠ 1 (ours = /2), TIM2 clock = 2×PCLK1 = 72 MHz, not 36 MHz. This affects PSC/ARR calculation. |
| GPIOA clock on APB2, not AHB | STM32F1 GPIO is on APB2 (RCC_APB2ENR). STM32F4 GPIO is on AHB1 (RCC_AHB1ENR). Wrong enable = GPIO stays input. |
| CRL/CRH GPIO config (not MODER) | F1 uses 4-bit packed CNF+MODE fields in CRL/CRH. F4 uses MODER/OTYPER/OSPEEDR/PUPDR. Code is not portable. |
| ADC1_2_IRQn shared vector | ADC1 and ADC2 share one interrupt vector (ADC1_2_IRQn). Check ADC1->SR->EOC inside the ISR. |
| USART2 BRR for 36MHz PCLK1 | BRR = 0x139 (313). Many online examples assume 72MHz APB or 8MHz HSI — use wrong BRR values for this project. |

---

## Sources

- STM32F103xB Reference Manual RM0008 Rev 21 (ST Microelectronics) — primary register reference. Sections: §6 (RCC), §11 (ADC), §14 (TIM2), §27 (USART), §3 (Flash ACR), §9 (GPIO)
- STM32F103RBT6 Datasheet DS5319 — pin multiplexing table, ADC channel mapping
- CMSIS 5 core_cm3.h — SysTick_Config(), NVIC_SetPriority(), NVIC_EnableIRQ() signatures
- Nucleo-F103RB User Manual UM1724 — PA2/PA3 routed to ST-Link virtual COM, 8MHz HSE crystal confirmed
- Confidence: HIGH — RM0008 register map is fixed silicon; no version ambiguity for STM32F103.
