# eSPL STM32G4

C99 peripheral driver library for the STM32G4 family, built on ST CMSIS Device and Arm CMSIS Core.
It follows the classic Standard Peripheral Library naming style (`GPIO_Init`, `USART_TryRead`,
`FDCAN_TryWrite`) but is an independent implementation, not source-compatible with ST SPL.

- No RTOS, no dynamic memory, non-blocking APIs
- Every status-returning API returns `SPL_Status`
- File prefix `stm32g4_`

Version: **v0.18.0-dev** (pre-release). See [RELEASE_NOTES.md](RELEASE_NOTES.md).

## Modules

RCC, GPIO, USART/LPUART, TIM, DMA, DMAMUX, SPI, ADC, I2C, FDCAN, EXTI, PWR, FLASH, IWDG, WWDG,
RTC/TAMP, DAC, COMP, OPAMP, VREFBUF, CRC, RNG, LPTIM, QUADSPI, SAI, FMC, NVIC/SysTick, DBGMCU,
SYSCFG, CRS, CORDIC, FMAC, HRTIM (G474/G484/G414; `SPL_ERR_UNSUPPORTED` on other devices).

Not included: USB, UCPD.

## Devices

Primary target STM32G431 (bench: NUCLEO-G431RB). Compile-checked: G411xB, G414, G431, G441, G471,
G474, G491, G4A1. A peripheral instance or feature that the selected device does not have returns
`SPL_ERR_PARAM` or `SPL_ERR_UNSUPPORTED`.

## Adding eSPL to a project

1. Add the `Src/*.c` files you need. `Src/stm32g4_check.c` must be compiled and linked exactly once.
2. Add the CMSIS startup file for your device from `CMSIS/Device/Source/startup/{arm,gcc,iar}`
   and `CMSIS/Device/Source/system_stm32g4xx.c`.
3. Include paths:
   - `eSPL/G4/Inc`
   - `eSPL/G4/Private`
   - `eSPL/Common/Inc`
   - `eSPL/G4/CMSIS/Device/Include`
   - `eSPL/G4/CMSIS/Core/Include`
4. Define the device, for example `STM32G431xx`.
5. Optionally select the validation level for the whole build (default FULL):

```c
-DSPL_CHECK_LEVEL=0   /* SPL_CHECK_FULL */
-DSPL_CHECK_LEVEL=1   /* SPL_CHECK_MIX  */
-DSPL_CHECK_LEVEL=2   /* SPL_CHECK_FAST */
```

All translation units must use the same level. Each public API header documents which checks
remain at which level.

The quickest start is **`Examples/G431_Template`** (NUCLEO-G431RB): 170 MHz clock setup, LED,
ST-LINK virtual COM port at 115200 baud, with a Keil MDK project and a CMake / Arm GCC build.
Copy it and adapt the board values. `Examples/G431_Blink` is a smaller Keil-only example.

## Toolchains

Keil MDK with Arm Compiler 6.22 (primary), clang (Arm target), Arm GCC 14.2 (template build).
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
