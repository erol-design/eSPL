/* RNG driver implementation (RM0444 Rev 6 section 19); public contract in stm32g0_rng.h.
 * Compiled only when SPL_G0_HAS_RNG. Ported from STM32G4 SPL v0.13 (Src/stm32g4_rng.c) and
 * reviewed against RM0444 Rev 6 section 19: same IP, registers and procedures; references changed
 * (v0.9). */
/* Library unit: this unit also emits the spl_check.h mismatch-guard reference; the only unit
 * that does not is stm32g0_check.c (SPL_CHECK_LEVEL_DEFINER). BULGU-02, CHECK_LEVEL.md 3.2. */
#define SPL_LIBRARY_BUILD 1
#include "stm32g0_rng.h"

#if SPL_G0_HAS_RNG

/* Check level (A4 wave 5, Docs/CHECK_LEVEL.md 5.4 RNG table): SPL_REQUIRE = checked at FULL only
 * (not compiled at FAST). Error flags, no DRDY and DR = 0 result: at every level. */

#define RNG_CR_KNOWN_BITS (RNG_CR_CED | RNG_CR_IE | RNG_CR_RNGEN)

static uint32_t Rng_ActiveErrors(uint32_t sr, uint32_t cr)
{
    uint32_t errors = sr & (RNG_SR_SEIS | RNG_SR_SECS | RNG_SR_CEIS);

    /* RM0444 19.7.2: CECS is valid only when CED=0. */
    if ((cr & RNG_CR_CED) == 0U)
    {
        errors |= sr & RNG_SR_CECS;
    }
    return errors;
}

SPL_Status RNG_ClockErrorDetectionConfig(uint8_t enable)
{
    uint32_t cr;

    SPL_REQUIRE(enable <= 1U, SPL_ERR_PARAM);
    cr = RNG->CR;
    /* RM0444 19.7.1: CED cannot change while the RNG is enabled. */
    SPL_REQUIRE((cr & RNG_CR_RNGEN) == 0U, SPL_ERR_STATE);

    cr &= RNG_CR_KNOWN_BITS & ~RNG_CR_CED;
    if (enable == 0U)
    {
        cr |= RNG_CR_CED;
    }
    RNG->CR = cr;
    return SPL_OK;
}

SPL_Status RNG_Enable(void)
{
    RNG->CR = (RNG->CR & RNG_CR_KNOWN_BITS) | RNG_CR_RNGEN;
    return SPL_OK;
}

SPL_Status RNG_Disable(void)
{
    RNG->CR = RNG->CR & (RNG_CR_KNOWN_BITS & ~RNG_CR_RNGEN);
    return SPL_OK;
}

SPL_Status RNG_InterruptConfig(uint8_t enable)
{
    uint32_t cr;

    SPL_REQUIRE(enable <= 1U, SPL_ERR_PARAM);
    cr = RNG->CR & (RNG_CR_KNOWN_BITS & ~RNG_CR_IE);
    if (enable != 0U)
    {
        cr |= RNG_CR_IE;
    }
    RNG->CR = cr;
    return SPL_OK;
}

SPL_Status RNG_TryRead(uint32_t *value)
{
    uint32_t sr;
    uint32_t cr;
    uint32_t data;

    SPL_REQUIRE(value != (uint32_t *)0, SPL_ERR_PARAM);
    cr = RNG->CR;
    sr = RNG->SR;
    /* RM0444 19.3.5: DR must not be read when SEIS or CEIS is set. */
    if (Rng_ActiveErrors(sr, cr) != 0U)
    {
        return SPL_ERR_HW;
    }
    /* DRDY=0: WOULD_BLOCK also with RNGEN=0. "The DRDY bit can rise when the
     * peripheral is disabled" (RM0444 19.3.8): post-processing and the output
     * buffer stay operational (19.3.3). No register shows whether buffered
     * bits remain, so "no data will come" (STATE) cannot be proven. The
     * caller bounds the poll and calls RNG_Enable if it expires. */
    if ((sr & RNG_SR_DRDY) == 0U)
    {
        return SPL_WOULD_BLOCK;
    }

    data = RNG->DR;
    /* RM0444 19.3.5 note: DR reads zero when no data is ready; zero with DRDY=1 means a
     * seed error occurred between the SR and DR reads (always check DR != 0). */
    if (data == 0U)
    {
        return SPL_ERR_HW;
    }
    *value = data;
    return SPL_OK;
}

SPL_Status RNG_GetErrors(uint32_t *errors)
{
    SPL_REQUIRE(errors != (uint32_t *)0, SPL_ERR_PARAM);
    *errors = RNG->SR & RNG_ERRORS_ALL;
    return SPL_OK;
}

SPL_Status RNG_ClearClockError(void)
{
    /* rc_w0: CEIS=0 clears it, SEIS=1 has no effect. Read-only bits
     * (SECS, CECS, DRDY) ignore writes. */
    RNG->SR = RNG_SR_SEIS;
    /* Read back: the clear reaches the peripheral before return (METHOD 1.7, issue #23). */
    (void)RNG->SR;
    return SPL_OK;
}

SPL_Status RNG_SeedErrorRecover(void)
{
    uint32_t i;
    volatile uint32_t discard;

    if ((RNG->SR & (RNG_SR_SEIS | RNG_SR_SECS)) == 0U)
    {
        return SPL_OK;
    }

    /* RM0444 19.3.7 step 1: clear SEIS by writing 0 (CEIS=1: no effect). */
    RNG->SR = RNG_SR_CEIS;
    /* Step 2: read and discard 12 words to clean the pipeline. */
    for (i = 0U; i < RNG_RECOVERY_DISCARD_WORDS; ++i)
    {
        discard = RNG->DR;
    }
    (void)discard;
    /* Step 3: SEIS must still be cleared; SECS is checked too because a
     * persisting noise-source fault keeps it set. */
    if ((RNG->SR & (RNG_SR_SEIS | RNG_SR_SECS)) != 0U)
    {
        return SPL_ERR_HW;
    }
    return SPL_OK;
}

#else
/* ISO C forbids an empty translation unit. */
typedef int stm32g0_rng_absent;
#endif /* SPL_G0_HAS_RNG */
