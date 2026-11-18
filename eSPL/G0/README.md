# eSPL STM32G0

C99 peripheral driver library for the STM32G0 family, built on ST CMSIS Device (cmsis_device_g0
v1.4.4) and Arm CMSIS Core 5.6 (both included under `CMSIS/`). Classic Standard Peripheral Library
naming style, independent implementation, not source-compatible with ST SPL.

- No RTOS, no dynamic memory, non-blocking APIs
- Every status-returning API returns `SPL_Status`
- File prefix `stm32g0_`; one umbrella header `stm32g0_spl.h`

Version: **v0.20.0-dev** (reference implementation; development paused). Not yet hardware-verified.
See [RELEASE_NOTES.md](RELEASE_NOTES.md).

## Modules

GPIO, RCC, FLASH, PWR, NVIC/SysTick, SYSCFG, DBGMCU, USART/LPUART, SPI/I2S, I2C/SMBus, DMA, DMAMUX,
CRC, IWDG, WWDG, CRS, TIM, LPTIM, RTC/TAMP, EXTI, ADC, DAC, COMP, VREFBUF, FDCAN1/2, AES, RNG,
HDMI-CEC, UCPD1/2, MPU.

Not included: USB.

API reference: [Docs/API.md](Docs/API.md) - every public function with its prototype, results,
`Caller:` duty and `Level:` line (generated from the headers).

## Devices

All 12 STM32G0 devices: G030, G031, G041, G050, G051, G061, G070, G071, G081, G0B0, G0B1, G0C1.
Primary target STM32G0B1 (bench: NUCLEO-G0B1RE). A peripheral instance or feature that the selected
device does not have returns `SPL_ERR_PARAM` or `SPL_ERR_UNSUPPORTED`; a peripheral the device does
not have at all declares no API, so a call does not compile.

## Quick start

Start from [`Examples/G0_Template`](Examples/G0_Template): NUCLEO-G0B1RE LED blink, Keil MDK project
or CMake + Arm GCC (build verified, not yet run on a board).
One small program per driver: [`Examples/Basic`](Examples/Basic) (15 examples: GPIO, EXTI, USART,
RCC, TIM, ADC, DAC, DMA, CRC, SPI, I2C, IWDG, RTC, FLASH; Keil MDK projects or CMake + Arm GCC,
build verified, not yet run on a board).

```c
#include "stm32g0_spl.h"   /* all modules; a single module header also works */
```

Check every returned `SPL_Status`. The `Caller:` line above each prototype is the duty the driver
does not check itself (peripheral clock, pins, NVIC, serialisation).

## Adding eSPL to a project

### CMake

```cmake
set(ESPL_G0_DEVICE STM32G0B1xx)   # required
set(ESPL_G0_CHECK_LEVEL 0)        # 0 FULL (default) or 2 FAST
add_subdirectory(eSPL/G0 espl_g0)
target_link_libraries(app PRIVATE espl::g0 espl::g0_startup)
```

`espl::g0_startup` adds the CMSIS system file and the GCC startup file; the linker script is the
application's. Example toolchain file: `cmake/arm-none-eabi-gcc.cmake`.

### Other build systems (Keil, IAR, Makefile)

1. Add the `Src/*.c` files you need. `Src/stm32g0_check.c` must be compiled and linked exactly once.
2. Add the CMSIS startup file for your device from `CMSIS/Device/Source/startup/{arm,gcc,iar}`
   and `CMSIS/Device/Source/system_stm32g0xx.c`.
3. Include paths:
   - `eSPL/G0/Inc`
   - `eSPL/G0/Private`
   - `eSPL/Common/Inc`
   - `eSPL/G0/CMSIS/Device/Include`
   - `eSPL/G0/CMSIS/Core/Include`
4. Define the device, for example `STM32G0B1xx`.
5. Optionally select the validation level for the whole build (default FULL):

```c
-DSPL_CHECK_LEVEL=0   /* SPL_CHECK_FULL */
-DSPL_CHECK_LEVEL=2   /* SPL_CHECK_FAST */
```

All translation units must use the same level; a mixed build does not link. The `/* Level: ... */`
line above each public API shows which checks remain at which level: `F` FULL only, `r` every level
(FAST keeps only `r`). FAST is not a safety mode.

## Toolchains

| Compiler | Status |
|---|---|
| Keil MDK, Arm Compiler 6.22 | Supported (primary) |
| clang, Arm target | Supported |
| Arm GCC 14.2 (binutils 2.36+) | Supported, also through CMake |
| IAR, Keil AC5 | Not tested |

## License

MIT, see [`LICENSE`](../../LICENSE) at the repository root; every own source file starts with
`SPDX-License-Identifier: MIT`. Not MIT: `CMSIS/Core` (Arm) and `CMSIS/Device` (ST), Apache-2.0
([THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)).

## Notes

- Silicon errata ES0548 Rev 4 and the errata sheets of the other G0 devices were reviewed;
  driver-relevant workarounds are implemented in the code and noted in the API headers.
- Irreversible operations (option bytes, flash protection, OTP) require an explicit flag and key.
- Some source comments refer to internal design documents, tests and tools that are not published.
