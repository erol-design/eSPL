#ifndef STM32G4_IWDG_H
#define STM32G4_IWDG_H
#include <stdint.h>
#include "stm32g4xx.h"
#include "stm32g4_status.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* Independent watchdog (RM0440 Rev 9 section 35). Single IWDG instance,
 * no static state. Once started the IWDG cannot be stopped except by a
 * reset (RM0440 35.3.2). The hardware-watchdog option bit may start it at
 * power-on without any software action.
 * Caller duty (CCL-2 WDG-02a, WDG-02b): call IWDG_Refresh only from thread context, never
 * from an interrupt handler or a callback run by one, and only in the main (task) loop or
 * inside a long multi-page/multi-block processing loop, never in delay, driver or helper
 * functions. The library itself never refreshes.
 * Caller duty (CCL-2 WDG-10): with several tasks or a cyclic schedule, refresh only after
 * every critical task has reported progress in the last refresh interval, or only after a
 * common cycle that called all critical tasks unconditionally has completed in that round.
 * Caller duty (CCL-2 WDG-08): keep WINR below RLR (or WINR = 0xFFF, window off) and refresh
 * only after the counter has dropped below WINR; the library cannot read the IWDG counter
 * and does not check the WINR/RLR relation (separate calls, no static state).
 * Caller duty (CCL-2 WDG-11): every operation without refresh after IWDG_Start (oscillator
 * and PLL polling, flash erase, start-up waits) must be shorter than the timeout computed at
 * the upper LSI tolerance from the datasheet, not at IWDG_LSI_NOMINAL_HZ; otherwise refresh
 * inside it or start the IWDG after it. No API of this library waits. */

/* IWDG_KR key values (RM0440 35.4.1). Not provided by CMSIS. */
#define IWDG_KEY_RELOAD 0x0000AAAAUL
#define IWDG_KEY_ACCESS 0x00005555UL
#define IWDG_KEY_START 0x0000CCCCUL

/* Nominal LSI frequency (RM0440 7.2.6: "32 kHz"). The real tolerance is in
 * the device datasheet; derate timeouts accordingly. */
#define IWDG_LSI_NOMINAL_HZ 32000UL

    /* KR=0xCCCC. Irreversible. Also starts the LSI if needed. */
    /* Level: - */
    SPL_Status IWDG_Start(void);
    /* KR=0x5555, then PR (0..7) and RLR (1..0xFFF). BUSY if PVU/RVU/WVU set. */
    /* Level: PARAM[F] BUSY[r] */
    SPL_Status IWDG_SetPrescalerReload(uint32_t prescaler, uint32_t reload);
    /* KR=0x5555, then WINR (1..0xFFF; 0xFFF disables the window). BUSY unless
     * IWDG_SR == 0. Writing WINR also reloads the counter (RM0440 35.3.2). */
    /* Level: PARAM[F] BUSY[r] */
    SPL_Status IWDG_SetWindow(uint32_t window);
    /* KR=0xAAAA. Outside the window this resets the MCU. */
    /* Level: - */
    SPL_Status IWDG_Refresh(void);
    /* BUSY while any of PVU/RVU/WVU is set, else OK. These bits clear only
     * with the LSI running; LSIRDY = 0 is not reported as STATE because it is
     * also the normal state during LSI start-up right after IWDG_Start. The
     * update takes up to five (LSI) cycles once the LSI runs (RM0440 35.4.4);
     * the caller bounds its retries. */
    /* Level: BUSY[r] */
    SPL_Status IWDG_UpdateStatus(void);
    /* Nominal timeout in microseconds at IWDG_LSI_NOMINAL_HZ (pure calculation). */
    /* Level: PARAM[F] */
    SPL_Status IWDG_CalcTimeoutUs(uint32_t prescaler, uint32_t reload, uint32_t *timeout_us);

#ifdef __cplusplus
}
#endif
#endif
