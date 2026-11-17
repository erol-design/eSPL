/* SPDX-License-Identifier: MIT */
#include <stddef.h>
#include "stm32g4_dbgmcu.h"

/* DBGMCU driver (RM0440 Rev 9 section 47.16). Bits come from the CMSIS device header of the
 * build SKU; a freeze source without a CMSIS macro has mask 0 (UNSUPPORTED).
 * Public contract: stm32g4_dbgmcu.h, Docs/v0.12_DBGMCU_CONTRACT.md. */

#define DBGMCU_REG_APB1FZR1 0U
#define DBGMCU_REG_APB1FZR2 1U
#define DBGMCU_REG_APB2FZ 2U

#if defined(DBGMCU_APB1FZR1_DBG_TIM5_STOP_Msk)
#define DBGMCU_M_TIM5 DBGMCU_APB1FZR1_DBG_TIM5_STOP_Msk
#else
#define DBGMCU_M_TIM5 0U
#endif
#if defined(DBGMCU_APB1FZR1_DBG_I2C3_STOP_Msk)
#define DBGMCU_M_I2C3 DBGMCU_APB1FZR1_DBG_I2C3_STOP_Msk
#else
#define DBGMCU_M_I2C3 0U
#endif
#if defined(DBGMCU_APB1FZR2_DBG_I2C4_STOP_Msk)
#define DBGMCU_M_I2C4 DBGMCU_APB1FZR2_DBG_I2C4_STOP_Msk
#else
#define DBGMCU_M_I2C4 0U
#endif
#if defined(DBGMCU_APB2FZ_DBG_TIM20_STOP_Msk)
#define DBGMCU_M_TIM20 DBGMCU_APB2FZ_DBG_TIM20_STOP_Msk
#else
#define DBGMCU_M_TIM20 0U
#endif
#if defined(DBGMCU_APB2FZ_DBG_HRTIM1_STOP_Msk)
#define DBGMCU_M_HRTIM1 DBGMCU_APB2FZ_DBG_HRTIM1_STOP_Msk
#else
#define DBGMCU_M_HRTIM1 0U
#endif

typedef struct
{
    uint32_t reg;
    uint32_t mask;
} Dbgmcu_FreezeBit;

/* Indexed by DBGMCU_FreezeTypeDef (order checked by the host test). */
static const Dbgmcu_FreezeBit dbgmcu_freeze[] = {
    {DBGMCU_REG_APB1FZR1, DBGMCU_APB1FZR1_DBG_TIM2_STOP_Msk},
    {DBGMCU_REG_APB1FZR1, DBGMCU_APB1FZR1_DBG_TIM3_STOP_Msk},
    {DBGMCU_REG_APB1FZR1, DBGMCU_APB1FZR1_DBG_TIM4_STOP_Msk},
    {DBGMCU_REG_APB1FZR1, DBGMCU_M_TIM5},
    {DBGMCU_REG_APB1FZR1, DBGMCU_APB1FZR1_DBG_TIM6_STOP_Msk},
    {DBGMCU_REG_APB1FZR1, DBGMCU_APB1FZR1_DBG_TIM7_STOP_Msk},
    {DBGMCU_REG_APB1FZR1, DBGMCU_APB1FZR1_DBG_RTC_STOP_Msk},
    {DBGMCU_REG_APB1FZR1, DBGMCU_APB1FZR1_DBG_WWDG_STOP_Msk},
    {DBGMCU_REG_APB1FZR1, DBGMCU_APB1FZR1_DBG_IWDG_STOP_Msk},
    {DBGMCU_REG_APB1FZR1, DBGMCU_APB1FZR1_DBG_I2C1_STOP_Msk},
    {DBGMCU_REG_APB1FZR1, DBGMCU_APB1FZR1_DBG_I2C2_STOP_Msk},
    {DBGMCU_REG_APB1FZR1, DBGMCU_M_I2C3},
    {DBGMCU_REG_APB1FZR1, DBGMCU_APB1FZR1_DBG_LPTIM1_STOP_Msk},
    {DBGMCU_REG_APB1FZR2, DBGMCU_M_I2C4},
    {DBGMCU_REG_APB2FZ, DBGMCU_APB2FZ_DBG_TIM1_STOP_Msk},
    {DBGMCU_REG_APB2FZ, DBGMCU_APB2FZ_DBG_TIM8_STOP_Msk},
    {DBGMCU_REG_APB2FZ, DBGMCU_APB2FZ_DBG_TIM15_STOP_Msk},
    {DBGMCU_REG_APB2FZ, DBGMCU_APB2FZ_DBG_TIM16_STOP_Msk},
    {DBGMCU_REG_APB2FZ, DBGMCU_APB2FZ_DBG_TIM17_STOP_Msk},
    {DBGMCU_REG_APB2FZ, DBGMCU_M_TIM20},
    {DBGMCU_REG_APB2FZ, DBGMCU_M_HRTIM1},
};

/* Compile-time check (CCL-2 YAP-03): one dbgmcu_freeze entry per DBGMCU_FreezeTypeDef value
 * 0..DBGMCU_FZ_COUNT-1 (an entry left out would otherwise be silently zero). */
typedef char Dbgmcu_FreezeTableCheck
    [((sizeof(dbgmcu_freeze) / sizeof(dbgmcu_freeze[0])) == (uint32_t)DBGMCU_FZ_COUNT) ? 1 : -1];

static volatile uint32_t *Dbgmcu_FreezeReg(uint32_t reg)
{
    volatile uint32_t *r;

    if (reg == DBGMCU_REG_APB1FZR1)
    {
        r = &DBGMCU->APB1FZR1;
    }
    else if (reg == DBGMCU_REG_APB1FZR2)
    {
        r = &DBGMCU->APB1FZR2;
    }
    else
    {
        r = &DBGMCU->APB2FZ;
    }
    return r;
}

/* Returns PARAM/UNSUPPORTED/OK and the table entry. */
static SPL_Status Dbgmcu_Lookup(DBGMCU_FreezeTypeDef id, const Dbgmcu_FreezeBit **bit)
{
    uint32_t index = (uint32_t)id;

    if (index >= (uint32_t)DBGMCU_FZ_COUNT)
    {
        return SPL_ERR_PARAM;
    }
    if (dbgmcu_freeze[index].mask == 0U)
    {
        return SPL_ERR_UNSUPPORTED;
    }
    *bit = &dbgmcu_freeze[index];
    return SPL_OK;
}

SPL_Status DBGMCU_SetFreeze(DBGMCU_FreezeTypeDef id, uint8_t freeze)
{
    const Dbgmcu_FreezeBit *bit = NULL;
    volatile uint32_t *reg;
    SPL_Status st;

    /* Normal: any nonzero value would act as enable. */
    SPL_REQUIRE(freeze <= 1U, SPL_ERR_PARAM);
    st = Dbgmcu_Lookup(id, &bit);
    if (st != SPL_OK)
    {
        return st;
    }
    reg = Dbgmcu_FreezeReg(bit->reg);
    if (freeze != 0U)
    {
        *reg = *reg | bit->mask;
    }
    else
    {
        *reg = *reg & ~bit->mask;
    }
    return SPL_OK;
}

SPL_Status DBGMCU_GetFreeze(DBGMCU_FreezeTypeDef id, uint8_t *frozen)
{
    const Dbgmcu_FreezeBit *bit = NULL;
    SPL_Status st;

    /* Invalid output pointer. */
    SPL_REQUIRE(frozen != NULL, SPL_ERR_PARAM);
    st = Dbgmcu_Lookup(id, &bit);
    if (st != SPL_OK)
    {
        return st;
    }
    *frozen = ((*Dbgmcu_FreezeReg(bit->reg) & bit->mask) != 0U) ? 1U : 0U;
    return SPL_OK;
}

SPL_Status DBGMCU_SetLowPowerDebug(uint32_t modes)
{
    /* Overflow into other CR fields. */
    SPL_REQUIRE((modes & ~DBGMCU_LP_ALL) == 0U, SPL_ERR_PARAM);
    DBGMCU->CR = (DBGMCU->CR & ~DBGMCU_LP_ALL) | modes;
    return SPL_OK;
}

SPL_Status DBGMCU_GetLowPowerDebug(uint32_t *modes)
{
    SPL_REQUIRE(modes != NULL, SPL_ERR_PARAM); /* Output pointer */
    *modes = DBGMCU->CR & DBGMCU_LP_ALL;
    return SPL_OK;
}

SPL_Status DBGMCU_GetIdCode(uint16_t *devId, uint16_t *revId)
{
    uint32_t idcode;

    /* Normal: with both outputs absent the read would have no effect. */
    SPL_REQUIRE((devId != NULL) || (revId != NULL), SPL_ERR_PARAM);
    idcode = DBGMCU->IDCODE;
    if (devId != NULL)
    {
        *devId = (uint16_t)((idcode & DBGMCU_IDCODE_DEV_ID_Msk) >> DBGMCU_IDCODE_DEV_ID_Pos);
    }
    if (revId != NULL)
    {
        *revId = (uint16_t)((idcode & DBGMCU_IDCODE_REV_ID_Msk) >> DBGMCU_IDCODE_REV_ID_Pos);
    }
    return SPL_OK;
}
