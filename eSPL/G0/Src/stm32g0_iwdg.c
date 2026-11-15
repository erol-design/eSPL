/* Ported from STM32G4 SPL v0.13.3 (Autometry_Mcu_Spl 130de7a) and reviewed against RM0444 Rev 6:
 * same IP, register layout and rules; only references changed (v0.3). */
/* Kutuphane birimi: spl_check.h karisma korumasi referansini bu birim de uretir; uretmeyen tek
 * birim stm32g0_check.c (SPL_CHECK_LEVEL_DEFINER). BULGU-02, CHECK_LEVEL.md 3.2. */
#define SPL_LIBRARY_BUILD 1
#include <stddef.h>
#include "stm32g0_iwdg.h"

/* RM0444 Rev 6 section 28. */

#define IWDG_SR_UPDATING (IWDG_SR_PVU | IWDG_SR_RVU | IWDG_SR_WVU)

/* Denetim seviyesi (A4 dalga 2, Docs/CHECK_LEVEL.md 5.4 IWDG tablosu): SPL_REQUIRE = normal sinif
 * (yalniz FULL), SPL_REQUIRE_SAFE = SAFE sinif (FULL ve MIX). Butun makrolar ilk MMIO yazimindan
 * once (METHOD 1.3). */

#if SPL_CHECK_LEVEL <= SPL_CHECK_MIX
/* SAFE: alan tasmasi / reload 0 hemen reset. Yalniz denetimde; saf. */
static uint8_t Iwdg_ValidPrescalerReload(uint32_t prescaler, uint32_t reload)
{
    return ((prescaler <= (IWDG_PR_PR_Msk >> IWDG_PR_PR_Pos)) && (reload != 0U) &&
            (reload <= (IWDG_RLR_RL_Msk >> IWDG_RLR_RL_Pos)))
               ? 1U
               : 0U;
}
#define IWDG_PR_RL_OK(prescaler, reload) (Iwdg_ValidPrescalerReload((prescaler), (reload)) != 0U)
#else
#define IWDG_PR_RL_OK(prescaler, reload) (1) /* kapali seviyede degerlendirilmez */
#endif

SPL_Status IWDG_Start(void)
{
    /* Irreversible: once running the IWDG cannot be stopped (RM0444 28.3.2). */
    IWDG->KR = IWDG_KEY_START;
    return SPL_OK;
}

SPL_Status IWDG_SetPrescalerReload(uint32_t prescaler, uint32_t reload)
{
    SPL_REQUIRE_SAFE(IWDG_PR_RL_OK(prescaler, reload), SPL_ERR_PARAM);
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
    /* SAFE: her yenileme pencere disi (reset) ya da alan tasmasi. */
    SPL_REQUIRE_SAFE((window != 0U) && (window <= (IWDG_WINR_WIN_Msk >> IWDG_WINR_WIN_Pos)),
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

/* IWDG_CalcTimeoutUs 125 / 4 oranini kullanir: LSI nominal 32 kHz olmali. */
typedef char Iwdg_LsiIs32k[(IWDG_LSI_NOMINAL_HZ == 32000UL) ? 1 : -1];

SPL_Status IWDG_CalcTimeoutUs(uint32_t prescaler, uint32_t reload, uint32_t *timeout_us)
{
    uint32_t divider;

    SPL_REQUIRE_SAFE(timeout_us != NULL, SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(IWDG_PR_RL_OK(prescaler, reload), SPL_ERR_PARAM); /* SAFE: hesap yanlis */

    /* RM0444 28.4.2: /4 << PR, PR 6 and 7 are both /256. */
    divider = (prescaler >= 6U) ? 256U : (4U << prescaler);
    /* 2026-11-13 (UFAST'tan): 1 000 000 / 32 000 = 125 / 4; reload <= 0xFFF denetlendi,
     * d * (r + 1) * 125 < 2^28: 64 bit ve bolme cagrisi yok, sonuc eskisiyle bayt bayt ayni
     * (max 32 768 000 us). */
    *timeout_us = (divider * (reload + 1U) * 125U) >> 2U;
    return SPL_OK;
}
