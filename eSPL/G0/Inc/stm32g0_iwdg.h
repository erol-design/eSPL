/* SPDX-License-Identifier: MIT */
#ifndef STM32G0_IWDG_H
#define STM32G0_IWDG_H
#include <stdint.h>
#include "stm32g0xx.h"
#include "stm32g0_status.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* Independent watchdog (RM0444 Rev 6 section 28).
 * Usage: IWDG_Start, then IWDG_SetPrescalerReload / IWDG_SetWindow (poll IWDG_UpdateStatus),
 * then IWDG_Refresh before every timeout.
 * Single IWDG instance, no static state, no function waits (BUSY is returned instead). Output
 * parameters are written only on OK. Once started the IWDG cannot be stopped except by a
 * reset (RM0444 28.3.2). The hardware-watchdog option bit may start it at power-on without any
 * software action.
 * Caller duties (all APIs): serialize calls; never run IWDG_Refresh (KR = 0xAAAA) in another
 * context between the KR = 0x5555 unlock and the PR / RLR / WINR writes of
 * IWDG_SetPrescalerReload / IWDG_SetWindow (the reload key re-locks them).
 * Contract: Docs/v0.3_WDG_CONTRACT.md. */

/* IWDG_KR key values (RM0444 28.4.1). Not provided by CMSIS. */
#define IWDG_KEY_RELOAD 0x0000AAAAUL
#define IWDG_KEY_ACCESS 0x00005555UL
#define IWDG_KEY_START 0x0000CCCCUL

/* Nominal LSI frequency (RM0444 5.2: "about 32 kHz"). The real tolerance is in
 * the device datasheet; derate timeouts accordingly. */
#define IWDG_LSI_NOMINAL_HZ 32000UL

/* KR=0xCCCC. Irreversible. Also starts the LSI if needed. */
/* Caller: from now on refresh before every timeout (no stop until reset); set the timing
 * afterwards with IWDG_SetPrescalerReload / IWDG_SetWindow (RM0444 28.3.2 order). */
/* Level: - */
SPL_Status IWDG_Start(void);
/* KR=0x5555, then PR (0..7) and RLR (1..0xFFF). BUSY if PVU/RVU/WVU set. */
/* Caller: poll IWDG_UpdateStatus (bounded) until OK before relying on the new timeout or
 * writing PR/RLR/WINR again. */
/* Level: PARAM[F] BUSY[r] */
SPL_Status IWDG_SetPrescalerReload(uint32_t prescaler, uint32_t reload);
/* KR=0x5555, then WINR (1..0xFFF; 0xFFF disables the window). BUSY unless
 * IWDG_SR == 0. Writing WINR also reloads the counter (RM0444 28.3.2). */
/* Caller: poll IWDG_UpdateStatus (bounded) after OK; with a window set, time every
 * IWDG_Refresh inside the window (the counter is not readable here). */
/* Level: PARAM[F] BUSY[r] */
SPL_Status IWDG_SetWindow(uint32_t window);
/* KR=0xAAAA. Outside the window this resets the MCU. */
/* Caller: time the call before the timeout and, with a window, inside it; the driver
 * cannot read the counter, so a mistimed refresh is not detected. */
/* Level: - */
SPL_Status IWDG_Refresh(void);
/* BUSY while any of PVU/RVU/WVU is set, else OK. These bits clear only
 * with the LSI running; LSIRDY = 0 is not reported as STATE because it is
 * also the normal state during LSI start-up right after IWDG_Start. The
 * update takes up to five (LSI) cycles once the LSI runs (RM0444 28.4.4);
 * the caller bounds its retries. */
/* Caller: bound the retries; a stopped LSI keeps BUSY forever and is not reported. */
/* Level: BUSY[r] */
SPL_Status IWDG_UpdateStatus(void);
/* Nominal timeout in microseconds at IWDG_LSI_NOMINAL_HZ (pure calculation). */
/* Caller: derate the nominal result with the datasheet LSI tolerance. */
/* Level: PARAM[F] */
SPL_Status IWDG_CalcTimeoutUs(uint32_t prescaler, uint32_t reload, uint32_t *timeout_us);

#ifdef __cplusplus
}
#endif
#endif
