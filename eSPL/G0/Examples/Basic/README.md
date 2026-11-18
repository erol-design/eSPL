# Basic examples - one small program per driver

Fifteen short programs, each one file, each showing one eSPL G0 driver doing one useful thing
on a NUCLEO-G0B1RE. They share `ex_board.c` (1 ms SysTick tick, LED, user button, console on the
ST-LINK virtual COM port) so that every example file shows only its own driver.

Open a terminal on the board's ST-LINK COM port (115200 8N1) to see the output.

| Example | Driver | What it does | Wiring |
|---|---|---|---|
| `ex_gpio.c` | GPIO | LED follows the user button (debounced polling) | - |
| `ex_exti.c` | EXTI, NVIC | Button interrupt on the falling edge toggles the LED, counts presses | - |
| `ex_usart.c` | USART | Echo on the virtual COM port, line counter, RX error report | - |
| `ex_rcc.c` | RCC, FLASH | HSI16 -> PLL 64 MHz in the required order, prints the clock tree | - |
| `ex_tim_pwm.c` | TIM | TIM2 CH1 PWM on PA5: the LED "breathes" | - |
| `ex_tim_irq.c` | TIM, NVIC | TIM6 update interrupt at 4 Hz, seconds counter | - |
| `ex_adc.c` | ADC | PA0 in millivolts, VDDA from VREFINT and its factory calibration | voltage on PA0 (A0) |
| `ex_dac.c` | DAC | Staircase 0..100 % VDDA on PA4 | meter on PA4 (A2) |
| `ex_dma.c` | DMA | Memory-to-memory copy of 64 words, verified | - |
| `ex_crc.c` | CRC | CRC-32 of "123456789", compared with 0xCBF43926 | - |
| `ex_spi.c` | SPI | SPI1 loop-back of 256 bytes | PA7 (D11) to PA6 (D12) |
| `ex_i2c.c` | I2C | I2C1 bus scan 0x08..0x77 at 100 kHz | device on PB8 (D15) / PB9 (D14) |
| `ex_iwdg.c` | IWDG, RCC | 1 s watchdog; hold the button to let it reset; prints the reset cause | - |
| `ex_rtc.c` | RTC, PWR | Calendar on the LSE crystal, kept across a reset (backup register marker) | - |
| `ex_flash.c` | FLASH | Erase the last page, program two double words, verify | - |

Examples with a check result (DMA, CRC, SPI, FLASH) also show it on the LED: slow blink = OK,
fast blink = failed.

## The three habits every example shows

1. **Caller duties first.** The drivers do not enable clocks, configure pins or pick kernel
   clocks; the example does it before the driver call (the "Caller duties" in each header).
2. **No driver waits.** A call that cannot finish now returns `SPL_BUSY` or `SPL_WOULD_BLOCK`.
   `EX_WAIT(call, timeout_ms)` repeats it with a time bound; `EX_WAIT_FLAG(query, flag,
   timeout_ms)` polls a ready flag. A missing crystal or a stuck bus ends in `SPL_ERR_TIMEOUT`
   instead of a silent hang.
3. **Every status is checked.** `EX_CHECK(call)` stops in `Ex_Fatal` on any status other than
   `SPL_OK`, prints the status number and source line on the console, and keeps both in
   `g_ex_fatal_status` / `g_ex_fatal_line` for the debugger. Errors stay visible; nothing is
   retried silently.

Status numbers: 1 PARAM, 2 STATE, 3 BUSY, 4 WOULD_BLOCK, 5 HW, 6 UNSUPPORTED, 7 TIMEOUT
(`Common/Inc/spl_status.h`).

## Building (CMake + Arm GCC)

```sh
cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=../../cmake/arm-none-eabi-gcc.cmake
cmake --build build
```

Every example becomes `build/ex_<name>.elf`, `.hex` and `.bin`; flash one with STM32CubeProgrammer
or any ST-LINK tool. Linker script: `../G0_Template/stm32g0b1re.ld`. FAST level:
`-DESPL_G0_CHECK_LEVEL=2`. Another SKU: `-DESPL_G0_DEVICE=...` and `-DEX_LINKER_SCRIPT=...`
(the DAC example needs a SKU with a DAC).

Keil MDK: copy `Examples/G0_Template`, replace its `main.c` with one `ex_<name>.c`, and add
`ex_board.c` and the folder of `ex_board.h` to the project.

## Board assumptions

NUCLEO-G0B1RE: LED LD4 on PA5 (high = on), button B1 on PC13 (pressed = low, pull-up on the
board), ST-LINK virtual COM port on USART2 PA2 / PA3, LSE crystal 32.768 kHz fitted. For another
board change the `EX_*` macros in `ex_board.h` and the pins named in each example.

## Status

Verified: every image builds without a warning at check levels 0 and 2 (Arm GCC, `-Wall
-Wextra -Wconversion -Werror`, `Tests/run_cmake.sh`), clang-format clean, 7-bit ASCII. Not run on
a board yet.
