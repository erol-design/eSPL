/* VREFBUF driver for STM32G0B1 (RM0444 Rev 6 section 17), ported from the G4 SPL v0.13.5. */
/* Kutuphane birimi: spl_check.h karisma korumasi referansini bu birim de uretir; uretmeyen tek
 * birim stm32g0_check.c (SPL_CHECK_LEVEL_DEFINER). BULGU-02, CHECK_LEVEL.md 3.2. */
#define SPL_LIBRARY_BUILD 1
#include "stm32g0_vrefbuf.h"

#if SPL_G0_HAS_VREFBUF

/* Denetim seviyesi (A4 dalga 5, Docs/CHECK_LEVEL.md 5.4 VREFBUF tablosu): SPL_REQUIRE = normal
 * sinif (yalniz FULL), SPL_REQUIRE_SAFE = SAFE sinif (FULL ve MIX). VRR = 0 (BUSY) result,
 * SetMode switch default yapisal: her seviyede. */

SPL_Status VREFBUF_SetScale(uint32_t scale)
{
    uint32_t csr;

    SPL_REQUIRE_SAFE(scale <= VREFBUF_SCALE_2V5, SPL_ERR_PARAM); /* SAFE: VRS tasmasi */
    csr = VREFBUF->CSR;
    /* G4 rule (RM0440 23.4.1) kept: VRS programmed only while ENVR=0 (SAFE). */
    SPL_REQUIRE_SAFE((csr & VREFBUF_CSR_ENVR) == 0U, SPL_ERR_STATE);

    /* VRR is read-only; only the writable fields are written back. */
    VREFBUF->CSR = (csr & (VREFBUF_CSR_ENVR | VREFBUF_CSR_HIZ)) | (scale << VREFBUF_CSR_VRS_Pos);
    return SPL_OK;
}

SPL_Status VREFBUF_SetMode(uint32_t mode)
{
    uint32_t bits;

    switch (mode)
    {
    case VREFBUF_MODE_OFF_PULLDOWN:
        bits = 0U;
        break;
    case VREFBUF_MODE_EXTERNAL:
        bits = VREFBUF_CSR_HIZ;
        break;
    case VREFBUF_MODE_INTERNAL:
        bits = VREFBUF_CSR_ENVR;
        break;
    case VREFBUF_MODE_HOLD:
        bits = VREFBUF_CSR_ENVR | VREFBUF_CSR_HIZ;
        break;
    default:
        return SPL_ERR_PARAM;
    }

    VREFBUF->CSR = (VREFBUF->CSR & VREFBUF_CSR_VRS) | bits;
    return SPL_OK;
}

SPL_Status VREFBUF_ReadyStatus(void)
{
    uint32_t csr = VREFBUF->CSR;

    /* RM0444 Table 91: VRR is meaningful only with the buffer on (ENVR=1,
     * HIZ=0); in hold mode it keeps its last state. */
    /* SAFE: bekletme kipinde VRR son durumu tutar (yanlis "hazir"). */
    SPL_REQUIRE_SAFE(((csr & VREFBUF_CSR_ENVR) != 0U) && ((csr & VREFBUF_CSR_HIZ) == 0U),
                     SPL_ERR_STATE);
    if ((csr & VREFBUF_CSR_VRR) == 0U)
    {
        return SPL_BUSY;
    }
    return SPL_OK;
}

SPL_Status VREFBUF_GetTrim(uint32_t *trim)
{
    SPL_REQUIRE_SAFE(trim != (uint32_t *)0, SPL_ERR_PARAM);
    *trim = (VREFBUF->CCR & VREFBUF_CCR_TRIM) >> VREFBUF_CCR_TRIM_Pos;
    return SPL_OK;
}

#else
/* ISO C forbids an empty translation unit. */
typedef int stm32g0_vrefbuf_absent;
#endif /* SPL_G0_HAS_VREFBUF */
