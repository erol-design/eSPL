/* SPDX-License-Identifier: MIT */
/* eSPL STM32G0 umbrella header: one include for the whole driver set. Declares no API.
 *
 * Usage: define exactly one CMSIS device macro (STM32G030xx .. STM32G0C1xx) and, optionally,
 * SPL_CHECK_LEVEL (0 = SPL_CHECK_FULL, default; 2 = SPL_CHECK_FAST) project-wide, add Inc/,
 * CMSIS/Core/Include and CMSIS/Device/Include to the include path, then
 *     #include "stm32g0_spl.h"
 * The library sources (every file in Src/) must be built with the same SPL_CHECK_LEVEL as the
 * application; a mixed build does not link (Docs/CHECK_LEVEL.md 3.2).
 *
 * Every module header is included on every SKU and compiles there. A module whose peripheral the
 * selected SKU does not have (AES, CEC, UCPD, FDCAN, DAC, ...: SPL_G0_HAS_* in stm32g0_sku.h)
 * declares no API on that SKU, so a call to it is a compile error, not a run-time failure
 * (Docs/v0.8_SKU_PROFILES.md). Including single module headers instead stays supported and
 * builds faster; this header only collects them.
 *
 * Caller: none beyond the module headers' own file-header duties (clock enable, pins, NVIC).
 * Template project: Examples/G0_Template. */
#ifndef STM32G0_SPL_H
#define STM32G0_SPL_H

#include "stm32g0xx.h"
#include "stm32g0_status.h"
#include "stm32g0_sku.h"

/* System, clock and power */
#include "stm32g0_rcc.h"
#include "stm32g0_pwr.h"
#include "stm32g0_flash.h"
#include "stm32g0_syscfg.h"
#include "stm32g0_dbgmcu.h"
#include "stm32g0_nvic.h"
#include "stm32g0_exti.h"
#include "stm32g0_mpu.h"
#include "stm32g0_crs.h"

/* GPIO and DMA */
#include "stm32g0_gpio.h"
#include "stm32g0_dma.h"
#include "stm32g0_dmamux.h"

/* Communication */
#include "stm32g0_usart.h"
#include "stm32g0_spi.h"
#include "stm32g0_i2c.h"
#include "stm32g0_fdcan.h"
#include "stm32g0_fdcan_layout.h"
#include "stm32g0_cec.h"
#include "stm32g0_ucpd.h"

/* Timers, watchdogs and RTC */
#include "stm32g0_tim.h"
#include "stm32g0_lptim.h"
#include "stm32g0_rtc.h"
#include "stm32g0_iwdg.h"
#include "stm32g0_wwdg.h"

/* Analog */
#include "stm32g0_adc.h"
#include "stm32g0_dac.h"
#include "stm32g0_comp.h"
#include "stm32g0_vrefbuf.h"

/* Data integrity and security */
#include "stm32g0_crc.h"
#include "stm32g0_rng.h"
#include "stm32g0_aes.h"

/* Hot-path accessors and the compile-time argument check (Docs/FAST_PATH.md) */
#include "stm32g0_diag.h"
#include "stm32g0_fast.h"

#endif /* STM32G0_SPL_H */
