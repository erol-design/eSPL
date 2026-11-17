/* SPDX-License-Identifier: MIT */
#define SPL_LIBRARY_BUILD 1
#include "stm32g4_crc.h"

/* CRC calculation unit driver (RM0440 Rev 9 section 16). Same register layout on every
 * supported G4 SKU (single instance), no variant code. Public contract: stm32g4_crc.h,
 * Docs/v0.11_CRC_CONTRACT.md. */

#define CRC_CR_CONFIG_BITS (CRC_CR_REV_OUT | CRC_CR_REV_IN | CRC_CR_POLYSIZE)

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

    /* NULL may reference the address-zero memory alias; never dereference it. */
    SPL_REQUIRE(config != (const CRC_ConfigTypeDef *)0, SPL_ERR_PARAM);
    /* Unsupported polynomial size changes CRC interpretation. */
    SPL_REQUIRE(config->polysize <= CRC_POLYSIZE_7, SPL_ERR_PARAM);
    /* Invalid input bit reversal changes the computed checksum. */
    SPL_REQUIRE(config->rev_in <= CRC_REV_IN_WORD, SPL_ERR_PARAM);
    /* Invalid output reversal may set unintended CR bits. */
    SPL_REQUIRE(config->rev_out <= 1U, SPL_ERR_PARAM);
    /* RM0440 16.3.3: even polynomials are not supported. */
    mask = Crc_SizeMask(config->polysize);
    /* Even polynomial violates CRC unit constraints. */
    SPL_REQUIRE((config->polynomial & 1U) != 0U, SPL_ERR_PARAM);
    /* Polynomial must fit the selected width. */
    SPL_REQUIRE((config->polynomial & ~mask) == 0U, SPL_ERR_PARAM);
    /* Truncated initial value changes CRC results silently. */
    SPL_REQUIRE((config->init & ~mask) == 0U, SPL_ERR_PARAM);

    /* RM0440 16.3.3: the polynomial must not change during a computation;
     * a DR read ends any pending one. */
    discard = CRC->DR;
    (void)discard;
    CRC->POL = config->polynomial;
    CRC->CR = (config->polysize << CRC_CR_POLYSIZE_Pos) | (config->rev_in << CRC_CR_REV_IN_Pos) |
              (config->rev_out << CRC_CR_REV_OUT_Pos);
    /* RM0440 16.3.3: a CRC_INIT write also initializes CRC_DR. */
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
    /* RM0440 16.4.1: right-aligned byte access (offset 0, little endian). */
    *(volatile uint8_t *)(volatile void *)&CRC->DR = data;
    return SPL_OK;
}

SPL_Status CRC_Feed16(uint16_t data)
{
    /* RM0440 16.4.1: right-aligned half-word access. */
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

    /* NULL output may fault or overwrite mapped address-zero memory. */
    SPL_REQUIRE(result != (uint32_t *)0, SPL_ERR_PARAM);
    polysize = (CRC->CR & CRC_CR_POLYSIZE) >> CRC_CR_POLYSIZE_Pos;
    *result = CRC->DR & Crc_SizeMask(polysize);
    return SPL_OK;
}
