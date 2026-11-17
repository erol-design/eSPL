/* SPDX-License-Identifier: MIT */
/* System window watchdog (WWDG) driver, RM0440 Rev 9 section 36.
 * Public contract: stm32g4_wwdg.h. */
#define SPL_LIBRARY_BUILD 1
#include <stddef.h>
#include "stm32g4_wwdg.h"

static inline uint8_t Wwdg_ValidCounter(uint32_t counter)
{
    return ((counter >= WWDG_COUNTER_MIN) && (counter <= WWDG_COUNTER_MAX)) ? 1U : 0U;
}

SPL_Status WWDG_Config(uint32_t timebase, uint32_t window, uint8_t ewi)
{
    uint32_t cfr;

    /* Bitfield overflow or invalid window can silently change reset behavior. */
    SPL_REQUIRE(timebase <= (WWDG_CFR_WDGTB_Msk >> WWDG_CFR_WDGTB_Pos), SPL_ERR_PARAM);
    SPL_REQUIRE(Wwdg_ValidCounter(window) != 0U, SPL_ERR_PARAM);
    SPL_REQUIRE(ewi <= 1U, SPL_ERR_PARAM);
    /* EWI is set-only and cleared by reset (RM0440 36.5.2). */
    if ((ewi == 0U) && ((WWDG->CFR & WWDG_CFR_EWI) != 0U))
    {
        return SPL_ERR_STATE;
    }

    cfr = (timebase << WWDG_CFR_WDGTB_Pos) | (window << WWDG_CFR_W_Pos);
    if (ewi != 0U)
    {
        cfr |= WWDG_CFR_EWI;
    }
    WWDG->CFR = cfr;
    return SPL_OK;
}

SPL_Status WWDG_Enable(uint32_t counter)
{
    /* T6 must be set to prevent an unintended watchdog reset. */
    SPL_REQUIRE(Wwdg_ValidCounter(counter) != 0U, SPL_ERR_PARAM);
    if ((WWDG->CR & WWDG_CR_WDGA) != 0U)
    {
        return SPL_ERR_STATE;
    }

    /* Irreversible until reset. T6=1 is guaranteed by the range check. */
    WWDG->CR = WWDG_CR_WDGA | (counter << WWDG_CR_T_Pos);
    return SPL_OK;
}

SPL_Status WWDG_Refresh(uint32_t counter)
{
    uint32_t current;
    uint32_t window;

    /* T6 must be set to prevent an unintended watchdog reset. */
    SPL_REQUIRE(Wwdg_ValidCounter(counter) != 0U, SPL_ERR_PARAM);
    current = WWDG->CR;
    if ((current & WWDG_CR_WDGA) == 0U)
    {
        return SPL_ERR_STATE;
    }
    /* A reload while T[6:0] > W[6:0] resets the MCU (RM0440 36.3). The counter
     * only decrements, so the check stays valid unless another context
     * refreshes between this read and the write below. */
    window = (WWDG->CFR & WWDG_CFR_W) >> WWDG_CFR_W_Pos;
    if (((current & WWDG_CR_T) >> WWDG_CR_T_Pos) > window)
    {
        return SPL_ERR_STATE;
    }

    WWDG->CR = WWDG_CR_WDGA | (counter << WWDG_CR_T_Pos);
    return SPL_OK;
}

SPL_Status WWDG_GetCounter(uint32_t *counter)
{
    SPL_REQUIRE(counter != NULL, SPL_ERR_PARAM); /* Output pointer. */
    *counter = (WWDG->CR & WWDG_CR_T) >> WWDG_CR_T_Pos;
    return SPL_OK;
}

SPL_Status WWDG_GetEarlyWakeupFlag(uint8_t *flag)
{
    SPL_REQUIRE(flag != NULL, SPL_ERR_PARAM); /* Output pointer. */
    *flag = ((WWDG->SR & WWDG_SR_EWIF) != 0U) ? 1U : 0U;
    return SPL_OK;
}

SPL_Status WWDG_ClearEarlyWakeupFlag(void)
{
    /* EWIF is rc_w0; the other SR bits are reserved. Write 0, no RMW. */
    WWDG->SR = 0U;
    /* Read back: the clear reaches the peripheral before return (METHOD 1.7, issue #23). */
    (void)WWDG->SR;
    return SPL_OK;
}

SPL_Status WWDG_CalcTimeoutUs(uint32_t pclk_hz, uint32_t timebase, uint32_t counter,
                              uint32_t *timeout_us)
{
    uint64_t us;

    /* Arithmetic range, division and output preconditions. */
    SPL_REQUIRE(timeout_us != NULL, SPL_ERR_PARAM);
    SPL_REQUIRE(pclk_hz != 0U, SPL_ERR_PARAM);
    SPL_REQUIRE(timebase <= (WWDG_CFR_WDGTB_Msk >> WWDG_CFR_WDGTB_Pos), SPL_ERR_PARAM);
    SPL_REQUIRE(Wwdg_ValidCounter(counter) != 0U, SPL_ERR_PARAM);

    /* RM0440 36.3.4: t = tPCLK * 4096 * 2^WDGTB * (T[5:0] + 1). */
    us = ((uint64_t)4096U << timebase) * (((uint64_t)counter & 0x3FU) + 1U) * 1000000ULL;
    us /= pclk_hz;
    if (us > 0xFFFFFFFFULL)
    {
        return SPL_ERR_PARAM;
    }
    *timeout_us = (uint32_t)us;
    return SPL_OK;
}
