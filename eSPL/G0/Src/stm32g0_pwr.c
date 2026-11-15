/* PWR driver for STM32G0B1 (RM0444 Rev 6 section 4, ES0548 Rev 4). */
/* Kutuphane birimi: spl_check.h karisma korumasi referansini bu birim de uretir; uretmeyen tek
 * birim stm32g0_check.c (SPL_CHECK_LEVEL_DEFINER). BULGU-02, CHECK_LEVEL.md 3.2. */
#define SPL_LIBRARY_BUILD 1
#include <stddef.h>
#include "stm32g0_pwr.h"
#include "stm32g0_sku.h"
#include "stm32g0_rcc.h"

#define PWR_REV_A 0x1000U
#define PWR_REV_Z 0x1001U
#define PWR_REV_Y 0x1003U

/* Denetim seviyesi (A4 dalga 1, Docs/CHECK_LEVEL.md 5.4 PWR tablosu): SPL_REQUIRE = normal sinif
 * (yalniz FULL), SPL_REQUIRE_SAFE = SAFE sinif (FULL ve MIX). Yapisal dallar (#if, SKU) ve silikon
 * revizyonu denetimi (result, K-A4-1) her seviyede duz if. Butun makrolar ilk MMIO yazimindan once
 * (METHOD 1.3). */

SPL_Status PWR_SetBackupAccess(uint8_t enable)
{
    SPL_REQUIRE(enable <= 1U, SPL_ERR_PARAM); /* normal: 1 gibi davranir */
    PWR->CR1 = (enable != 0U) ? (PWR->CR1 | PWR_CR1_DBP) : (PWR->CR1 & ~PWR_CR1_DBP);
    return SPL_OK;
}

SPL_Status PWR_StartVoltageScaling(PWR_RangeTypeDef range, uint32_t hse_hz)
{
    /* SAFE: VOS'a ayrilmis kod (00 / 11) ya da komsu bite tasma. */
    SPL_REQUIRE_SAFE((range == PWR_RANGE1) || (range == PWR_RANGE2), SPL_ERR_PARAM);
#if SPL_CHECK_LEVEL <= SPL_CHECK_MIX
    /* SAFE (CHECK_LEVEL.md 5.3): Range 2'de HCLK > 16 MHz ya da PLL acik kalirsa cekirdek gerilim
     * sinirinin ustunde calisir. Saat hesabi yalniz bu denetim icin: FAST'ta derlenmez. */
    if (range == PWR_RANGE2)
    {
        RCC_ClocksTypeDef c;
        const SPL_Status st = RCC_GetClocks(hse_hz, &c);
        if (st != SPL_OK)
        {
            return st;
        }
        SPL_REQUIRE_SAFE(c.hclk_hz <= 16000000UL, SPL_ERR_STATE);
        SPL_REQUIRE_SAFE((RCC->CR & RCC_CR_PLLON) == 0U, SPL_ERR_STATE);
    }
#else
    (void)hse_hz;
#endif
    PWR->CR1 = (PWR->CR1 & ~PWR_CR1_VOS_Msk) | ((uint32_t)range << PWR_CR1_VOS_Pos);
    return SPL_OK;
}

SPL_Status PWR_GetVoltageScalingStatus(uint8_t *ready)
{
    SPL_REQUIRE_SAFE(ready != NULL, SPL_ERR_PARAM);
    *ready = ((PWR->SR2 & PWR_SR2_VOSF) == 0U) ? 1U : 0U;
    return SPL_OK;
}

SPL_Status PWR_ConfigPVD(uint32_t falling, uint32_t rising, uint8_t enable)
{
    /* SAFE: PVDFT'ye ayrilmis kod (7) ya da PVDRT / komsu alana tasma. */
    SPL_REQUIRE_SAFE((falling <= 6U) && (rising <= 7U), SPL_ERR_PARAM);
    SPL_REQUIRE(enable <= 1U, SPL_ERR_PARAM); /* normal: 1 gibi davranir */
#if !SPL_G0_HAS_PVD
    return SPL_ERR_UNSUPPORTED; /* v0.8: no PVD on the value line (G030 / G050 / G070 / G0B0) */
#else
    /* SAFE: PVD kilitliyken (PVDL) CR2 yazimi sessizce yok sayilir. */
    SPL_REQUIRE_SAFE((SYSCFG->CFGR2 & SYSCFG_CFGR2_PVDL) == 0U, SPL_ERR_STATE);
    uint32_t cr2 = PWR->CR2 & ~(PWR_CR2_PVDE | PWR_CR2_PVDFT_Msk | PWR_CR2_PVDRT_Msk);
    cr2 |= (falling << PWR_CR2_PVDFT_Pos) | (rising << PWR_CR2_PVDRT_Pos);
    if (enable != 0U)
    {
        cr2 |= PWR_CR2_PVDE;
    }
    PWR->CR2 = cr2;
    return SPL_OK;
#endif
}

SPL_Status PWR_GetPVDOutput(uint8_t *below)
{
    SPL_REQUIRE_SAFE(below != NULL, SPL_ERR_PARAM);
#if SPL_G0_HAS_PVD
    *below = ((PWR->SR2 & PWR_SR2_PVDO) != 0U) ? 1U : 0U;
    return SPL_OK;
#else
    *below = 0U;
    return SPL_ERR_UNSUPPORTED;
#endif
}

SPL_Status PWR_ConfigWakeupPins(uint32_t pins, uint8_t enable, uint8_t falling)
{
    SPL_REQUIRE(pins != 0U, SPL_ERR_PARAM); /* normal: etkisiz yazim */
    /* SAFE: kaydirilan fazla bitler CR3 / CR4'un baska bitlerine (RRS, ULPEN, APC) yazilir. */
    SPL_REQUIRE_SAFE((pins & ~PWR_WAKEUP_PINS_ALL) == 0U, SPL_ERR_PARAM);
    SPL_REQUIRE((enable <= 1U) && (falling <= 1U), SPL_ERR_PARAM); /* normal: 1 gibi */
    if (enable == 0U)
    {
        PWR->CR3 = PWR->CR3 & ~(pins << PWR_CR3_EWUP1_Pos);
        return SPL_OK;
    }
    PWR->CR4 = (falling != 0U) ? (PWR->CR4 | (pins << PWR_CR4_WP1_Pos))
                               : (PWR->CR4 & ~(pins << PWR_CR4_WP1_Pos));
    PWR->CR3 = PWR->CR3 | (pins << PWR_CR3_EWUP1_Pos);
    /* ERRATA ES0548 2.2.2: WUFx may rise spuriously during the configuration: clear it. */
    PWR->SCR = pins << PWR_SCR_CWUF1_Pos;
    return SPL_OK;
}

SPL_Status PWR_GetWakeupFlags(uint32_t *flags)
{
    SPL_REQUIRE_SAFE(flags != NULL, SPL_ERR_PARAM);
    *flags = (PWR->SR1 >> PWR_SR1_WUF1_Pos) & PWR_WAKEUP_PINS_ALL;
    return SPL_OK;
}

SPL_Status PWR_ClearWakeupFlags(uint32_t pins)
{
    SPL_REQUIRE(pins != 0U, SPL_ERR_PARAM); /* normal: etkisiz yazim */
    /* SAFE: SCR'de fazla bitler CSBF'yi de siler, Standby bayragi kaybolur. */
    SPL_REQUIRE_SAFE((pins & ~PWR_WAKEUP_PINS_ALL) == 0U, SPL_ERR_PARAM);
    PWR->SCR = pins << PWR_SCR_CWUF1_Pos;
    /* Read back: the clear reaches the peripheral before return (METHOD 1.7, issue #23). */
    (void)PWR->SR1;
    return SPL_OK;
}

SPL_Status PWR_GetStandbyFlag(uint8_t *set)
{
    SPL_REQUIRE_SAFE(set != NULL, SPL_ERR_PARAM);
    *set = ((PWR->SR1 & PWR_SR1_SBF) != 0U) ? 1U : 0U;
    return SPL_OK;
}

SPL_Status PWR_ClearStandbyFlag(void)
{
    PWR->SCR = PWR_SCR_CSBF;
    /* Read back: the clear reaches the peripheral before return (METHOD 1.7, issue #23). */
    (void)PWR->SR1;
    return SPL_OK;
}

SPL_Status PWR_SetLowPowerMode(PWR_LowPowerModeTypeDef mode)
{
    const uint32_t m = (uint32_t)mode;

    /* SAFE: LPMS'e ayrilmis kod (010) ya da tasma. */
    SPL_REQUIRE_SAFE((m == (uint32_t)PWR_LPM_STOP0) || (m == (uint32_t)PWR_LPM_STOP1) ||
                         (m == (uint32_t)PWR_LPM_STANDBY) || (m == (uint32_t)PWR_LPM_SHUTDOWN),
                     SPL_ERR_PARAM);
    /* SAFE (ornek yetenegi): deger serisinde Shutdown yok, LPMS 1xx ayrilmis (RM0454 PWR_CR1). */
    SPL_REQUIRE_SAFE((m != (uint32_t)PWR_LPM_SHUTDOWN) || (PWR_SHUTDOWN_SUPPORTED != 0U),
                     SPL_ERR_UNSUPPORTED);
    /* ERRATA ES0548 2.2.4: HSIDIV != 0 blocks Stop entry (SYSCLK = HSE) and peripheral wake-up.
     * SAFE (errata). */
    SPL_REQUIRE_SAFE((m > (uint32_t)PWR_LPM_STOP1) || ((RCC->CR & RCC_CR_HSIDIV_Msk) == 0U),
                     SPL_ERR_STATE);
    PWR->CR1 = (PWR->CR1 & ~PWR_CR1_LPMS_Msk) | (m << PWR_CR1_LPMS_Pos);
    return SPL_OK;
}

SPL_Status PWR_SetSramRetention(uint8_t enable)
{
    SPL_REQUIRE(enable <= 1U, SPL_ERR_PARAM); /* normal: 1 gibi davranir */
#if !defined(PWR_CR3_RRS)
    return SPL_ERR_UNSUPPORTED; /* v0.8: no RRS bit in the CMSIS header of this SKU */
#else
    /* v0.12: the REV_ID check is ES0548 / ES0549 only. On G031..G081 the old check returned
     * STATE for every silicon revision (REV_ID 0x2000..0x2003, 0x1007) and UNSUPPORTED on G051
     * Rev A, which has no SRAM item (ES0545 2.2.x).
     * Denetim seviyesi: result (K-A4-1), her seviyede. Revizyon donanimdan okunur, cagiranin
     * girdisi degil. */
    if ((enable != 0U) && (SPL_G0_ERRATA_RRS_REV_A != 0))
    {
        const uint32_t rev = (DBG->IDCODE & DBG_IDCODE_REV_ID_Msk) >> DBG_IDCODE_REV_ID_Pos;
        if (rev == PWR_REV_A)
        {
            return SPL_ERR_UNSUPPORTED; /* ERRATA ES0548 2.2.7: SRAM corrupted in Standby */
        }
        if ((rev != PWR_REV_Z) && (rev != PWR_REV_Y))
        {
            return SPL_ERR_STATE;
        }
    }
    PWR->CR3 = (enable != 0U) ? (PWR->CR3 | PWR_CR3_RRS) : (PWR->CR3 & ~PWR_CR3_RRS);
    return SPL_OK;
#endif
}
