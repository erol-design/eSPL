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
 * (WDGA) is irreversible until reset; EWI cannot be cleared by software. */

/* Valid counter/window values: T6 must be 1 (RM0440 36.3.3). */
#define WWDG_COUNTER_MIN 0x40U
#define WWDG_COUNTER_MAX 0x7FU

    /* CFR: timebase WDGTB 0..7, window 0x40..0x7F, ewi 0/1. ERR_STATE if EWI is
     * already set and ewi=0 (EWI is set-only). */
    /* Seviye: PARAM[S] STATE[r] */
    SPL_Status WWDG_Config(uint32_t timebase, uint32_t window, uint8_t ewi);
    /* CR = WDGA | counter (0x40..0x7F). ERR_STATE if already active. */
    /* Seviye: PARAM[S] STATE[r] */
    SPL_Status WWDG_Enable(uint32_t counter);
    /* Reload counter (0x40..0x7F). ERR_STATE if not active or if the current
     * counter is above the window (a write now would reset the MCU). */
    /* Seviye: PARAM[S] STATE[r] */
    SPL_Status WWDG_Refresh(uint32_t counter);
    /* Current 7-bit counter T[6:0]. */
    /* Seviye: PARAM[S] */
    SPL_Status WWDG_GetCounter(uint32_t *counter);
    /* Early wakeup interrupt flag (SR.EWIF). */
    /* Seviye: PARAM[S] */
    SPL_Status WWDG_GetEarlyWakeupFlag(uint8_t *flag);
    /* Clear EWIF (rc_w0: writes 0). Caller order: clear AFTER WWDG_Enable, then enable the
     * NVIC line. The counter is free-running also while WDGA = 0 and sets EWIF whenever it
     * passes 0x40 (RM0440 36.3.3, 36.5.3), so a clear done before the enable can be undone
     * before it (seen on NUCLEO-G431RB, G6 T34). */
    /* Seviye: - */
    SPL_Status WWDG_ClearEarlyWakeupFlag(void);
    /* Timeout in microseconds: 4096 * 2^timebase * (T[5:0] + 1) / pclk_hz. */
    /* Seviye: PARAM[S,r] */
    SPL_Status WWDG_CalcTimeoutUs(uint32_t pclk_hz, uint32_t timebase, uint32_t counter,
                                  uint32_t *timeout_us);

#ifdef __cplusplus
}
#endif
#endif
