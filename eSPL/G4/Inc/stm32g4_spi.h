/* SPDX-License-Identifier: MIT */
#ifndef STM32G4_SPI_H
#define STM32G4_SPI_H
#include <stdint.h>
#include "stm32g4xx.h"
#include "stm32g4_status.h"

#ifdef __cplusplus
extern "C"
{
#endif
    /* SPI driver for STM32G4 (RM0440 section 42); SPI mode only, I2S mode is out of scope.
     * Scope: SPI1, SPI2, SPI3 and SPI4 (SPI3 / SPI4 only where CMSIS defines them); legacy API
     * (v0.8 .. v0.10) and extended configuration (v0.11).
     * Usage: SPI_Init (or the legacy SPI_MasterInit) with SPE = 0, optional SPI_ITConfig /
     * SPI_DmaConfig / SPI_DmaPackingConfig, SPI_Enable(spi, 1), then the Try* transfers;
     * SPI_Enable(spi, 0) to stop.
     * Common rules: no dynamic memory, no static state; no function waits (BUSY / WOULD_BLOCK are
     * returned instead); not reentrant per instance; output parameters are unchanged unless
     * SPL_OK (METHOD 1.8). Any read of SPIx_SR clears FRE by hardware (RM0440 42.9.3); every
     * service that checks a flag reads SR. In TI slave mode capture FRE with SPI_GetErrors before
     * calling other services.
     * Caller duties (all APIs; not done by the driver):
     *  - Clock: RCC bus clock and reset of the instance; prescaler values divide PCLK.
     *  - Pins: SCK / MISO / MOSI / NSS in their GPIO alternate function; pin ownership and the
     *    idle levels after SPI_Enable(0) are the caller's (CCL-2 SPI-16b).
     *  - Interrupts / DMA: NVIC line, priority and handler; DMA channel / DMAMUX setup.
     *  - Serialize all calls per instance (ISR included); CR1 / CR2 are read-modify-written.
     * CCL-2 driver-library hand-off: this driver has no chip select, no transaction, no sleep
     * entry and no device knowledge; the layer above it owns:
     *  - Caller duty (CCL-2 SPI-01): when devices on one bus differ in CPOL/CPHA, bit order or
     *    frame size, reconfigure the unit (SPI_Enable(0), SPI_Init, SPI_Enable(1)) before
     *    asserting the chip select of each device.
     *  - Caller duty (CCL-2 SPI-04b, SPI-05b): drive chip selects (GPIO, SPI_NSS_SOFT); release
     *    CS on every exit path, including error returns of these services and the caller's own
     *    timeout exits, and never assert a second CS while one is active.
     *  - Caller duty (CCL-2 SPI-04c): send command, address and data of one device operation
     *    inside one CS assertion; do not use nss_pulse=1 for devices that end an operation on
     *    CS high.
     *  - Caller duty (CCL-2 SPI-06b): in master mode the clock runs only while TX data is sent;
     *    write one dummy frame (SPI_TryWrite8/16) for every frame to be received, or program
     *    the TX DMA channel for the same count.
     *  - Caller duty (CCL-2 SPI-11b): in full-duplex DMA program the RX and TX channel counters
     *    from the same length value.
     *  - Caller duty (CCL-2 SPI-22): SPI_DmaConfig(enable=1) returns BUSY while SPE=1 and
     *    BSY=1; do not reprogram the DMA channels before both channels of the previous transfer
     *    completed and the stop order (see SPI_DmaConfig) was run.
     *  - Caller duty (CCL-2 SPI-10d): SPI_TryRead8/16 return ERR_HW while CRCERR is set (flag
     *    kept); with crc_enable=1 and a DMA transfer read SPI_GetErrors after the CRC frame; on
     *    CRCERR clear it with SPI_ClearCrcError and report a failure to the upper layer.
     *  - Caller duty (CCL-2 SPI-23): before a latch/strobe pulse wait for FTLVL=0 and BSY=0
     *    (SPI_GetFifoLevels, SPI_Status); TXE alone means the last frame is still shifting.
     *  - Caller duty (CCL-2 SPI-15a): this driver never enters a low-power mode; before WFI/WFE
     *    make sure no transfer runs (FTLVL=0, BSY=0, DMA channels stopped) and CS is high.
     *  - Caller duty (CCL-2 SPI-13a, SPI-13b, SPI-13c): the device driver sends the write enable
     *    command in its own CS cycle before each write/erase, polls the device busy status (or
     *    waits a fixed time) before the next command, and splits writes at the device page
     *    boundary.
     *  - Caller duty (CCL-2 SPI-14a, SPI-14b, SPI-14c): the device driver checks the device ID
     *    at start-up, rejects all-0x00/all-0xFF reads (or uses a CRC or known field), and
     *    verifies any checksum field the device sends before using the data.
     *  - Caller duty (CCL-2 SPI-15c): the device driver sends the device power-down command
     *    before MCU sleep and the wake-up command before the first access after wake-up.
     * Contract: Docs/v0.11_SPI_CONTRACT.md. */

    /* ---- Legacy API (v0.8 .. v0.10), behaviour kept ---- */

    typedef struct
    {
        uint32_t prescaler; /* exponent 0..7: PCLK / (2 << prescaler) */
        uint32_t cpol;
        uint32_t cpha;
        uint32_t lsb_first;
    } SPI_InitTypeDef;
    /* 8-bit, 2-wire full duplex, software NSS, master, no CRC; GPIO AF/RCC external.
     * SPE=1: ERR_STATE. v0.11.1: MODF=1 -> ERR_HW without a write (as SPI_Init). */
    /* Caller: software NSS: drive the slave chip select GPIO (inactive before SPI_Enable, active
     * only around a transfer). */
    /* Level: PARAM[F] STATE[r] HW[r] */
    SPL_Status SPI_MasterInit(SPI_TypeDef *spi, const SPI_InitTypeDef *cfg);
    /* Disabling returns SPL_BUSY while BSY=1 (slave: only with TX FIFO not empty, see
     * ERRATA below), or while TX FIFO is not empty in master mode.
     * v0.11.1: slave with TX FIFO not empty and BSY=0 -> ERR_STATE (the master may have
     * stopped clocking; only RCC_AssertPeriphReset/RCC_ReleasePeriphReset empties it).
     * The RX FIFO can be drained with SPI_TryRead8 while SPE=0.
     * OVR reports SPL_ERR_HW and requires a separate recovery procedure.
     * v0.11: enabling with MODF set returns SPL_ERR_HW without a write
     * (hardware refuses SPE=1 while MODF=1, RM0440 42.5.11).
     * ERRATA ES0431 2.14.2 / ES0430 2.18.2 / ES0523 2.14.2: slave (MSTR=0) with TX FIFO empty
     * disables even while BSY=1 (BSY may stick high). The caller detects the end
     * of the slave transaction first (NSS high, or the expected RXNE count) and
     * calls SPI_Enable(0) only then. Slave BUSY (BSY=1, FTLVL!=0): bound the
     * retries, then RCC reset. Master: BSY=1 or FTLVL!=0 -> BUSY (unchanged).
     * ERRATA ES0431 2.14.1 / ES0430 2.18.1 / ES0523 2.14.1: master disables only with FTLVL=0
     * (TXE=1) and BSY=0; master RX-only is refused by SPI_Init; slave: do not
     * remove NSS during a frame. */
    /* Caller: enable = 0: wait for the end of the transfer (slave: NSS high or the expected RXNE
     * count, ES0431 2.14.2); bound BUSY retries; after slave ERR_STATE use RCC reset and init
     * again; drain the RX FIFO before the next enable; end the transaction (CS high) only after
     * OK (CCL-2 SPI-07). Slave: do not remove NSS during a frame. With DMA follow the
     * SPI_DmaConfig order. */
    /* Level: PARAM[F] STATE[r] HW[r] BUSY[r] */
    SPL_Status SPI_Enable(SPI_TypeDef *spi, uint8_t enable);
    /* 8-bit DR access. v0.11: SPL_ERR_STATE when the data size is > 8 bits. Read: ERR_HW while OVR
     * or CRCERR is set (flags kept; see SPI_TryRead16). */
    /* Caller: in full duplex read one RX frame per written frame, else OVR (SPI_TryRead8). */
    /* Level: PARAM[F] STATE[r] WOULD_BLOCK[r] */
    SPL_Status SPI_TryWrite8(SPI_TypeDef *spi, uint8_t data);
    /* Caller: after ERR_HW (OVR) data is already lost: SPI_ClearOverrun and resynchronise with
     * the peer; CRCERR: SPI_ClearCrcError and report the failure upwards. */
    /* Level: PARAM[F] STATE[r] HW[r] WOULD_BLOCK[r] */
    SPL_Status SPI_TryRead8(SPI_TypeDef *spi, uint8_t *data);
    /* Caller: none beyond the file-header duties (this SR read clears FRE, see above). */
    /* Level: PARAM[F] */
    SPL_Status SPI_Status(SPI_TypeDef *spi, uint32_t *status);
    /* Clear OVR by the DR-then-SR read sequence; consumes unread RX data.
     * This does not recover data already lost due to overrun.
     * v0.11: the DR read is 16-bit wide when the data size is > 8 bits. */
    /* Caller: resynchronise the frame stream with the peer; frames lost by the overrun and the
     * unread RX data consumed here are not recovered. */
    /* Level: PARAM[F] */
    SPL_Status SPI_ClearOverrun(SPI_TypeDef *spi);

    /* ---- v0.11: extended configuration ---- */

#define SPI_MODE_SLAVE 0U
#define SPI_MODE_MASTER 1U

/* NSS management (RM0440 42.5.5). */
#define SPI_NSS_SOFT 0U /* SSM=1; SSI=1 in master, SSI=0 (selected) in slave. */
/* SPI_NSS_SOFT in slave mode: the slave is always selected and drives MISO while SPE=1; do not use
 * it on a MISO line shared with other slaves (line contention). */
#define SPI_NSS_HARD_INPUT 1U  /* SSM=0, SSOE=0: slave chip select / multimaster input. */
#define SPI_NSS_HARD_OUTPUT 2U /* SSM=0, SSOE=1: master drives NSS while SPE=1. */

#define SPI_FRAME_MOTOROLA 0U
#define SPI_FRAME_TI 1U

#define SPI_DIR_FULL_DUPLEX 0U    /* BIDIMODE=0, RXONLY=0. */
#define SPI_DIR_HALF_DUPLEX_TX 1U /* BIDIMODE=1, BIDIOE=1 (transmit only on one line). */
#define SPI_DIR_HALF_DUPLEX_RX 2U /* BIDIMODE=1, BIDIOE=0. Slave only. */
#define SPI_DIR_RX_ONLY 3U        /* RXONLY=1. Slave only. */

    /* All fields are validated before any register write. Rules (PARAM):
     * - data_bits 4..16; FRXTH is derived: 1 for <= 8 bits, 0 for > 8 bits.
     * - prescaler 0..7 in master and TI slave; must be 0 in Motorola slave.
     * - SPI_NSS_HARD_OUTPUT: master only.
     * - nss_pulse=1: master, Motorola, cpha=0, SPI_NSS_HARD_OUTPUT, crc_enable=0.
     * - TI frame: nss=SPI_NSS_HARD_INPUT, nss_pulse=0, lsb_first=0,
     *   full duplex, cpol=0; cpha=0 without CRC, cpha=1 with crc_enable=1.
     * - SPI_DIR_HALF_DUPLEX_RX / SPI_DIR_RX_ONLY: slave only, no CRC.
     * - crc_enable=1: data_bits 8 or 16, crc_bits 8 or 16, odd polynomial
     *   1..0xFFFF. crc_enable=0: crc_bits and crc_polynomial must be 0. */
    typedef struct
    {
        uint32_t mode;           /* SPI_MODE_MASTER / SPI_MODE_SLAVE */
        uint32_t data_bits;      /* 4..16 */
        uint32_t cpol;           /* 0/1 */
        uint32_t cpha;           /* 0/1 */
        uint32_t lsb_first;      /* 0/1 */
        uint32_t prescaler;      /* BR exponent 0..7: PCLK / (2 << prescaler) */
        uint32_t nss;            /* SPI_NSS_* */
        uint32_t nss_pulse;      /* 0/1 (NSSP) */
        uint32_t frame_format;   /* SPI_FRAME_* */
        uint32_t direction;      /* SPI_DIR_* */
        uint32_t crc_enable;     /* 0/1 */
        uint32_t crc_bits;       /* 8 or 16 when crc_enable=1 */
        uint32_t crc_polynomial; /* odd, written to CRCPR when crc_enable=1 */
    } SPI_ConfigTypeDef;

    /* Requires SPE=0 (else STATE) and MODF=0 (else ERR_HW). Writes CR1 (SPE=0)
     * and CR2 completely (interrupt, DMA and LDMA bits cleared), then CRCPR
     * when CRC is enabled. With CRC, CR1 is written twice (CRCEN=0, then the
     * final value) so the CRC is reset (RM0440 42.5.14). */
    /* Caller: chip select per cfg->nss (SPI_NSS_SOFT master: caller GPIO; SPI_NSS_SOFT slave: see
     * the MISO contention note at SPI_NSS_SOFT); interrupt and DMA enables are cleared here, set
     * them again (SPI_ITConfig / SPI_DmaConfig) after each init. */
    /* Level: PARAM[F] STATE[r] HW[r] */
    SPL_Status SPI_Init(SPI_TypeDef *spi, const SPI_ConfigTypeDef *cfg);

    /* 16-bit DR access; requires data size > 8 bits (else STATE). Write also
     * requires SPE=1. Read returns ERR_HW while OVR or CRCERR is set (flags kept: read them with
     * SPI_GetErrors, clear with SPI_ClearOverrun / SPI_ClearCrcError, then drain). */
    /* Caller: in full duplex read one RX frame per written frame, else OVR (SPI_TryRead16). */
    /* Level: PARAM[F] STATE[r] WOULD_BLOCK[r] */
    SPL_Status SPI_TryWrite16(SPI_TypeDef *spi, uint16_t data);
    /* Caller: after ERR_HW (OVR) data is already lost: SPI_ClearOverrun and resynchronise with
     * the peer; CRCERR: SPI_ClearCrcError and report the failure upwards. */
    /* Level: PARAM[F] STATE[r] HW[r] WOULD_BLOCK[r] */
    SPL_Status SPI_TryRead16(SPI_TypeDef *spi, uint16_t *data);

/* Interrupt enable bits (SPIx_CR2). */
#define SPI_IT_TXE SPI_CR2_TXEIE
#define SPI_IT_RXNE SPI_CR2_RXNEIE
#define SPI_IT_ERR SPI_CR2_ERRIE
#define SPI_IT_ALL (SPI_IT_TXE | SPI_IT_RXNE | SPI_IT_ERR)
    /* Sets or clears the given CR2 interrupt enables (read-modify-write of
     * CR2; serialize with other CR2 writers). Allowed with SPE=0 or 1. */
    /* Caller: clear stale error flags (SPI_ClearOverrun / SPI_ClearModeFault / SPI_ClearCrcError)
     * before enabling SPI_IT_ERR; the handler services TXE / RXNE (DR access) and the errors. */
    /* Level: PARAM[F] */
    SPL_Status SPI_ITConfig(SPI_TypeDef *spi, uint32_t it_mask, uint8_t enable);

/* DMA request enable bits (SPIx_CR2). */
#define SPI_DMA_TX SPI_CR2_TXDMAEN
#define SPI_DMA_RX SPI_CR2_RXDMAEN
    /* RM0440 42.5.9 start order: SPI_DmaConfig(RX,1); enable DMA channels;
     * SPI_DmaConfig(TX,1); SPI_Enable(1). Stop order: disable DMA channels;
     * SPI_Enable(0) (retry while BUSY); SPI_DmaConfig(TX|RX,0).
     * Enabling is allowed with SPE=0 or 1, but not while SPE=1 and BSY=1 (previous transfer
     * still in progress: BUSY, no write); with SPE=0 a stuck BSY is ignored (ERRATA ES0431 2.14.1
     * / ES0430 2.18.1 / ES0523 2.14.1). Disabling requires SPE=0 (STATE). */
    /* Caller: follow the start / stop order above; DMA buffers stay valid until the DMA channel is
     * disabled; bound the BUSY retries (CCL-2 SPI-22, file header). */
    /* Level: PARAM[F] STATE[r] BUSY[r] */
    SPL_Status SPI_DmaConfig(SPI_TypeDef *spi, uint32_t dma_mask, uint8_t enable);

/* DMA packing options (data size <= 8 bits, DMA PSIZE = 16 bit). */
#define SPI_PACK_RX16 0x1U   /* FRXTH=0: RXNE per two frames (16-bit DR reads). */
#define SPI_PACK_TX_ODD 0x2U /* LDMA_TX=1: odd number of TX frames. */
#define SPI_PACK_RX_ODD 0x4U /* LDMA_RX=1: odd number of RX frames; needs RX16. */
    /* Requires SPE=0 (STATE). Non-zero options require data size <= 8 bits
     * (STATE). options=0 restores FRXTH for the data size and clears LDMA_*. */
    /* Caller: DMA PSIZE = 16 bit with packing; LDMA_TX / LDMA_RX must match the parity of the DMA
     * frame count; no SPI_TryRead8 while SPI_PACK_RX16 is active. */
    /* Level: PARAM[F] STATE[r] */
    SPL_Status SPI_DmaPackingConfig(SPI_TypeDef *spi, uint32_t options);

    /* FTLVL / FRLVL as 0..3 (empty, 1/4, 1/2, full). */
    /* Caller: none beyond the file-header duties. */
    /* Level: PARAM[F] */
    SPL_Status SPI_GetFifoLevels(SPI_TypeDef *spi, uint32_t *tx_level, uint32_t *rx_level);

/* Error flags in SPIx_SR reported by SPI_GetErrors. */
#define SPI_ERRORS_ALL (SPI_SR_OVR | SPI_SR_MODF | SPI_SR_CRCERR | SPI_SR_FRE)
    /* One SR read; *errors = SR & SPI_ERRORS_ALL. This read clears FRE in
     * hardware (TI slave frame error). No register write. */
    /* Caller: after FRE disable the SPI and restart the exchange (RM0440 42.5.11). */
    /* Level: PARAM[F] */
    SPL_Status SPI_GetErrors(SPI_TypeDef *spi, uint32_t *errors);
    /* MODF clear (RM0440 42.5.11): SR read while MODF=1, then a CR1 write.
     * The written CR1 keeps SPE=0 and MSTR=0 (hardware already cleared them).
     * No write when MODF=0. */
    /* Caller: keep NSS high during the sequence; then SPI_Init / SPI_MasterInit and SPI_Enable
     * once NSS is no longer driven by another master (CCL-2 SPI-10c). */
    /* Level: PARAM[F] */
    SPL_Status SPI_ClearModeFault(SPI_TypeDef *spi);
    /* CRCERR is rc_w0: writes 0 to CRCERR only (other written bits are
     * read-only ones). No write when CRCERR=0. */
    /* Caller: none beyond the file-header duties. */
    /* Level: PARAM[F] */
    SPL_Status SPI_ClearCrcError(SPI_TypeDef *spi);
    /* Sets CRCNEXT after the last data frame was written (CPU-managed CRC).
     * Requires CRCEN=1 and SPE=1 (else STATE). */
    /* Caller: call right after the last data frame is written; read the received CRC frame and
     * check CRCERR (SPI_GetErrors) afterwards. */
    /* Level: PARAM[F] STATE[r] */
    SPL_Status SPI_CrcNext(SPI_TypeDef *spi);

#ifdef __cplusplus
}
#endif
#endif
