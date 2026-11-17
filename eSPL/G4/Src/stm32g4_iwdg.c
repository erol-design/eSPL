#define SPL_LIBRARY_BUILD 1
#include <stddef.h>
#include "stm32g4_iwdg.h"

/* RM0440 Rev 9 section 35. */

#define IWDG_SR_UPDATING (IWDG_SR_PVU | IWDG_SR_RVU | IWDG_SR_WVU)

static inline uint8_t Iwdg_ValidPrescalerReload(uint32_t prescaler, uint32_t reload)
{
    return ((prescaler <= (IWDG_PR_PR_Msk >> IWDG_PR_PR_Pos)) && (reload != 0U) &&
            (reload <= (IWDG_RLR_RL_Msk >> IWDG_RLR_RL_Pos)))
               ? 1U
               : 0U;
}

SPL_Status IWDG_Start(void)
{
    /* Irreversible: once running the IWDG cannot be stopped (RM0440 35.3.2). */
    IWDG->KR = IWDG_KEY_START;
    return SPL_OK;
}

SPL_Status IWDG_SetPrescalerReload(uint32_t prescaler, uint32_t reload)
{
    /* Invalid PR/RLR produces a wrong or unpredictable watchdog period. */
    SPL_REQUIRE(Iwdg_ValidPrescalerReload(prescaler, reload) != 0U, SPL_ERR_PARAM);
    /* RM0440 35.4.4: PR/RLR may change only while PVU/RVU are reset. WVU is
     * included so a pending window write is not overtaken. */
    if ((IWDG->SR & IWDG_SR_UPDATING) != 0U)
    {
        return SPL_BUSY;
    }

    IWDG->KR = IWDG_KEY_ACCESS;
    IWDG->PR = prescaler << IWDG_PR_PR_Pos;
    IWDG->RLR = reload << IWDG_RLR_RL_Pos;
    return SPL_OK;
}

SPL_Status IWDG_SetWindow(uint32_t window)
{
    /* Invalid WINR may silently change watchdog behavior. */
    SPL_REQUIRE(window != 0U, SPL_ERR_PARAM);
    SPL_REQUIRE(window <= (IWDG_WINR_WIN_Msk >> IWDG_WINR_WIN_Pos), SPL_ERR_PARAM);
    /* RM0440 35.3.2 step 5: wait for IWDG_SR = 0 before writing WINR. */
    if ((IWDG->SR & IWDG_SR_UPDATING) != 0U)
    {
        return SPL_BUSY;
    }

    /* The WINR write also reloads the counter from RLR. */
    IWDG->KR = IWDG_KEY_ACCESS;
    IWDG->WINR = window << IWDG_WINR_WIN_Pos;
    return SPL_OK;
}

SPL_Status IWDG_Refresh(void)
{
    IWDG->KR = IWDG_KEY_RELOAD;
    return SPL_OK;
}

SPL_Status IWDG_UpdateStatus(void)
{
    if ((IWDG->SR & IWDG_SR_UPDATING) != 0U)
    {
        return SPL_BUSY;
    }
    return SPL_OK;
}

SPL_Status IWDG_CalcTimeoutUs(uint32_t prescaler, uint32_t reload, uint32_t *timeout_us)
{
    uint32_t divider;
    uint64_t us;

    /* Invalid output pointer or arithmetic parameters corrupt the result. */
    SPL_REQUIRE(timeout_us != NULL, SPL_ERR_PARAM);
    SPL_REQUIRE(Iwdg_ValidPrescalerReload(prescaler, reload) != 0U, SPL_ERR_PARAM);

    /* RM0440 35.4.2: /4 << PR, PR 6 and 7 are both /256. */
    divider = (prescaler >= 6U) ? 256U : (4U << prescaler);
    us = ((uint64_t)divider * ((uint64_t)reload + 1U) * 1000000ULL) / IWDG_LSI_NOMINAL_HZ;
    *timeout_us = (uint32_t)us; /* max 32 768 000 us: fits */
    return SPL_OK;
}
