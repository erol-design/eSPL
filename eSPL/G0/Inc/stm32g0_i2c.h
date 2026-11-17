/* SPDX-License-Identifier: MIT */
#ifndef STM32G0_I2C_H
#define STM32G0_I2C_H
#include <stdint.h>
#include "stm32g0xx.h"
#include "stm32g0_status.h"

#ifdef __cplusplus
extern "C"
{
#endif
/* I2C driver for STM32G0 (RM0444 Rev 6 section 32): controller (master) transfers, long and 10-bit
 * transfers (v0.10), target (slave) mode and SMBus (v0.10, last section).
 * Scope: the base API is a single chunk of 1..255 bytes, master, 7-bit address, nonblocking byte
 * service; I2C_StartLong / I2C_RestartLong extend it to longer and 10-bit transfers.
 * Usage: I2C_MasterInit, then per transfer I2C_Start (or I2C_StartNoStop / I2C_Restart),
 * I2C_TryWrite / I2C_TryRead per byte, end with I2C_StopDetected / I2C_ClearStop (AUTOEND) or
 * I2C_TransferComplete and I2C_Stop / I2C_Restart (software end).
 * Common rules: no dynamic memory, no static state (long-transfer bookkeeping is caller-owned);
 * no function waits for a flag (I2C_TargetTimeoutRecover holds PE = 0 for three CR1 reads); not
 * reentrant per instance (see the serialization duty below); output parameters are unchanged
 * unless SPL_OK (METHOD 1.8).
 * ERRATA ES0548 2.11.1: SDA is mis-sampled when tSU;DAT < one
 * I2CCLK period; for spec-minimum transmitters use I2CCLK >= 4 MHz (Sm),
 * >= 10 MHz (Fm), >= 20 MHz (Fm+) (caller's RCC kernel clock choice).
 * ERRATA ES0548 2.11.4: TX may stall after the first byte
 * when PCLK/I2CCLK is in 1.5..3 (the driver never prefills TXDR): keep the
 * ratio < 1.5 or > 3 (e.g. I2CCLK = PCLK; G0 has a single APB clock PCLK).
 * ERRATA ES0548 2.11.3 / 2.11.5: target-mode items, see the v0.10 target API at the end of this
 * header (2.11.3 CALLER, 2.11.5 driver recovery API).
 * Caller duties (all APIs; not done by the driver):
 *  - Clock: RCC bus clock of the instance and the kernel clock (RCC_KERNEL_I2C1 / RCC_KERNEL_I2C2;
 *    I2C3 runs on PCLK); timingr and timeoutr values are computed for that I2CCLK frequency.
 *  - Pins: SCL / SDA (SMBA with I2C_SMBUS_ALERT) in alternate function, open drain, with external
 *    pull-ups; bus recovery of a stuck SDA is the board layer (see I2C_SoftwareReset).
 *  - Retry bounds: WOULD_BLOCK / BUSY on a stuck bus repeat forever; the caller bounds them.
 *  - No interrupt or DMA API: NVIC, handler and DMA setup are the caller's if used.
 *  - Serialize all calls per instance (ISR included).
 * Contract: Docs/v0.3_I2C_CONTRACT.md; target mode and SMBus: Docs/v0.10_COMM_MODES.md. */
/* Caller: none beyond the file-header duties (timingr for the selected I2CCLK, errata ratios
 * above). */
/* Level: PARAM[F] STATE[F] */
SPL_Status I2C_MasterInit(I2C_TypeDef *i2c, uint32_t timingr);
/* Start/StartNoStop/StartLong: TC=1 or TCR=1 (bus still held by the
 * previous transfer) => STATE, finish it with Stop/Restart/ReloadService.
 * BUSY=1 or START still pending => BUSY. CR2.START is read before ISR
 * (v0.11.1), so a START completing between the reads is still BUSY.
 * Stuck bus (SDA/SCL held low): START stays pending with ISR.BUSY=0 and
 * BUSY is returned indefinitely (no START is ever detected, RM0444
 * 32.9.2/32.9.7). Bound the retries; on expiry call I2C_SoftwareReset
 * (PE=0 clears START) and recover the bus. I2C_TimeoutConfig with
 * TIDLE=0 detects SCL held low only (RM0444 32.4.17 TIMEOUT): it
 * clears START and sets ISR.TIMEOUT (I2C_GetErrors; ERR_HW from the
 * *Long APIs only). SDA held low with SCL high needs the retry bound. */
/* address7 0..127; reserved addresses (0x00-0x07, 0x78-0x7F) are not refused: 0x00 is the
 * general call (G4 v0.13.5 #18 decision). */
/* Caller: bound the BUSY retries, then I2C_SoftwareReset and bus recovery; transfer exactly
 * length bytes with I2C_TryWrite / I2C_TryRead (STOP is automatic, AUTOEND). */
/* Level: PARAM[F] STATE[F] HW[r] BUSY[r] */
SPL_Status I2C_Start(I2C_TypeDef *i2c, uint8_t address7, uint8_t length, uint8_t read);
/* Software-end first phase: complete NBYTES then wait for TC.
 * AUTOEND=0, no automatic STOP. 1..255 bytes. */
/* Caller: bound the BUSY retries as for I2C_Start; after TC end the bus with I2C_Restart or
 * I2C_Stop (the bus stays held until then). */
/* Level: PARAM[F] STATE[F] HW[r] BUSY[r] */
SPL_Status I2C_StartNoStop(I2C_TypeDef *i2c, uint8_t address7, uint8_t length, uint8_t read);
/* Repeated START allowed only after TC. Does not block. TC=0: WOULD_BLOCK
 * while a software-end transfer runs (BUSY=1) or START is pending; STATE
 * when TC cannot set (BUSY=0, STOPF=1 or TCR=1; RM0444 32.9.7).
 * CR2.START is read before ISR (v0.11.1 I2C-M1). A START pending on a
 * stuck bus gives WOULD_BLOCK indefinitely: bound the poll as for Start. */
/* Caller: bound the WOULD_BLOCK poll (stuck bus); autoend = 0 needs a later I2C_Stop or
 * I2C_Restart. */
/* Level: PARAM[F] STATE[F] HW[r] WOULD_BLOCK[r] */
SPL_Status I2C_Restart(I2C_TypeDef *i2c, uint8_t address7, uint8_t length, uint8_t read,
                       uint8_t autoend);
/* Caller: after ERR_HW capture ISR (I2C_GetErrors), then I2C_ClearErrors; BERR alone is not
 * reported here (ES0548 2.11.2 above). */
/* Level: PARAM[F] HW[r] */
SPL_Status I2C_TransferComplete(I2C_TypeDef *i2c, uint8_t *complete);
/* *detected = ISR.STOPF (not acknowledged). v0.13.3: NACKF or ARLO set ->
 * SPL_ERR_HW, *detected untouched (a target NACK in an AUTOEND transfer
 * also ends with STOPF). */
/* Caller: acknowledge STOPF with I2C_ClearStop; after ERR_HW diagnose, then I2C_ClearErrors. */
/* Level: PARAM[F] HW[r] */
SPL_Status I2C_StopDetected(I2C_TypeDef *i2c, uint8_t *detected);
/* Byte services require PE=1; otherwise ERR_STATE without register writes
 * or output mutation. Serialize access to a given instance.
 * ERRATA ES0548 2.11.2: TryWrite, TryRead, TransferComplete,
 * Restart, RestartLong and ReloadService return ERR_HW on NACKF/ARLO (Long
 * APIs also OVR/PECERR/TIMEOUT) but NOT on BERR alone: a controller-mode
 * BERR may be spurious and the transfer continues. BERR stays visible
 * (I2C_GetErrors / I2C_Status); clear it with I2C_ClearErrorFlags(BERR)
 * before the next fresh START (Start/StartNoStop/StartLong still return
 * ERR_HW while BERR=1). */
/* Caller: write exactly the programmed byte count; clear a left BERR with
 * I2C_ClearErrorFlags(I2C_ISR_BERR) before the next fresh START (ES0548 2.11.2). */
/* Level: PARAM[F] STATE[F] HW[r] WOULD_BLOCK[r] */
SPL_Status I2C_TryWrite(I2C_TypeDef *i2c, uint8_t byte);
/* Caller: read exactly the programmed byte count; clear a left BERR with
 * I2C_ClearErrorFlags(I2C_ISR_BERR) before the next fresh START (ES0548 2.11.2). */
/* Level: PARAM[F] STATE[F] HW[r] WOULD_BLOCK[r] */
SPL_Status I2C_TryRead(I2C_TypeDef *i2c, uint8_t *byte);
/* Caller: not gated on TC: call only after TC of a software-end transfer
 * (I2C_TransferComplete), else the running byte transfer is cut by the STOP. */
/* Level: PARAM[F] */
SPL_Status I2C_Stop(I2C_TypeDef *i2c);
/* Clears NACK/BERR/ARLO/OVR only; does NOT acknowledge STOPF.
 * Capture ISR and diagnose the error before clearing. */
/* Caller: capture ISR and diagnose before clearing; acknowledge STOPF separately
 * (I2C_ClearStop). */
/* Level: PARAM[F] */
SPL_Status I2C_ClearErrors(I2C_TypeDef *i2c);
/* Acknowledge STOPF explicitly; does not clear error flags. */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status I2C_ClearStop(I2C_TypeDef *i2c);
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status I2C_Status(I2C_TypeDef *i2c, uint32_t *status);

/* ---- v0.10: long transfers, 10-bit addressing, errors, reset, timeout ---- */

/* I2C_ISR error flags reported by I2C_GetErrors and accepted by
 * I2C_ClearErrorFlags. Each ISR flag has its clear bit at the same position
 * in I2C_ICR (RM0444 32.9.7 / 32.9.8). ALERT (SMBus host) is not included. PECERR / TIMEOUT exist
 * only on SMBus instances (I2C1; I2C2 on G0B0 / G0B1 / G0C1): clearing them on another instance
 * -> I2C_ClearErrorFlags UNSUPPORTED (RM0444 32.9.8, reserved there). */
#define I2C_ERRORS_ALL                                                                             \
    (I2C_ISR_NACKF | I2C_ISR_BERR | I2C_ISR_ARLO | I2C_ISR_OVR | I2C_ISR_PECERR | I2C_ISR_TIMEOUT)

/* Option bits for I2C_StartLong / I2C_RestartLong. */
#define I2C_XFER_READ 0x01U    /* RD_WRN=1 (controller receiver). */
#define I2C_XFER_AUTOEND 0x02U /* STOP after the last chunk; else TC. */
#define I2C_XFER_ADDR10 0x04U  /* ADD10=1, address is 10 bits. */
#define I2C_XFER_HEAD10R 0x08U /* Header-only read; RestartLong only. */

/* Caller-owned long transfer bookkeeping. The driver has no hidden state:
 * one object per active transfer, owned and serialized by the caller. */
typedef struct
{
    uint32_t remaining; /* Bytes not yet programmed into NBYTES. */
    uint32_t autoend;   /* 1: STOP after the last chunk; 0: software end (TC). */
} I2C_LongTransferTypeDef;

/* Fresh START for 1..0xFFFFFFFF bytes. Chunks of up to 255 bytes; RELOAD
 * on every chunk but the last; AUTOEND only on the last chunk. Requires
 * PE=1, no error flags, BUSY=0 and START=0. Clears a stale STOPF. */
/* Caller: keep *xfer alive and unchanged until the last chunk (or I2C_SoftwareReset); call
 * I2C_ReloadService on each TCR (SCL is stretched meanwhile). */
/* Level: PARAM[F] STATE[F] HW[r] BUSY[r] */
SPL_Status I2C_StartLong(I2C_TypeDef *i2c, I2C_LongTransferTypeDef *xfer, uint16_t address,
                         uint32_t options, uint32_t length);
/* Repeated START after TC (software end). Same chunking as I2C_StartLong.
 * TC=0 handling as I2C_Restart (WOULD_BLOCK or STATE).
 * HEAD10R is accepted here only, with ADDR10 and READ. */
/* Caller: as I2C_StartLong; bound the WOULD_BLOCK poll (stuck bus). */
/* Level: PARAM[F] STATE[F] HW[r] WOULD_BLOCK[r] */
SPL_Status I2C_RestartLong(I2C_TypeDef *i2c, I2C_LongTransferTypeDef *xfer, uint16_t address,
                           uint32_t options, uint32_t length);
/* Nonblocking: call when TCR may be set. Programs the next chunk, clears
 * RELOAD and applies AUTOEND on the last chunk, decrements remaining.
 * WOULD_BLOCK while TCR=0; STATE when nothing remains. */
/* Caller: pass the same *xfer used by I2C_StartLong / I2C_RestartLong for this transfer. */
/* Level: PARAM[F] STATE[F] HW[r] WOULD_BLOCK[r] */
SPL_Status I2C_ReloadService(I2C_TypeDef *i2c, I2C_LongTransferTypeDef *xfer);

/* Reads ISR & I2C_ERRORS_ALL (no side effects). */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status I2C_GetErrors(I2C_TypeDef *i2c, uint32_t *errors);
/* Clears the given ISR error flags (subset of I2C_ERRORS_ALL, non-zero)
 * by writing their ICR bits. Never touches STOPF or ADDR. When NACKF, BERR,
 * ARLO, PECERR or TIMEOUT is cleared, TXDR is also flushed (ISR.TXE = 1), so a
 * byte loaded for the failed transfer is not sent by the next one; OVR alone
 * keeps TXDR. */
/* Caller: capture and diagnose the flags first; a TXDR byte flushed here must be sent again by
 * the next transfer if still needed. */
/* Level: PARAM[F] UNSUPPORTED[F] */
SPL_Status I2C_ClearErrorFlags(I2C_TypeDef *i2c, uint32_t errors);

/* RM0444 32.4.6 software reset: PE=0, read back PE=0 (the RM sequence that
 * keeps PE low for at least three APB clock cycles), then PE=1 when
 * reenable=1. Configuration registers are kept; CR2 commands and status
 * flags are reset by hardware. Any I2C_LongTransferTypeDef in flight is
 * void afterwards.
 * Bus recovery (SDA held low by a target) is NOT done here: the board layer
 * must switch SCL to GPIO open-drain, clock up to 9 pulses until SDA is
 * high, generate a STOP, restore the AF mapping, then call this reset. */
/* Caller: recover a stuck bus first (board layer, see above); discard any
 * I2C_LongTransferTypeDef in flight. */
/* Level: PARAM[F] HW[r] */
SPL_Status I2C_SoftwareReset(I2C_TypeDef *i2c, uint8_t reenable);

/* Writes I2C_TIMEOUTR (TIMEOUTA, TIDLE, TIMOUTEN, TIMEOUTB, TEXTEN).
 * Reserved bits must be zero. Rejected with BUSY while ISR.BUSY=1. The
 * enables are cleared before the fields are written, as the RM requires.
 * G0 difference: I2C3 has no SMBus support on G0B1 (RM0444 Table 165) ->
 * SPL_ERR_UNSUPPORTED, nothing written (checked after the parameters). */
/* Caller: none beyond the file-header duties (timeoutr for the selected I2CCLK). */
/* Level: PARAM[F] UNSUPPORTED[F] BUSY[r] */
SPL_Status I2C_TimeoutConfig(I2C_TypeDef *i2c, uint32_t timeoutr);

/* ---- v0.10 (G0): target (slave) mode and SMBus, Docs/v0.10_COMM_MODES.md ----
 * Sequence (RM0444 32.4.8): I2C_TargetInit -> per transfer: I2C_TargetPollAddress (ADDR) ->
 * [SBC = 1: I2C_TargetSetBytes, then > 3 I2C kernel clocks] -> I2C_TargetAckAddress ->
 * I2C_TargetTryWrite / I2C_TargetTryRead per byte -> I2C_StopDetected / I2C_ClearStop (and
 * I2C_ClearErrorFlags(NACKF) after a controller read). Target transmitter: flush a stale TXDR
 * with I2C_TargetFlushTx before acknowledging a read address.
 * ERRATA ES0418 2.11.3 / ES0412 2.12.3 / ES0468 2.8.3 / ES0486 2.8.3 / ES0487 2.9.3 / ES0488
 * 2.10.3 (spurious controller transfer on own address match, controller + target at once):
 * I2C_TargetAckAddress applies ST's sequence (ADDRCF, then CR2 written with START low; the
 * companion documentation item ES0487 2.9.4 / ES0418 2.11.4 / ES0412 2.12.4 says START is cleared
 * by ADDRCF, not by the address match); in
 * SBC = 1 mode the caller keeps > 3 I2C kernel clocks between I2C_TargetSetBytes and
 * I2C_TargetAckAddress, and writes CR2 before the STOP (clock stretching ends at ADDRCF).
 * ERRATA ES0548 2.11.3 (family): with NOSTRETCH = 1 a TXDR write near the start of a byte can
 * send 0xFF without OVR; no workaround (keep NOSTRETCH = 0 when the data timing is not fixed).
 * ERRATA ES0548 2.11.5 (family): SMBus timeout in target mode leaves SDA low; on TIMEOUT call
 * I2C_TargetTimeoutRecover once the frame should have ended. */
typedef struct
{
    uint32_t timingr;     /* TIMINGR (SDADEL / SCLDEL are used in target mode too); != 0 */
    uint16_t own_address; /* OA1: 7-bit 0..127, or 10-bit 0..1023 with addr10 = 1 */
    uint8_t addr10;       /* OA1MODE */
    uint8_t own_address2; /* OA2, 7-bit 0..127 */
    uint8_t oa2_mask;     /* OA2MSK 0..7 (number of masked low address bits) */
    uint8_t oa2_enable;   /* OA2EN */
    uint8_t general_call; /* GCEN */
    uint8_t no_stretch;   /* NOSTRETCH (not with byte_control) */
    uint8_t byte_control; /* SBC (needed for SMBus / PEC in target mode) */
    uint8_t wakeup;       /* WUPEN: I2C1, I2C2 on G0B0 / G0B1 / G0C1 only -> else UNSUPPORTED */
} I2C_TargetConfigTypeDef;

/* PE = 1 -> STATE (NOSTRETCH writable only with PE = 0). PARAM: field ranges, SBC + NOSTRETCH
 * (RM0444 32.4.8 caution). Writes TIMINGR, OAR1, OAR2 (each disabled before the new value) and
 * CR1 = PE | GCEN | NOSTRETCH | SBC | WUPEN (DNF = 0, interrupts / DMA off). */
/* Caller: then serve each transfer in the sequence above (poll address, acknowledge, bytes,
 * STOP). */
/* Level: PARAM[F] STATE[F] UNSUPPORTED[F] */
SPL_Status I2C_TargetInit(I2C_TypeDef *i2c, const I2C_TargetConfigTypeDef *cfg);
/* ADDR = 0 -> WOULD_BLOCK. Else *addcode = ADDCODE (7-bit address or 10-bit header code) and
 * *controller_reads = DIR (1: the controller reads, we transmit). ADDR is not cleared. */
/* Caller: then I2C_TargetAckAddress (SCL stretched until ADDRCF); SBC = 1: I2C_TargetSetBytes
 * first; controller read: I2C_TargetFlushTx first. */
/* Level: PARAM[F] WOULD_BLOCK[r] */
SPL_Status I2C_TargetPollAddress(I2C_TypeDef *i2c, uint8_t *addcode, uint8_t *controller_reads);
/* SBC = 1 only (else STATE): CR2 NBYTES = nbytes, RELOAD = reload, START low. */
/* Caller: keep > 3 I2C kernel clocks between this call and I2C_TargetAckAddress (errata
 * above). */
/* Level: PARAM[F] STATE[F] */
SPL_Status I2C_TargetSetBytes(I2C_TypeDef *i2c, uint8_t nbytes, uint8_t reload);
/* ADDR = 0 -> STATE. ADDRCF, then CR2 rewritten with START low (errata above). */
/* Caller: controller read: flush a stale TXDR (I2C_TargetFlushTx) first; SBC = 1: > 3 I2C kernel
 * clocks after I2C_TargetSetBytes (errata above). */
/* Level: PARAM[F] STATE[F] */
SPL_Status I2C_TargetAckAddress(I2C_TypeDef *i2c);
/* CR2.NACK = 1: NACK the next received byte (target receiver; RM0444 32.9.2). PE = 0 -> STATE. */
/* Caller: set before the ACK phase of the byte to be refused (RM0444 32.9.2). */
/* Level: PARAM[F] STATE[F] */
SPL_Status I2C_TargetNack(I2C_TypeDef *i2c);
/* ISR.TXE = 1: flushes TXDR (RM0444 32.9.7). */
/* Caller: none beyond the file-header duties (use before acknowledging a read address). */
/* Level: PARAM[F] */
SPL_Status I2C_TargetFlushTx(I2C_TypeDef *i2c);
/* OVR / BERR -> ERR_HW; NACKF -> STATE (the controller ended its read); TXIS = 0 -> WOULD_BLOCK;
 * else TXDR = byte. */
/* Caller: after STATE (NACKF) clear NACKF with I2C_ClearErrorFlags, then I2C_StopDetected /
 * I2C_ClearStop; NOSTRETCH = 1: write TXDR in time (ES0548 2.11.3 above). */
/* Level: PARAM[F] STATE[r] HW[r] WOULD_BLOCK[r] */
SPL_Status I2C_TargetTryWrite(I2C_TypeDef *i2c, uint8_t byte);
/* OVR / BERR -> ERR_HW; RXNE = 0 -> WOULD_BLOCK; else *byte = RXDR. */
/* Caller: NOSTRETCH = 1: read before the next byte completes, else OVR; after ERR_HW clear the
 * flags with I2C_ClearErrorFlags. */
/* Level: PARAM[F] HW[r] WOULD_BLOCK[r] */
SPL_Status I2C_TargetTryRead(I2C_TypeDef *i2c, uint8_t *byte);
/* ERRATA ES0548 2.11.5 recovery, called explicitly: TIMEOUT = 0 -> STATE (nothing to do);
 * STOPF = 1 -> OK (bus free, nothing written); else PE = 0, three CR1 reads (>= 3 APB clocks),
 * PE = 1. The caller clears TIMEOUT with I2C_ClearErrorFlags. */
/* Caller: call once the frame should have ended; then clear TIMEOUT with
 * I2C_ClearErrorFlags. */
/* Level: PARAM[F] STATE[r] */
SPL_Status I2C_TargetTimeoutRecover(I2C_TypeDef *i2c);

/* SMBus options for I2C_SmbusConfig (CR1 bits). */
#define I2C_SMBUS_HOST I2C_CR1_SMBHEN   /* host address 0b0001000 acknowledged */
#define I2C_SMBUS_DEVICE I2C_CR1_SMBDEN /* device default address 0b1100001 acknowledged */
#define I2C_SMBUS_ALERT I2C_CR1_ALERTEN /* SMBA pin: host listens / device drives */
#define I2C_SMBUS_PEC I2C_CR1_PECEN     /* packet error checking */
#define I2C_SMBUS_ALL (I2C_SMBUS_HOST | I2C_SMBUS_DEVICE | I2C_SMBUS_ALERT | I2C_SMBUS_PEC)
/* CR1 SMBus bits = options (subset of I2C_SMBUS_ALL). Instance without SMBus (I2C3; I2C2 outside
 * group 0BX) -> UNSUPPORTED; PE = 1 -> STATE. ALERT flag: I2C_Status / ICR.ALERTCF. */
/* Caller: target-mode PEC needs byte_control = 1 in I2C_TargetInit; SMBA pin set up when
 * I2C_SMBUS_ALERT is used (file header). */
/* Level: PARAM[F] STATE[F] UNSUPPORTED[F] */
SPL_Status I2C_SmbusConfig(I2C_TypeDef *i2c, uint32_t options);
/* CR2.PECBYTE = 1 (PEC transmitted / checked after NBYTES - 1 bytes, RM0444 32.4.12). Requires
 * PE = 1 and PECEN = 1, else STATE. */
/* Caller: NBYTES of the transfer includes the PEC byte. */
/* Level: PARAM[F] STATE[F] */
SPL_Status I2C_SetPecByte(I2C_TypeDef *i2c);
/* *pec = PECR (PECEN = 0 -> STATE). PEC errors: I2C_GetErrors (PECERR). */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] STATE[F] */
SPL_Status I2C_GetPec(I2C_TypeDef *i2c, uint8_t *pec);

#ifdef __cplusplus
}
#endif
#endif
