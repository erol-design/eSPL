#define SPL_LIBRARY_BUILD 1
#include "stm32g4_vrefbuf.h"

/* RM0440 Rev 9 section 23. */

#if defined(VREFBUF)

SPL_Status VREFBUF_SetScale(uint32_t scale)
{
    uint32_t csr;

    /* Invalid VRS coding changes analog reference voltage. */
    SPL_REQUIRE(scale <= VREFBUF_SCALE_2V9, SPL_ERR_PARAM);
    csr = VREFBUF->CSR;
    /* RM0440 23.4.1: VRS may be programmed only while ENVR=0. */
    if ((csr & VREFBUF_CSR_ENVR) != 0U)
    {
        return SPL_ERR_STATE;
    }

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

    /* RM0440 Table 197: VRR is meaningful only with the buffer on (ENVR=1,
     * HIZ=0); in hold mode it keeps its last state. */
    if (((csr & VREFBUF_CSR_ENVR) == 0U) || ((csr & VREFBUF_CSR_HIZ) != 0U))
    {
        return SPL_ERR_STATE;
    }
    if ((csr & VREFBUF_CSR_VRR) == 0U)
    {
        return SPL_BUSY;
    }
    return SPL_OK;
}

SPL_Status VREFBUF_GetTrim(uint32_t *trim)
{
    /* NULL result pointer cannot receive the trim value. */
    SPL_REQUIRE(trim != (uint32_t *)0, SPL_ERR_PARAM);
    *trim = (VREFBUF->CCR & VREFBUF_CCR_TRIM) >> VREFBUF_CCR_TRIM_Pos;
    return SPL_OK;
}

#else /* VREFBUF not defined by CMSIS for this SKU (G411xB, G414). */

SPL_Status VREFBUF_SetScale(uint32_t scale)
{
    (void)scale;
    return SPL_ERR_UNSUPPORTED;
}

SPL_Status VREFBUF_SetMode(uint32_t mode)
{
    (void)mode;
    return SPL_ERR_UNSUPPORTED;
}

SPL_Status VREFBUF_ReadyStatus(void)
{
    return SPL_ERR_UNSUPPORTED;
}

SPL_Status VREFBUF_GetTrim(uint32_t *trim)
{
    (void)trim;
    return SPL_ERR_UNSUPPORTED;
}

#endif
