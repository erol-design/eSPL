/* COMP driver for STM32G0B1 (RM0444 Rev 6 section 18), ported from the G4 SPL v0.13.5.
 * Built only where SPL_G0_HAS_COMP; public contract in stm32g0_comp.h. */
/* Library unit: this unit also emits the spl_check.h mismatch-guard reference; the only unit
 * that does not is stm32g0_check.c (SPL_CHECK_LEVEL_DEFINER). BULGU-02, CHECK_LEVEL.md 3.2. */
#define SPL_LIBRARY_BUILD 1
#include "stm32g0_comp.h"
#include "stm32g0_sku_inst.h"
#include <stddef.h>

#if SPL_G0_HAS_COMP

/* Check level (A4 wave 5, Docs/CHECK_LEVEL.md 5.4 COMP table): SPL_REQUIRE = checked at FULL only
 * (not compiled at FAST). This unit has no result line. */

static inline uint8_t Comp_Valid(const COMP_TypeDef *comp)
{
    /* v0.8: COMP3 only on group 0BX. */
    return (uint8_t)(((comp == COMP1) || (comp == COMP2) || SPL_G0_IS_COMP3(comp)) ? 1U : 0U);
}

static uint8_t Comp_Locked(const COMP_TypeDef *comp)
{
    return ((comp->CSR & COMP_CSR_LOCK) != 0U) ? 1U : 0U;
}

/* Clears bits through a uint32_t parameter (no cast of a composite mask). */
static uint32_t Comp_ClearBits(uint32_t value, uint32_t bits)
{
    return value & ~bits;
}

/* Instance valid and register writable. */
static SPL_Status Comp_CheckWritable(const COMP_TypeDef *comp)
{
    SPL_REQUIRE(Comp_Valid(comp) != 0U, SPL_ERR_PARAM);  /* arbitrary address */
    SPL_REQUIRE(Comp_Locked(comp) == 0U, SPL_ERR_STATE); /* the write is ignored */
    return SPL_OK;
}

SPL_Status COMP_Config(COMP_TypeDef *comp, const COMP_ConfigTypeDef *config)
{
    SPL_Status status;
    uint32_t csr;

    SPL_REQUIRE(Comp_Valid(comp) != 0U, SPL_ERR_PARAM); /* arbitrary address */
    SPL_REQUIRE(config != NULL, SPL_ERR_PARAM);
    /* shifted CSR fields. */
    SPL_REQUIRE((config->inp_sel <= COMP_INP_SEL_MAX) && (config->inm_sel <= COMP_INM_SEL_MAX) &&
                    (config->hysteresis <= COMP_HYST_MAX) &&
                    (config->polarity <= COMP_POL_INVERTED) &&
                    (config->blanking <= COMP_BLANK_MAX) &&
                    (config->power_mode <= COMP_POWER_MEDIUM_SPEED) &&
                    (config->window_mode <= 1U) && (config->window_out <= 1U),
                SPL_ERR_PARAM);
    status = Comp_CheckWritable(comp);
    if (status != SPL_OK)
    {
        return status;
    }
    /* Configure with the comparator disabled (caller disables first). the whole-CSR write
     * silently disables the comparator. */
    SPL_REQUIRE((comp->CSR & COMP_CSR_EN) == 0U, SPL_ERR_STATE);

    /* Whole-register write: reserved bits and EN/LOCK are written 0. */
    csr = ((uint32_t)config->inm_sel << COMP_CSR_INMSEL_Pos) |
          ((uint32_t)config->inp_sel << COMP_CSR_INPSEL_Pos) |
          ((uint32_t)config->window_mode << COMP_CSR_WINMODE_Pos) |
          ((uint32_t)config->window_out << COMP_CSR_WINOUT_Pos) |
          ((uint32_t)config->polarity << COMP_CSR_POLARITY_Pos) |
          ((uint32_t)config->hysteresis << COMP_CSR_HYST_Pos) |
          ((uint32_t)config->power_mode << COMP_CSR_PWRMODE_Pos) |
          ((uint32_t)config->blanking << COMP_CSR_BLANKING_Pos);
    comp->CSR = csr;
    return SPL_OK;
}

SPL_Status COMP_Enable(COMP_TypeDef *comp)
{
    SPL_Status status = Comp_CheckWritable(comp);

    if (status != SPL_OK)
    {
        return status;
    }
    comp->CSR = comp->CSR | COMP_CSR_EN;
    return SPL_OK;
}

SPL_Status COMP_Disable(COMP_TypeDef *comp)
{
    SPL_Status status = Comp_CheckWritable(comp);

    if (status != SPL_OK)
    {
        return status;
    }
    comp->CSR = Comp_ClearBits(comp->CSR, COMP_CSR_EN);
    return SPL_OK;
}

SPL_Status COMP_GetOutput(const COMP_TypeDef *comp, uint8_t *level)
{
    uint32_t csr;

    SPL_REQUIRE(Comp_Valid(comp) != 0U, SPL_ERR_PARAM); /* arbitrary address */
    SPL_REQUIRE(level != NULL, SPL_ERR_PARAM);
    csr = comp->CSR;
    SPL_REQUIRE((csr & COMP_CSR_EN) != 0U, SPL_ERR_STATE); /* VALUE meaningless */
    *level = ((csr & COMP_CSR_VALUE) != 0U) ? 1U : 0U;
    return SPL_OK;
}

SPL_Status COMP_Lock(COMP_TypeDef *comp)
{
    SPL_Status status = Comp_CheckWritable(comp);

    if (status != SPL_OK)
    {
        return status;
    }
    /* VALUE is read-only; writing back its read value has no effect. */
    comp->CSR = comp->CSR | COMP_CSR_LOCK;
    return SPL_OK;
}

SPL_Status COMP_GetLockState(const COMP_TypeDef *comp, uint8_t *locked)
{
    SPL_REQUIRE(Comp_Valid(comp) != 0U, SPL_ERR_PARAM); /* arbitrary address */
    SPL_REQUIRE(locked != NULL, SPL_ERR_PARAM);
    *locked = Comp_Locked(comp);
    return SPL_OK;
}

#else
/* ISO C forbids an empty translation unit. */
typedef int stm32g0_comp_absent;
#endif /* SPL_G0_HAS_COMP */
