/* SPDX-License-Identifier: MIT */
#ifndef STM32G4_WWDG_H
#define STM32G4_WWDG_H
#include <stdint.h>
#include "stm32g4xx.h"
#include "stm32g4_status.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* System window watchdog driver (WWDG, RM0440 Rev 9 section 36).
 * Scope: the single WWDG instance; timeout helper without register access.
 * Usage: WWDG_CalcTimeoutUs, WWDG_Config, WWDG_Enable, then (EWI used) WWDG_ClearEarlyWakeupFlag
 * and the NVIC enable; WWDG_Refresh inside the window from the main loop.
 * Common rules: no function waits; no static state; no clock is enabled here; the library
 * itself never refreshes. Activation (WDGA) is irreversible until reset; EWI cannot be cleared
 * by software.
 * Caller duties (all APIs):
 *  - Clock: WWDG APB clock (RCC) enabled first.
 *  - Refresh policy: see WWDG_Enable (CCL-2 WDG-11) and WWDG_Refresh (CCL-2 WDG-02a, WDG-02b,
 *    WDG-08, WDG-10).
 * Contract: Docs/v0.10_WDG_CONTRACT.md. */

/* Valid counter/window values: T6 must be 1 (RM0440 36.3.3). */
#define WWDG_COUNTER_MIN 0x40U
#define WWDG_COUNTER_MAX 0x7FU

    /* CFR: timebase WDGTB 0..7, window 0x40..0x7F, ewi 0/1. ERR_STATE if EWI is
     * already set and ewi=0 (EWI is set-only). */
    /* Caller: pick timebase and window from WWDG_CalcTimeoutUs for the actual PCLK1 (the
     * window/counter relation is not checked, see WWDG_Refresh). */
    /* Level: PARAM[F] STATE[r] */
    SPL_Status WWDG_Config(uint32_t timebase, uint32_t window, uint8_t ewi);
    /* Irreversible: CR = WDGA | counter (0x40..0x7F); the WWDG runs until reset. ERR_STATE if
     * already active. */
    /* Caller duty (CCL-2 WDG-11): every operation without refresh after WWDG_Enable must be
     * shorter than the timeout computed for the actual PCLK1; otherwise refresh inside it or
     * enable the WWDG after it. */
    /* Level: PARAM[F] STATE[r] */
    SPL_Status WWDG_Enable(uint32_t counter);
    /* Reload counter (0x40..0x7F). ERR_STATE if not active or if the current
     * counter is above the window (a write now would reset the MCU). */
    /* Caller duty (CCL-2 WDG-02a, WDG-02b): call WWDG_Refresh only from thread context, never
     * from an interrupt handler (EWI included) or a callback run by one, and only in the main
     * (task) loop or inside a long multi-page/multi-block processing loop, never in delay,
     * driver or helper functions.
     * Caller duty (CCL-2 WDG-10): with several tasks or a cyclic schedule, refresh only after
     * every critical task has reported progress in the last refresh interval, or only after a
     * common cycle that called all critical tasks unconditionally has completed in that round.
     * Caller duty (CCL-2 WDG-08): choose a window below the reload counter and time each
     * refresh after the counter drops below the window; an early refresh is rejected
     * (ERR_STATE) but the window/counter relation is not checked. */
    /* Level: PARAM[F] STATE[r] */
    SPL_Status WWDG_Refresh(uint32_t counter);
    /* Current 7-bit counter T[6:0]. */
    /* Caller: none beyond the file-header duties. */
    /* Level: PARAM[F] */
    SPL_Status WWDG_GetCounter(uint32_t *counter);
    /* Early wakeup interrupt flag (SR.EWIF). */
    /* Caller: none beyond the file-header duties. */
    /* Level: PARAM[F] */
    SPL_Status WWDG_GetEarlyWakeupFlag(uint8_t *flag);
    /* Clear EWIF (rc_w0: writes 0). The counter is free-running also while WDGA = 0 and sets
     * EWIF whenever it passes 0x40 (RM0440 36.3.3, 36.5.3), so a clear done before the enable
     * can be undone before it (seen on NUCLEO-G431RB, G6 T34). */
    /* Caller: clear AFTER WWDG_Enable, then enable the NVIC line of the WWDG vector (handler
     * provided by the caller). */
    /* Level: - */
    SPL_Status WWDG_ClearEarlyWakeupFlag(void);
    /* Timeout in microseconds: 4096 * 2^timebase * (T[5:0] + 1) / pclk_hz. */
    /* Caller: pass the actual PCLK1 frequency (recompute after clock changes). */
    /* Level: PARAM[F,r] */
    SPL_Status WWDG_CalcTimeoutUs(uint32_t pclk_hz, uint32_t timebase, uint32_t counter,
                                  uint32_t *timeout_us);

#ifdef __cplusplus
}
#endif
#endif
