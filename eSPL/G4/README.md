# eSPL STM32G4

C99 peripheral driver library for the STM32G4 family, built on ST CMSIS Device (cmsis_device_g4
V1.2.6) and Arm CMSIS Core 5.6 (both included under `CMSIS/`). It follows the classic Standard
Peripheral Library naming style (`GPIO_Init`, `USART_TryRead`, `FDCAN_TryWrite`) but is an
independent implementation, not source-compatible with ST SPL.

- No RTOS, no dynamic memory, non-blocking APIs
- Every status-returning API returns `SPL_Status`
- File prefix `stm32g4_`; one umbrella header `stm32g4_spl.h`
- API reference: [Docs/API.md](Docs/API.md) (description, results, `Caller:` duty and check
  level of every public function)

Version: **v0.18.0 candidate** (pre-release). See [RELEASE_NOTES.md](RELEASE_NOTES.md).

## Modules

RCC, GPIO, USART/LPUART, TIM, DMA, DMAMUX, SPI, ADC, I2C, FDCAN, EXTI, PWR, FLASH, IWDG, WWDG,
RTC/TAMP, DAC, COMP, OPAMP, VREFBUF, CRC, RNG, LPTIM, QUADSPI, SAI, FMC, NVIC/SysTick, DBGMCU,
SYSCFG, CRS, CORDIC, FMAC, HRTIM (G474/G484/G414; `SPL_ERR_UNSUPPORTED` on other devices).

Not included: USB, UCPD.

## Devices

Builds for 12 devices: G411xB, G411xC, G414, G431, G441, G471, G473, G474, G483, G484, G491, G4A1.
Primary target STM32G431; bench tests on NUCLEO-G431RB and NUCLEO-G474RE. A peripheral instance or
feature that the selected device does not have returns `SPL_ERR_PARAM` or `SPL_ERR_UNSUPPORTED`.

## Adding eSPL to a project

With CMake:

```cmake
set(ESPL_G4_DEVICE STM32G431xx)   # required
set(ESPL_G4_CHECK_LEVEL 0)        # 0 FULL (default) or 2 FAST
add_subdirectory(eSPL/G4 espl_g4)
target_link_libraries(app PRIVATE espl::g4 espl::g4_startup)
```

`espl::g4_startup` adds the CMSIS system file and the GCC startup file; the linker script is the
application's. Example toolchain file: `cmake/arm-none-eabi-gcc.cmake`.

Without CMake (for example Keil MDK):

1. Add the `Src/*.c` files you need. `Src/stm32g4_check.c` must be compiled and linked exactly once.
2. Add the CMSIS startup file for your device from `CMSIS/Device/Source/startup/{arm,gcc}`
   and `CMSIS/Device/Source/system_stm32g4xx.c`.
3. Include paths: `eSPL/G4/Inc`, `eSPL/G4/Private`, `eSPL/G4/CMSIS/Device/Include`,
   `eSPL/G4/CMSIS/Core/Include`.
4. Define the device, for example `STM32G431xx`, and `#include "stm32g4_spl.h"`.
5. Optionally select the validation level for the whole build (default FULL):

```c
-DSPL_CHECK_LEVEL=0   /* SPL_CHECK_FULL */
-DSPL_CHECK_LEVEL=2   /* SPL_CHECK_FAST */
```

All translation units must use the same level; a mixed build does not link. Each public API
documents which checks remain at FAST (`/* Level: ... */`) and what the caller must do that the
code does not check (`Caller:`).

The quickest start is **`Examples/G431_Template`** (NUCLEO-G431RB): 170 MHz clock setup, LED,
ST-LINK virtual COM port at 115200 baud, with a Keil MDK project and a CMake / Arm GCC build.
Copy it and adapt the board values. `Examples/G431_Blink` is a smaller Keil-only example.

## Toolchains

Keil MDK with Arm Compiler 6.22 (primary), clang (Arm target), Arm GCC 14.2 (CMake build).
Other compilers are not tested.

## Notes

- Fault handlers belong to the application. The CMSIS startup `NMI_Handler` and
  `HardFault_Handler` are weak endless loops. Flash ECC double errors and the clock security system
  raise the NMI: define `NMI_Handler` and diagnose with `FLASH_GetEcc` / `RCC_GetCssFlag`.
- HRTIM support covers time base, compare, output crossbar, dead time, DLL and flags. Master timer,
  burst, external events, fault inputs, capture, chopper and DMA are not handled yet.
- Silicon errata ES0430 Rev 10, ES0431 Rev 10 and ES0523 Rev 7 were reviewed; driver-relevant
  workarounds are implemented in the code and noted in the API headers.
- Some source comments refer to internal design documents that are not published.

## License

MIT (`LICENSE`). `CMSIS/Core` (Arm) and `CMSIS/Device` (ST) are third-party code under
Apache-2.0 (`THIRD_PARTY_NOTICES.md`).
