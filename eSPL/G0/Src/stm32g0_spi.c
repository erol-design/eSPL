/* Ported from STM32G4 SPL v0.13.3 (Autometry_Mcu_Spl 130de7a); reviewed against RM0444 Rev 6
 * section 35 (same SPI IP; section numbers 42.x -> 35.x) and ES0548 2.14.1 / 2.14.2. */
/* Kutuphane birimi: spl_check.h karisma korumasi referansini bu birim de uretir; uretmeyen tek
 * birim stm32g0_check.c (SPL_CHECK_LEVEL_DEFINER). BULGU-02, CHECK_LEVEL.md 3.2. */
#define SPL_LIBRARY_BUILD 1
#include <stddef.h>
#include "stm32g0_spi.h"
#include "stm32g0_sku.h"

/* Denetim seviyesi (A4 dalga 4b, Docs/CHECK_LEVEL.md 5.4 SPI / I2S tablosu): SPL_REQUIRE = normal
 * sinif (yalniz FULL), SPL_REQUIRE_SAFE = SAFE sinif (FULL ve MIX). MODF, BSY / FTLVL (K-A4-5),
 * OVR / UDR / FRE ve WOULD_BLOCK result: her seviyede. Yalniz denetimde kullanilan yardimcilar
 * static inline (saf, Tools/pure_checks.txt). */

/* DS field value 7 = 8-bit frame; values >= 8 are 9..16-bit frames
 * (RM0444 35.9.2). "Not used" values 0..2 are forced to 8 bit by hardware. */
#define SPI_DS_8BIT_FIELD 7U

/* DR'ye 16 bit erisim (veri paketleme: erisim genisligi FIFO'ya giden bayt sayisini belirler).
 * CMSIS DR'yi uint32_t tanimlar; *(uint16_t *)&spi->DR GCC'de -Wstrict-aliasing uyarisi verir
 * (uintptr_t ara cevrimi de yetmez, GCC nesneyi izler). Adres taban + offsetof ile tamsayi olarak
 * kurulur: AC6 nesnesi bayt bayt ayni, GCC uyarisi yok. 8 bit erisim (karakter tipi) kurala
 * takilmaz, degismedi. */
#define SPI_DR16(spi) (*(__IO uint16_t *)((uintptr_t)(spi) + offsetof(SPI_TypeDef, DR)))

/* All defined SPIx_SR bits (RM0444 35.9.3); only CRCERR is writable. */
#define SPI_SR_DEFINED                                                                             \
    (SPI_SR_FTLVL | SPI_SR_FRLVL | SPI_SR_FRE | SPI_SR_BSY | SPI_SR_OVR | SPI_SR_MODF |            \
     SPI_SR_CRCERR | SPI_SR_UDR | SPI_SR_CHSIDE | SPI_SR_TXE | SPI_SR_RXNE)

static inline uint8_t Spi_Valid(const SPI_TypeDef *spi)
{
    uint8_t valid = (uint8_t)(((spi == SPI1) || (spi == SPI2)) ? 1U : 0U);
#if defined(SPI3)
    valid = (uint8_t)(((valid != 0U) || (spi == SPI3)) ? 1U : 0U);
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

    SPL_REQUIRE_SAFE(Spi_Valid(spi) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE_SAFE(cfg != (const SPI_InitTypeDef *)0, SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(cfg->prescaler <= 7U, SPL_ERR_PARAM); /* SAFE: BR tasmasi */
    SPL_REQUIRE((cfg->cpol <= 1U) && (cfg->cpha <= 1U) && (cfg->lsb_first <= 1U), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE((spi->CR1 & SPI_CR1_SPE) == 0U, SPL_ERR_STATE); /* SAFE */
    /* v0.11.1: hardware refuses MSTR while MODF=1 (RM0444 35.5.11); report it like
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
    SPL_REQUIRE_SAFE(Spi_Valid(spi) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE(enable <= 1U, SPL_ERR_PARAM);

    if (enable != 0U)
    {
        /* Hardware does not allow SPE=1 while MODF=1 (RM0444 35.5.11).
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

        /* ERRATA ES0548 2.14.2: slave BSY may stay high after the
         * last frame; a slave (FTLVL = 0) disables without the BSY gate. */
        if (((sr & SPI_SR_BSY) != 0U) && ((slave == 0U) || ((sr & SPI_SR_FTLVL) != 0U)))
        {
            return SPL_BUSY;
        }
        /* Slave: TX FIFO data leaves only when the master clocks it out. With no frame in
         * progress the master may have stopped for good (RM0444 35.5.9 disable procedure,
         * slave BSY note): retrying cannot help. Recover with an RCC peripheral reset
         * (RCC_AssertPeriphReset / RCC_ReleasePeriphReset), then init again. A master
         * empties its own FIFO: BUSY. */
        if ((sr & SPI_SR_FTLVL) != 0U)
        {
            if (slave != 0U)
            {
                return SPL_ERR_STATE; /* result (K-A4-5): karsi master'a bagli, her seviyede */
            }
            return SPL_BUSY;
        }
        spi->CR1 &= (uint32_t)~SPI_CR1_SPE;
        /* RX FIFO should be drained by caller before next enable.
         * No implicit DR reads: preserving received data is intentional. */
    }
    return SPL_OK;
}

SPL_Status SPI_TryWrite8(SPI_TypeDef *spi, uint8_t data)
{
    SPL_REQUIRE_SAFE(Spi_Valid(spi) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    /* SAFE: SPE = 0 iken bayt FIFO'da bekler; genis cerceve FIFO hizasini bozar. */
    SPL_REQUIRE_SAFE(((spi->CR1 & SPI_CR1_SPE) != 0U) && (Spi_IsWide(spi) == 0U), SPL_ERR_STATE);
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
    SPL_REQUIRE_SAFE(Spi_Valid(spi) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE_SAFE(data != (uint8_t *)0, SPL_ERR_PARAM);
    /* An 8-bit read of a 9..16-bit frame breaks FIFO alignment (SAFE). */
    SPL_REQUIRE_SAFE(Spi_IsWide(spi) == 0U, SPL_ERR_STATE);
    /* RX FIFO may still hold data after SPE has been cleared.
     * Draining that data is a required part of normal shutdown. */
    if ((spi->SR & SPI_SR_OVR) != 0U)
    {
        return SPL_ERR_HW;
    }
    if ((spi->SR & SPI_SR_RXNE) == 0U)
    {
        /* No new receive data can arrive while SPI is disabled.
         * WOULD_BLOCK would incorrectly invite endless polling (SAFE: K-A4-4 ilkesi). */
        SPL_REQUIRE_SAFE((spi->CR1 & SPI_CR1_SPE) != 0U, SPL_ERR_STATE);
        return SPL_WOULD_BLOCK;
    }

    /* Receive FIFO may be drained even after clearing SPE. */
    *data = *(__IO uint8_t *)&spi->DR;
    return SPL_OK;
}

SPL_Status SPI_Status(SPI_TypeDef *spi, uint32_t *status)
{
    SPL_REQUIRE_SAFE(Spi_Valid(spi) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE_SAFE(status != (uint32_t *)0, SPL_ERR_PARAM);

    *status = spi->SR;
    return SPL_OK;
}

/* SPI overrun recovery (RM0444 35.5.11): read DR, then SR in this order.
 * Calling this function consumes one unread DR value. Other pending RX
 * bytes must be handled by the application; no transparent recovery is
 * promised for a discarded/overrun transfer.
 * The DR/SR reads are deliberately volatile and ordered. */
SPL_Status SPI_ClearOverrun(SPI_TypeDef *spi)
{
    volatile uint16_t discarded;
    volatile uint32_t status_snapshot;

    SPL_REQUIRE_SAFE(Spi_Valid(spi) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */

    if ((spi->SR & SPI_SR_OVR) == 0U)
    {
        return SPL_OK;
    }

    /* DR read width follows the frame size (RM0444 35.5.9 alignment). */
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

/* Validates field ranges and RM0444 cross-rules; no register access. */
/* A4 (CHECK_LEVEL.md 5.4): 0 / 1 alanlarinin araligi normal sinif (yazimda `!= 0`); yalniz FULL. */
static inline uint8_t Spi_ConfigBoolsValid(const SPI_ConfigTypeDef *cfg)
{
    return ((cfg->cpol <= 1U) && (cfg->cpha <= 1U) && (cfg->lsb_first <= 1U) &&
            (cfg->nss_pulse <= 1U) && (cfg->crc_enable <= 1U))
               ? 1U
               : 0U;
}

/* SAFE: kaydirilan alanlarin araligi, RM0444 35.5 capraz kurallari ve CRC alanlari. */
static inline uint8_t Spi_ConfigValid(const SPI_ConfigTypeDef *cfg)
{
    uint8_t master;
    uint8_t ti;

    if ((cfg->mode > SPI_MODE_MASTER) || (cfg->data_bits < 4U) || (cfg->data_bits > 16U) ||
        (cfg->prescaler > 7U) || (cfg->nss > SPI_NSS_HARD_OUTPUT) ||
        (cfg->frame_format > SPI_FRAME_TI) || (cfg->direction > SPI_DIR_RX_ONLY))
    {
        return 0U;
    }
    master = (uint8_t)((cfg->mode == SPI_MODE_MASTER) ? 1U : 0U);
    ti = (uint8_t)((cfg->frame_format == SPI_FRAME_TI) ? 1U : 0U);

    /* BR is not used by a Motorola slave (RM0444 35.5.7 note 4). */
    if ((master == 0U) && (ti == 0U) && (cfg->prescaler != 0U))
    {
        return 0U;
    }
    /* SSOE=1 is a master-only configuration (RM0444 35.5.5). */
    if ((master == 0U) && (cfg->nss == SPI_NSS_HARD_OUTPUT))
    {
        return 0U;
    }
    /* NSSP: master, Motorola, CPHA=0 (RM0444 35.5.12, CR2.NSSP); the pulse is
     * produced on the hardware NSS output (SSOE=1, 35.5.5). */
    /* "CRC calculation cannot be used at NSS Pulse mode" (RM0444 35.5.14). */
    if ((cfg->nss_pulse != 0U) && ((master == 0U) || (ti != 0U) || (cfg->cpha != 0U) ||
                                   (cfg->nss != SPI_NSS_HARD_OUTPUT) || (cfg->crc_enable != 0U)))
    {
        return 0U;
    }
    /* TI: SSM/SSI/SSOE and LSBFIRST are not used (RM0444 35.5.13, 35.9.1); keep them at reset
     * values. Full duplex only. CPOL/CPHA: reset values 0/0 without CRC; with CRC they must
     * be kept at CPOL=0, CPHA=1 (RM0444 35.5.14). */
    if ((ti != 0U) && ((cfg->nss != SPI_NSS_HARD_INPUT) || (cfg->lsb_first != 0U) ||
                       (cfg->direction != SPI_DIR_FULL_DUPLEX) || (cfg->cpol != 0U) ||
                       (cfg->cpha != cfg->crc_enable)))
    {
        return 0U;
    }
    /* Receive-only modes: slave only (a master clocks continuously and needs
     * a timed SPE=0, RM0444 35.5.9); no CRC (single-frame RX buffer). */
    if (((cfg->direction == SPI_DIR_HALF_DUPLEX_RX) || (cfg->direction == SPI_DIR_RX_ONLY)) &&
        ((master != 0U) || (cfg->crc_enable != 0U)))
    {
        return 0U;
    }
    if (cfg->crc_enable != 0U)
    {
        /* CRC only for 8/16-bit frames; odd polynomial (RM0444 35.5.14). */
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

    /* RXFIFO threshold aligned with the DR read width (RM0444 35.5.7 3e). */
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

    SPL_REQUIRE_SAFE(Spi_Valid(spi) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE_SAFE(cfg != (const SPI_ConfigTypeDef *)0, SPL_ERR_PARAM);
    SPL_REQUIRE(Spi_ConfigBoolsValid(cfg) != 0U, SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(Spi_ConfigValid(cfg) != 0U, SPL_ERR_PARAM); /* SAFE: alan tasmasi, RM kurali */
    SPL_REQUIRE_SAFE((spi->CR1 & SPI_CR1_SPE) == 0U, SPL_ERR_STATE); /* SAFE */
    /* MSTR cannot be set while MODF=1 (RM0444 35.5.11). */
    if ((spi->SR & SPI_SR_MODF) != 0U)
    {
        return SPL_ERR_HW;
    }

    /* RM0444 35.5.7 order: CR1, CR2, CRCPR. With CRC, CR1 is first written with CRCEN=0 so
     * that CRCEN sees a 0 -> 1 transition and the CRC is reset even when the previous
     * session left CRCEN=1 (RM0444 35.5.14 re-enable sequence; whether writing 1 over 1
     * also resets it is not stated unambiguously, 35.9.6/35.9.7). */
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
    SPL_REQUIRE_SAFE(Spi_Valid(spi) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    /* SAFE: SPE = 0 iken bayt FIFO'da bekler; dar cerceve FIFO hizasini bozar. */
    SPL_REQUIRE_SAFE(((spi->CR1 & SPI_CR1_SPE) != 0U) && (Spi_IsWide(spi) != 0U), SPL_ERR_STATE);
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
    SPL_REQUIRE_SAFE(Spi_Valid(spi) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE_SAFE(data != (uint16_t *)0, SPL_ERR_PARAM);
    /* 16-bit reads need a 9..16-bit frame and FRXTH=0 (RM0444 35.9.2); SAFE: FIFO hizasi. */
    SPL_REQUIRE_SAFE((Spi_IsWide(spi) != 0U) && ((spi->CR2 & SPI_CR2_FRXTH) == 0U), SPL_ERR_STATE);
    if ((spi->SR & SPI_SR_OVR) != 0U)
    {
        return SPL_ERR_HW;
    }
    if ((spi->SR & SPI_SR_RXNE) == 0U)
    {
        SPL_REQUIRE_SAFE((spi->CR1 & SPI_CR1_SPE) != 0U, SPL_ERR_STATE); /* SAFE: K-A4-4 ilkesi */
        return SPL_WOULD_BLOCK;
    }

    *data = SPI_DR16(spi);
    return SPL_OK;
}

SPL_Status SPI_ITConfig(SPI_TypeDef *spi, uint32_t it_mask, uint8_t enable)
{
    SPL_REQUIRE_SAFE(Spi_Valid(spi) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE((it_mask != 0U) && (enable <= 1U), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE((it_mask & ~SPI_IT_ALL) == 0U, SPL_ERR_PARAM); /* SAFE: CR2 |= fazla bit */

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

    SPL_REQUIRE_SAFE(Spi_Valid(spi) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE((dma_mask != 0U) && (enable <= 1U), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE((dma_mask & ~valid_mask) == 0U, SPL_ERR_PARAM); /* SAFE: CR2 |= fazla bit */

    if (enable != 0U)
    {
        spi->CR2 |= dma_mask;
    }
    else
    {
        /* Close order: DMA requests are cleared after SPE=0 (RM0444 35.5.9); SAFE. */
        SPL_REQUIRE_SAFE((spi->CR1 & SPI_CR1_SPE) == 0U, SPL_ERR_STATE);
        spi->CR2 &= ~dma_mask;
    }
    return SPL_OK;
}

SPL_Status SPI_DmaPackingConfig(SPI_TypeDef *spi, uint32_t options)
{
    const uint32_t valid_mask = SPI_PACK_RX16 | SPI_PACK_TX_ODD | SPI_PACK_RX_ODD;
    uint32_t cr2;

    SPL_REQUIRE_SAFE(Spi_Valid(spi) != 0U, SPL_ERR_PARAM);     /* SAFE: rastgele adres */
    SPL_REQUIRE((options & ~valid_mask) == 0U, SPL_ERR_PARAM); /* normal: & ile test edilir */
    /* SAFE: RX_ODD, RX16'siz (RM yasak birlesim). */
    SPL_REQUIRE_SAFE(((options & SPI_PACK_RX_ODD) == 0U) || ((options & SPI_PACK_RX16) != 0U),
                     SPL_ERR_PARAM);
    /* LDMA_TX/LDMA_RX must be written with SPE=0 (RM0444 35.9.2); SAFE. */
    SPL_REQUIRE_SAFE((spi->CR1 & SPI_CR1_SPE) == 0U, SPL_ERR_STATE);
    /* Packing exists only for frames of 8 bits or less (SAFE). */
    SPL_REQUIRE_SAFE((options == 0U) || (Spi_IsWide(spi) == 0U), SPL_ERR_STATE);

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

    SPL_REQUIRE_SAFE(Spi_Valid(spi) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE_SAFE((tx_level != (uint32_t *)0) && (rx_level != (uint32_t *)0), SPL_ERR_PARAM);

    sr = spi->SR;
    *tx_level = (sr & SPI_SR_FTLVL) >> SPI_SR_FTLVL_Pos;
    *rx_level = (sr & SPI_SR_FRLVL) >> SPI_SR_FRLVL_Pos;
    return SPL_OK;
}

SPL_Status SPI_GetErrors(SPI_TypeDef *spi, uint32_t *errors)
{
    SPL_REQUIRE_SAFE(Spi_Valid(spi) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE_SAFE(errors != (uint32_t *)0, SPL_ERR_PARAM);

    *errors = spi->SR & SPI_ERRORS_ALL;
    return SPL_OK;
}

SPL_Status SPI_ClearModeFault(SPI_TypeDef *spi)
{
    uint32_t cr1;

    SPL_REQUIRE_SAFE(Spi_Valid(spi) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
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
    SPL_REQUIRE_SAFE(Spi_Valid(spi) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
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

    SPL_REQUIRE_SAFE(Spi_Valid(spi) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    cr1 = spi->CR1;
    /* SAFE: CRCNEXT sessizce yok sayilir. */
    SPL_REQUIRE_SAFE(((cr1 & SPI_CR1_CRCEN) != 0U) && ((cr1 & SPI_CR1_SPE) != 0U), SPL_ERR_STATE);
    spi->CR1 = cr1 | SPI_CR1_CRCNEXT;
    return SPL_OK;
}

/* ---- v0.10: I2S mode of SPI1 / SPI2 (Docs/v0.10_COMM_MODES.md, RM0444 35.7) ---- */

/* RM0444 Table 205 / RM0454 Table 129: I2S on SPI1 always, on SPI2 only in group 0BX. */
static inline uint8_t I2s_Valid(const SPI_TypeDef *spi)
{
    return ((spi == SPI1) || ((SPL_G0_SPI2_I2S != 0) && (spi == SPI2))) ? 1U : 0U;
}

#define I2S_SR_ERRORS (SPI_SR_UDR | SPI_SR_OVR | SPI_SR_FRE)

SPL_Status I2S_Init(SPI_TypeDef *spi, const I2S_InitTypeDef *cfg)
{
    uint32_t i2scfgr;
    uint32_t i2spr;

    SPL_REQUIRE_SAFE(Spi_Valid(spi) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE_SAFE(cfg != NULL, SPL_ERR_PARAM);
    /* SAFE: kaydirilan I2SCFG / I2SSTD / DATLEN alanlari. */
    SPL_REQUIRE_SAFE((cfg->mode <= 3U) && (cfg->standard <= 3U) && (cfg->data_length <= 2U),
                     SPL_ERR_PARAM);
    SPL_REQUIRE((cfg->pcm_long <= 1U) && (cfg->channel_32 <= 1U) && (cfg->cpol <= 1U) &&
                    (cfg->mclk_out <= 1U) && (cfg->odd <= 1U) && (cfg->async_start <= 1U),
                SPL_ERR_PARAM);
    /* DATLEN 24 / 32 bit fixes a 32-bit channel (RM0444 35.9.8 CHLEN); PCMSYNC means something
     * in PCM only; I2SDIV 0 and 1 are forbidden (35.9.9), the divider is used in master mode.
     * SAFE: RM yasak deger / birlesim. */
    SPL_REQUIRE_SAFE(((cfg->data_length == 0U) || (cfg->channel_32 != 0U)) &&
                         ((cfg->pcm_long == 0U) || (cfg->standard == 3U)) &&
                         ((cfg->mode < 2U) || (cfg->divider >= 2U)),
                     SPL_ERR_PARAM);
    /* SPI3; SPI2 outside group 0BX (SAFE). */
    SPL_REQUIRE_SAFE(I2s_Valid(spi) != 0U, SPL_ERR_UNSUPPORTED);
    /* I2SMOD with SPE = 0, the other fields with I2SE = 0 (RM0444 35.9.8); SAFE. */
    SPL_REQUIRE_SAFE(((spi->I2SCFGR & SPI_I2SCFGR_I2SE) == 0U) && ((spi->CR1 & SPI_CR1_SPE) == 0U),
                     SPL_ERR_STATE);
    i2spr = ((uint32_t)cfg->divider << SPI_I2SPR_I2SDIV_Pos) |
            ((cfg->odd != 0U) ? SPI_I2SPR_ODD : 0U) |
            ((cfg->mclk_out != 0U) ? SPI_I2SPR_MCKOE : 0U);
    i2scfgr = SPI_I2SCFGR_I2SMOD | ((uint32_t)cfg->mode << SPI_I2SCFGR_I2SCFG_Pos) |
              ((uint32_t)cfg->standard << SPI_I2SCFGR_I2SSTD_Pos) |
              ((uint32_t)cfg->data_length << SPI_I2SCFGR_DATLEN_Pos) |
              ((cfg->channel_32 != 0U) ? SPI_I2SCFGR_CHLEN : 0U) |
              ((cfg->cpol != 0U) ? SPI_I2SCFGR_CKPOL : 0U) |
              ((cfg->pcm_long != 0U) ? SPI_I2SCFGR_PCMSYNC : 0U) |
              ((cfg->async_start != 0U) ? SPI_I2SCFGR_ASTRTEN : 0U);
    spi->I2SPR = i2spr;
    spi->I2SCFGR = i2scfgr;
    return SPL_OK;
}

SPL_Status I2S_Enable(SPI_TypeDef *spi, uint8_t enable)
{
    SPL_REQUIRE_SAFE(Spi_Valid(spi) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE(enable <= 1U, SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(I2s_Valid(spi) != 0U, SPL_ERR_UNSUPPORTED); /* SAFE: ornekte I2S yok */
    /* not configured for I2S (I2S_Init first); SAFE: I2SE sessizce yok sayilir. */
    SPL_REQUIRE_SAFE((spi->I2SCFGR & SPI_I2SCFGR_I2SMOD) != 0U, SPL_ERR_STATE);
    spi->I2SCFGR = (spi->I2SCFGR & ~SPI_I2SCFGR_I2SE) | ((enable != 0U) ? SPI_I2SCFGR_I2SE : 0U);
    return SPL_OK;
}

SPL_Status I2S_TryWrite(SPI_TypeDef *spi, uint16_t data)
{
    uint32_t sr;

    SPL_REQUIRE_SAFE(Spi_Valid(spi) != 0U, SPL_ERR_PARAM);       /* SAFE: rastgele adres */
    SPL_REQUIRE_SAFE(I2s_Valid(spi) != 0U, SPL_ERR_UNSUPPORTED); /* SAFE: ornekte I2S yok */
    sr = spi->SR;
    if ((sr & (SPI_SR_UDR | SPI_SR_FRE)) != 0U)
    {
        return SPL_ERR_HW;
    }
    if ((sr & SPI_SR_TXE) == 0U)
    {
        return SPL_WOULD_BLOCK;
    }
    SPI_DR16(spi) = data; /* 16-bit access: half of a 24 / 32-bit sample per write */
    return SPL_OK;
}

SPL_Status I2S_TryRead(SPI_TypeDef *spi, uint16_t *data, uint8_t *right_channel)
{
    uint32_t sr;

    SPL_REQUIRE_SAFE(Spi_Valid(spi) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE_SAFE((data != NULL) && (right_channel != NULL), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(I2s_Valid(spi) != 0U, SPL_ERR_UNSUPPORTED); /* SAFE: ornekte I2S yok */
    sr = spi->SR;
    if ((sr & (SPI_SR_OVR | SPI_SR_FRE)) != 0U)
    {
        return SPL_ERR_HW;
    }
    if ((sr & SPI_SR_RXNE) == 0U)
    {
        return SPL_WOULD_BLOCK;
    }
    *right_channel = ((sr & SPI_SR_CHSIDE) != 0U) ? 1U : 0U;
    *data = SPI_DR16(spi);
    return SPL_OK;
}

SPL_Status I2S_GetErrors(SPI_TypeDef *spi, uint32_t *errors)
{
    SPL_REQUIRE_SAFE(Spi_Valid(spi) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE_SAFE(errors != NULL, SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(I2s_Valid(spi) != 0U, SPL_ERR_UNSUPPORTED); /* SAFE: ornekte I2S yok */
    *errors = spi->SR & I2S_SR_ERRORS;
    return SPL_OK;
}

SPL_Status I2S_ClearErrors(SPI_TypeDef *spi)
{
    SPL_REQUIRE_SAFE(Spi_Valid(spi) != 0U, SPL_ERR_PARAM);       /* SAFE: rastgele adres */
    SPL_REQUIRE_SAFE(I2s_Valid(spi) != 0U, SPL_ERR_UNSUPPORTED); /* SAFE: ornekte I2S yok */
    /* RM0444 35.7.8: OVR is cleared by a DR read followed by an SR read; UDR and FRE by an SR read.
     * The discarded DR word is the one received after the overrun. */
    if ((spi->SR & SPI_SR_OVR) != 0U)
    {
        volatile uint16_t discarded = SPI_DR16(spi);
        (void)discarded;
    }
    (void)spi->SR;
    return SPL_OK;
}
