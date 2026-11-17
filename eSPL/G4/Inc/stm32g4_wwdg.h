#ifndef STM32G4_WWDG_H
#define STM32G4_WWDG_H
#include <stdint.h>
#include "stm32g4xx.h"
#include "stm32g4_status.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* System window watchdog (RM0440 Rev 9 section 36). Single WWDG instance,
 * no static state. The caller enables the WWDG APB clock first. Activation
 * (WDGA) is irreversible until reset; EWI cannot be cleared by software.
 * Caller duty (CCL-2 WDG-02a, WDG-02b): call WWDG_Refresh only from thread context, never
 * from an interrupt handler (EWI included) or a callback run by one, and only in the main
 * (task) loop or inside a long multi-page/multi-block processing loop, never in delay,
 * driver or helper functions. The library itself never refreshes.
 * Caller duty (CCL-2 WDG-10): with several tasks or a cyclic schedule, refresh only after
 * every critical task has reported progress in the last refresh interval, or only after a
 * common cycle that called all critical tasks unconditionally has completed in that round.
 * Caller duty (CCL-2 WDG-08): choose a window below the reload counter and time each refresh
 * after the counter drops below the window; WWDG_Refresh rejects an early refresh
 * (ERR_STATE) but the library does not check the window/counter relation.
 * Caller duty (CCL-2 WDG-11): every operation without refresh after WWDG_Enable must be
 * shorter than the timeout computed for the actual PCLK1; otherwise refresh inside it or
 * enable the WWDG after it. No API of this library waits. */

/* Valid counter/window values: T6 must be 1 (RM0440 36.3.3). */
#define WWDG_COUNTER_MIN 0x40U
#define WWDG_COUNTER_MAX 0x7FU

    /* CFR: timebase WDGTB 0..7, window 0x40..0x7F, ewi 0/1. ERR_STATE if EWI is
     * already set and ewi=0 (EWI is set-only). */
    /* Level: PARAM[F] STATE[r] */
    SPL_Status WWDG_Config(uint32_t timebase, uint32_t window, uint8_t ewi);
    /* CR = WDGA | counter (0x40..0x7F). ERR_STATE if already active. */
    /* Level: PARAM[F] STATE[r] */
    SPL_Status WWDG_Enable(uint32_t counter);
    /* Reload counter (0x40..0x7F). ERR_STATE if not active or if the current
     * counter is above the window (a write now would reset the MCU). */
    /* Level: PARAM[F] STATE[r] */
    SPL_Status WWDG_Refresh(uint32_t counter);
    /* Current 7-bit counter T[6:0]. */
    /* Level: PARAM[F] */
    SPL_Status WWDG_GetCounter(uint32_t *counter);
    /* Early wakeup interrupt flag (SR.EWIF). */
    /* Level: PARAM[F] */
    SPL_Status WWDG_GetEarlyWakeupFlag(uint8_t *flag);
    /* Clear EWIF (rc_w0: writes 0). Caller order: clear AFTER WWDG_Enable, then enable the
     * NVIC line. The counter is free-running also while WDGA = 0 and sets EWIF whenever it
     * passes 0x40 (RM0440 36.3.3, 36.5.3), so a clear done before the enable can be undone
     * before it (seen on NUCLEO-G431RB, G6 T34). */
    /* Level: - */
    SPL_Status WWDG_ClearEarlyWakeupFlag(void);
    /* Timeout in microseconds: 4096 * 2^timebase * (T[5:0] + 1) / pclk_hz. */
    /* Level: PARAM[F,r] */
    SPL_Status WWDG_CalcTimeoutUs(uint32_t pclk_hz, uint32_t timebase, uint32_t counter,
                                  uint32_t *timeout_us);

#ifdef __cplusplus
}
#endif
#endif
