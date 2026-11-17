/* SPDX-License-Identifier: MIT */
/* Analog comparator driver (RM0440 Rev 9 section 24). Variants: COMP4..COMP7 and the TIM20
 * blanking source are compiled only where the CMSIS header defines them. Public contract:
 * stm32g4_comp.h, Docs/v0.11_COMP_CONTRACT.md. */
#define SPL_LIBRARY_BUILD 1
#include "stm32g4_comp.h"
#include <stddef.h>

static uint8_t Comp_Valid(const COMP_TypeDef *comp)
{
    uint8_t valid = ((comp == COMP1) || (comp == COMP2) || (comp == COMP3)) ? 1U : 0U;
#if defined(COMP4)
    valid = ((valid != 0U) || (comp == COMP4)) ? 1U : 0U;
#endif
#if defined(COMP5)
    valid = ((valid != 0U) || (comp == COMP5)) ? 1U : 0U;
#endif
#if defined(COMP6)
    valid = ((valid != 0U) || (comp == COMP6)) ? 1U : 0U;
#endif
#if defined(COMP7)
    valid = ((valid != 0U) || (comp == COMP7)) ? 1U : 0U;
#endif
    return valid;
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
    /* Reject an invalid peripheral address before reading CSR. */
    SPL_REQUIRE(Comp_Valid(comp) != 0U, SPL_ERR_PARAM);
    if (Comp_Locked(comp) != 0U)
    {
        return SPL_ERR_STATE;
    }
    return SPL_OK;
}

SPL_Status COMP_Config(COMP_TypeDef *comp, const COMP_ConfigTypeDef *config)
{
    SPL_Status status;
    uint32_t csr;

    /* An invalid CSR address or NULL configuration can cause silent reads. */
    SPL_REQUIRE(Comp_Valid(comp) != 0U, SPL_ERR_PARAM);
    SPL_REQUIRE(config != NULL, SPL_ERR_PARAM);
    /* Out-of-range fields may modify adjacent CSR bit fields. */
    SPL_REQUIRE(config->inp_sel <= COMP_INP_SEL_MAX, SPL_ERR_PARAM);
    SPL_REQUIRE(config->inm_sel <= COMP_INM_SEL_MAX, SPL_ERR_PARAM);
    SPL_REQUIRE(config->hysteresis <= COMP_HYST_MAX, SPL_ERR_PARAM);
    SPL_REQUIRE(config->polarity <= COMP_POL_INVERTED, SPL_ERR_PARAM);
    SPL_REQUIRE(config->blanking <= COMP_BLANK_MAX, SPL_ERR_PARAM);
#if !defined(TIM20)
    /* BLANKSEL 101 = TIM20_OC5 (RM0440 Table 201); TIM20 is absent on this SKU. */
    if (config->blanking == COMP_BLANK_TIM20_OC5)
    {
        return SPL_ERR_UNSUPPORTED;
    }
#endif
    status = Comp_CheckWritable(comp);
    if (status != SPL_OK)
    {
        return status;
    }
    /* Configure with the comparator disabled (caller disables first). */
    if ((comp->CSR & COMP_CSR_EN) != 0U)
    {
        return SPL_ERR_STATE;
    }

    /* Whole-register write: reserved bits 29:24, 14:9, 7, 3:1 and EN/LOCK are written 0. */
    csr = ((uint32_t)config->inm_sel << COMP_CSR_INMSEL_Pos) |
          ((uint32_t)config->hysteresis << COMP_CSR_HYST_Pos) |
          ((uint32_t)config->blanking << COMP_CSR_BLANKING_Pos);
    if (config->inp_sel != 0U)
    {
        csr |= COMP_CSR_INPSEL;
    }
    if (config->polarity != 0U)
    {
        csr |= COMP_CSR_POLARITY;
    }
    /* RM0440 24.6.1: VREFINT inputs need the scaler (SCALEN); the 1/4, 1/2, 3/4 fractions
     * additionally need the resistor bridge (BRGEN), otherwise they receive VREFINT. */
    if (config->inm_sel <= COMP_INM_VREFINT)
    {
        csr |= COMP_CSR_SCALEN;
    }
    if (config->inm_sel < COMP_INM_VREFINT)
    {
        csr |= COMP_CSR_BRGEN;
    }
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

    /* An invalid peripheral or output address can return silent wrong data. */
    SPL_REQUIRE(Comp_Valid(comp) != 0U, SPL_ERR_PARAM);
    SPL_REQUIRE(level != NULL, SPL_ERR_PARAM);
    csr = comp->CSR;
    if ((csr & COMP_CSR_EN) == 0U)
    {
        return SPL_ERR_STATE;
    }
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
    /* Invalid addresses can cause silent wrong status observations. */
    SPL_REQUIRE(Comp_Valid(comp) != 0U, SPL_ERR_PARAM);
    SPL_REQUIRE(locked != NULL, SPL_ERR_PARAM);
    *locked = Comp_Locked(comp);
    return SPL_OK;
}
