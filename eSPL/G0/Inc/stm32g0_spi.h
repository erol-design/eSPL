#ifndef STM32G0_SPI_H
#define STM32G0_SPI_H
#include <stdint.h>
#include "stm32g0xx.h"
#include "stm32g0_status.h"

#ifdef __cplusplus
extern "C"
{
#endif
/* SPI driver for STM32G0 (RM0444 Rev 6 section 35), with the I2S mode of SPI1 / SPI2 (v0.10).
 * Scope: SPI1, SPI2, SPI3 on G0B1 (SPI3 guarded for G0 SKUs without it); legacy API (v0.8 ..
 * v0.10), extended configuration (v0.11) and I2S (last section).
 * Usage: SPI_Init (or the legacy SPI_MasterInit) with SPE = 0, optional SPI_ITConfig /
 * SPI_DmaConfig / SPI_DmaPackingConfig, SPI_Enable(spi, 1), then the Try* transfers;
 * SPI_Enable(spi, 0) to stop.
 * Common rules: no dynamic memory, no static state; no function waits (BUSY / WOULD_BLOCK are
 * returned instead); not reentrant per instance (see the serialization duty below); output
 * parameters are unchanged unless SPL_OK (METHOD 1.8). Any read of SPIx_SR clears FRE by hardware
 * (RM0444 35.9.3); every service that checks a flag reads SR. In TI slave mode capture FRE with
 * SPI_GetErrors before calling other services.
 * Caller duties (all APIs; not done by the driver):
 *  - Clock: RCC bus clock and reset of the instance; prescaler values divide PCLK (I2S: kernel
 *    clock RCC_KERNEL_I2S1 / RCC_KERNEL_I2S2 selected before I2S_Init).
 *  - Pins: SCK / MISO / MOSI / NSS (I2S: CK / WS / SD / MCK) in their GPIO alternate function;
 *    with software NSS the chip select is a GPIO driven by the caller.
 *  - Interrupts / DMA: NVIC line, priority and handler; DMA channel / DMAMUX setup.
 *  - Serialize all calls per instance (ISR included); CR1 / CR2 are read-modify-written.
 * Contract: Docs/v0.3_SPI_CONTRACT.md (SPI; I2S mode is out of its scope); I2S:
 * Docs/v0.10_COMM_MODES.md. */

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
/* Level: PARAM[F] STATE[F] HW[r] */
SPL_Status SPI_MasterInit(SPI_TypeDef *spi, const SPI_InitTypeDef *cfg);
/* Disabling returns SPL_BUSY while BSY=1 (slave: only with TX FIFO not empty, see
 * ERRATA below), or while TX FIFO is not empty in master mode.
 * v0.11.1: slave with TX FIFO not empty and BSY=0 -> ERR_STATE (the master may have
 * stopped clocking; only RCC_AssertPeriphReset/RCC_ReleasePeriphReset empties it).
 * Caller may drain RX FIFO using SPI_TryRead8 while SPE=0.
 * OVR reports SPL_ERR_HW and requires a separate recovery procedure.
 * v0.11: enabling with MODF set returns SPL_ERR_HW without a write
 * (hardware refuses SPE=1 while MODF=1, RM0444 35.5.11).
 * ERRATA ES0548 2.14.2: slave (MSTR=0) with TX FIFO empty
 * disables even while BSY=1 (BSY may stick high). The caller detects the end
 * of the slave transaction first (NSS high, or the expected RXNE count) and
 * calls SPI_Enable(0) only then. Slave BUSY (BSY=1, FTLVL!=0): bound the
 * retries, then RCC reset. Master: BSY=1 or FTLVL!=0 -> BUSY (unchanged).
 * ERRATA ES0548 2.14.1: master disables only with FTLVL=0
 * (TXE=1) and BSY=0; master RX-only is refused by SPI_Init; slave: do not
 * remove NSS during a frame. */
/* Caller: enable = 0: wait for the end of the transfer (slave: NSS high or the expected RXNE
 * count, ES0548 2.14.2); bound BUSY retries; after slave ERR_STATE use RCC reset and init again;
 * drain the RX FIFO before the next enable. With DMA follow the SPI_DmaConfig order. */
/* Level: PARAM[F] STATE[r] HW[r] BUSY[r] */
SPL_Status SPI_Enable(SPI_TypeDef *spi, uint8_t enable);
/* 8-bit DR access. v0.11: SPL_ERR_STATE when the data size is > 8 bits. */
/* Caller: in full duplex read one RX frame per written frame, else OVR (SPI_TryRead8). */
/* Level: PARAM[F] STATE[F] WOULD_BLOCK[r] */
SPL_Status SPI_TryWrite8(SPI_TypeDef *spi, uint8_t data);
/* Caller: after ERR_HW (OVR) data is already lost: SPI_ClearOverrun and resynchronise with
 * the peer. */
/* Level: PARAM[F] STATE[F] HW[r] WOULD_BLOCK[r] */
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

/* NSS management (RM0444 35.5.5). */
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
 * final value) so the CRC is reset (RM0444 35.5.14). */
/* Caller: chip select per cfg->nss (SPI_NSS_SOFT master: caller GPIO); interrupt and DMA enables
 * are cleared here, set them again (SPI_ITConfig / SPI_DmaConfig) after each init. */
/* Level: PARAM[F] STATE[F] HW[r] */
SPL_Status SPI_Init(SPI_TypeDef *spi, const SPI_ConfigTypeDef *cfg);

/* 16-bit DR access; requires data size > 8 bits (else STATE). Write also
 * requires SPE=1. Read returns ERR_HW while OVR is set. */
/* Caller: in full duplex read one RX frame per written frame, else OVR (SPI_TryRead16). */
/* Level: PARAM[F] STATE[F] WOULD_BLOCK[r] */
SPL_Status SPI_TryWrite16(SPI_TypeDef *spi, uint16_t data);
/* Caller: after ERR_HW (OVR) data is already lost: SPI_ClearOverrun and resynchronise with
 * the peer. */
/* Level: PARAM[F] STATE[F] HW[r] WOULD_BLOCK[r] */
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
/* RM0444 35.5.9 start order: SPI_DmaConfig(RX,1); enable DMA channels;
 * SPI_DmaConfig(TX,1); SPI_Enable(1). Stop order: disable DMA channels;
 * SPI_Enable(0) (retry while BUSY); SPI_DmaConfig(TX|RX,0).
 * Enabling is allowed with SPE=0 or 1; disabling requires SPE=0 (STATE). */
/* Caller: follow the start / stop order above; DMA buffers stay valid until the DMA channel is
 * disabled. */
/* Level: PARAM[F] STATE[F] */
SPL_Status SPI_DmaConfig(SPI_TypeDef *spi, uint32_t dma_mask, uint8_t enable);

/* DMA packing options (data size <= 8 bits, DMA PSIZE = 16 bit). */
#define SPI_PACK_RX16 0x1U   /* FRXTH=0: RXNE per two frames (16-bit DR reads). */
#define SPI_PACK_TX_ODD 0x2U /* LDMA_TX=1: odd number of TX frames. */
#define SPI_PACK_RX_ODD 0x4U /* LDMA_RX=1: odd number of RX frames; needs RX16. */
/* Requires SPE=0 (STATE). Non-zero options require data size <= 8 bits
 * (STATE). options=0 restores FRXTH for the data size and clears LDMA_*.
 * While SPI_PACK_RX16 is active, do not use SPI_TryRead8. */
/* Caller: DMA PSIZE = 16 bit with packing; LDMA_TX / LDMA_RX must match the parity of the DMA
 * frame count. */
/* Level: PARAM[F] STATE[F] */
SPL_Status SPI_DmaPackingConfig(SPI_TypeDef *spi, uint32_t options);

/* FTLVL / FRLVL as 0..3 (empty, 1/4, 1/2, full). */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status SPI_GetFifoLevels(SPI_TypeDef *spi, uint32_t *tx_level, uint32_t *rx_level);

/* Error flags in SPIx_SR reported by SPI_GetErrors. */
#define SPI_ERRORS_ALL (SPI_SR_OVR | SPI_SR_MODF | SPI_SR_CRCERR | SPI_SR_FRE)
/* One SR read; *errors = SR & SPI_ERRORS_ALL. This read clears FRE in
 * hardware (TI slave frame error): disable the SPI and restart the
 * exchange after FRE (RM0444 35.5.11). No register write. */
/* Caller: after FRE disable the SPI and restart the exchange (RM0444 35.5.11). */
/* Level: PARAM[F] */
SPL_Status SPI_GetErrors(SPI_TypeDef *spi, uint32_t *errors);
/* MODF clear (RM0444 35.5.11): SR read while MODF=1, then a CR1 write.
 * The written CR1 keeps SPE=0 and MSTR=0 (hardware already cleared them);
 * restore the role with SPI_Init/SPI_MasterInit and SPI_Enable. Keep NSS
 * high during the sequence. No write when MODF=0. */
/* Caller: keep NSS high during the sequence; then SPI_Init / SPI_MasterInit and SPI_Enable. */
/* Level: PARAM[F] */
SPL_Status SPI_ClearModeFault(SPI_TypeDef *spi);
/* CRCERR is rc_w0: writes 0 to CRCERR only (other written bits are
 * read-only ones). No write when CRCERR=0. */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status SPI_ClearCrcError(SPI_TypeDef *spi);
/* Sets CRCNEXT after the last data frame was written (CPU-managed CRC).
 * Requires CRCEN=1 and SPE=1 (else STATE). */
/* Caller: call right after the last data frame is written; read the received CRC frame and check
 * CRCERR (SPI_GetErrors) afterwards. */
/* Level: PARAM[F] STATE[F] */
SPL_Status SPI_CrcNext(SPI_TypeDef *spi);

/* ---- v0.10 (G0): I2S mode, Docs/v0.10_COMM_MODES.md, RM0444 35.7 / 35.9.8 / 35.9.9 ----
 * SPI1 on every SKU, SPI2 only on G0B0 / G0B1 / G0C1 (SPL_G0_SPI2_I2S); SPI3 and SPI2 elsewhere ->
 * SPL_ERR_UNSUPPORTED. Kernel clock: RCC_KERNEL_I2S1 / RCC_KERNEL_I2S2. 24 / 32-bit samples take
 * two 16-bit DR accesses (most significant half first, RM0444 35.7.2). No errata items for I2S in
 * the G0 errata sheets (v0.10 scan). */
typedef struct
{
    uint8_t mode;        /* I2SCFG: 0 slave TX, 1 slave RX, 2 master TX, 3 master RX */
    uint8_t standard;    /* I2SSTD: 0 Philips, 1 MSB justified, 2 LSB justified, 3 PCM */
    uint8_t pcm_long;    /* PCMSYNC: long frame sync (PCM only) */
    uint8_t data_length; /* DATLEN: 0 = 16, 1 = 24, 2 = 32 bit (24 / 32 need channel_32) */
    uint8_t channel_32;  /* CHLEN: 32-bit channel */
    uint8_t cpol;        /* CKPOL */
    uint8_t mclk_out;    /* MCKOE (master) */
    uint8_t divider;     /* I2SDIV 2..255 in master mode (0 / 1 forbidden) */
    uint8_t odd;         /* ODD: divider x 2 + 1 */
    uint8_t async_start; /* ASTRTEN (slave) */
} I2S_InitTypeDef;
/* Writes I2SPR and I2SCFGR (I2SMOD = 1, I2SE = 0). I2SE = 1 or SPE = 1 -> STATE. */
/* Caller: divider / odd assume the selected I2S kernel clock frequency (file header). */
/* Level: PARAM[F] STATE[F] UNSUPPORTED[F] */
SPL_Status I2S_Init(SPI_TypeDef *spi, const I2S_InitTypeDef *cfg);
/* I2SE = enable; I2SMOD = 0 (not initialised for I2S) -> STATE. */
/* Caller: enable = 0 is not gated on TXE / BSY: finish the current frame (RM0444 35.7 disable
 * procedure) before calling, else the frame is cut. */
/* Level: PARAM[F] STATE[F] UNSUPPORTED[F] */
SPL_Status I2S_Enable(SPI_TypeDef *spi, uint8_t enable);
/* UDR / FRE -> ERR_HW; TXE = 0 -> WOULD_BLOCK; else 16-bit DR write. */
/* Caller: 24 / 32-bit samples take two calls, most significant half first; keep TXE serviced in
 * time, else UDR (slave). After ERR_HW: I2S_ClearErrors. */
/* Level: PARAM[F] UNSUPPORTED[F] HW[r] WOULD_BLOCK[r] */
SPL_Status I2S_TryWrite(SPI_TypeDef *spi, uint16_t data);
/* OVR / FRE -> ERR_HW; RXNE = 0 -> WOULD_BLOCK; else *data = DR, *right_channel = CHSIDE. */
/* Caller: 24 / 32-bit samples take two calls; after ERR_HW I2S_ClearErrors and resynchronise the
 * channel with right_channel (CHSIDE). */
/* Level: PARAM[F] UNSUPPORTED[F] HW[r] WOULD_BLOCK[r] */
SPL_Status I2S_TryRead(SPI_TypeDef *spi, uint16_t *data, uint8_t *right_channel);
/* *errors = SR & (UDR | OVR | FRE). */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] UNSUPPORTED[F] */
SPL_Status I2S_GetErrors(SPI_TypeDef *spi, uint32_t *errors);
/* Clears OVR (DR read, then SR read; the word read is discarded), UDR and FRE (SR read). */
/* Caller: the OVR clear discards one word: resynchronise the channel (CHSIDE) afterwards. */
/* Level: PARAM[F] UNSUPPORTED[F] */
SPL_Status I2S_ClearErrors(SPI_TypeDef *spi);

#ifdef __cplusplus
}
#endif
#endif
