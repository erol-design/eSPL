/* SPDX-License-Identifier: MIT */
/* eSPL STM32G4 umbrella header: one include for the whole driver set. Declares no API.
 *
 * Usage: define exactly one CMSIS device macro (STM32G411xB .. STM32G4A1xx) and, optionally,
 * SPL_CHECK_LEVEL (0 = SPL_CHECK_FULL, default; 2 = SPL_CHECK_FAST) project-wide, add Inc/,
 * CMSIS/Core/Include and CMSIS/Device/Include to the include path, then
 *     #include "stm32g4_spl.h"
 * The library sources (every file in Src/) must be built with the same SPL_CHECK_LEVEL as the
 * application; a mixed build does not link (../Common/Inc/spl_check.h).
 *
 * Every module header is included on every SKU and compiles there. A peripheral the selected SKU
 * does not have is handled inside its module: HRTIM APIs return SPL_ERR_UNSUPPORTED outside
 * G474/G484/G414, FMC / QUADSPI / instance-specific APIs follow the CMSIS device macros of the
 * SKU (see the module header). Including single module headers instead stays supported and
 * builds faster; this header only collects them.
 *
 * Caller: none beyond the module headers' own file-header duties (clock enable, pins, NVIC).
 * Template project: Examples/G431_Template. */
#ifndef STM32G4_SPL_H
#define STM32G4_SPL_H

#include "stm32g4xx.h"
#include "stm32g4_status.h"

/* System, clock and power */
#include "stm32g4_rcc.h"
#include "stm32g4_pwr.h"
#include "stm32g4_flash.h"
#include "stm32g4_syscfg.h"
#include "stm32g4_dbgmcu.h"
#include "stm32g4_nvic.h"
#include "stm32g4_exti.h"
#include "stm32g4_crs.h"

/* GPIO and DMA */
#include "stm32g4_gpio.h"
#include "stm32g4_dma.h"
#include "stm32g4_dmamux.h"

/* Communication */
#include "stm32g4_usart.h"
#include "stm32g4_spi.h"
#include "stm32g4_i2c.h"
#include "stm32g4_fdcan.h"
#include "stm32g4_fdcan_layout.h"
#include "stm32g4_sai.h"

/* External memory */
#include "stm32g4_qspi.h"
#include "stm32g4_fmc.h"

/* Timers, watchdogs and RTC */
#include "stm32g4_tim.h"
#include "stm32g4_hrtim.h"
#include "stm32g4_lptim.h"
#include "stm32g4_rtc.h"
#include "stm32g4_iwdg.h"
#include "stm32g4_wwdg.h"

/* Analog */
#include "stm32g4_adc.h"
#include "stm32g4_dac.h"
#include "stm32g4_comp.h"
#include "stm32g4_opamp.h"
#include "stm32g4_vrefbuf.h"

/* Math accelerators */
#include "stm32g4_cordic.h"
#include "stm32g4_fmac.h"

/* Data integrity */
#include "stm32g4_crc.h"
#include "stm32g4_rng.h"

/* Hot-path accessors and the compile-time argument check (Docs/FAST_PATH.md) */
#include "stm32g4_diag.h"
#include "stm32g4_fast.h"

#endif /* STM32G4_SPL_H */
