#define SPL_LIBRARY_BUILD 1
#include <stddef.h>
#include "stm32g4_pwr.h"

/* One pin mask is used for CR3.EWUPx, CR4.WPx, SR1.WUFx and SCR.CWUFx. */
#if (PWR_CR3_EWUP_Msk != PWR_SR1_WUF_Msk) || (PWR_CR3_EWUP_Msk != PWR_SCR_CWUF_Msk) ||             \
    (PWR_CR3_EWUP_Msk !=                                                                           \
     (PWR_CR4_WP1_Msk | PWR_CR4_WP2_Msk | PWR_CR4_WP3_Msk | PWR_CR4_WP4_Msk | PWR_CR4_WP5_Msk))
#error "PWR wake-up pin bit layouts differ between CR3/CR4/SR1/SCR"
#endif

static uint8_t Pwr_IsBool(uint8_t value)
{
    return (uint8_t)((value <= 1U) ? 1U : 0U);
}

static uint8_t Pwr_ValidPins(uint32_t pins)
{
    return (uint8_t)(((pins != 0U) && ((pins & ~(uint32_t)PWR_WAKEUP_PIN_ALL) == 0U)) ? 1U : 0U);
}

SPL_Status PWR_SetBackupAccess(uint8_t enable)
{
    uint32_t cr1;

    /* SAFE: invalid boolean can silently change power control. */
    SPL_REQUIRE_SAFE(Pwr_IsBool(enable) != 0U, SPL_ERR_PARAM);

    cr1 = PWR->CR1 & ~(uint32_t)PWR_CR1_DBP;
    if (enable != 0U)
    {
        cr1 |= PWR_CR1_DBP;
    }
    PWR->CR1 = cr1;

    /* RM0440 6.4.1. A write is ignored while the PWR clock is off. */
    if ((PWR->CR1 & PWR_CR1_DBP) != (cr1 & PWR_CR1_DBP))
    {
        return SPL_ERR_STATE;
    }
    return SPL_OK;
}

SPL_Status PWR_StartVoltageScaling(PWR_RangeTypeDef range)
{
    const uint32_t r = (uint32_t)range;
    uint32_t vos;

    /* SAFE: invalid VOS encoding may silently change core voltage mode. */
    SPL_REQUIRE_SAFE(r <= (uint32_t)PWR_RANGE2, SPL_ERR_PARAM);
    /* Library policy: range changes only from Run mode with the main regulator
     * (RM0440 6.1.5 sequences); leave low-power run first (RM0440 6.3.2). */
    if ((PWR->CR1 & PWR_CR1_LPR) != 0U)
    {
        return SPL_ERR_STATE;
    }
    /* Previous range change or LPR exit still in progress: retry later. */
    if ((PWR->SR2 & (PWR_SR2_VOSF | PWR_SR2_REGLPF)) != 0U)
    {
        return SPL_BUSY;
    }

    if (range == PWR_RANGE2)
    {
        vos = PWR_CR1_VOS_1;
    }
    else
    {
        vos = PWR_CR1_VOS_0;
        /* R1MODE has no effect in Range 2 (RM0440 6.4.22), so it is set before
         * VOS=01. Table 38: 1 = normal (<= 150 MHz), 0 = boost (<= 170 MHz). */
        if (range == PWR_RANGE1_NORMAL)
        {
            PWR->CR5 = PWR->CR5 | PWR_CR5_R1MODE;
        }
        else
        {
            PWR->CR5 = PWR->CR5 & ~(uint32_t)PWR_CR5_R1MODE;
        }
    }
    /* VOS reads back 01 or 10 only (00/11 cannot be written), so the RMW never
     * writes a forbidden value. Caller polls PWR_GetVoltageScalingStatus. */
    PWR->CR1 = (PWR->CR1 & ~(uint32_t)PWR_CR1_VOS) | vos;
    return SPL_OK;
}

SPL_Status PWR_GetVoltageScalingStatus(uint8_t *ready)
{
    SPL_REQUIRE_SAFE(ready != NULL, SPL_ERR_PARAM); /* SAFE: output pointer. */
    *ready = (uint8_t)(((PWR->SR2 & PWR_SR2_VOSF) == 0U) ? 1U : 0U);
    return SPL_OK;
}

SPL_Status PWR_ConfigPVD(PWR_PvdLevelTypeDef level, uint8_t enable)
{
    const uint32_t lvl = (uint32_t)level;
    uint32_t cr2;

    /* SAFE: invalid PLS encoding and enable can alter PVD behavior. */
    SPL_REQUIRE_SAFE(lvl <= (uint32_t)PWR_PVD_EXT_INPUT, SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(Pwr_IsBool(enable) != 0U, SPL_ERR_PARAM);
    /* RM0440 6.4.2: PLS/PVDE are write-protected until system reset when
     * SYSCFG_CFGR2.PVDL is set. */
    if ((SYSCFG->CFGR2 & SYSCFG_CFGR2_PVDL) != 0U)
    {
        return SPL_ERR_STATE;
    }

    cr2 = PWR->CR2 & ~(uint32_t)(PWR_CR2_PLS | PWR_CR2_PVDE);
    cr2 |= lvl << PWR_CR2_PLS_Pos;
    if (enable != 0U)
    {
        cr2 |= PWR_CR2_PVDE;
    }
    PWR->CR2 = cr2;

    if ((PWR->CR2 & (PWR_CR2_PLS | PWR_CR2_PVDE)) != (cr2 & (PWR_CR2_PLS | PWR_CR2_PVDE)))
    {
        return SPL_ERR_STATE;
    }
    return SPL_OK;
}

SPL_Status PWR_GetPVDOutput(uint8_t *below)
{
    SPL_REQUIRE_SAFE(below != NULL, SPL_ERR_PARAM); /* SAFE: output pointer. */
    *below = (uint8_t)(((PWR->SR2 & PWR_SR2_PVDO) != 0U) ? 1U : 0U);
    return SPL_OK;
}

SPL_Status PWR_ConfigWakeupPins(uint32_t pins, uint8_t enable, uint8_t falling)
{
    /* SAFE: invalid masks may configure unrelated wake-up control bits. */
    SPL_REQUIRE_SAFE(Pwr_ValidPins(pins) != 0U, SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(Pwr_IsBool(enable) != 0U, SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(Pwr_IsBool(falling) != 0U, SPL_ERR_PARAM);

    if (enable == 0U)
    {
        PWR->CR3 = PWR->CR3 & ~pins;
        return SPL_OK;
    }
    /* Polarity before enable (RM0440 6.4.3/6.4.4). Flags are not cleared. */
    if (falling != 0U)
    {
        PWR->CR4 = PWR->CR4 | pins;
    }
    else
    {
        PWR->CR4 = PWR->CR4 & ~pins;
    }
    PWR->CR3 = PWR->CR3 | pins;
    return SPL_OK;
}

SPL_Status PWR_GetWakeupFlags(uint32_t *flags)
{
    SPL_REQUIRE_SAFE(flags != NULL, SPL_ERR_PARAM); /* SAFE: output pointer. */
    *flags = PWR->SR1 & PWR_SR1_WUF;
    return SPL_OK;
}

SPL_Status PWR_ClearWakeupFlags(uint32_t pins)
{
    /* SAFE: SCR is W1C; bad mask may clear unrelated fault flags. */
    SPL_REQUIRE_SAFE(Pwr_ValidPins(pins) != 0U, SPL_ERR_PARAM);
    /* SCR is write-1-to-clear: write only the requested CWUFx (never CSBF). */
    PWR->SCR = pins;
    /* Read back: the clear reaches the peripheral before return (METHOD 1.7, issue #23). */
    (void)PWR->SR1;
    return SPL_OK;
}

SPL_Status PWR_SetLowPowerMode(PWR_LowPowerModeTypeDef mode)
{
    const uint32_t m = (uint32_t)mode;

    /* SAFE: invalid LPMS value may select an unintended low-power state. */
    SPL_REQUIRE_SAFE((m == (uint32_t)PWR_LPM_STOP0) || (m == (uint32_t)PWR_LPM_STOP1) ||
                     (m == (uint32_t)PWR_LPM_STANDBY) || (m == (uint32_t)PWR_LPM_SHUTDOWN), SPL_ERR_PARAM);
    /* RM0440 6.4.1: 000 Stop0, 001 Stop1, 011 Standby, 1xx Shutdown. */
    PWR->CR1 = (PWR->CR1 & ~(uint32_t)PWR_CR1_LPMS) | (m << PWR_CR1_LPMS_Pos);
    return SPL_OK;
}
