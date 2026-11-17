/* SPDX-License-Identifier: MIT */
/* CRS driver (RM0444 Rev 6 section 6), built only where SPL_G0_HAS_CRS; public contract in
 * stm32g0_crs.h.
 * Ported from STM32G4 SPL v0.13.3 (Autometry_Mcu_Spl 130de7a); reviewed against RM0444 Rev 6
 * section 6: same CRS IP. G0 change: USB SOF availability guard (USB_DRD_FS). */
/* Library unit: this unit also emits the spl_check.h mismatch-guard reference; the only unit
 * that does not is stm32g0_check.c (SPL_CHECK_LEVEL_DEFINER). BULGU-02, CHECK_LEVEL.md 3.2. */
#define SPL_LIBRARY_BUILD 1
#include "stm32g0_crs.h"

#if SPL_G0_HAS_CRS

/* Check level (A4 wave 5, Docs/CHECK_LEVEL.md 5.4 CRS table): SPL_REQUIRE = checked at FULL only
 * (not compiled at FAST). The calculators' answers (CalcReload, CalcErrorLimit), SWSYNC pending,
 * the error flags and WOULD_BLOCK are results; the USB SOF `#if` is structural: at every level. */

/* CR bits that are kept on read-modify-write. SWSYNC (bit 7, self-clearing
 * command) and reserved bits are never written back. */
#define CRS_CR_KEEP (CRS_IT_ALL | CRS_CR_CEN | CRS_CR_AUTOTRIMEN | CRS_CR_TRIM)

#define CRS_SYNCDIV_MAX 7U
#define CRS_RELOAD_MAX 0xFFFFU
#define CRS_FELIM_MAX 255U
#define CRS_OUTRANGE_FACTOR 128U  /* OUTRANGE = 128 * FELIM (RM0444 6.4.4) */
#define CRS_STEP_MAX 10000U       /* 100 % in 0.01 % units */
#define CRS_FELIM_DIVISOR 20000UL /* 100 % (in 0.01 %) * 2 */

static SPL_Status Crs_Ratio(uint32_t target_hz, uint32_t sync_hz, uint32_t *ratio)
{
    if ((sync_hz == 0U) || (target_hz < sync_hz))
    {
        return SPL_ERR_PARAM;
    }
    *ratio = target_hz / sync_hz;
    return SPL_OK;
}

SPL_Status CRS_CalcReload(uint32_t target_hz, uint32_t sync_hz, uint32_t *reload)
{
    uint32_t ratio = 0U;

    SPL_REQUIRE(reload != (uint32_t *)0, SPL_ERR_PARAM);
    if (Crs_Ratio(target_hz, sync_hz, &ratio) != SPL_OK)
    {
        return SPL_ERR_PARAM;
    }
    if ((ratio - 1U) > CRS_RELOAD_MAX)
    {
        return SPL_ERR_PARAM;
    }
    *reload = ratio - 1U;
    return SPL_OK;
}

SPL_Status CRS_CalcErrorLimit(uint32_t target_hz, uint32_t sync_hz, uint32_t step_centi_pct,
                              uint32_t *error_limit)
{
    uint32_t ratio = 0U;
    uint64_t product;
    uint64_t felim;

    SPL_REQUIRE(error_limit != (uint32_t *)0, SPL_ERR_PARAM);
    if ((step_centi_pct == 0U) || (step_centi_pct > CRS_STEP_MAX))
    {
        return SPL_ERR_PARAM; /* result: the calculator's answer */
    }
    if (Crs_Ratio(target_hz, sync_hz, &ratio) != SPL_OK)
    {
        return SPL_ERR_PARAM;
    }
    /* Rounded up (RM0444 6.4.6). */
    product = (uint64_t)ratio * (uint64_t)step_centi_pct;
    felim = (product + (CRS_FELIM_DIVISOR - 1UL)) / CRS_FELIM_DIVISOR;
    if (felim > CRS_FELIM_MAX)
    {
        return SPL_ERR_PARAM;
    }
    *error_limit = (uint32_t)felim;
    return SPL_OK;
}

SPL_Status CRS_Config(const CRS_InitTypeDef *init)
{
    uint32_t cfgr;
    uint32_t cr;

    SPL_REQUIRE(init != (const CRS_InitTypeDef *)0, SPL_ERR_PARAM);
    /* shifted CFGR / CR fields. */
    SPL_REQUIRE((init->sync_source <= CRS_SYNCSRC_USB_SOF) &&
                    (init->sync_divider <= CRS_SYNCDIV_MAX) && (init->sync_polarity <= 1U) &&
                    (init->reload <= CRS_RELOAD_MAX) && (init->error_limit != 0U) &&
                    (init->error_limit <= CRS_FELIM_MAX) && (init->trim <= CRS_TRIM_MAX),
                SPL_ERR_PARAM);
    SPL_REQUIRE(init->auto_trim <= 1U, SPL_ERR_PARAM);
    /* RM0444 6.4.6: RELOAD must be greater than the OUTRANGE limit. */
    SPL_REQUIRE(init->reload > (CRS_OUTRANGE_FACTOR * init->error_limit), SPL_ERR_PARAM);
    /* G0 port fix: G0B1/G0C1 CMSIS names the USB instance USB_DRD_FS, not USB; the G4 guard
     * alone would reject the reset-default USB SOF source on G0B1 (CRS-T7). */
#if !defined(USB) && !defined(USB_DRD_FS)
    if (init->sync_source == CRS_SYNCSRC_USB_SOF)
    {
        return SPL_ERR_UNSUPPORTED;
    }
#endif
    cr = CRS->CR & CRS_CR_KEEP;
    /* RM0444 6.7.2: CFGR is write-protected while CEN=1. */
    SPL_REQUIRE((cr & CRS_CR_CEN) == 0U, SPL_ERR_STATE);

    cfgr = (init->sync_polarity << CRS_CFGR_SYNCPOL_Pos) |
           (init->sync_source << CRS_CFGR_SYNCSRC_Pos) |
           (init->sync_divider << CRS_CFGR_SYNCDIV_Pos) |
           (init->error_limit << CRS_CFGR_FELIM_Pos) | (init->reload << CRS_CFGR_RELOAD_Pos);
    CRS->CFGR = cfgr;

    /* TRIM is read-only while AUTOTRIMEN=1 (RM0444 6.7.1): leave auto mode
     * first, write TRIM in manual mode, then re-enable auto mode. */
    if ((cr & CRS_CR_AUTOTRIMEN) != 0U)
    {
        cr &= ~CRS_CR_AUTOTRIMEN;
        CRS->CR = cr;
    }
    cr = (cr & ~CRS_CR_TRIM) | (init->trim << CRS_CR_TRIM_Pos);
    CRS->CR = cr;
    if (init->auto_trim != 0U)
    {
        CRS->CR = cr | CRS_CR_AUTOTRIMEN;
    }
    return SPL_OK;
}

SPL_Status CRS_Enable(void)
{
    CRS->CR = (CRS->CR & CRS_CR_KEEP) | CRS_CR_CEN;
    return SPL_OK;
}

SPL_Status CRS_Disable(void)
{
    CRS->CR = CRS->CR & (CRS_CR_KEEP & ~CRS_CR_CEN);
    return SPL_OK;
}

SPL_Status CRS_GenerateSwSync(void)
{
    uint32_t cr = CRS->CR;

    SPL_REQUIRE((cr & CRS_CR_CEN) != 0U, SPL_ERR_STATE); /* SWSYNC ignored */
    if ((cr & CRS_CR_SWSYNC) != 0U)
    {
        return SPL_BUSY;
    }
    CRS->CR = (cr & CRS_CR_KEEP) | CRS_CR_SWSYNC;
    return SPL_OK;
}

SPL_Status CRS_SetAutoTrim(uint8_t enable)
{
    uint32_t cr;

    SPL_REQUIRE(enable <= 1U, SPL_ERR_PARAM);
    cr = CRS->CR & (CRS_CR_KEEP & ~CRS_CR_AUTOTRIMEN);
    if (enable != 0U)
    {
        cr |= CRS_CR_AUTOTRIMEN;
    }
    CRS->CR = cr;
    return SPL_OK;
}

SPL_Status CRS_SetTrim(uint32_t trim)
{
    uint32_t cr;

    SPL_REQUIRE(trim <= CRS_TRIM_MAX, SPL_ERR_PARAM); /* TRIM overflow */
    cr = CRS->CR & CRS_CR_KEEP;
    SPL_REQUIRE((cr & CRS_CR_AUTOTRIMEN) == 0U, SPL_ERR_STATE); /* TRIM read-only */
    CRS->CR = (cr & ~CRS_CR_TRIM) | (trim << CRS_CR_TRIM_Pos);
    return SPL_OK;
}

SPL_Status CRS_GetTrim(uint32_t *trim)
{
    SPL_REQUIRE(trim != (uint32_t *)0, SPL_ERR_PARAM);
    *trim = (CRS->CR & CRS_CR_TRIM) >> CRS_CR_TRIM_Pos;
    return SPL_OK;
}

SPL_Status CRS_InterruptConfig(uint32_t mask, uint8_t enable)
{
    uint32_t cr;

    SPL_REQUIRE((mask != 0U) && (enable <= 1U), SPL_ERR_PARAM);
    SPL_REQUIRE((mask & ~CRS_IT_ALL) == 0U, SPL_ERR_PARAM); /* ORed into CR */
    cr = CRS->CR & (CRS_CR_KEEP & ~mask);
    if (enable != 0U)
    {
        cr |= mask;
    }
    CRS->CR = cr;
    return SPL_OK;
}

SPL_Status CRS_ClearFlags(uint32_t flags)
{
    SPL_REQUIRE(flags != 0U, SPL_ERR_PARAM); /* no effect */
    /* (EDR-003): write to a reserved bit. */
    SPL_REQUIRE((flags & ~CRS_CLEAR_ALL) == 0U, SPL_ERR_PARAM);
    CRS->ICR = flags;
    /* Read back: the clear reaches the peripheral before return (METHOD 1.7, issue #23). */
    (void)CRS->ISR;
    return SPL_OK;
}

static uint32_t Crs_ReadStatus(CRS_StatusTypeDef *status, uint32_t cr)
{
    uint32_t isr = CRS->ISR;

    status->flags = isr & CRS_FLAGS_ALL;
    status->fecap = (isr & CRS_ISR_FECAP) >> CRS_ISR_FECAP_Pos;
    status->fedir = (isr & CRS_ISR_FEDIR) >> CRS_ISR_FEDIR_Pos;
    status->trim = (cr & CRS_CR_TRIM) >> CRS_CR_TRIM_Pos;
    return isr;
}

SPL_Status CRS_GetStatus(CRS_StatusTypeDef *status)
{
    uint32_t isr;

    SPL_REQUIRE(status != (CRS_StatusTypeDef *)0, SPL_ERR_PARAM);
    isr = Crs_ReadStatus(status, CRS->CR);
    if ((isr & CRS_ERRORS_ALL) != 0U)
    {
        return SPL_ERR_HW;
    }
    return SPL_OK;
}

SPL_Status CRS_TrySyncOk(CRS_StatusTypeDef *status)
{
    uint32_t cr;
    uint32_t isr;

    SPL_REQUIRE(status != (CRS_StatusTypeDef *)0, SPL_ERR_PARAM);
    cr = CRS->CR;
    isr = Crs_ReadStatus(status, cr);
    /* Counter stopped: no SYNC is evaluated, polling cannot help (K-A4-4 principle). */
    SPL_REQUIRE((cr & CRS_CR_CEN) != 0U, SPL_ERR_STATE);
    if ((isr & CRS_ERRORS_ALL) != 0U)
    {
        return SPL_ERR_HW;
    }
    if ((isr & CRS_ISR_SYNCOKF) != 0U)
    {
        return SPL_OK;
    }
    return SPL_WOULD_BLOCK;
}

#else
/* ISO C forbids an empty translation unit. */
typedef int stm32g0_crs_absent;
#endif /* SPL_G0_HAS_CRS */
