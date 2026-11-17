#define SPL_LIBRARY_BUILD 1
#include "stm32g4_crs.h"

/* RM0440 Rev 9 section 8 (CRS). */

/* CR bits that are kept on read-modify-write. SWSYNC (bit 7, self-clearing
 * command) and reserved bits are never written back. */
#define CRS_CR_KEEP (CRS_IT_ALL | CRS_CR_CEN | CRS_CR_AUTOTRIMEN | CRS_CR_TRIM)

#define CRS_SYNCDIV_MAX 7U
#define CRS_RELOAD_MAX 0xFFFFU
#define CRS_FELIM_MAX 255U
#define CRS_OUTRANGE_FACTOR 128U  /* OUTRANGE = 128 * FELIM (RM0440 8.4.4) */
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

    /* NULL output can cause an invalid write. */
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

    /* NULL output pointer. */
    SPL_REQUIRE(error_limit != (uint32_t *)0, SPL_ERR_PARAM);
    /* Zero step is not a valid configuration. */
    SPL_REQUIRE(step_centi_pct != 0U, SPL_ERR_PARAM);
    /* Step outside the defined range. */
    SPL_REQUIRE(step_centi_pct <= CRS_STEP_MAX, SPL_ERR_PARAM);
    if (Crs_Ratio(target_hz, sync_hz, &ratio) != SPL_OK)
    {
        return SPL_ERR_PARAM;
    }
    /* Rounded up (RM0440 8.4.6). */
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

    /* NULL configuration pointer. */
    SPL_REQUIRE(init != (const CRS_InitTypeDef *)0, SPL_ERR_PARAM);
    /* Invalid SYNCSRC encoding. */
    SPL_REQUIRE(init->sync_source <= CRS_SYNCSRC_USB_SOF, SPL_ERR_PARAM);
    /* Invalid SYNCDIV encoding. */
    SPL_REQUIRE(init->sync_divider <= CRS_SYNCDIV_MAX, SPL_ERR_PARAM);
    /* Invalid SYNCPOL bit. */
    SPL_REQUIRE(init->sync_polarity <= 1U, SPL_ERR_PARAM);
    /* RELOAD exceeds field width. */
    SPL_REQUIRE(init->reload <= CRS_RELOAD_MAX, SPL_ERR_PARAM);
    /* Zero error limit is disallowed. */
    SPL_REQUIRE(init->error_limit != 0U, SPL_ERR_PARAM);
    /* FELIM exceeds field width. */
    SPL_REQUIRE(init->error_limit <= CRS_FELIM_MAX, SPL_ERR_PARAM);
    /* TRIM exceeds field width. */
    SPL_REQUIRE(init->trim <= CRS_TRIM_MAX, SPL_ERR_PARAM);
    /* Invalid AUTOTRIMEN bit. */
    SPL_REQUIRE(init->auto_trim <= 1U, SPL_ERR_PARAM);
    /* RM0440 8.4.6: RELOAD must be greater than the OUTRANGE limit. */
    /* RELOAD must exceed OUTRANGE; invalid configuration. */
    SPL_REQUIRE(init->reload > (CRS_OUTRANGE_FACTOR * init->error_limit), SPL_ERR_PARAM);
#if !defined(USB)
    if (init->sync_source == CRS_SYNCSRC_USB_SOF)
    {
        return SPL_ERR_UNSUPPORTED;
    }
#endif
    cr = CRS->CR & CRS_CR_KEEP;
    /* RM0440 8.7.2: CFGR is write-protected while CEN=1. */
    if ((cr & CRS_CR_CEN) != 0U)
    {
        return SPL_ERR_STATE;
    }

    cfgr = (init->sync_polarity << CRS_CFGR_SYNCPOL_Pos) |
           (init->sync_source << CRS_CFGR_SYNCSRC_Pos) |
           (init->sync_divider << CRS_CFGR_SYNCDIV_Pos) |
           (init->error_limit << CRS_CFGR_FELIM_Pos) | (init->reload << CRS_CFGR_RELOAD_Pos);
    CRS->CFGR = cfgr;

    /* TRIM is read-only while AUTOTRIMEN=1 (RM0440 8.7.1): leave auto mode
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

    if ((cr & CRS_CR_CEN) == 0U)
    {
        return SPL_ERR_STATE;
    }
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

    /* Invalid boolean enable. */
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

    /* TRIM exceeds its field width. */
    SPL_REQUIRE(trim <= CRS_TRIM_MAX, SPL_ERR_PARAM);
    cr = CRS->CR & CRS_CR_KEEP;
    if ((cr & CRS_CR_AUTOTRIMEN) != 0U)
    {
        return SPL_ERR_STATE;
    }
    CRS->CR = (cr & ~CRS_CR_TRIM) | (trim << CRS_CR_TRIM_Pos);
    return SPL_OK;
}

SPL_Status CRS_GetTrim(uint32_t *trim)
{
    /* NULL output pointer. */
    SPL_REQUIRE(trim != (uint32_t *)0, SPL_ERR_PARAM);
    *trim = (CRS->CR & CRS_CR_TRIM) >> CRS_CR_TRIM_Pos;
    return SPL_OK;
}

SPL_Status CRS_InterruptConfig(uint32_t mask, uint8_t enable)
{
    uint32_t cr;

    /* Empty interrupt mask. */
    SPL_REQUIRE(mask != 0U, SPL_ERR_PARAM);
    /* Invalid interrupt mask writes reserved CR bits. */
    SPL_REQUIRE((mask & ~CRS_IT_ALL) == 0U, SPL_ERR_PARAM);
    /* Invalid boolean enable. */
    SPL_REQUIRE(enable <= 1U, SPL_ERR_PARAM);
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
    /* Empty clear mask. */
    SPL_REQUIRE(flags != 0U, SPL_ERR_PARAM);
    /* Invalid W1C clear mask. */
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

    /* NULL output pointer. */
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

    /* NULL output pointer. */
    SPL_REQUIRE(status != (CRS_StatusTypeDef *)0, SPL_ERR_PARAM);
    cr = CRS->CR;
    isr = Crs_ReadStatus(status, cr);
    /* Counter stopped: no SYNC is evaluated, polling cannot help. */
    if ((cr & CRS_CR_CEN) == 0U)
    {
        return SPL_ERR_STATE;
    }
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
