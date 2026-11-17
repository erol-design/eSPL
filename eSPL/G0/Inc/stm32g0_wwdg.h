/* SPDX-License-Identifier: MIT */
#ifndef STM32G0_WWDG_H
#define STM32G0_WWDG_H
#include <stdint.h>
#include "stm32g0xx.h"
#include "stm32g0_status.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* System window watchdog (RM0444 Rev 6 section 29).
 * Usage: WWDG_Config, then WWDG_Enable, then WWDG_Refresh inside the window.
 * Single WWDG instance, no static state, no function waits. Output parameters are written only
 * on OK. Activation (WDGA) is irreversible until reset; EWI cannot be cleared by software.
 * Caller duties (all APIs): WWDG APB clock enabled (RCC) before the first call; serialize calls
 * (ISR included).
 * Contract: Docs/v0.3_WDG_CONTRACT.md. */

/* Valid counter/window values: T6 must be 1 (RM0444 29.3.3). */
#define WWDG_COUNTER_MIN 0x40U
#define WWDG_COUNTER_MAX 0x7FU

/* CFR: timebase WDGTB 0..7, window 0x40..0x7F, ewi 0/1. ERR_STATE if EWI is
 * already set and ewi=0 (EWI is set-only). */
/* Caller: choose timebase for the actual PCLK; call before WWDG_Enable. ewi = 1: enable
 * WWDG_IRQn in the NVIC with a handler that calls WWDG_ClearEarlyWakeupFlag. */
/* Level: PARAM[F] STATE[F] */
SPL_Status WWDG_Config(uint32_t timebase, uint32_t window, uint8_t ewi);
/* CR = WDGA | counter (0x40..0x7F). ERR_STATE if already active. */
/* Caller: irreversible until reset; WWDG_Config first, then refresh inside the window
 * before T6 clears. */
/* Level: PARAM[F] STATE[F] */
SPL_Status WWDG_Enable(uint32_t counter);
/* Reload counter (0x40..0x7F). ERR_STATE if not active or if the current
 * counter is above the window (a write now would reset the MCU). */
/* Caller: refresh from one context only; a refresh from another context between the
 * window check and the write is not detected (see source). */
/* Level: PARAM[F] STATE[F] */
SPL_Status WWDG_Refresh(uint32_t counter);
/* Current 7-bit counter T[6:0]. */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status WWDG_GetCounter(uint32_t *counter);
/* Early wakeup interrupt flag (SR.EWIF). */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status WWDG_GetEarlyWakeupFlag(uint8_t *flag);
/* Clear EWIF (rc_w0: writes 0). */
/* Caller: call in the EWI handler before it returns. */
/* Level: - */
SPL_Status WWDG_ClearEarlyWakeupFlag(void);
/* Timeout in microseconds: 4096 * 2^timebase * (T[5:0] + 1) / pclk_hz. */
/* Caller: pass the actual PCLK frequency (RCC_GetClocks). */
/* Level: PARAM[F,r] */
SPL_Status WWDG_CalcTimeoutUs(uint32_t pclk_hz, uint32_t timebase, uint32_t counter,
                              uint32_t *timeout_us);

#ifdef __cplusplus
}
#endif
#endif
