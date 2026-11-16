# eSPL STM32G4 release notes

## v0.18.0 candidate (2026-11-16)

- Added parameter, state and size checks: LPTIM / HRTIM period shrink without preload, TIM dead
  time 0 with complementary outputs, SPI DMA start while busy, SPI CRC error and I2C PEC error on
  the receive / completion services, stale TIM update flag, I2C target address acknowledge, USART
  data mask with parity.
- Compile-time checks for driver tables against CMSIS device sizes (NVIC, DMA, FDCAN, ADC, RCC,
  RTC and others).
- Hardware tests repeated on NUCLEO-G431RB and NUCLEO-G474RE at FULL, MIX and FAST: all automated
  steps pass (HRTIM on G474 only).

Known limitations: pre-release. I2C, SPI, QSPI, SAI and FMC are host-verified only, not
hardware-tested. Manual steps (button, oscilloscope, second CAN node) not yet run.
USB and UCPD are not part of this library.

## v0.18.0-dev (2026-11-15)

- 33 driver modules with FULL / MIX / FAST validation levels from a single source.
- Validation level tags on every public status API header.
- Hardware tests passed on NUCLEO-G431RB and on an STM32G474 board for the tested modules.
- Errata ES0430 / ES0431 Rev 10 and ES0523 Rev 7 reviewed and applied.
- Arm CMSIS Core included under `CMSIS/Core`.
- `Examples/G431_Template`: 170 MHz, LED and virtual COM port template, Keil and CMake / Arm GCC.

Known limitations: pre-release; not every API is hardware-verified. HRTIM is not hardware-tested.
USB and UCPD are not part of this library.
