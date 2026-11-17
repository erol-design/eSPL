/* CRC calculation unit driver implementation (RM0444 Rev 6 section 14); public contract in
 * stm32g0_crc.h. Ported from STM32G4 SPL v0.13.3 (Autometry_Mcu_Spl 130de7a) and reviewed against
 * RM0444 Rev 6: same IP, register layout and rules; only references changed (v0.3). */
/* Library unit: this unit also emits the spl_check.h mismatch-guard reference; the only unit
 * that does not is stm32g0_check.c (SPL_CHECK_LEVEL_DEFINER). BULGU-02, CHECK_LEVEL.md 3.2. */
#define SPL_LIBRARY_BUILD 1
#include "stm32g0_crc.h"

#define CRC_CR_CONFIG_BITS (CRC_CR_REV_OUT | CRC_CR_REV_IN | CRC_CR_POLYSIZE)

/* Check level (A4 wave 2, Docs/CHECK_LEVEL.md 5.4 CRC table): SPL_REQUIRE = checked at FULL only
 * (not compiled at FAST). All macros come before the first MMIO write (METHOD 1.3). */

static uint32_t Crc_SizeMask(uint32_t polysize)
{
    uint32_t mask;

    switch (polysize)
    {
    case CRC_POLYSIZE_16:
        mask = 0x0000FFFFUL;
        break;
    case CRC_POLYSIZE_8:
        mask = 0x000000FFUL;
        break;
    case CRC_POLYSIZE_7:
        mask = 0x0000007FUL;
        break;
    default:
        mask = 0xFFFFFFFFUL;
        break;
    }
    return mask;
}

SPL_Status CRC_Config(const CRC_ConfigTypeDef *config)
{
    uint32_t mask;
    volatile uint32_t discard;

    SPL_REQUIRE(config != (const CRC_ConfigTypeDef *)0, SPL_ERR_PARAM);
    /* overflow into a neighbouring CR field. */
    SPL_REQUIRE((config->polysize <= CRC_POLYSIZE_7) && (config->rev_in <= CRC_REV_IN_WORD) &&
                    (config->rev_out <= 1U),
                SPL_ERR_PARAM);
    /* RM0444 14.3.3: even polynomials are not supported. silently wrong CRC. */
    mask = Crc_SizeMask(config->polysize);
    SPL_REQUIRE(((config->polynomial & 1U) != 0U) && ((config->polynomial & ~mask) == 0U) &&
                    ((config->init & ~mask) == 0U),
                SPL_ERR_PARAM);

    /* RM0444 14.3.3: the polynomial must not change during a computation;
     * a DR read ends any pending one. */
    discard = CRC->DR;
    (void)discard;
    CRC->POL = config->polynomial;
    CRC->CR = (config->polysize << CRC_CR_POLYSIZE_Pos) | (config->rev_in << CRC_CR_REV_IN_Pos) |
              (config->rev_out << CRC_CR_REV_OUT_Pos);
    /* RM0444 14.3.3: a CRC_INIT write also initializes CRC_DR. */
    CRC->INIT = config->init;
    return SPL_OK;
}

SPL_Status CRC_Reset(void)
{
    /* RESET is set-only and cleared by hardware; only configuration bits
     * are carried over. */
    CRC->CR = (CRC->CR & CRC_CR_CONFIG_BITS) | CRC_CR_RESET;
    return SPL_OK;
}

SPL_Status CRC_Feed8(uint8_t data)
{
    /* RM0444 14.3.3: right-aligned byte access (offset 0, little endian). */
    *(volatile uint8_t *)(volatile void *)&CRC->DR = data;
    return SPL_OK;
}

SPL_Status CRC_Feed16(uint16_t data)
{
    /* RM0444 14.3.3: right-aligned half-word access. */
    *(volatile uint16_t *)(volatile void *)&CRC->DR = data;
    return SPL_OK;
}

SPL_Status CRC_Feed32(uint32_t data)
{
    CRC->DR = data;
    return SPL_OK;
}

SPL_Status CRC_GetResult(uint32_t *result)
{
    uint32_t polysize;

    SPL_REQUIRE(result != (uint32_t *)0, SPL_ERR_PARAM);
    polysize = (CRC->CR & CRC_CR_POLYSIZE) >> CRC_CR_POLYSIZE_Pos;
    *result = CRC->DR & Crc_SizeMask(polysize);
    return SPL_OK;
}
