#include <stddef.h>
#include "stm32g4_syscfg.h"

/* Contract: Docs/v0.12_SYSCFG_CONTRACT.md. RM0440 Rev 9 chapter 10. */

/* RM0440 10.2.10 SYSCFG_SKR: "Write 0xCA ... Write 0x53" unlocks SCSR.CCMER.
 * CMSIS defines only the KEY field mask, not the key values. */
#define SYSCFG_SKR_KEY1 0xCAU
#define SYSCFG_SKR_KEY2 0x53U

#define SYSCFG_ANASW_MASK (SYSCFG_CFGR1_BOOSTEN_Msk | SYSCFG_CFGR1_ANASWVDD_Msk)

static uint32_t Syscfg_ClockOn(void)
{
    return ((RCC->APB2ENR & RCC_APB2ENR_SYSCFGEN) != 0U) ? 1U : 0U;
}

SPL_Status SYSCFG_SetMemoryMode(SYSCFG_MemoryModeTypeDef mode)
{
    uint32_t value;

    /* SAFE: MEMRMP kodlamasi gecerli aralikta olmali. */
    SPL_REQUIRE_SAFE((uint32_t)mode <= (uint32_t)SYSCFG_MEM_QUADSPI, SPL_ERR_PARAM);
#if !defined(FMC_BASE)
    if (mode == SYSCFG_MEM_FMC)
    {
        return SPL_ERR_UNSUPPORTED;
    }
#endif
#if !defined(QSPI_R_BASE)
    if (mode == SYSCFG_MEM_QUADSPI)
    {
        return SPL_ERR_UNSUPPORTED;
    }
#endif
    if (Syscfg_ClockOn() == 0U)
    {
        return SPL_ERR_STATE;
    }
    value = SYSCFG->MEMRMP & ~SYSCFG_MEMRMP_MEM_MODE_Msk;
    SYSCFG->MEMRMP = value | ((uint32_t)mode << SYSCFG_MEMRMP_MEM_MODE_Pos);
    return SPL_OK;
}

SPL_Status SYSCFG_GetMemoryMode(uint32_t *mode)
{
    SPL_REQUIRE_SAFE(mode != NULL, SPL_ERR_PARAM); /* SAFE: cikti pointer */
    if (Syscfg_ClockOn() == 0U)
    {
        return SPL_ERR_STATE;
    }
    *mode = (SYSCFG->MEMRMP & SYSCFG_MEMRMP_MEM_MODE_Msk) >> SYSCFG_MEMRMP_MEM_MODE_Pos;
    return SPL_OK;
}

SPL_Status SYSCFG_SetFlashBankSwap(uint8_t swap)
{
#if defined(FLASH_OPTR_DBANK_Msk)
    SPL_REQUIRE(swap <= 1U, SPL_ERR_PARAM); /* Normal: sifir-disinda true. */
    if (Syscfg_ClockOn() == 0U)
    {
        return SPL_ERR_STATE;
    }
    if (swap != 0U)
    {
        SYSCFG->MEMRMP = SYSCFG->MEMRMP | SYSCFG_MEMRMP_FB_MODE_Msk;
    }
    else
    {
        SYSCFG->MEMRMP = SYSCFG->MEMRMP & ~SYSCFG_MEMRMP_FB_MODE_Msk;
    }
    return SPL_OK;
#else
    (void)swap;
    return SPL_ERR_UNSUPPORTED;
#endif
}

SPL_Status SYSCFG_GetFlashBankSwap(uint8_t *swapped)
{
#if defined(FLASH_OPTR_DBANK_Msk)
    SPL_REQUIRE_SAFE(swapped != NULL, SPL_ERR_PARAM); /* SAFE: cikti pointer */
    if (Syscfg_ClockOn() == 0U)
    {
        return SPL_ERR_STATE;
    }
    *swapped = ((SYSCFG->MEMRMP & SYSCFG_MEMRMP_FB_MODE_Msk) != 0U) ? 1U : 0U;
    return SPL_OK;
#else
    (void)swapped;
    return SPL_ERR_UNSUPPORTED;
#endif
}

SPL_Status SYSCFG_CcmEraseStart(void)
{
    if (Syscfg_ClockOn() == 0U)
    {
        return SPL_ERR_STATE;
    }
    if ((SYSCFG->SCSR & (SYSCFG_SCSR_CCMBSY_Msk | SYSCFG_SCSR_CCMER_Msk)) != 0U)
    {
        return SPL_BUSY;
    }
    SYSCFG->SKR = SYSCFG_SKR_KEY1;
    SYSCFG->SKR = SYSCFG_SKR_KEY2;
    /* CCMBSY is read-only, bits 31:2 reserved: write CCMER alone. */
    SYSCFG->SCSR = SYSCFG_SCSR_CCMER_Msk;
    return SPL_OK;
}

SPL_Status SYSCFG_CcmEraseStatus(void)
{
    if (Syscfg_ClockOn() == 0U)
    {
        return SPL_ERR_STATE;
    }
    if ((SYSCFG->SCSR & (SYSCFG_SCSR_CCMBSY_Msk | SYSCFG_SCSR_CCMER_Msk)) != 0U)
    {
        return SPL_BUSY;
    }
    return SPL_OK;
}

SPL_Status SYSCFG_CcmWriteProtect(uint32_t pages)
{
    uint32_t current;

    SPL_REQUIRE(pages != 0U, SPL_ERR_PARAM); /* Normal: etkisiz yazim. */
    /* SAFE: write-protect alanina tasma kalici korumayi degistirebilir. */
    SPL_REQUIRE_SAFE((pages & ~SYSCFG_CCM_PAGES_ALL) == 0U, SPL_ERR_PARAM);
    if (Syscfg_ClockOn() == 0U)
    {
        return SPL_ERR_STATE;
    }
    current = SYSCFG->SWPR;
    if ((current & pages) == pages)
    {
        return SPL_OK;
    }
    /* rs bits: writing 0 has no effect, so OR-ing the read value is exact. */
    SYSCFG->SWPR = current | pages;
    return SPL_OK;
}

SPL_Status SYSCFG_GetCcmWriteProtect(uint32_t *pages)
{
    SPL_REQUIRE_SAFE(pages != NULL, SPL_ERR_PARAM); /* SAFE: cikti pointer */
    if (Syscfg_ClockOn() == 0U)
    {
        return SPL_ERR_STATE;
    }
    *pages = SYSCFG->SWPR & SYSCFG_CCM_PAGES_ALL;
    return SPL_OK;
}

SPL_Status SYSCFG_SetFpuInterrupts(uint32_t mask)
{
    uint32_t value;

    SPL_REQUIRE_SAFE((mask & ~(uint32_t)SYSCFG_FPU_IE_ALL) == 0U, SPL_ERR_PARAM); /* SAFE: bit tasmasi */
    if (Syscfg_ClockOn() == 0U)
    {
        return SPL_ERR_STATE;
    }
    value = SYSCFG->CFGR1 & ~(uint32_t)SYSCFG_FPU_IE_ALL;
    SYSCFG->CFGR1 = value | mask;
    return SPL_OK;
}

SPL_Status SYSCFG_GetFpuInterrupts(uint32_t *mask)
{
    SPL_REQUIRE_SAFE(mask != NULL, SPL_ERR_PARAM); /* SAFE: cikti pointer */
    if (Syscfg_ClockOn() == 0U)
    {
        return SPL_ERR_STATE;
    }
    *mask = SYSCFG->CFGR1 & (uint32_t)SYSCFG_FPU_IE_ALL;
    return SPL_OK;
}

SPL_Status SYSCFG_SetAnalogSwitchSupply(SYSCFG_AnalogSwitchTypeDef supply)
{
    uint32_t bits = 0U;
    uint32_t value;
    SPL_Status st = SPL_OK;

    switch (supply)
    {
    case SYSCFG_ANASW_VDDA:
        break;
    case SYSCFG_ANASW_VDD:
        bits = SYSCFG_CFGR1_ANASWVDD_Msk;
        break;
    case SYSCFG_ANASW_BOOSTER:
        bits = SYSCFG_CFGR1_BOOSTEN_Msk;
        break;
    default:
        st = SPL_ERR_PARAM;
        break;
    }
    if (st != SPL_OK)
    {
        return st;
    }
    if (Syscfg_ClockOn() == 0U)
    {
        return SPL_ERR_STATE;
    }
    value = SYSCFG->CFGR1 & ~SYSCFG_ANASW_MASK;
    SYSCFG->CFGR1 = value | bits;
    return SPL_OK;
}

SPL_Status SYSCFG_SetFastModePlus(uint32_t mask, uint8_t enable)
{
    SPL_REQUIRE(mask != 0U, SPL_ERR_PARAM); /* Normal: etkisiz secim */
    SPL_REQUIRE_SAFE((mask & ~SYSCFG_FMP_ALL) == 0U, SPL_ERR_PARAM); /* SAFE: komsu bit */
    SPL_REQUIRE(enable <= 1U, SPL_ERR_PARAM); /* Normal: boolean normalizasyonu */
    if (Syscfg_ClockOn() == 0U)
    {
        return SPL_ERR_STATE;
    }
    if (enable != 0U)
    {
        SYSCFG->CFGR1 = SYSCFG->CFGR1 | mask;
    }
    else
    {
        SYSCFG->CFGR1 = SYSCFG->CFGR1 & ~mask;
    }
    return SPL_OK;
}

SPL_Status SYSCFG_Lock(uint32_t locks)
{
    uint32_t current;

    SPL_REQUIRE(locks != 0U, SPL_ERR_PARAM); /* Normal: etkisiz secim */
    SPL_REQUIRE_SAFE((locks & ~SYSCFG_LOCK_ALL) == 0U, SPL_ERR_PARAM); /* SAFE: geri alinmaz kilit */
    if (Syscfg_ClockOn() == 0U)
    {
        return SPL_ERR_STATE;
    }
    current = SYSCFG->CFGR2 & SYSCFG_LOCK_ALL;
    if ((current & locks) == locks)
    {
        return SPL_OK;
    }
    /* SPF (rc_w1) is masked out: a lock request never clears the parity flag. */
    SYSCFG->CFGR2 = current | locks;
    return SPL_OK;
}

SPL_Status SYSCFG_GetLocks(uint32_t *locks)
{
    SPL_REQUIRE_SAFE(locks != NULL, SPL_ERR_PARAM); /* SAFE: cikti pointer */
    if (Syscfg_ClockOn() == 0U)
    {
        return SPL_ERR_STATE;
    }
    *locks = SYSCFG->CFGR2 & SYSCFG_LOCK_ALL;
    return SPL_OK;
}

SPL_Status SYSCFG_GetParityError(uint8_t *flag)
{
    SPL_REQUIRE_SAFE(flag != NULL, SPL_ERR_PARAM); /* SAFE: cikti pointer */
    if (Syscfg_ClockOn() == 0U)
    {
        return SPL_ERR_STATE;
    }
    *flag = ((SYSCFG->CFGR2 & SYSCFG_CFGR2_SPF_Msk) != 0U) ? 1U : 0U;
    return SPL_OK;
}

SPL_Status SYSCFG_ClearParityError(void)
{
    if (Syscfg_ClockOn() == 0U)
    {
        return SPL_ERR_STATE;
    }
    SYSCFG->CFGR2 = (SYSCFG->CFGR2 & SYSCFG_LOCK_ALL) | SYSCFG_CFGR2_SPF_Msk;
    /* Read back: the clear reaches the peripheral before return (METHOD 1.7, issue #23). */
    (void)SYSCFG->CFGR2;
    return SPL_OK;
}
