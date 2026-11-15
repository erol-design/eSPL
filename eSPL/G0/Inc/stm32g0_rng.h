#ifndef STM32G0_RNG_H
#define STM32G0_RNG_H
#include <stdint.h>
#include "stm32g0xx.h"
#include "stm32g0_status.h"
#include "stm32g0_sku.h"

#ifdef __cplusplus
extern "C"
{
#endif

#if SPL_G0_HAS_RNG

/* True random number generator (RNG, RM0444 Rev 6 section 19). Single
 * instance, no static state, no waits.
 * G0: only on STM32G041 / G061 / G081 / G0C1 (SPL_G0_HAS_RNG); elsewhere this API is not declared.
 * Caller duties (RCC module / board layer, not done here):
 *  - RNG kernel clock: RCC_SetKernelClock(RCC_KERNEL_RNG, HSI16 / SYSCLK / PLLQ) and
 *    RCC_SetRngDivider (RM0444 5.4.21; RNGSEL reset value 00 = no clock). Validation
 *    conditions: 48 MHz with CED = 0 (RM0444 19.6.2). With clock error detection on, the
 *    RNG clock must be above HCLK/32 or CECS is always set (RM0444 19.3.6).
 *  - AHB clock: RCC_EnablePeriphClock(RCC_PERIPH_RNG) (RCC_AHBENR.RNGEN).
 *  - Voltage range 1 for certified entropy: ES0488 2.6.1 / ES0546 2.8.1 / ES0412 2.8.1 /
 *    ES0549 2.8.1 "RNG entropy not guaranteed with Range 2" (documentation item; the driver
 *    does not read PWR).
 * Serialize callers; RNG_TryRead and RNG_SeedErrorRecover must not run
 * concurrently. Contract: Docs/v0.11_RNG_CONTRACT.md. */

/* RNG_SR error bits reported by RNG_GetErrors. */
#define RNG_ERRORS_ALL (RNG_SR_SEIS | RNG_SR_CEIS | RNG_SR_SECS | RNG_SR_CECS)

/* Number of RNG_DR words discarded by RNG_SeedErrorRecover (RM0444 19.3.7). */
#define RNG_RECOVERY_DISCARD_WORDS 12U

/* enable=1: CED=0 (clock error detection on, reset value); enable=0:
 * CED=1. STATE while RNGEN=1 (RM0444 19.7.1: CED changes need RNGEN=0). */
/* Seviye: PARAM[n] STATE[S] */
SPL_Status RNG_ClockErrorDetectionConfig(uint8_t enable);
/* RNGEN=1 / RNGEN=0. CED and IE kept. */
/* Seviye: - */
SPL_Status RNG_Enable(void);
/* Seviye: - */
SPL_Status RNG_Disable(void);
/* IE=1/0 (DRDY, SEIS, CEIS share one interrupt; RM0444 19.4). */
/* Seviye: PARAM[n] */
SPL_Status RNG_InterruptConfig(uint8_t enable);
/* Polling read (RM0444 19.3.5). ERR_HW if SEIS/SECS/CEIS (or CECS with
 * CED=0) is set, or if DR reads 0 although DRDY was 1 (seed error
 * between SR and DR read). WOULD_BLOCK while DRDY=0, also with RNGEN=0:
 * buffered words can still raise DRDY (RM0444 19.3.8), and no
 * register shows when none are left. With RNGEN=0 bound the poll and
 * call RNG_Enable when it expires. *value written on OK only. */
/* Seviye: PARAM[S] HW[r] WOULD_BLOCK[r] */
SPL_Status RNG_TryRead(uint32_t *value);
/* *errors = RNG_SR & RNG_ERRORS_ALL. No side effects. */
/* Seviye: PARAM[S] */
SPL_Status RNG_GetErrors(uint32_t *errors);
/* Clears CEIS (rc_w0) after the caller fixed the RNG clock; SEIS is not
 * touched (written 1 = no effect). CECS clears itself in hardware. */
/* Seviye: - */
SPL_Status RNG_ClearClockError(void);
/* RM0444 19.3.7 seed-error recovery: clear SEIS (CEIS written 1 = no
 * effect), read and discard 12 DR words, then check SR. OK when SEIS
 * and SECS are 0 afterwards (nothing done when both were 0 at entry);
 * ERR_HW when either is set again: retry later, or RNG_Disable /
 * RNG_Enable, or an RCC reset of the RNG. */
/* Seviye: HW[r] */
SPL_Status RNG_SeedErrorRecover(void);

#endif /* SPL_G0_HAS_RNG */

#ifdef __cplusplus
}
#endif
#endif
