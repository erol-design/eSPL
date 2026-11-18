# eSPL STM32G0 release notes

## v0.20.0-dev (2026-11-18 examples)

- `Examples/Basic`: 15 one-file driver examples for NUCLEO-G0B1RE (GPIO, EXTI, USART, RCC PLL,
  TIM PWM and update interrupt, ADC with VREFINT, DAC, DMA, CRC-32, SPI loop-back, I2C scan, IWDG,
  RTC, FLASH), built with CMake + Arm GCC without warnings at both check levels. Not yet run on a
  board. No driver change.
- Keil MDK project for every example (`ex_<name>.uvprojx`) and a workspace `Basic.uvmpw` for batch
  build; Arm Compiler 6, 0 errors, 0 warnings.

## v0.20.0-dev (2026-11-18 update)

- Fix: `FLASH_GetEcc` reads the ECC fail address (`ADDR_ECC`) with the reference manual width
  (RM0444, bits 15:0). The CMSIS mask is 14 or 15 bits wide; on STM32G0B1 / G0C1 bit 15 of the
  offset was dropped. No other driver change.
- Register field masks of the CMSIS headers are now checked against the ST SVD files on all 12
  SKUs (internal verification); the differences found are documented and need no driver change.

## v0.20.0-dev (2026-11-17 update)

- Umbrella header `stm32g0_spl.h`: one include for every module available on the selected device.
- Start project `Examples/G0_Template` (NUCLEO-G0B1RE LED blink; Keil MDK or CMake + Arm GCC).
- CMake integration: `espl::g0` and `espl::g0_startup` targets, example Arm GCC toolchain file.
- API reference `Docs/API.md`, generated from the headers.
- MIT license; `SPDX-License-Identifier: MIT` in every own source file; CMSIS notices in
  `THIRD_PARTY_NOTICES.md`.
- No change in driver code: apart from the new header and the SPDX lines the sources are
  unchanged, and the Keil image is byte-identical to the previous version.

Known limitations unchanged: development paused, kept as the reference implementation. Not yet
verified on hardware. USB is not part of this library.

## v0.20.0-dev (2026-11-14)

- 30 driver modules for all 12 STM32G0 devices.
- FULL / FAST validation levels from a single source, level tag on every public API.
- Builds with Keil AC6, clang and Arm GCC without warnings.
- Errata ES0548 Rev 4 and the family errata sheets reviewed and applied.

Known limitations: development paused, kept as the reference implementation. Not yet verified on
hardware. USB is not part of this library.
