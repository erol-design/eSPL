/* VREFBUF driver for STM32G0B1 (RM0444 Rev 6 section 17), ported from the G4 SPL v0.13.5.
 * Built only where SPL_G0_HAS_VREFBUF; public contract in stm32g0_vrefbuf.h. */
/* Library unit: this unit also emits the spl_check.h mismatch-guard reference; the only unit
 * that does not is stm32g0_check.c (SPL_CHECK_LEVEL_DEFINER). BULGU-02, CHECK_LEVEL.md 3.2. */
#define SPL_LIBRARY_BUILD 1
#include "stm32g0_vrefbuf.h"

#if SPL_G0_HAS_VREFBUF

/* Check level (A4 wave 5, Docs/CHECK_LEVEL.md 5.4 VREFBUF table): SPL_REQUIRE = checked at FULL
 * only (not compiled at FAST). VRR = 0 (BUSY) is a result and the SetMode switch default is
 * structural: at every level. */

SPL_Status VREFBUF_SetScale(uint32_t scale)
{
    uint32_t csr;

    SPL_REQUIRE(scale <= VREFBUF_SCALE_2V5, SPL_ERR_PARAM); /* VRS overflow */
    csr = VREFBUF->CSR;
    /* G4 rule (RM0440 23.4.1) kept: VRS programmed only while ENVR=0. */
    SPL_REQUIRE((csr & VREFBUF_CSR_ENVR) == 0U, SPL_ERR_STATE);

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
    /* in hold mode VRR keeps its last state (false "ready"). */
    SPL_REQUIRE(((csr & VREFBUF_CSR_ENVR) != 0U) && ((csr & VREFBUF_CSR_HIZ) == 0U), SPL_ERR_STATE);
    if ((csr & VREFBUF_CSR_VRR) == 0U)
    {
        return SPL_BUSY;
    }
    return SPL_OK;
}

SPL_Status VREFBUF_GetTrim(uint32_t *trim)
{
    SPL_REQUIRE(trim != (uint32_t *)0, SPL_ERR_PARAM);
    *trim = (VREFBUF->CCR & VREFBUF_CCR_TRIM) >> VREFBUF_CCR_TRIM_Pos;
    return SPL_OK;
}

#else
/* ISO C forbids an empty translation unit. */
typedef int stm32g0_vrefbuf_absent;
#endif /* SPL_G0_HAS_VREFBUF */
