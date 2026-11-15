#ifndef STM32G4_RNG_H
#define STM32G4_RNG_H
#include <stdint.h>
#include "stm32g4xx.h"
#include "stm32g4_status.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* True random number generator (RNG, RM0440 Rev 9 section 26). Single
 * instance, no static state, no waits.
 * Caller duties (RCC module / board layer, not done here):
 *  - RNG kernel clock: 48 MHz from RCC_CCIPR.CLK48SEL (HSI48 or PLL "Q"),
 *    RM0440 26.6.2. With clock error detection on, the RNG clock must be
 *    above HCLK/32 or CECS is always set (RM0440 26.3.6).
 *  - AHB clock: RCC_AHB2ENR.RNGEN.
 * Serialize callers; RNG_TryRead and RNG_SeedErrorRecover must not run
 * concurrently. Contract: Docs/v0.11_RNG_CONTRACT.md. */

/* RNG_SR error bits reported by RNG_GetErrors. */
#define RNG_ERRORS_ALL (RNG_SR_SEIS | RNG_SR_CEIS | RNG_SR_SECS | RNG_SR_CECS)

/* Number of RNG_DR words discarded by RNG_SeedErrorRecover (RM0440 26.3.7). */
#define RNG_RECOVERY_DISCARD_WORDS 12U

    /* enable=1: CED=0 (clock error detection on, reset value); enable=0:
     * CED=1. STATE while RNGEN=1 (RM0440 26.7.1: CED changes need RNGEN=0). */
    /* Seviye: PARAM[S] STATE[r] */
    SPL_Status RNG_ClockErrorDetectionConfig(uint8_t enable);
    /* RNGEN=1 / RNGEN=0. CED and IE kept. */
    /* Seviye: - */
    SPL_Status RNG_Enable(void);
    /* Seviye: - */
    SPL_Status RNG_Disable(void);
    /* IE=1/0 (DRDY, SEIS, CEIS share one interrupt; RM0440 26.4). */
    /* Seviye: PARAM[S] */
    SPL_Status RNG_InterruptConfig(uint8_t enable);
    /* Polling read (RM0440 26.3.5). ERR_HW if SEIS/SECS/CEIS (or CECS with
     * CED=0) is set, or if DR reads 0 although DRDY was 1 (seed error
     * between SR and DR read). WOULD_BLOCK while DRDY=0, also with RNGEN=0:
     * buffered words can still raise DRDY (RM0440 26.3.8, 26.7.2), and no
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
    /* RM0440 26.3.7 seed-error recovery: clear SEIS (CEIS written 1 = no
     * effect), read and discard 12 DR words, then check SR. OK when SEIS
     * and SECS are 0 afterwards (nothing done when both were 0 at entry);
     * ERR_HW when either is set again: retry later, or RNG_Disable /
     * RNG_Enable, or an RCC reset of the RNG. */
    /* Seviye: HW[r] */
    SPL_Status RNG_SeedErrorRecover(void);

#ifdef __cplusplus
}
#endif
#endif
