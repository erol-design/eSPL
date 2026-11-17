/* SPDX-License-Identifier: MIT */
/* SPI driver (SPI mode). Reviewed against RM0440 Rev 9 section 42. SPI3 / SPI4 are compiled
 * only where CMSIS defines them. Public contract: stm32g4_spi.h. */
#include "stm32g4_spi.h"
#include <stddef.h>

/* DS field value 7 = 8-bit frame; values >= 8 are 9..16-bit frames
 * (RM0440 42.9.2). "Not used" values 0..2 are forced to 8 bit by hardware. */
#define SPI_DS_8BIT_FIELD 7U

/* 16-bit DR access (data packing: the access width sets the number of bytes sent to the FIFO).
 * CMSIS defines DR as uint32_t; *(uint16_t *)&spi->DR gives a -Wstrict-aliasing warning on GCC
 * (an intermediate uintptr_t cast is not enough either, GCC tracks the object). The address is
 * built as an integer from base + offsetof: the AC6 object is byte-identical, no GCC warning.
 * 8-bit access (character type) is not affected by the rule and is unchanged. */
#define SPI_DR16(spi) (*(__IO uint16_t *)((uintptr_t)(spi) + offsetof(SPI_TypeDef, DR)))

/* All defined SPIx_SR bits (RM0440 42.9.3); only CRCERR is writable. */
#define SPI_SR_DEFINED                                                                             \
    (SPI_SR_FTLVL | SPI_SR_FRLVL | SPI_SR_FRE | SPI_SR_BSY | SPI_SR_OVR | SPI_SR_MODF |            \
     SPI_SR_CRCERR | SPI_SR_UDR | SPI_SR_CHSIDE | SPI_SR_TXE | SPI_SR_RXNE)

static __attribute__((unused)) uint8_t Spi_Valid(const SPI_TypeDef *spi)
{
    uint8_t valid = (uint8_t)(((spi == SPI1) || (spi == SPI2)) ? 1U : 0U);
#if defined(SPI3)
    valid = (uint8_t)(((valid != 0U) || (spi == SPI3)) ? 1U : 0U);
#endif
#if defined(SPI4)
    valid = (uint8_t)(((valid != 0U) || (spi == SPI4)) ? 1U : 0U);
#endif
    return valid;
}

/* Nonzero when the configured frame is wider than 8 bits (16-bit DR access). */
static uint8_t Spi_IsWide(const SPI_TypeDef *spi)
{
    uint32_t ds = (spi->CR2 & SPI_CR2_DS) >> SPI_CR2_DS_Pos;
    return (uint8_t)((ds > SPI_DS_8BIT_FIELD) ? 1U : 0U);
}

SPL_Status SPI_MasterInit(SPI_TypeDef *spi, const SPI_InitTypeDef *cfg)
{
    uint32_t cr1;

    SPL_REQUIRE(!((Spi_Valid(spi) == 0U)),
                SPL_ERR_PARAM); /* Invalid peripheral address could access unrelated MMIO */
    SPL_REQUIRE(!((cfg == (const SPI_InitTypeDef *)0)),
                SPL_ERR_PARAM); /* NULL access does not provide a recoverable API error */
    SPL_REQUIRE(!((cfg->prescaler > 7U)),
                SPL_ERR_PARAM); /* Invalid baud-rate field or configuration encoding */
    SPL_REQUIRE(!((cfg->cpol > 1U)),
                SPL_ERR_PARAM); /* Invalid mode field must not be silently coerced */
    SPL_REQUIRE(!((cfg->cpha > 1U)),
                SPL_ERR_PARAM); /* Invalid mode field must not be silently coerced */
    SPL_REQUIRE(!((cfg->lsb_first > 1U)),
                SPL_ERR_PARAM); /* Invalid mode field must not be silently coerced */
    if ((spi->CR1 & SPI_CR1_SPE) != 0U)
    {
        return SPL_ERR_STATE;
    }
    /* v0.11.1: hardware refuses MSTR while MODF=1 (RM0440 42.5.11); report it like
     * SPI_Init and SPI_Enable instead of returning OK for an ignored write. */
    if ((spi->SR & SPI_SR_MODF) != 0U)
    {
        return SPL_ERR_HW;
    }

    cr1 = SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI | (cfg->prescaler << SPI_CR1_BR_Pos);
    if (cfg->cpol != 0U)
    {
        cr1 |= SPI_CR1_CPOL;
    }
    if (cfg->cpha != 0U)
    {
        cr1 |= SPI_CR1_CPHA;
    }
    if (cfg->lsb_first != 0U)
    {
        cr1 |= SPI_CR1_LSBFIRST;
    }

    spi->CR1 = cr1;
    /* Eight-bit frame with RXNE asserted as soon as one byte is ready. */
    spi->CR2 = (7UL << SPI_CR2_DS_Pos) | SPI_CR2_FRXTH;
    return SPL_OK;
}

SPL_Status SPI_Enable(SPI_TypeDef *spi, uint8_t enable)
{
    SPL_REQUIRE(!((Spi_Valid(spi) == 0U)),
                SPL_ERR_PARAM); /* Invalid peripheral address could access unrelated MMIO */
    SPL_REQUIRE(!((enable > 1U)), SPL_ERR_PARAM); /* Nonzero boolean acts as enable */

    if (enable != 0U)
    {
        /* Hardware does not allow SPE=1 while MODF=1 (RM0440 42.5.11).
         * Report it instead of a silent no-op; recover with
         * SPI_ClearModeFault and a new init. */
        if ((spi->SR & SPI_SR_MODF) != 0U)
        {
            return SPL_ERR_HW;
        }
        spi->CR1 |= SPI_CR1_SPE;
    }
    else
    {
        /* Do not drop SPE during an active transmission.
         * FTLVL occupies SR[12:11]. The transaction is complete only
         * when TX FIFO is empty and BSY has cleared. */
        uint32_t sr = spi->SR;
        uint8_t slave = (uint8_t)(((spi->CR1 & SPI_CR1_MSTR) == 0U) ? 1U : 0U);

        /* ERRATA ES0431 2.14.2 / ES0430 2.18.2 / ES0523 2.14.2: slave BSY may stay high after the
         * last frame; a slave (FTLVL = 0) disables without the BSY gate. */
        if (((sr & SPI_SR_BSY) != 0U) && ((slave == 0U) || ((sr & SPI_SR_FTLVL) != 0U)))
        {
            return SPL_BUSY;
        }
        /* Slave: TX FIFO data leaves only when the master clocks it out. With no frame in
         * progress the master may have stopped for good (RM0440 42.5.9 disable procedure,
         * slave BSY note): retrying cannot help. Recover with an RCC peripheral reset
         * (RCC_AssertPeriphReset / RCC_ReleasePeriphReset), then init again. A master
         * empties its own FIFO: BUSY. */
        if ((sr & SPI_SR_FTLVL) != 0U)
        {
            if (slave != 0U)
            {
                return SPL_ERR_STATE;
            }
            return SPL_BUSY;
        }
        spi->CR1 &= ~SPI_CR1_SPE;
        /* RX FIFO should be drained by caller before next enable.
         * No implicit DR reads: preserving received data is intentional. */
    }
    return SPL_OK;
}

SPL_Status SPI_TryWrite8(SPI_TypeDef *spi, uint8_t data)
{
    SPL_REQUIRE(!(Spi_Valid(spi) == 0U),
                SPL_ERR_PARAM); /* Invalid peripheral address could access unrelated MMIO */
    if (((spi->CR1 & SPI_CR1_SPE) == 0U) || (Spi_IsWide(spi) != 0U))
    {
        return SPL_ERR_STATE;
    }
    if ((spi->SR & SPI_SR_TXE) == 0U)
    {
        return SPL_WOULD_BLOCK;
    }

    /* Explicit byte-wide DR access prevents unintended 16-bit framing. */
    *(__IO uint8_t *)&spi->DR = data;
    return SPL_OK;
}

SPL_Status SPI_TryRead8(SPI_TypeDef *spi, uint8_t *data)
{
    SPL_REQUIRE(!((Spi_Valid(spi) == 0U)),
                SPL_ERR_PARAM); /* Invalid peripheral address could access unrelated MMIO */
    SPL_REQUIRE(!((data == (uint8_t *)0)),
                SPL_ERR_PARAM); /* NULL access does not provide a recoverable API error */
    /* An 8-bit read of a 9..16-bit frame breaks FIFO alignment. */
    if (Spi_IsWide(spi) != 0U)
    {
        return SPL_ERR_STATE;
    }
    /* RX FIFO may still hold data after SPE has been cleared.
     * Draining that data is a required part of normal shutdown.
     * CRCERR (CRC enabled; CCL-2 SPI-10d) is reported like OVR: the flag is kept. */
    if ((spi->SR & (SPI_SR_OVR | SPI_SR_CRCERR)) != 0U)
    {
        return SPL_ERR_HW;
    }
    if ((spi->SR & SPI_SR_RXNE) == 0U)
    {
        /* No new receive data can arrive while SPI is disabled.
         * WOULD_BLOCK would incorrectly invite endless polling. */
        if ((spi->CR1 & SPI_CR1_SPE) == 0U)
        {
            return SPL_ERR_STATE;
        }

        return SPL_WOULD_BLOCK;
    }

    /* Receive FIFO may be drained even after clearing SPE. */
    *data = *(__IO uint8_t *)&spi->DR;
    return SPL_OK;
}

SPL_Status SPI_Status(SPI_TypeDef *spi, uint32_t *status)
{
    SPL_REQUIRE(!((Spi_Valid(spi) == 0U)),
                SPL_ERR_PARAM); /* Invalid peripheral address could access unrelated MMIO */
    SPL_REQUIRE(!((status == (uint32_t *)0)),
                SPL_ERR_PARAM); /* NULL access does not provide a recoverable API error */

    *status = spi->SR;
    return SPL_OK;
}

/* STM32G4 SPI overrun recovery: read DR, then SR in this order.
 * Calling this function consumes one unread DR value. Other pending RX
 * bytes must be handled by the application; no transparent recovery is
 * promised for a discarded/overrun transfer.
 * The DR/SR reads are deliberately volatile and ordered. */
SPL_Status SPI_ClearOverrun(SPI_TypeDef *spi)
{
    volatile uint16_t discarded;
    volatile uint32_t status_snapshot;

    SPL_REQUIRE(!(Spi_Valid(spi) == 0U),
                SPL_ERR_PARAM); /* Invalid peripheral address could access unrelated MMIO */

    if ((spi->SR & SPI_SR_OVR) == 0U)
    {
        return SPL_OK;
    }

    /* DR read width follows the frame size (RM0440 42.5.9 alignment). */
    if (Spi_IsWide(spi) != 0U)
    {
        discarded = SPI_DR16(spi);
    }
    else
    {
        discarded = *(__IO uint8_t *)&spi->DR;
    }
    status_snapshot = spi->SR;

    /* Consume snapshots without changing any other registers. */
    (void)discarded;
    (void)status_snapshot;
    return SPL_OK;
}

/* ---- v0.11 ---- */

/* Validates field ranges and RM0440 cross-rules; no register access. */
static __attribute__((unused)) uint8_t Spi_ConfigValid(const SPI_ConfigTypeDef *cfg)
{
    uint8_t master;
    uint8_t ti;

    if ((cfg->mode > SPI_MODE_MASTER) || (cfg->data_bits < 4U) || (cfg->data_bits > 16U) ||
        (cfg->cpol > 1U) || (cfg->cpha > 1U) || (cfg->lsb_first > 1U) || (cfg->prescaler > 7U) ||
        (cfg->nss > SPI_NSS_HARD_OUTPUT) || (cfg->nss_pulse > 1U) ||
        (cfg->frame_format > SPI_FRAME_TI) || (cfg->direction > SPI_DIR_RX_ONLY) ||
        (cfg->crc_enable > 1U))
    {
        return 0U;
    }
    master = (uint8_t)((cfg->mode == SPI_MODE_MASTER) ? 1U : 0U);
    ti = (uint8_t)((cfg->frame_format == SPI_FRAME_TI) ? 1U : 0U);

    /* BR is not used by a Motorola slave (RM0440 42.5.7 note 4). */
    if ((master == 0U) && (ti == 0U) && (cfg->prescaler != 0U))
    {
        return 0U;
    }
    /* SSOE=1 is a master-only configuration (RM0440 42.5.5). */
    if ((master == 0U) && (cfg->nss == SPI_NSS_HARD_OUTPUT))
    {
        return 0U;
    }
    /* NSSP: master, Motorola, CPHA=0 (RM0440 42.5.12, CR2.NSSP); the pulse is
     * produced on the hardware NSS output (SSOE=1, 42.5.5). */
    /* "CRC calculation cannot be used at NSS Pulse mode" (RM0440 42.5.14). */
    if ((cfg->nss_pulse != 0U) && ((master == 0U) || (ti != 0U) || (cfg->cpha != 0U) ||
                                   (cfg->nss != SPI_NSS_HARD_OUTPUT) || (cfg->crc_enable != 0U)))
    {
        return 0U;
    }
    /* TI: SSM/SSI/SSOE and LSBFIRST are not used (RM0440 42.5.13, 42.9.1); keep them at reset
     * values. Full duplex only. CPOL/CPHA: reset values 0/0 without CRC; with CRC they must
     * be kept at CPOL=0, CPHA=1 (RM0440 42.5.14). */
    if ((ti != 0U) && ((cfg->nss != SPI_NSS_HARD_INPUT) || (cfg->lsb_first != 0U) ||
                       (cfg->direction != SPI_DIR_FULL_DUPLEX) || (cfg->cpol != 0U) ||
                       (cfg->cpha != cfg->crc_enable)))
    {
        return 0U;
    }
    /* Receive-only modes: slave only (a master clocks continuously and needs
     * a timed SPE=0, RM0440 42.5.9); no CRC (single-frame RX buffer). */
    if (((cfg->direction == SPI_DIR_HALF_DUPLEX_RX) || (cfg->direction == SPI_DIR_RX_ONLY)) &&
        ((master != 0U) || (cfg->crc_enable != 0U)))
    {
        return 0U;
    }
    if (cfg->crc_enable != 0U)
    {
        /* CRC only for 8/16-bit frames; odd polynomial (RM0440 42.5.14). */
        if (((cfg->data_bits != 8U) && (cfg->data_bits != 16U)) ||
            ((cfg->crc_bits != 8U) && (cfg->crc_bits != 16U)) || (cfg->crc_polynomial > 0xFFFFU) ||
            ((cfg->crc_polynomial & 1U) == 0U))
        {
            return 0U;
        }
    }
    else if ((cfg->crc_bits != 0U) || (cfg->crc_polynomial != 0U))
    {
        return 0U;
    }
    else
    {
        /* CRC disabled and its fields are zero. */
    }
    return 1U;
}

static uint32_t Spi_BuildCr1(const SPI_ConfigTypeDef *cfg)
{
    uint32_t cr1 = cfg->prescaler << SPI_CR1_BR_Pos;

    if (cfg->mode == SPI_MODE_MASTER)
    {
        cr1 |= SPI_CR1_MSTR;
    }
    if (cfg->cpol != 0U)
    {
        cr1 |= SPI_CR1_CPOL;
    }
    if (cfg->cpha != 0U)
    {
        cr1 |= SPI_CR1_CPHA;
    }
    if (cfg->lsb_first != 0U)
    {
        cr1 |= SPI_CR1_LSBFIRST;
    }
    if (cfg->nss == SPI_NSS_SOFT)
    {
        cr1 |= SPI_CR1_SSM;
        /* Master: SSI=1 avoids MODF. Slave: SSI=0 selects the slave. */
        if (cfg->mode == SPI_MODE_MASTER)
        {
            cr1 |= SPI_CR1_SSI;
        }
    }
    if (cfg->direction == SPI_DIR_HALF_DUPLEX_TX)
    {
        cr1 |= SPI_CR1_BIDIMODE | SPI_CR1_BIDIOE;
    }
    else if (cfg->direction == SPI_DIR_HALF_DUPLEX_RX)
    {
        cr1 |= SPI_CR1_BIDIMODE;
    }
    else if (cfg->direction == SPI_DIR_RX_ONLY)
    {
        cr1 |= SPI_CR1_RXONLY;
    }
    else
    {
        /* Full duplex: BIDIMODE=0, RXONLY=0. */
    }
    if (cfg->crc_enable != 0U)
    {
        cr1 |= SPI_CR1_CRCEN;
        if (cfg->crc_bits == 16U)
        {
            cr1 |= SPI_CR1_CRCL;
        }
    }
    return cr1;
}

static uint32_t Spi_BuildCr2(const SPI_ConfigTypeDef *cfg)
{
    uint32_t cr2 = (cfg->data_bits - 1U) << SPI_CR2_DS_Pos;

    /* RXFIFO threshold aligned with the DR read width (RM0440 42.5.7 3e). */
    if (cfg->data_bits <= 8U)
    {
        cr2 |= SPI_CR2_FRXTH;
    }
    if (cfg->nss == SPI_NSS_HARD_OUTPUT)
    {
        cr2 |= SPI_CR2_SSOE;
    }
    if (cfg->nss_pulse != 0U)
    {
        cr2 |= SPI_CR2_NSSP;
    }
    if (cfg->frame_format == SPI_FRAME_TI)
    {
        cr2 |= SPI_CR2_FRF;
    }
    return cr2;
}

SPL_Status SPI_Init(SPI_TypeDef *spi, const SPI_ConfigTypeDef *cfg)
{
    uint32_t cr1;

    SPL_REQUIRE(!((Spi_Valid(spi) == 0U)),
                SPL_ERR_PARAM); /* Invalid peripheral address could access unrelated MMIO */
    SPL_REQUIRE(!((cfg == (const SPI_ConfigTypeDef *)0)),
                SPL_ERR_PARAM); /* NULL access does not provide a recoverable API error */
    SPL_REQUIRE(
        !((Spi_ConfigValid(cfg) == 0U)),
        SPL_ERR_PARAM); /* Invalid cross-field configuration can program unsupported modes */
    if ((spi->CR1 & SPI_CR1_SPE) != 0U)
    {
        return SPL_ERR_STATE;
    }
    /* MSTR cannot be set while MODF=1 (RM0440 42.5.11). */
    if ((spi->SR & SPI_SR_MODF) != 0U)
    {
        return SPL_ERR_HW;
    }

    /* RM0440 42.5.7 order: CR1, CR2, CRCPR. With CRC, CR1 is first written with CRCEN=0 so
     * that CRCEN sees a 0 -> 1 transition and the CRC is reset even when the previous
     * session left CRCEN=1 (RM0440 42.5.14 re-enable sequence; whether writing 1 over 1
     * also resets it is not stated unambiguously, 42.9.6/42.9.7). */
    cr1 = Spi_BuildCr1(cfg);
    if (cfg->crc_enable != 0U)
    {
        spi->CR1 = cr1 & ~SPI_CR1_CRCEN;
    }
    spi->CR1 = cr1;
    spi->CR2 = Spi_BuildCr2(cfg);
    if (cfg->crc_enable != 0U)
    {
        spi->CRCPR = cfg->crc_polynomial;
    }
    return SPL_OK;
}

SPL_Status SPI_TryWrite16(SPI_TypeDef *spi, uint16_t data)
{
    SPL_REQUIRE(!(Spi_Valid(spi) == 0U),
                SPL_ERR_PARAM); /* Invalid peripheral address could access unrelated MMIO */
    if (((spi->CR1 & SPI_CR1_SPE) == 0U) || (Spi_IsWide(spi) == 0U))
    {
        return SPL_ERR_STATE;
    }
    if ((spi->SR & SPI_SR_TXE) == 0U)
    {
        return SPL_WOULD_BLOCK;
    }

    /* Half-word DR access: one frame of 9..16 bits, right-aligned. */
    SPI_DR16(spi) = data;
    return SPL_OK;
}

SPL_Status SPI_TryRead16(SPI_TypeDef *spi, uint16_t *data)
{
    SPL_REQUIRE(!((Spi_Valid(spi) == 0U)),
                SPL_ERR_PARAM); /* Invalid peripheral address could access unrelated MMIO */
    SPL_REQUIRE(!((data == (uint16_t *)0)),
                SPL_ERR_PARAM); /* NULL access does not provide a recoverable API error */
    /* 16-bit reads need a 9..16-bit frame and FRXTH=0 (RM0440 42.9.2). */
    if ((Spi_IsWide(spi) == 0U) || ((spi->CR2 & SPI_CR2_FRXTH) != 0U))
    {
        return SPL_ERR_STATE;
    }
    if ((spi->SR & (SPI_SR_OVR | SPI_SR_CRCERR)) != 0U)
    {
        return SPL_ERR_HW;
    }
    if ((spi->SR & SPI_SR_RXNE) == 0U)
    {
        if ((spi->CR1 & SPI_CR1_SPE) == 0U)
        {
            return SPL_ERR_STATE;
        }
        return SPL_WOULD_BLOCK;
    }

    *data = SPI_DR16(spi);
    return SPL_OK;
}

SPL_Status SPI_ITConfig(SPI_TypeDef *spi, uint32_t it_mask, uint8_t enable)
{
    SPL_REQUIRE(!((Spi_Valid(spi) == 0U)),
                SPL_ERR_PARAM); /* Invalid peripheral address could access unrelated MMIO */
    SPL_REQUIRE(!((it_mask == 0U)), SPL_ERR_PARAM); /* Empty mask has no hardware effect */
    SPL_REQUIRE(!(((it_mask & ~SPI_IT_ALL) != 0U)),
                SPL_ERR_PARAM); /* Invalid mask can affect reserved or unrelated bits */
    SPL_REQUIRE(!((enable > 1U)), SPL_ERR_PARAM); /* Nonzero boolean acts as enable */

    /* CR2 has no write-1-to-clear or self-clearing bits. */
    if (enable != 0U)
    {
        spi->CR2 |= it_mask;
    }
    else
    {
        spi->CR2 &= ~it_mask;
    }
    return SPL_OK;
}

SPL_Status SPI_DmaConfig(SPI_TypeDef *spi, uint32_t dma_mask, uint8_t enable)
{
    const uint32_t valid_mask = SPI_DMA_TX | SPI_DMA_RX;

    SPL_REQUIRE(!((Spi_Valid(spi) == 0U)),
                SPL_ERR_PARAM); /* Invalid peripheral address could access unrelated MMIO */
    SPL_REQUIRE(!((dma_mask == 0U)), SPL_ERR_PARAM); /* Empty mask has no hardware effect */
    SPL_REQUIRE(!(((dma_mask & ~valid_mask) != 0U)),
                SPL_ERR_PARAM); /* Invalid mask can affect reserved or unrelated bits */
    SPL_REQUIRE(!((enable > 1U)), SPL_ERR_PARAM); /* Nonzero boolean acts as enable */

    if (enable != 0U)
    {
        /* A transfer is still in progress (SPE = 1 and BSY = 1): starting the next DMA transfer
         * now would mix the two (CCL-2 SPI-22). BUSY, no write; retry when BSY = 0. With SPE = 0
         * BSY is not checked: it may stay high after the disable (ERRATA ES0431 2.14.1 /
         * ES0430 2.18.1 / ES0523 2.14.1) and must not block the next start. */
        if (((spi->CR1 & SPI_CR1_SPE) != 0U) && ((spi->SR & SPI_SR_BSY) != 0U))
        {
            return SPL_BUSY;
        }
        spi->CR2 |= dma_mask;
    }
    else
    {
        /* Close order: DMA requests are cleared after SPE=0 (RM0440 42.5.9). */
        if ((spi->CR1 & SPI_CR1_SPE) != 0U)
        {
            return SPL_ERR_STATE;
        }
        spi->CR2 &= ~dma_mask;
    }
    return SPL_OK;
}

SPL_Status SPI_DmaPackingConfig(SPI_TypeDef *spi, uint32_t options)
{
    const uint32_t valid_mask = SPI_PACK_RX16 | SPI_PACK_TX_ODD | SPI_PACK_RX_ODD;
    uint32_t cr2;

    SPL_REQUIRE(!((Spi_Valid(spi) == 0U)),
                SPL_ERR_PARAM); /* Invalid peripheral address could access unrelated MMIO */
    SPL_REQUIRE(!(((options & ~valid_mask) != 0U)),
                SPL_ERR_PARAM); /* Invalid mask can affect reserved or unrelated bits */
    SPL_REQUIRE(!((((options & SPI_PACK_RX_ODD) != 0U) && ((options & SPI_PACK_RX16) == 0U))),
                SPL_ERR_PARAM); /* Invalid FIFO packing combination */
    /* LDMA_TX/LDMA_RX must be written with SPE=0 (RM0440 42.9.2). */
    if ((spi->CR1 & SPI_CR1_SPE) != 0U)
    {
        return SPL_ERR_STATE;
    }
    /* Packing exists only for frames of 8 bits or less. */
    if ((options != 0U) && (Spi_IsWide(spi) != 0U))
    {
        return SPL_ERR_STATE;
    }

    cr2 = spi->CR2 & ~(SPI_CR2_FRXTH | SPI_CR2_LDMATX | SPI_CR2_LDMARX);
    if ((Spi_IsWide(spi) == 0U) && ((options & SPI_PACK_RX16) == 0U))
    {
        cr2 |= SPI_CR2_FRXTH;
    }
    if ((options & SPI_PACK_TX_ODD) != 0U)
    {
        cr2 |= SPI_CR2_LDMATX;
    }
    if ((options & SPI_PACK_RX_ODD) != 0U)
    {
        cr2 |= SPI_CR2_LDMARX;
    }
    spi->CR2 = cr2;
    return SPL_OK;
}

SPL_Status SPI_GetFifoLevels(SPI_TypeDef *spi, uint32_t *tx_level, uint32_t *rx_level)
{
    uint32_t sr;

    SPL_REQUIRE(!((Spi_Valid(spi) == 0U)),
                SPL_ERR_PARAM); /* Invalid peripheral address could access unrelated MMIO */
    SPL_REQUIRE(!((tx_level == (uint32_t *)0)),
                SPL_ERR_PARAM); /* NULL access does not provide a recoverable API error */
    SPL_REQUIRE(!((rx_level == (uint32_t *)0)),
                SPL_ERR_PARAM); /* NULL access does not provide a recoverable API error */

    sr = spi->SR;
    *tx_level = (sr & SPI_SR_FTLVL) >> SPI_SR_FTLVL_Pos;
    *rx_level = (sr & SPI_SR_FRLVL) >> SPI_SR_FRLVL_Pos;
    return SPL_OK;
}

SPL_Status SPI_GetErrors(SPI_TypeDef *spi, uint32_t *errors)
{
    SPL_REQUIRE(!((Spi_Valid(spi) == 0U)),
                SPL_ERR_PARAM); /* Invalid peripheral address could access unrelated MMIO */
    SPL_REQUIRE(!((errors == (uint32_t *)0)),
                SPL_ERR_PARAM); /* NULL access does not provide a recoverable API error */

    *errors = spi->SR & SPI_ERRORS_ALL;
    return SPL_OK;
}

SPL_Status SPI_ClearModeFault(SPI_TypeDef *spi)
{
    uint32_t cr1;

    SPL_REQUIRE(!(Spi_Valid(spi) == 0U),
                SPL_ERR_PARAM); /* Invalid peripheral address could access unrelated MMIO */
    /* Step 1: SR read while MODF=1. */
    if ((spi->SR & SPI_SR_MODF) == 0U)
    {
        return SPL_OK;
    }
    /* Step 2: CR1 write. SPE/MSTR stay 0 (hardware cleared them and refuses
     * to set them while MODF=1). */
    cr1 = spi->CR1 & ~(SPI_CR1_SPE | SPI_CR1_MSTR);
    spi->CR1 = cr1;
    /* Read back: the clear reaches the peripheral before return (METHOD 1.7, issue #23). */
    (void)spi->SR;
    return SPL_OK;
}

SPL_Status SPI_ClearCrcError(SPI_TypeDef *spi)
{
    SPL_REQUIRE(!(Spi_Valid(spi) == 0U),
                SPL_ERR_PARAM); /* Invalid peripheral address could access unrelated MMIO */
    if ((spi->SR & SPI_SR_CRCERR) == 0U)
    {
        return SPL_OK;
    }
    /* rc_w0: 0 at CRCERR, 1 at all other defined (read-only) bits. */
    spi->SR = SPI_SR_DEFINED & ~SPI_SR_CRCERR;
    /* Read back: the clear reaches the peripheral before return (METHOD 1.7, issue #23). */
    (void)spi->SR;
    return SPL_OK;
}

SPL_Status SPI_CrcNext(SPI_TypeDef *spi)
{
    uint32_t cr1;

    SPL_REQUIRE(!(Spi_Valid(spi) == 0U),
                SPL_ERR_PARAM); /* Invalid peripheral address could access unrelated MMIO */
    cr1 = spi->CR1;
    if (((cr1 & SPI_CR1_CRCEN) == 0U) || ((cr1 & SPI_CR1_SPE) == 0U))
    {
        return SPL_ERR_STATE;
    }
    spi->CR1 = cr1 | SPI_CR1_CRCNEXT;
    return SPL_OK;
}
