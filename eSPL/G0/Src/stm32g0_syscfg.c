/* SYSCFG driver for STM32G0B1 (RM0444 Rev 6 section 8). */
/* Kutuphane birimi: spl_check.h karisma korumasi referansini bu birim de uretir; uretmeyen tek
 * birim stm32g0_check.c (SPL_CHECK_LEVEL_DEFINER). BULGU-02, CHECK_LEVEL.md 3.2. */
#define SPL_LIBRARY_BUILD 1
#include <stddef.h>
#include "stm32g0_syscfg.h"

#define SYSCFG_ITLINE_COUNT 32U

/* Denetim seviyesi (A4 dalga 2, Docs/CHECK_LEVEL.md 5.4 SYSCFG tablosu): SPL_REQUIRE = normal sinif
 * (yalniz FULL), SPL_REQUIRE_SAFE = SAFE sinif (FULL ve MIX). Butun makrolar ilk MMIO yazimindan
 * once (METHOD 1.3). */

static SPL_Status Syscfg_SetCfgr1Bits(uint32_t mask, uint32_t allowed, uint8_t enable)
{
    SPL_REQUIRE((mask != 0U) && (enable <= 1U), SPL_ERR_PARAM); /* normal */
    /* SAFE: izinli bitler disi CFGR1'in baska alanlarini (MEM_MODE dahil) degistirir. */
    SPL_REQUIRE_SAFE((mask & ~allowed) == 0U, SPL_ERR_PARAM);
    SYSCFG->CFGR1 = (enable != 0U) ? (SYSCFG->CFGR1 | mask) : (SYSCFG->CFGR1 & ~mask);
    return SPL_OK;
}

SPL_Status SYSCFG_SetMemoryMode(SYSCFG_MemoryModeTypeDef mode)
{
    /* SAFE: bellek eslemesi / vektor tablosu degisir. */
    SPL_REQUIRE_SAFE((mode == SYSCFG_MEM_FLASH) || (mode == SYSCFG_MEM_SYSTEM) ||
                         (mode == SYSCFG_MEM_SRAM),
                     SPL_ERR_PARAM);
    SYSCFG->CFGR1 = (SYSCFG->CFGR1 & ~SYSCFG_CFGR1_MEM_MODE_Msk) |
                    ((uint32_t)mode << SYSCFG_CFGR1_MEM_MODE_Pos);
    return SPL_OK;
}

SPL_Status SYSCFG_GetMemoryMode(uint32_t *mode)
{
    SPL_REQUIRE_SAFE(mode != NULL, SPL_ERR_PARAM);
    *mode = (SYSCFG->CFGR1 & SYSCFG_CFGR1_MEM_MODE_Msk) >> SYSCFG_CFGR1_MEM_MODE_Pos;
    return SPL_OK;
}

SPL_Status SYSCFG_SetFastModePlus(uint32_t mask, uint8_t enable)
{
    return Syscfg_SetCfgr1Bits(mask, SYSCFG_FMP_ALL, enable);
}

SPL_Status SYSCFG_SetPinRemap(uint32_t mask, uint8_t enable)
{
    return Syscfg_SetCfgr1Bits(mask, SYSCFG_REMAP_ALL, enable);
}

SPL_Status SYSCFG_SetAnalogBooster(uint8_t enable)
{
    return Syscfg_SetCfgr1Bits(SYSCFG_CFGR1_BOOSTEN, SYSCFG_CFGR1_BOOSTEN, enable);
}

SPL_Status SYSCFG_Lock(uint32_t locks)
{
    SPL_REQUIRE(locks != 0U, SPL_ERR_PARAM); /* normal: etkisiz yazim */
    /* SAFE: CFGR2'nin baska bitleri; kilitler reset'e kadar geri alinmaz. */
    SPL_REQUIRE_SAFE((locks & ~SYSCFG_LOCK_ALL) == 0U, SPL_ERR_PARAM);
    /* SPF is rc_w1: writing back a pending 1 would clear the parity evidence. */
    SYSCFG->CFGR2 = (SYSCFG->CFGR2 & ~SYSCFG_CFGR2_SPF) | locks;
    return SPL_OK;
}

SPL_Status SYSCFG_GetLocks(uint32_t *locks)
{
    SPL_REQUIRE_SAFE(locks != NULL, SPL_ERR_PARAM);
    *locks = SYSCFG->CFGR2 & SYSCFG_LOCK_ALL;
    return SPL_OK;
}

SPL_Status SYSCFG_GetParityError(uint8_t *flag)
{
    SPL_REQUIRE_SAFE(flag != NULL, SPL_ERR_PARAM);
    *flag = ((SYSCFG->CFGR2 & SYSCFG_CFGR2_SPF) != 0U) ? 1U : 0U;
    return SPL_OK;
}

SPL_Status SYSCFG_ClearParityError(void)
{
    /* Keep the set-only locks and the clamping-diode bits as they are; SPF written as 1. */
    SYSCFG->CFGR2 = SYSCFG->CFGR2 | SYSCFG_CFGR2_SPF;
    /* Read back: the clear reaches the peripheral before return (METHOD 1.7, issue #23). */
    (void)SYSCFG->CFGR2;
    return SPL_OK;
}

SPL_Status SYSCFG_GetInterruptLineStatus(uint32_t line, uint32_t *status)
{
    SPL_REQUIRE_SAFE(line < SYSCFG_ITLINE_COUNT, SPL_ERR_PARAM); /* SAFE: dizi disi okuma */
    SPL_REQUIRE_SAFE(status != NULL, SPL_ERR_PARAM);
    *status = SYSCFG->IT_LINE_SR[line];
    return SPL_OK;
}
