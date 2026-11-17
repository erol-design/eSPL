# G431_Template

Starting point for an eSPL STM32G4 application on **NUCLEO-G431RB**.

- SYSCLK = HCLK = PCLK1 = PCLK2 = **170 MHz** from the 24 MHz HSE crystal (PLL, Range 1 boost,
  flash wait states and caches)
- SysTick 1 ms interrupt
- LD2 (PA5) blinks at 1 Hz
- LPUART1 on PA2/PA3 (ST-LINK virtual COM port), **115200 8N1**: prints a banner, echoes input

Any unexpected `SPL_Status` stops the program in `error_trap()` and LD2 blinks fast.

Expected terminal output after reset:

```
eSPL G431 template
SYSCLK = 170000000 Hz
Type something, it is echoed back.
```

## Build

**Keil MDK (Arm Compiler 6):** open `G431_Template.uvprojx`, Rebuild, Download.
Requires the `Keil.STM32G4xx_DFP` pack; CMSIS Core comes from `../../CMSIS/Core`.

**Arm GCC + CMake:**

```sh
cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=../../cmake/arm-none-eabi-gcc.cmake
cmake --build build
```

Output: `build/G431_Template.elf`, `.hex`, `.bin`. Flash with STM32CubeProgrammer or any
SWD tool. Validation level: `-DESPL_G4_CHECK_LEVEL=0|2` (FULL / FAST). The template links the
library target `espl::g4` (`../../CMakeLists.txt`).

## Files

| File | Purpose |
|---|---|
| `main.c` | Clock, LED, VCP and main loop |
| `G431_Template.uvprojx` | Keil MDK project |
| `CMakeLists.txt` | GCC build (toolchain file `../../cmake/arm-none-eabi-gcc.cmake`) |
| `STM32G431xB_FLASH.ld` | GNU linker script (128 KB flash, 32 KB SRAM) |

## Using it for your own project

1. Copy the folder and keep the relative paths to `eSPL/G4` and `eSPL/Common`, or adjust them.
2. Keil: add the eSPL modules you use (`Src/stm32g4_<module>.c`); CMake builds all of `Src/`.
   `stm32g4_check.c` must stay in the build.
3. For another board, change `BOARD_HSE_HZ`, the PLL values, LED and VCP pins in `main.c`.
4. For another device, change the device define (`STM32G431xx`), the startup file and the linker
   script memory sizes.
