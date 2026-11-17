# G0_Template - eSPL STM32G0 start project

Minimal application to copy when starting a new STM32G0 project with eSPL. It blinks one LED
(SysTick 1 ms tick, checked GPIO API) and shows the three habits every eSPL application needs:
one include (`stm32g0_spl.h`), the caller duties the driver does not do itself (port clock before
`GPIO_Init`), and a checked `SPL_Status` on every call (failure stops in `App_Fatal`, status kept
in `g_fatal_status`).

Unlike `Examples/G0B1_DriverCompile` (link-coverage project, never flashed) this image is meant to
run on a board.

## Default target

| Item | Value |
|---|---|
| Board | NUCLEO-G0B1RE, user LED LD4 on PA5 |
| Device | STM32G0B1RETx, CMSIS macro `STM32G0B1xx` |
| Clock | HSI16 after reset, `SystemCoreClock` = 16 MHz, no PLL |
| Check level | `SPL_CHECK_LEVEL=0` (FULL) in the project defines |
| Toolchain | Keil MDK, Arm Compiler 6, debugger ST-LINK (`G0_Template.uvprojx`); or CMake + Arm GCC (`CMakeLists.txt`) |
| Output | Keil: `Objects/G0_Template.axf` and `.hex`; CMake: `build/G0_Template.elf`, `.hex`, `.bin` |

Not run on hardware yet: the build is verified (Keil AC6, 0 errors, 0 warnings; Arm GCC compile
for all 12 SKUs in `Tests/run_umbrella.sh`; CMake + Arm GCC link in `Tests/run_cmake.sh`), the
blink itself is not.

## Building with CMake and Arm GCC

```sh
cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=../../cmake/arm-none-eabi-gcc.cmake
cmake --build build
```

`CMakeLists.txt` adds the library with `add_subdirectory(../..)` and links `espl::g0` (drivers)
and `espl::g0_startup` (CMSIS system file + GCC startup). Linker script: `stm32g0b1re.ld`
(512 KB flash, 144 KB SRAM). Another SKU: `-DESPL_G0_DEVICE=STM32G031xx` and
`-DG0_TEMPLATE_LINKER_SCRIPT=<script with that SKU's memory sizes>`. FAST level:
`-DESPL_G0_CHECK_LEVEL=2`. Extra compiler flags go in the `CFLAGS` environment variable:
`-DCMAKE_C_FLAGS=...` replaces the CPU flags of the toolchain file.

## Starting a new project

1. Copy this folder next to the library or anywhere; if it moves, fix the relative paths
   (`..\..\Inc`, `..\..\Src`, `..\..\CMSIS`) in the Keil include path and file groups, or the
   `add_subdirectory` path in `CMakeLists.txt`.
2. Device: change the Keil device (Options > Device), the CMSIS macro in Options > C/C++ > Define
   and the startup file in group `CMSIS_Device` (`CMSIS/Device/Source/startup/arm/startup_<sku>.s`).
3. LED: change `LED_PORT` / `LED_PIN` in `main.c`, or remove the blink.
4. Check level: keep `SPL_CHECK_LEVEL=0` (FULL) during development; 2 (FAST) removes the argument
   checks (Docs/CHECK_LEVEL.md). The library and the application are always built with the same
   value; a mixed build does not link.
5. Group `SPL_Driver` holds every `Src/` file. Unused drivers cost no flash (the linker removes
   unreferenced sections); keep `stm32g0_check.c`, it defines the check-level link guard.
