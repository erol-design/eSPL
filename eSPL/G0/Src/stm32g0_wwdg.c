/* Ported from STM32G4 SPL v0.13.3 (Autometry_Mcu_Spl 130de7a) and reviewed against RM0444 Rev 6:
 * same IP, register layout and rules; only references changed (v0.3). */
/* Kutuphane birimi: spl_check.h karisma korumasi referansini bu birim de uretir; uretmeyen tek
 * birim stm32g0_check.c (SPL_CHECK_LEVEL_DEFINER). BULGU-02, CHECK_LEVEL.md 3.2. */
#define SPL_LIBRARY_BUILD 1
#include <stddef.h>
#include "stm32g0_wwdg.h"

/* RM0444 Rev 6 section 29. */

/* Denetim seviyesi (A4 dalga 2, Docs/CHECK_LEVEL.md 5.4 WWDG tablosu): SPL_REQUIRE = normal sinif
 * (yalniz FULL), SPL_REQUIRE_SAFE = SAFE sinif (FULL ve MIX). Butun makrolar ilk MMIO yazimindan
 * once (METHOD 1.3). */

#if SPL_CHECK_LEVEL <= SPL_CHECK_MIX
/* SAFE: 0x40..0x7F disi sayac / pencere T6 = 0 ile hemen reset. Yalniz denetimde; saf. */
static uint8_t Wwdg_ValidCounter(uint32_t counter)
{
    return ((counter >= WWDG_COUNTER_MIN) && (counter <= WWDG_COUNTER_MAX)) ? 1U : 0U;
}
#define WWDG_COUNTER_OK(counter) (Wwdg_ValidCounter(counter) != 0U)
#else
#define WWDG_COUNTER_OK(counter) (1) /* kapali seviyede degerlendirilmez */
#endif

SPL_Status WWDG_Config(uint32_t timebase, uint32_t window, uint8_t ewi)
{
    uint32_t cfr;

    SPL_REQUIRE_SAFE((timebase <= (WWDG_CFR_WDGTB_Msk >> WWDG_CFR_WDGTB_Pos)) &&
                         WWDG_COUNTER_OK(window),
                     SPL_ERR_PARAM);
    SPL_REQUIRE(ewi <= 1U, SPL_ERR_PARAM); /* normal: 1 gibi */
    /* EWI is set-only and cleared by reset (RM0444 29.5.2). SAFE: EWI sessizce acik kalir. */
    SPL_REQUIRE_SAFE((ewi != 0U) || ((WWDG->CFR & WWDG_CFR_EWI) == 0U), SPL_ERR_STATE);

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
    SPL_REQUIRE_SAFE(WWDG_COUNTER_OK(counter), SPL_ERR_PARAM);
    /* SAFE: pencere denetimsiz yenileme, reset olabilir. */
    SPL_REQUIRE_SAFE((WWDG->CR & WWDG_CR_WDGA) == 0U, SPL_ERR_STATE);

    /* Irreversible until reset. T6=1 is guaranteed by the range check. */
    WWDG->CR = WWDG_CR_WDGA | (counter << WWDG_CR_T_Pos);
    return SPL_OK;
}

SPL_Status WWDG_Refresh(uint32_t counter)
{
    SPL_REQUIRE_SAFE(WWDG_COUNTER_OK(counter), SPL_ERR_PARAM);
#if SPL_CHECK_LEVEL <= SPL_CHECK_MIX
    /* Okumalar yalniz SAFE denetimler icin: FAST'ta yapilmaz. */
    {
        const uint32_t current = WWDG->CR;
        uint32_t window;

        /* SAFE: yenileme WDGA'yi da yazar, bekci istenmeden acilir. */
        SPL_REQUIRE_SAFE((current & WWDG_CR_WDGA) != 0U, SPL_ERR_STATE);
        /* A reload while T[6:0] > W[6:0] resets the MCU (RM0444 29.3). The counter
         * only decrements, so the check stays valid unless another context
         * refreshes between this read and the write below. SAFE: erken yenileme reset. */
        window = (WWDG->CFR & WWDG_CFR_W) >> WWDG_CFR_W_Pos;
        SPL_REQUIRE_SAFE(((current & WWDG_CR_T) >> WWDG_CR_T_Pos) <= window, SPL_ERR_STATE);
    }
#endif

    WWDG->CR = WWDG_CR_WDGA | (counter << WWDG_CR_T_Pos);
    return SPL_OK;
}

SPL_Status WWDG_GetCounter(uint32_t *counter)
{
    SPL_REQUIRE_SAFE(counter != NULL, SPL_ERR_PARAM);
    *counter = (WWDG->CR & WWDG_CR_T) >> WWDG_CR_T_Pos;
    return SPL_OK;
}

SPL_Status WWDG_GetEarlyWakeupFlag(uint8_t *flag)
{
    SPL_REQUIRE_SAFE(flag != NULL, SPL_ERR_PARAM);
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

    SPL_REQUIRE_SAFE(timeout_us != NULL, SPL_ERR_PARAM);
    /* SAFE: sifira bolme / sessizce yanlis hesap. */
    SPL_REQUIRE_SAFE((pclk_hz != 0U) && (timebase <= (WWDG_CFR_WDGTB_Msk >> WWDG_CFR_WDGTB_Pos)) &&
                         WWDG_COUNTER_OK(counter),
                     SPL_ERR_PARAM);

    /* RM0444 29.3.4: t = tPCLK * 4096 * 2^WDGTB * (T[5:0] + 1). */
    us = ((uint64_t)4096U << timebase) * (((uint64_t)counter & 0x3FU) + 1U) * 1000000ULL;
    us /= pclk_hz;
    if (us > 0xFFFFFFFFULL)
    {
        return SPL_ERR_PARAM; /* result: 32 bite sigmaz, her seviyede */
    }
    *timeout_us = (uint32_t)us;
    return SPL_OK;
}
