#ifndef STM32G4_I2C_H
#define STM32G4_I2C_H
#include <stdint.h>
#include "stm32g4xx.h"
#include "stm32g4_status.h"

#ifdef __cplusplus
extern "C"
{
#endif
    /* Single chunk 1..255 bytes, master, 7-bit address, nonblocking byte service.
     * ERRATA ES0431 2.11.1 / ES0430 2.15.1 / ES0523 2.11.1: SDA is mis-sampled when tSU;DAT < one
     * I2CCLK period; for spec-minimum transmitters use I2CCLK >= 4 MHz (Sm),
     * >= 10 MHz (Fm), >= 20 MHz (Fm+) (caller's RCC kernel clock choice).
     * ERRATA ES0431 2.11.5 / ES0430 2.15.5 / ES0523 2.11.4: TX may stall after the first byte
     * when PCLK/I2CCLK is in 1.5..3 (the driver never prefills TXDR): keep the
     * ratio < 1.5 or > 3 (e.g. I2CCLK = PCLK1).
     * Target-mode errata are handled by the v0.17 target APIs below. */
    /* Seviye: PARAM[S] STATE[r] */
    SPL_Status I2C_MasterInit(I2C_TypeDef *i2c, uint32_t timingr);
    /* Start/StartNoStop/StartLong: TC=1 or TCR=1 (bus still held by the
     * previous transfer) => STATE, finish it with Stop/Restart/ReloadService.
     * BUSY=1 or START still pending => BUSY. CR2.START is read before ISR
     * (v0.11.1), so a START completing between the reads is still BUSY.
     * Stuck bus (SDA/SCL held low): START stays pending with ISR.BUSY=0 and
     * BUSY is returned indefinitely (no START is ever detected, RM0440
     * 39.9.2/39.9.7). Bound the retries; on expiry call I2C_SoftwareReset
     * (PE=0 clears START) and recover the bus. I2C_TimeoutConfig with
     * TIDLE=0 detects SCL held low only (RM0440 39.4.17 TIMEOUT): it
     * clears START and sets ISR.TIMEOUT (I2C_GetErrors; ERR_HW from the
     * *Long APIs only). SDA held low with SCL high needs the retry bound. */
    /* address7 0..127; reserved addresses (0x00-0x07, 0x78-0x7F) are not refused: 0x00 is
     * the general call (v0.13.5 decision, contract LEG-I2C-02). */
    /* Seviye: PARAM[S] STATE[r] HW[r] BUSY[r] */
    SPL_Status I2C_Start(I2C_TypeDef *i2c, uint8_t address7, uint8_t length, uint8_t read);
    /* Software-end first phase: complete NBYTES then wait for TC.
     * AUTOEND=0, no automatic STOP. 1..255 bytes. */
    /* Seviye: PARAM[S] STATE[r] HW[r] BUSY[r] */
    SPL_Status I2C_StartNoStop(I2C_TypeDef *i2c, uint8_t address7, uint8_t length, uint8_t read);
    /* Repeated START allowed only after TC. Does not block. TC=0: WOULD_BLOCK
     * while a software-end transfer runs (BUSY=1) or START is pending; STATE
     * when TC cannot set (BUSY=0, STOPF=1 or TCR=1; RM0440 39.9.7).
     * CR2.START is read before ISR (v0.11.1 I2C-M1). A START pending on a
     * stuck bus gives WOULD_BLOCK indefinitely: bound the poll as for Start. */
    /* Seviye: PARAM[S] STATE[r] HW[r] WOULD_BLOCK[r] */
    SPL_Status I2C_Restart(I2C_TypeDef *i2c, uint8_t address7, uint8_t length, uint8_t read,
                           uint8_t autoend);
    /* Seviye: PARAM[S] HW[r] */
    SPL_Status I2C_TransferComplete(I2C_TypeDef *i2c, uint8_t *complete);
    /* *detected = ISR.STOPF (not acknowledged). v0.13.3: NACKF or ARLO set ->
     * SPL_ERR_HW, *detected untouched (a target NACK in an AUTOEND transfer
     * also ends with STOPF). */
    /* Seviye: PARAM[S] HW[r] */
    SPL_Status I2C_StopDetected(I2C_TypeDef *i2c, uint8_t *detected);
    /* Byte services require PE=1; otherwise ERR_STATE without register writes
     * or output mutation. Serialize access to a given instance.
     * ERRATA ES0431 2.11.2 / ES0430 2.15.2 / ES0523 2.11.2: TryWrite, TryRead, TransferComplete,
     * Restart, RestartLong and ReloadService return ERR_HW on NACKF/ARLO (Long
     * APIs also OVR/PECERR/TIMEOUT) but NOT on BERR alone: a controller-mode
     * BERR may be spurious and the transfer continues. BERR stays visible
     * (I2C_GetErrors / I2C_Status); clear it with I2C_ClearErrorFlags(BERR)
     * before the next fresh START (Start/StartNoStop/StartLong still return
     * ERR_HW while BERR=1). */
    /* Seviye: PARAM[S] STATE[r] HW[r] WOULD_BLOCK[r] */
    SPL_Status I2C_TryWrite(I2C_TypeDef *i2c, uint8_t byte);
    /* Seviye: PARAM[S] STATE[r] HW[r] WOULD_BLOCK[r] */
    SPL_Status I2C_TryRead(I2C_TypeDef *i2c, uint8_t *byte);
    /* Seviye: PARAM[S] */
    SPL_Status I2C_Stop(I2C_TypeDef *i2c);
    /* Clears NACK/BERR/ARLO/OVR only; does NOT acknowledge STOPF.
     * Capture ISR and diagnose the error before clearing. */
    /* Seviye: PARAM[S] */
    SPL_Status I2C_ClearErrors(I2C_TypeDef *i2c);
    /* Acknowledge STOPF explicitly; does not clear error flags. */
    /* Seviye: PARAM[S] */
    SPL_Status I2C_ClearStop(I2C_TypeDef *i2c);
    /* Seviye: PARAM[S] */
    SPL_Status I2C_Status(I2C_TypeDef *i2c, uint32_t *status);

/* ---- v0.10: long transfers, 10-bit addressing, errors, reset, timeout ---- */

/* I2C_ISR error flags reported by I2C_GetErrors and accepted by
 * I2C_ClearErrorFlags. Each ISR flag has its clear bit at the same position
 * in I2C_ICR (RM0440 39.9.7 / 39.9.8). ALERT (SMBus host) is not included. */
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
    /* Seviye: PARAM[S] STATE[r] HW[r] BUSY[r] */
    SPL_Status I2C_StartLong(I2C_TypeDef *i2c, I2C_LongTransferTypeDef *xfer, uint16_t address,
                             uint32_t options, uint32_t length);
    /* Repeated START after TC (software end). Same chunking as I2C_StartLong.
     * TC=0 handling as I2C_Restart (WOULD_BLOCK or STATE).
     * HEAD10R is accepted here only, with ADDR10 and READ. */
    /* Seviye: PARAM[S] STATE[r] HW[r] WOULD_BLOCK[r] */
    SPL_Status I2C_RestartLong(I2C_TypeDef *i2c, I2C_LongTransferTypeDef *xfer, uint16_t address,
                               uint32_t options, uint32_t length);
    /* Nonblocking: call when TCR may be set. Programs the next chunk, clears
     * RELOAD and applies AUTOEND on the last chunk, decrements remaining.
     * WOULD_BLOCK while TCR=0; STATE when nothing remains. */
    /* Seviye: PARAM[S] STATE[r] HW[r] WOULD_BLOCK[r] */
    SPL_Status I2C_ReloadService(I2C_TypeDef *i2c, I2C_LongTransferTypeDef *xfer);

    /* Reads ISR & I2C_ERRORS_ALL (no side effects). */
    /* Seviye: PARAM[S] */
    SPL_Status I2C_GetErrors(I2C_TypeDef *i2c, uint32_t *errors);
    /* Clears the given ISR error flags (subset of I2C_ERRORS_ALL, non-zero)
     * by writing their ICR bits. Never touches STOPF or ADDR. */
    /* Seviye: PARAM[S] */
    SPL_Status I2C_ClearErrorFlags(I2C_TypeDef *i2c, uint32_t errors);

    /* RM0440 39.4.6 software reset: PE=0, read back PE=0 (the RM sequence that
     * keeps PE low for at least three APB clock cycles), then PE=1 when
     * reenable=1. Configuration registers are kept; CR2 commands and status
     * flags are reset by hardware. Any I2C_LongTransferTypeDef in flight is
     * void afterwards.
     * Bus recovery (SDA held low by a target) is NOT done here: the board layer
     * must switch SCL to GPIO open-drain, clock up to 9 pulses until SDA is
     * high, generate a STOP, restore the AF mapping, then call this reset. */
    /* Seviye: PARAM[S] HW[r] */
    SPL_Status I2C_SoftwareReset(I2C_TypeDef *i2c, uint8_t reenable);

    /* Writes I2C_TIMEOUTR (TIMEOUTA, TIDLE, TIMOUTEN, TIMEOUTB, TEXTEN).
     * RM0440 Table 352 exposes timeout/SMBus only on I2C1. Reserved bits must
     * be zero. Rejected with BUSY while ISR.BUSY=1. The enables are cleared
     * before the fields are written, as the RM requires. */
    /* Seviye: PARAM[S] UNSUPPORTED[S] BUSY[r] */
    SPL_Status I2C_TimeoutConfig(I2C_TypeDef *i2c, uint32_t timeoutr);

    /* ---- v0.17: target mode and SMBus/PEC ----
     * Target transfers are available on every instance accepted by this
     * driver (I2C1..3). RM0440 Table 352 limits SMBus/PMBus, TIMEOUTR,
     * independent clock and Stop wake-up to I2C1. */
    typedef struct
    {
        uint32_t timingr;     /* TIMINGR; non-zero, reserved bits 27:24 clear. */
        uint16_t own_address; /* OA1: 7-bit, or 10-bit with addr10 = 1. */
        uint8_t addr10;       /* OAR1.OA1MODE. */
        uint8_t own_address2; /* OA2, 7-bit. */
        uint8_t oa2_mask;     /* OAR2.OA2MSK, 0..7. */
        uint8_t oa2_enable;   /* OAR2.OA2EN. */
        uint8_t general_call; /* CR1.GCEN. */
        uint8_t no_stretch;   /* CR1.NOSTRETCH; incompatible with SBC. */
        uint8_t byte_control; /* CR1.SBC for target byte-count control. */
        uint8_t wakeup;       /* CR1.WUPEN; I2C1 only. */
        uint8_t reserved[2];  /* Must be zero; prevents hidden tail padding. */
    } I2C_TargetConfigTypeDef;

    /* PE must be zero. Programs TIMINGR, OAR1/OAR2 and target CR1. WUPEN
     * requires I2C1, clock stretching and DNF=0; the caller must select HSI16
     * as I2C1 kernel clock before entering Stop mode. */
    /* Seviye: PARAM[S,n] STATE[S] UNSUPPORTED[S] */
    SPL_Status I2C_TargetInit(I2C_TypeDef *i2c, const I2C_TargetConfigTypeDef *cfg);
    /* ADDR=0 -> WOULD_BLOCK. Returns ADDCODE and DIR without clearing ADDR. */
    /* Seviye: PARAM[S] WOULD_BLOCK[r] */
    SPL_Status I2C_TargetPollAddress(I2C_TypeDef *i2c, uint8_t *addcode, uint8_t *controller_reads);
    /* SBC=1 and PE=1 required. Programs NBYTES/RELOAD with START low. */
    /* Seviye: PARAM[S,n] STATE[S] */
    SPL_Status I2C_TargetSetBytes(I2C_TypeDef *i2c, uint8_t nbytes, uint8_t reload);
    /* Clears ADDR, then rewrites CR2 with START low (G4 own-address errata). */
    /* Seviye: PARAM[S] STATE[n] */
    SPL_Status I2C_TargetAckAddress(I2C_TypeDef *i2c);
    /* Requests NACK for the next target-receiver byte. PE=1 required. */
    /* Seviye: PARAM[S] STATE[S] */
    SPL_Status I2C_TargetNack(I2C_TypeDef *i2c);
    /* Flushes a stale target-transmitter TXDR by writing ISR.TXE. */
    /* Seviye: PARAM[S] */
    SPL_Status I2C_TargetFlushTx(I2C_TypeDef *i2c);
    /* Nonblocking target byte services. BERR/OVR -> ERR_HW; a controller
     * NACK ending a target transmission -> STATE. */
    /* Seviye: PARAM[S] STATE[r] HW[r] WOULD_BLOCK[r] */
    SPL_Status I2C_TargetTryWrite(I2C_TypeDef *i2c, uint8_t byte);
    /* Seviye: PARAM[S] HW[r] WOULD_BLOCK[r] */
    SPL_Status I2C_TargetTryRead(I2C_TypeDef *i2c, uint8_t *byte);
    /* G4 errata recovery after target-mode SMBus TIMEOUT: if STOPF is absent,
     * cycle PE low for at least three APB clocks. I2C1 only. */
    /* Seviye: PARAM[S] STATE[r] UNSUPPORTED[S] */
    SPL_Status I2C_TargetTimeoutRecover(I2C_TypeDef *i2c);

#define I2C_SMBUS_HOST I2C_CR1_SMBHEN
#define I2C_SMBUS_DEVICE I2C_CR1_SMBDEN
#define I2C_SMBUS_ALERT I2C_CR1_ALERTEN
#define I2C_SMBUS_PEC I2C_CR1_PECEN
#define I2C_SMBUS_ALL (I2C_SMBUS_HOST | I2C_SMBUS_DEVICE | I2C_SMBUS_ALERT | I2C_SMBUS_PEC)
    /* I2C1 only; PE must be zero. Replaces the four SMBus CR1 option bits. */
    /* Seviye: PARAM[S] STATE[S] UNSUPPORTED[S] */
    SPL_Status I2C_SmbusConfig(I2C_TypeDef *i2c, uint32_t options);
    /* I2C1 only. PE and PECEN required. Programs PECBYTE with START low. */
    /* Seviye: PARAM[S] STATE[S] UNSUPPORTED[S] */
    SPL_Status I2C_SetPecByte(I2C_TypeDef *i2c);
    /* I2C1 only. Returns PECR.PEC; PECEN required. */
    /* Seviye: PARAM[S] STATE[S] UNSUPPORTED[S] */
    SPL_Status I2C_GetPec(I2C_TypeDef *i2c, uint8_t *pec);

#ifdef __cplusplus
}
#endif
#endif
