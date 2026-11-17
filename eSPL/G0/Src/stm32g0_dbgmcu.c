/* DBG driver for STM32G0B1 (RM0444 Rev 6 section 40.10); public contract in stm32g0_dbgmcu.h.
 * Only 32-bit accesses (RM0444). */
/* Library unit: this unit also emits the spl_check.h mismatch-guard reference; the only unit
 * that does not is stm32g0_check.c (SPL_CHECK_LEVEL_DEFINER). BULGU-02, CHECK_LEVEL.md 3.2. */
#define SPL_LIBRARY_BUILD 1
#include <stddef.h>
#include "stm32g0_dbgmcu.h"
#include "stm32g0_sku.h"
#include "stm32g0_sku_masks.h"

/* v0.8: freeze bits that the selected SKU has (generated from its CMSIS header; G0B1 equals the
 * v0.2 hand-written list). A freeze id of a timer the SKU lacks -> SPL_ERR_PARAM. */
#define DBGMCU_FZ1_MASK SPL_G0_MASK_DBG_APB_FZ1
#define DBGMCU_FZ2_MASK SPL_G0_MASK_DBG_APB_FZ2

/* LER C03 / C15.6 (2026-11-14 DBGMCU pilot): the register and bit position in the public freeze
 * ids were hand-written; here each one is bound at compile time to the CMSIS DBG_APB_FZx_*_Pos
 * definition. A bit the SKU lacks is not checked (no definition); such an id already returns
 * PARAM through the mask. A mismatch stops the build (negative array size). */
#define DBGMCU_POS_CHECK(id, reg, pos)                                                             \
    typedef char dbgmcu_pos_##id[((((uint32_t)(id)) == DBGMCU_FREEZE_ID((reg), (pos))) ? 1 : -1)]
#if defined(DBG_APB_FZ1_DBG_TIM2_STOP_Pos)
DBGMCU_POS_CHECK(DBGMCU_FREEZE_TIM2, 1, DBG_APB_FZ1_DBG_TIM2_STOP_Pos);
#endif
DBGMCU_POS_CHECK(DBGMCU_FREEZE_TIM3, 1, DBG_APB_FZ1_DBG_TIM3_STOP_Pos);
#if defined(DBG_APB_FZ1_DBG_TIM4_STOP_Pos)
DBGMCU_POS_CHECK(DBGMCU_FREEZE_TIM4, 1, DBG_APB_FZ1_DBG_TIM4_STOP_Pos);
#endif
#if defined(DBG_APB_FZ1_DBG_TIM6_STOP_Pos)
DBGMCU_POS_CHECK(DBGMCU_FREEZE_TIM6, 1, DBG_APB_FZ1_DBG_TIM6_STOP_Pos);
#endif
#if defined(DBG_APB_FZ1_DBG_TIM7_STOP_Pos)
DBGMCU_POS_CHECK(DBGMCU_FREEZE_TIM7, 1, DBG_APB_FZ1_DBG_TIM7_STOP_Pos);
#endif
DBGMCU_POS_CHECK(DBGMCU_FREEZE_RTC, 1, DBG_APB_FZ1_DBG_RTC_STOP_Pos);
DBGMCU_POS_CHECK(DBGMCU_FREEZE_WWDG, 1, DBG_APB_FZ1_DBG_WWDG_STOP_Pos);
DBGMCU_POS_CHECK(DBGMCU_FREEZE_IWDG, 1, DBG_APB_FZ1_DBG_IWDG_STOP_Pos);
DBGMCU_POS_CHECK(DBGMCU_FREEZE_I2C1_SMBUS, 1, DBG_APB_FZ1_DBG_I2C1_SMBUS_TIMEOUT_STOP_Pos);
#if defined(DBG_APB_FZ1_DBG_I2C2_SMBUS_TIMEOUT_STOP_Pos)
DBGMCU_POS_CHECK(DBGMCU_FREEZE_I2C2_SMBUS, 1, DBG_APB_FZ1_DBG_I2C2_SMBUS_TIMEOUT_STOP_Pos);
#endif
#if defined(DBG_APB_FZ1_DBG_LPTIM2_STOP_Pos)
DBGMCU_POS_CHECK(DBGMCU_FREEZE_LPTIM2, 1, DBG_APB_FZ1_DBG_LPTIM2_STOP_Pos);
#endif
#if defined(DBG_APB_FZ1_DBG_LPTIM1_STOP_Pos)
DBGMCU_POS_CHECK(DBGMCU_FREEZE_LPTIM1, 1, DBG_APB_FZ1_DBG_LPTIM1_STOP_Pos);
#endif
DBGMCU_POS_CHECK(DBGMCU_FREEZE_TIM1, 2, DBG_APB_FZ2_DBG_TIM1_STOP_Pos);
DBGMCU_POS_CHECK(DBGMCU_FREEZE_TIM14, 2, DBG_APB_FZ2_DBG_TIM14_STOP_Pos);
#if defined(DBG_APB_FZ2_DBG_TIM15_STOP_Pos)
DBGMCU_POS_CHECK(DBGMCU_FREEZE_TIM15, 2, DBG_APB_FZ2_DBG_TIM15_STOP_Pos);
#endif
DBGMCU_POS_CHECK(DBGMCU_FREEZE_TIM16, 2, DBG_APB_FZ2_DBG_TIM16_STOP_Pos);
DBGMCU_POS_CHECK(DBGMCU_FREEZE_TIM17, 2, DBG_APB_FZ2_DBG_TIM17_STOP_Pos);

/* Check level (A4 wave 2, Docs/CHECK_LEVEL.md 5.4 DBGMCU table): SPL_REQUIRE = checked at FULL only
 * (not compiled at FAST). All macros come before the first MMIO write (METHOD 1.3). */

#if SPL_CHECK_LEVEL == SPL_CHECK_FULL
/* with the DBG clock off a write is silently lost, a read returns 0. Used only by the
 * checks; pure. */
static uint8_t Dbgmcu_ClockOn(void)
{
    return ((RCC->APBENR1 & RCC_APBENR1_DBGEN) != 0U) ? 1U : 0U;
}
#define DBGMCU_CLOCK_ON() (Dbgmcu_ClockOn() != 0U)
#else
#define DBGMCU_CLOCK_ON() (1) /* not evaluated at the disabled level */
#endif

static volatile uint32_t *Dbgmcu_FreezeReg(DBGMCU_FreezeTypeDef id, uint32_t *bit)
{
    const uint32_t v = (uint32_t)id;
    const uint32_t reg = v >> 5U;

    *bit = (uint32_t)1U << (v & 31U);
    if ((reg == 1U) && ((DBGMCU_FZ1_MASK & *bit) != 0U))
    {
        return &DBG->APBFZ1;
    }
    if ((reg == 2U) && ((DBGMCU_FZ2_MASK & *bit) != 0U))
    {
        return &DBG->APBFZ2;
    }
    return NULL;
}

SPL_Status DBGMCU_GetIdCode(uint16_t *dev_id, uint16_t *rev_id)
{
    uint32_t idcode;

    SPL_REQUIRE((dev_id != NULL) && (rev_id != NULL), SPL_ERR_PARAM);
    idcode = DBG->IDCODE;
    *dev_id = (uint16_t)((idcode & DBG_IDCODE_DEV_ID_Msk) >> DBG_IDCODE_DEV_ID_Pos);
    *rev_id = (uint16_t)((idcode & DBG_IDCODE_REV_ID_Msk) >> DBG_IDCODE_REV_ID_Pos);
    return SPL_OK;
}

SPL_Status DBGMCU_SetLowPowerDebug(uint32_t modes)
{
    /* (EDR-003): write to a reserved bit. */
    SPL_REQUIRE((modes & ~DBGMCU_LP_ALL) == 0U, SPL_ERR_PARAM);
    SPL_REQUIRE(DBGMCU_CLOCK_ON(), SPL_ERR_STATE); /* the write is lost / read 0 */
    DBG->CR = (DBG->CR & ~DBGMCU_LP_ALL) | modes;
    return SPL_OK;
}

SPL_Status DBGMCU_GetLowPowerDebug(uint32_t *modes)
{
    SPL_REQUIRE(modes != NULL, SPL_ERR_PARAM);
    SPL_REQUIRE(DBGMCU_CLOCK_ON(), SPL_ERR_STATE); /* the write is lost / read 0 */
    *modes = DBG->CR & DBGMCU_LP_ALL;
    return SPL_OK;
}

SPL_Status DBGMCU_SetFreeze(DBGMCU_FreezeTypeDef id, uint8_t freeze)
{
    uint32_t bit;
    volatile uint32_t *reg = Dbgmcu_FreezeReg(id, &bit);

    /* a freeze id this SKU lacks -> reg NULL (write to NULL). */
    SPL_REQUIRE(reg != NULL, SPL_ERR_PARAM);
    SPL_REQUIRE(freeze <= 1U, SPL_ERR_PARAM); /* treated as 1 */
    /* ERRATA ES0544 2.2.6 / ES0545 2.2.7 / ES0546 2.2.7: APB_FZ1 reads back wrong TIM6 / TIM7 /
     * RTC bits, a read-modify-write would store them: use DBGMCU_WriteFreeze1 (whole register).
     * (errata). */
    SPL_REQUIRE((SPL_G0_ERRATA_DBG_FZ1_READ == 0) || (reg != &DBG->APBFZ1), SPL_ERR_UNSUPPORTED);
    SPL_REQUIRE(DBGMCU_CLOCK_ON(), SPL_ERR_STATE); /* the write is lost / read 0 */
    *reg = (freeze != 0U) ? (*reg | bit) : (*reg & ~bit);
    return SPL_OK;
}

SPL_Status DBGMCU_GetFreeze(DBGMCU_FreezeTypeDef id, uint8_t *frozen)
{
    uint32_t bit;
    const volatile uint32_t *reg = Dbgmcu_FreezeReg(id, &bit);

    SPL_REQUIRE(reg != NULL, SPL_ERR_PARAM); /* read from NULL */
    SPL_REQUIRE(frozen != NULL, SPL_ERR_PARAM);
#if SPL_G0_ERRATA_DBG_FZ1_READ
    /* ERRATA ES0544 2.2.6 / ES0545 2.2.7 / ES0546 2.2.7: these read-backs are wrong (freeze
     * itself works). errata: a wrong value would be read. */
    SPL_REQUIRE((reg != &DBG->APBFZ1) ||
                    ((bit & (DBG_APB_FZ1_DBG_TIM6_STOP | DBG_APB_FZ1_DBG_TIM7_STOP |
                             DBG_APB_FZ1_DBG_RTC_STOP)) == 0U),
                SPL_ERR_UNSUPPORTED);
#endif
    SPL_REQUIRE(DBGMCU_CLOCK_ON(), SPL_ERR_STATE); /* the write is lost / read 0 */
    *frozen = ((*reg & bit) != 0U) ? 1U : 0U;
    return SPL_OK;
}

SPL_Status DBGMCU_WriteFreeze1(uint32_t mask)
{
    /* (EDR-003): write to a reserved bit. */
    SPL_REQUIRE((mask & ~DBGMCU_FZ1_MASK) == 0U, SPL_ERR_PARAM);
    SPL_REQUIRE(DBGMCU_CLOCK_ON(), SPL_ERR_STATE); /* the write is lost / read 0 */
    DBG->APBFZ1 = mask; /* write only: no read-back (ES0545 / ES0546 2.2.7) */
    return SPL_OK;
}
