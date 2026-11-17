/* IWDG driver (RM0444 Rev 6 section 28); public contract in stm32g0_iwdg.h.
 * Ported from STM32G4 SPL v0.13.3 (Autometry_Mcu_Spl 130de7a) and reviewed against RM0444 Rev 6:
 * same IP, register layout and rules; only references changed (v0.3). */
/* Library unit: this unit also emits the spl_check.h mismatch-guard reference; the only unit
 * that does not is stm32g0_check.c (SPL_CHECK_LEVEL_DEFINER). BULGU-02, CHECK_LEVEL.md 3.2. */
#define SPL_LIBRARY_BUILD 1
#include <stddef.h>
#include "stm32g0_iwdg.h"

#define IWDG_SR_UPDATING (IWDG_SR_PVU | IWDG_SR_RVU | IWDG_SR_WVU)

/* Check level (A4 wave 2, Docs/CHECK_LEVEL.md 5.4 IWDG table): SPL_REQUIRE = checked at FULL only
 * (not compiled at FAST). All macros come before the first MMIO write (METHOD 1.3). */

#if SPL_CHECK_LEVEL == SPL_CHECK_FULL
/* field overflow / reload 0 resets at once. Used only by the checks; pure. */
static uint8_t Iwdg_ValidPrescalerReload(uint32_t prescaler, uint32_t reload)
{
    return ((prescaler <= (IWDG_PR_PR_Msk >> IWDG_PR_PR_Pos)) && (reload != 0U) &&
            (reload <= (IWDG_RLR_RL_Msk >> IWDG_RLR_RL_Pos)))
               ? 1U
               : 0U;
}
#define IWDG_PR_RL_OK(prescaler, reload) (Iwdg_ValidPrescalerReload((prescaler), (reload)) != 0U)
#else
#define IWDG_PR_RL_OK(prescaler, reload) (1) /* not evaluated at the disabled level */
#endif

SPL_Status IWDG_Start(void)
{
    /* Irreversible: once running the IWDG cannot be stopped (RM0444 28.3.2). */
    IWDG->KR = IWDG_KEY_START;
    return SPL_OK;
}

SPL_Status IWDG_SetPrescalerReload(uint32_t prescaler, uint32_t reload)
{
    SPL_REQUIRE(IWDG_PR_RL_OK(prescaler, reload), SPL_ERR_PARAM);
    /* RM0444 28.4.4: PR/RLR may change only while PVU/RVU are reset. WVU is
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
    /* every refresh outside the window (reset) or field overflow. */
    SPL_REQUIRE((window != 0U) && (window <= (IWDG_WINR_WIN_Msk >> IWDG_WINR_WIN_Pos)),
                SPL_ERR_PARAM);
    /* RM0444 28.3.2 step 5: wait for IWDG_SR = 0 before writing WINR. */
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

/* IWDG_CalcTimeoutUs uses the ratio 125 / 4: the nominal LSI must be 32 kHz. */
typedef char Iwdg_LsiIs32k[(IWDG_LSI_NOMINAL_HZ == 32000UL) ? 1 : -1];

SPL_Status IWDG_CalcTimeoutUs(uint32_t prescaler, uint32_t reload, uint32_t *timeout_us)
{
    uint32_t divider;

    SPL_REQUIRE(timeout_us != NULL, SPL_ERR_PARAM);
    SPL_REQUIRE(IWDG_PR_RL_OK(prescaler, reload), SPL_ERR_PARAM); /* wrong result */

    /* RM0444 28.4.2: /4 << PR, PR 6 and 7 are both /256. */
    divider = (prescaler >= 6U) ? 256U : (4U << prescaler);
    /* 2026-11-13 (from UFAST): 1 000 000 / 32 000 = 125 / 4; reload <= 0xFFF is checked,
     * d * (r + 1) * 125 < 2^28: no 64-bit math and no division call, result byte-for-byte the
     * same as before (max 32 768 000 us). */
    *timeout_us = (divider * (reload + 1U) * 125U) >> 2U;
    return SPL_OK;
}
