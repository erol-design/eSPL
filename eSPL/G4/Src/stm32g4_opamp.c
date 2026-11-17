#define SPL_LIBRARY_BUILD 1
#include "stm32g4_opamp.h"
#include <stddef.h>

#if defined(OPAMP1)

/* VM_SEL encodings (RM0440 25.5.1 bits 6:5). */
#define OPAMP_VM_SEL_PGA 2U
#define OPAMP_VM_SEL_FOLLOWER 3U

/* CALSEL encodings (RM0440 25.5.1 bits 13:12). */
#define OPAMP_CALSEL_P 1U
#define OPAMP_CALSEL_N 3U

#define OPAMP_PGA_TOPOLOGY_POS 3U

/* Fields kept by OPAMP_Config: the user trim state survives reconfiguration. */
#define OPAMP_TRIM_FIELDS                                                                          \
    (OPAMP_CSR_USERTRIM | OPAMP_CSR_TRIMOFFSETP_Msk | OPAMP_CSR_TRIMOFFSETN_Msk)

static uint8_t Opamp_Valid(const OPAMP_TypeDef *opamp)
{
    uint8_t valid = (opamp == OPAMP1) ? 1U : 0U;
#if defined(OPAMP2)
    valid = ((valid != 0U) || (opamp == OPAMP2)) ? 1U : 0U;
#endif
#if defined(OPAMP3)
    valid = ((valid != 0U) || (opamp == OPAMP3)) ? 1U : 0U;
#endif
#if defined(OPAMP4)
    valid = ((valid != 0U) || (opamp == OPAMP4)) ? 1U : 0U;
#endif
#if defined(OPAMP5)
    valid = ((valid != 0U) || (opamp == OPAMP5)) ? 1U : 0U;
#endif
#if defined(OPAMP6)
    valid = ((valid != 0U) || (opamp == OPAMP6)) ? 1U : 0U;
#endif
    return valid;
}

/* Clears bits through a uint32_t parameter (no cast of a composite mask). */
static uint32_t Opamp_ClearBits(uint32_t value, uint32_t bits)
{
    return value & ~bits;
}

/* Instance valid and CSR writable. */
static SPL_Status Opamp_CheckWritable(const OPAMP_TypeDef *opamp)
{
    /* Reject an invalid peripheral address before reading CSR. */
    SPL_REQUIRE(Opamp_Valid(opamp) != 0U, SPL_ERR_PARAM);
    if ((opamp->CSR & OPAMP_CSR_LOCK) != 0U)
    {
        return SPL_ERR_STATE;
    }
    return SPL_OK;
}

static uint8_t Opamp_Calibrating(const OPAMP_TypeDef *opamp)
{
    return ((opamp->CSR & OPAMP_CSR_CALON) != 0U) ? 1U : 0U;
}

static uint32_t Opamp_CalSel(uint32_t pair)
{
    uint32_t calsel = OPAMP_CALSEL_P;

    if (pair == OPAMP_CAL_PAIR_N)
    {
        calsel = OPAMP_CALSEL_N;
    }
    return calsel << OPAMP_CSR_CALSEL_Pos;
}

/* Returns 1 when the mode-specific fields are consistent. */
static uint8_t Opamp_ModeFieldsValid(const OPAMP_ConfigTypeDef *config)
{
    uint8_t valid = 0U;

    if (config->mode == OPAMP_MODE_STANDALONE)
    {
        if ((config->vm_sel <= OPAMP_VM_SEL_MAX) && (config->pga_gain == 0U) &&
            (config->pga_topology == 0U))
        {
            valid = 1U;
        }
    }
    else if (config->mode == OPAMP_MODE_FOLLOWER)
    {
        if ((config->vm_sel == 0U) && (config->pga_gain == 0U) && (config->pga_topology == 0U))
        {
            valid = 1U;
        }
    }
    else if (config->mode == OPAMP_MODE_PGA)
    {
        if ((config->vm_sel == 0U) && (config->pga_gain <= OPAMP_PGA_GAIN_64) &&
            (config->pga_topology <= OPAMP_PGA_VINM0_BIAS_VINM1_FILTER))
        {
            valid = 1U;
        }
    }
    else
    {
        valid = 0U;
    }
    return valid;
}

SPL_Status OPAMP_Config(OPAMP_TypeDef *opamp, const OPAMP_ConfigTypeDef *config)
{
    SPL_Status status;
    uint32_t csr;
    uint32_t vm_sel;
    uint32_t pga_code = 0U;

    /* Check pointer validity before using the configuration structure. */
    SPL_REQUIRE(Opamp_Valid(opamp) != 0U, SPL_ERR_PARAM);
    SPL_REQUIRE(config != NULL, SPL_ERR_PARAM);
    /* Invalid field encodings can modify unrelated CSR bits. */
    SPL_REQUIRE(config->vp_sel <= OPAMP_VP_SEL_MAX, SPL_ERR_PARAM);
    SPL_REQUIRE(config->internal_output <= 1U, SPL_ERR_PARAM);
    SPL_REQUIRE(config->high_speed <= 1U, SPL_ERR_PARAM);
    SPL_REQUIRE(Opamp_ModeFieldsValid(config) != 0U, SPL_ERR_PARAM);
    status = Opamp_CheckWritable(opamp);
    if (status != SPL_OK)
    {
        return status;
    }
    /* RM0440 25.3.2: change the configuration before setting OPAEN; OPAHSM only with the
     * amplifier disabled (25.5.1 bit 7). */
    if ((opamp->CSR & OPAMP_CSR_OPAMPxEN) != 0U)
    {
        return SPL_ERR_STATE;
    }

    if (config->mode == OPAMP_MODE_FOLLOWER)
    {
        vm_sel = OPAMP_VM_SEL_FOLLOWER;
    }
    else if (config->mode == OPAMP_MODE_PGA)
    {
        vm_sel = OPAMP_VM_SEL_PGA;
        pga_code =
            ((uint32_t)config->pga_topology << OPAMP_PGA_TOPOLOGY_POS) | (uint32_t)config->pga_gain;
    }
    else
    {
        vm_sel = (uint32_t)config->vm_sel;
    }

    csr = opamp->CSR & OPAMP_TRIM_FIELDS;
    csr |= ((uint32_t)config->vp_sel << OPAMP_CSR_VPSEL_Pos) | (vm_sel << OPAMP_CSR_VMSEL_Pos) |
           (pga_code << OPAMP_CSR_PGGAIN_Pos);
    if (config->internal_output != 0U)
    {
        csr |= OPAMP_CSR_OPAMPINTEN;
    }
    if (config->high_speed != 0U)
    {
        csr |= OPAMP_CSR_HIGHSPEEDEN;
    }
    opamp->CSR = csr;
    return SPL_OK;
}

SPL_Status OPAMP_Enable(OPAMP_TypeDef *opamp)
{
    SPL_Status status = Opamp_CheckWritable(opamp);

    if (status != SPL_OK)
    {
        return status;
    }
    opamp->CSR = opamp->CSR | OPAMP_CSR_OPAMPxEN;
    return SPL_OK;
}

SPL_Status OPAMP_Disable(OPAMP_TypeDef *opamp)
{
    SPL_Status status = Opamp_CheckWritable(opamp);

    if (status != SPL_OK)
    {
        return status;
    }
    if (Opamp_Calibrating(opamp) != 0U)
    {
        return SPL_ERR_STATE;
    }
    opamp->CSR = Opamp_ClearBits(opamp->CSR, OPAMP_CSR_OPAMPxEN);
    return SPL_OK;
}

SPL_Status OPAMP_SetUserTrim(OPAMP_TypeDef *opamp, uint32_t trim_p, uint32_t trim_n)
{
    SPL_Status status;
    uint32_t csr;

    SPL_REQUIRE(Opamp_Valid(opamp) != 0U, SPL_ERR_PARAM);
    /* An oversized trim spills into adjacent CSR fields. */
    SPL_REQUIRE(trim_p <= OPAMP_TRIM_MAX, SPL_ERR_PARAM);
    SPL_REQUIRE(trim_n <= OPAMP_TRIM_MAX, SPL_ERR_PARAM);
    status = Opamp_CheckWritable(opamp);
    if (status != SPL_OK)
    {
        return status;
    }
    csr = opamp->CSR;
    /* RM0440 25.5.1: the trim fields can be modified only when USERTRIM = 1. */
    if ((csr & OPAMP_CSR_USERTRIM) == 0U)
    {
        csr |= OPAMP_CSR_USERTRIM;
        opamp->CSR = csr;
    }
    csr = Opamp_ClearBits(csr, OPAMP_CSR_TRIMOFFSETP_Msk | OPAMP_CSR_TRIMOFFSETN_Msk);
    csr |= (trim_p << OPAMP_CSR_TRIMOFFSETP_Pos) | (trim_n << OPAMP_CSR_TRIMOFFSETN_Pos);
    opamp->CSR = csr;
    return SPL_OK;
}

SPL_Status OPAMP_GetTrim(const OPAMP_TypeDef *opamp, uint32_t *trim_p, uint32_t *trim_n)
{
    uint32_t csr;

    SPL_REQUIRE(Opamp_Valid(opamp) != 0U, SPL_ERR_PARAM);
    SPL_REQUIRE(trim_p != NULL, SPL_ERR_PARAM);
    SPL_REQUIRE(trim_n != NULL, SPL_ERR_PARAM);
    csr = opamp->CSR;
    *trim_p = (csr & OPAMP_CSR_TRIMOFFSETP_Msk) >> OPAMP_CSR_TRIMOFFSETP_Pos;
    *trim_n = (csr & OPAMP_CSR_TRIMOFFSETN_Msk) >> OPAMP_CSR_TRIMOFFSETN_Pos;
    return SPL_OK;
}

SPL_Status OPAMP_UseFactoryTrim(OPAMP_TypeDef *opamp)
{
    SPL_Status status = Opamp_CheckWritable(opamp);

    if (status != SPL_OK)
    {
        return status;
    }
    if (Opamp_Calibrating(opamp) != 0U)
    {
        return SPL_ERR_STATE;
    }
    opamp->CSR = Opamp_ClearBits(opamp->CSR, OPAMP_CSR_USERTRIM);
    return SPL_OK;
}

SPL_Status OPAMP_CalibrationEnter(OPAMP_TypeDef *opamp, uint32_t pair)
{
    SPL_Status status;
    uint32_t csr;

    SPL_REQUIRE(Opamp_Valid(opamp) != 0U, SPL_ERR_PARAM);
    SPL_REQUIRE(pair <= OPAMP_CAL_PAIR_N, SPL_ERR_PARAM);
    status = Opamp_CheckWritable(opamp);
    if (status != SPL_OK)
    {
        return status;
    }
    csr = opamp->CSR;
    /* RM0440 25.3.8 step 1: the amplifier is enabled before calibration. */
    if (((csr & OPAMP_CSR_OPAMPxEN) == 0U) || ((csr & OPAMP_CSR_CALON) != 0U))
    {
        return SPL_ERR_STATE;
    }
    /* Step 2: USERTRIM; step 3: calibration selection, then calibration mode on. */
    csr = Opamp_ClearBits(csr, OPAMP_CSR_CALSEL_Msk) | OPAMP_CSR_USERTRIM | Opamp_CalSel(pair);
    opamp->CSR = csr;
    opamp->CSR = csr | OPAMP_CSR_CALON;
    return SPL_OK;
}

SPL_Status OPAMP_CalibrationSelect(OPAMP_TypeDef *opamp, uint32_t pair)
{
    SPL_Status status;

    SPL_REQUIRE(Opamp_Valid(opamp) != 0U, SPL_ERR_PARAM);
    SPL_REQUIRE(pair <= OPAMP_CAL_PAIR_N, SPL_ERR_PARAM);
    status = Opamp_CheckWritable(opamp);
    if (status != SPL_OK)
    {
        return status;
    }
    if (Opamp_Calibrating(opamp) == 0U)
    {
        return SPL_ERR_STATE;
    }
    opamp->CSR = Opamp_ClearBits(opamp->CSR, OPAMP_CSR_CALSEL_Msk) | Opamp_CalSel(pair);
    return SPL_OK;
}

SPL_Status OPAMP_CalibrationOutput(const OPAMP_TypeDef *opamp, uint8_t *calout)
{
    uint32_t csr;

    SPL_REQUIRE(Opamp_Valid(opamp) != 0U, SPL_ERR_PARAM);
    SPL_REQUIRE(calout != NULL, SPL_ERR_PARAM);
    csr = opamp->CSR;
    if ((csr & OPAMP_CSR_CALON) == 0U)
    {
        return SPL_ERR_STATE;
    }
    *calout = ((csr & OPAMP_CSR_OUTCAL) != 0U) ? 1U : 0U;
    return SPL_OK;
}

SPL_Status OPAMP_CalibrationExit(OPAMP_TypeDef *opamp)
{
    SPL_Status status = Opamp_CheckWritable(opamp);

    if (status != SPL_OK)
    {
        return status;
    }
    if (Opamp_Calibrating(opamp) == 0U)
    {
        return SPL_ERR_STATE;
    }
    opamp->CSR = Opamp_ClearBits(opamp->CSR, OPAMP_CSR_CALON | OPAMP_CSR_CALSEL_Msk);
    return SPL_OK;
}

SPL_Status OPAMP_Lock(OPAMP_TypeDef *opamp)
{
    SPL_Status status = Opamp_CheckWritable(opamp);

    if (status != SPL_OK)
    {
        return status;
    }
    /* Locking in calibration mode would freeze the inputs disconnected. */
    if (Opamp_Calibrating(opamp) != 0U)
    {
        return SPL_ERR_STATE;
    }
    opamp->CSR = opamp->CSR | OPAMP_CSR_LOCK;
    return SPL_OK;
}

SPL_Status OPAMP_GetLockState(const OPAMP_TypeDef *opamp, uint8_t *locked)
{
    SPL_REQUIRE(Opamp_Valid(opamp) != 0U, SPL_ERR_PARAM);
    SPL_REQUIRE(locked != NULL, SPL_ERR_PARAM);
    *locked = ((opamp->CSR & OPAMP_CSR_LOCK) != 0U) ? 1U : 0U;
    return SPL_OK;
}

#else
/* No OPAMP on this SKU (e.g. G414): keep the translation unit non-empty (ISO C). */
typedef int Opamp_NotPresent;
#endif /* OPAMP1 */
