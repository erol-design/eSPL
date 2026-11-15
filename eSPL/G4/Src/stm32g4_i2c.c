#define SPL_LIBRARY_BUILD 1
#include "stm32g4_i2c.h"

/* ERRATA ES0431 2.11.2 / ES0430 2.15.2 / ES0523 2.11.2: BERR can be set spuriously in
 * controller mode and the transfer continues normally. Services that drive an
 * ongoing transfer (byte services, TC poll, reload, repeated START) do not
 * abort on BERR; it stays in ISR (I2C_GetErrors) until the caller clears it.
 * Fresh-START gates keep BERR so a stale flag is seen before a new transfer. */
#define I2C_XFER_ABORT_LEGACY (I2C_ISR_NACKF | I2C_ISR_ARLO)
#define I2C_XFER_ABORT_ALL (I2C_ERRORS_ALL & ~I2C_ISR_BERR)

static uint8_t I2c_Valid(const I2C_TypeDef *i2c)
{
    uint8_t valid = (uint8_t)(((i2c == I2C1) || (i2c == I2C2)) ? 1U : 0U);
#if defined(I2C3)
    valid = (uint8_t)(((valid != 0U) || (i2c == I2C3)) ? 1U : 0U);
#endif
    return valid;
}

/* RM0440 Table 352: target mode exists on all I2Cs, while SMBus/PMBus,
 * TIMEOUTR, independent clock and Stop wake-up are implemented only by I2C1. */
static uint8_t I2c_HasSmbus(const I2C_TypeDef *i2c)
{
    return (uint8_t)((i2c == I2C1) ? 1U : 0U);
}

/* RM0440 39.9.7: with TC = 1 (software end) or TCR = 1 (RELOAD chunk done)
 * the controller holds SCL low; BUSY never clears by itself. A fresh START
 * is then a caller sequencing error (STATE): finish with I2C_Stop, a
 * repeated START or I2C_ReloadService first. */
static uint8_t I2c_BusHeld(uint32_t isr)
{
    return (uint8_t)(((isr & (I2C_ISR_TC | I2C_ISR_TCR)) != 0U) ? 1U : 0U);
}

/* Read order for START decisions (v0.11.1 I2C-M1). BUSY is set when the
 * START condition is detected (RM0440 39.9.7) and START is cleared only after
 * START + address are sent (39.9.2), i.e. after BUSY = 1. CR2.START is read
 * first, ISR second: START = 0 then guarantees that the ISR snapshot already
 * shows BUSY = 1 for a START that was pending. The reverse order can pair a
 * stale BUSY = 0 with START = 0 when an interrupt runs between the reads. */
static uint32_t I2c_Snapshot(const I2C_TypeDef *i2c, uint32_t *isr)
{
    uint32_t start = i2c->CR2 & I2C_CR2_START;

    *isr = i2c->ISR;
    return start;
}

/* Fresh-START gate (Start, StartNoStop, StartLong). Order: error flags ->
 * bus held (TC/TCR: STATE) -> BUSY = 1 or START pending (BUSY). */
static SPL_Status I2c_StartGate(const I2C_TypeDef *i2c, uint32_t errors)
{
    uint32_t isr;
    uint32_t start = I2c_Snapshot(i2c, &isr);

    if ((isr & errors) != 0U)
    {
        return SPL_ERR_HW;
    }
    if (I2c_BusHeld(isr) != 0U)
    {
        return SPL_ERR_STATE;
    }
    /* SADD/RD_WRN/NBYTES must not change while START is set (RM0440
     * 39.9.2): a START still waiting for the bus is BUSY as well. */
    if (((isr & I2C_ISR_BUSY) != 0U) || (start != 0U))
    {
        return SPL_BUSY;
    }
    return SPL_OK;
}

/* Repeated-START gate. RM0440 39.9.7: TC is set only when RELOAD = 0,
 * AUTOEND = 0 and NBYTES bytes are transferred. OK when TC = 1. A START
 * still waiting for the bus may reach TC (WOULD_BLOCK). With no transfer on
 * the bus (BUSY = 0), after STOP (STOPF = 1, AUTOEND transfer) or with a
 * RELOAD chunk pending (TCR = 1) TC can never set: STATE. */
static SPL_Status I2c_RestartGate(const I2C_TypeDef *i2c, uint32_t errors)
{
    uint32_t isr;
    uint32_t start = I2c_Snapshot(i2c, &isr);

    if ((isr & errors) != 0U)
    {
        return SPL_ERR_HW;
    }
    if ((isr & I2C_ISR_TC) != 0U)
    {
        return SPL_OK;
    }
    if (start != 0U)
    {
        return SPL_WOULD_BLOCK;
    }
    if (((isr & I2C_ISR_BUSY) == 0U) || ((isr & (I2C_ISR_STOPF | I2C_ISR_TCR)) != 0U))
    {
        return SPL_ERR_STATE;
    }
    return SPL_WOULD_BLOCK;
}

SPL_Status I2C_MasterInit(I2C_TypeDef *i2c, uint32_t timingr)
{
    /* SAFE: gecersiz girdi sessiz register bozulmasi veya yanlis adres riski. */
    SPL_REQUIRE_SAFE(!((I2c_Valid(i2c) == 0U)), SPL_ERR_PARAM);
    /* SAFE: gecersiz girdi sessiz register bozulmasi veya yanlis adres riski. */
    SPL_REQUIRE_SAFE(!((timingr == 0U)), SPL_ERR_PARAM);
    /* SAFE: gecersiz girdi sessiz register bozulmasi veya yanlis adres riski. */
    SPL_REQUIRE_SAFE(!(((timingr & 0x0F000000UL) != 0U)), SPL_ERR_PARAM);
    if ((i2c->CR1 & I2C_CR1_PE) != 0U)
    {
        return SPL_ERR_STATE;
    }

    i2c->TIMINGR = timingr;
    i2c->CR1 = I2C_CR1_PE;
    return SPL_OK;
}

SPL_Status I2C_Start(I2C_TypeDef *i2c, uint8_t address7, uint8_t length, uint8_t read)
{
    uint32_t cr2;
    SPL_Status gate;

    /* SAFE: gecersiz girdi sessiz register bozulmasi veya yanlis adres riski. */
    SPL_REQUIRE_SAFE(!((I2c_Valid(i2c) == 0U)), SPL_ERR_PARAM);
    /* SAFE: gecersiz girdi sessiz register bozulmasi veya yanlis adres riski. */
    SPL_REQUIRE_SAFE(!((address7 > 127U)), SPL_ERR_PARAM);
    /* SAFE: gecersiz girdi sessiz register bozulmasi veya yanlis adres riski. */
    SPL_REQUIRE_SAFE(!((length == 0U)), SPL_ERR_PARAM);
    /* SAFE: gecersiz girdi sessiz register bozulmasi veya yanlis adres riski. */
    SPL_REQUIRE_SAFE(!((read > 1U)), SPL_ERR_PARAM);
    if ((i2c->CR1 & I2C_CR1_PE) == 0U)
    {
        return SPL_ERR_STATE;
    }
    gate = I2c_StartGate(i2c, I2C_ISR_NACKF | I2C_ISR_BERR | I2C_ISR_ARLO);
    if (gate != SPL_OK)
    {
        return gate;
    }

    /* STOPF is W1C and belongs to the previous transfer. Clear it
     * only once a fresh START has been accepted for preparation. */
    i2c->ICR = I2C_ICR_STOPCF;

    /* AUTOEND produces STOP at the end of this transfer. */
    cr2 = ((uint32_t)address7 << 1U) | ((uint32_t)length << I2C_CR2_NBYTES_Pos) | I2C_CR2_AUTOEND |
          I2C_CR2_START;
    if (read != 0U)
    {
        cr2 |= I2C_CR2_RD_WRN;
    }

    i2c->CR2 = cr2;
    return SPL_OK;
}

SPL_Status I2C_TryWrite(I2C_TypeDef *i2c, uint8_t byte)
{
    /* SAFE: gecersiz girdi sessiz register bozulmasi veya yanlis adres riski. */
    SPL_REQUIRE_SAFE(!(I2c_Valid(i2c) == 0U), SPL_ERR_PARAM);
    if ((i2c->CR1 & I2C_CR1_PE) == 0U)
    {
        return SPL_ERR_STATE;
    }
    /* ERRATA ES0431 2.11.2 / ES0430 2.15.2 / ES0523 2.11.2: spurious BERR does not abort. */
    if ((i2c->ISR & I2C_XFER_ABORT_LEGACY) != 0U)
    {
        return SPL_ERR_HW;
    }
    if ((i2c->ISR & I2C_ISR_TXIS) == 0U)
    {
        return SPL_WOULD_BLOCK;
    }

    i2c->TXDR = byte;
    return SPL_OK;
}

SPL_Status I2C_TryRead(I2C_TypeDef *i2c, uint8_t *byte)
{
    /* SAFE: gecersiz girdi sessiz register bozulmasi veya yanlis adres riski. */
    SPL_REQUIRE_SAFE(!((I2c_Valid(i2c) == 0U)), SPL_ERR_PARAM);
    /* SAFE: gecersiz girdi sessiz register bozulmasi veya yanlis adres riski. */
    SPL_REQUIRE_SAFE(!((byte == (uint8_t *)0)), SPL_ERR_PARAM);
    if ((i2c->CR1 & I2C_CR1_PE) == 0U)
    {
        return SPL_ERR_STATE;
    }
    /* ERRATA ES0431 2.11.2 / ES0430 2.15.2 / ES0523 2.11.2: spurious BERR does not abort. */
    if ((i2c->ISR & I2C_XFER_ABORT_LEGACY) != 0U)
    {
        return SPL_ERR_HW;
    }
    if ((i2c->ISR & I2C_ISR_RXNE) == 0U)
    {
        return SPL_WOULD_BLOCK;
    }

    *byte = (uint8_t)i2c->RXDR;
    return SPL_OK;
}

SPL_Status I2C_Stop(I2C_TypeDef *i2c)
{
    /* SAFE: gecersiz girdi sessiz register bozulmasi veya yanlis adres riski. */
    SPL_REQUIRE_SAFE(!(I2c_Valid(i2c) == 0U), SPL_ERR_PARAM);

    /* START is cleared by hardware and writing 0 has no effect. Never copy a
     * START=1 read back: hardware may clear it between read and write, and
     * the write-back would request a new START/RESTART. */
    i2c->CR2 = (i2c->CR2 & ~I2C_CR2_START) | I2C_CR2_STOP;
    return SPL_OK;
}

SPL_Status I2C_ClearErrors(I2C_TypeDef *i2c)
{
    /* SAFE: gecersiz girdi sessiz register bozulmasi veya yanlis adres riski. */
    SPL_REQUIRE_SAFE(!(I2c_Valid(i2c) == 0U), SPL_ERR_PARAM);

    /* ICR is write-one-to-clear; do not use read-modify-write. */
    /* STOPF is a transfer-completion event, not an error.
     * Preserve it until the caller explicitly acknowledges STOP. */
    i2c->ICR = I2C_ICR_NACKCF | I2C_ICR_BERRCF | I2C_ICR_ARLOCF | I2C_ICR_OVRCF;
    /* RM0440 39.9.7: writing TXE=1 flushes TXDR, so a byte loaded for the
     * failed transfer is not sent first by the next one. Other ISR bits are
     * read-only (TXIS is writable only with NOSTRETCH=1, never set here). */
    i2c->ISR = I2C_ISR_TXE;
    /* Read back: the clear reaches the peripheral before return (METHOD 1.7, issue #23). */
    (void)i2c->ISR;
    return SPL_OK;
}

SPL_Status I2C_Status(I2C_TypeDef *i2c, uint32_t *status)
{
    /* SAFE: gecersiz girdi sessiz register bozulmasi veya yanlis adres riski. */
    SPL_REQUIRE_SAFE(!((I2c_Valid(i2c) == 0U)), SPL_ERR_PARAM);
    /* SAFE: gecersiz girdi sessiz register bozulmasi veya yanlis adres riski. */
    SPL_REQUIRE_SAFE(!((status == (uint32_t *)0)), SPL_ERR_PARAM);

    *status = i2c->ISR;
    return SPL_OK;
}

/* Nonblocking, software-end first phase (AUTOEND=0).
 * The caller must service the data bytes, then wait for TC. */
SPL_Status I2C_StartNoStop(I2C_TypeDef *i2c, uint8_t address7, uint8_t length, uint8_t read)
{
    uint32_t cr2;
    SPL_Status gate;

    /* SAFE: gecersiz girdi sessiz register bozulmasi veya yanlis adres riski. */
    SPL_REQUIRE_SAFE(!((I2c_Valid(i2c) == 0U)), SPL_ERR_PARAM);
    /* SAFE: gecersiz girdi sessiz register bozulmasi veya yanlis adres riski. */
    SPL_REQUIRE_SAFE(!((address7 > 127U)), SPL_ERR_PARAM);
    /* SAFE: gecersiz girdi sessiz register bozulmasi veya yanlis adres riski. */
    SPL_REQUIRE_SAFE(!((length == 0U)), SPL_ERR_PARAM);
    /* SAFE: gecersiz girdi sessiz register bozulmasi veya yanlis adres riski. */
    SPL_REQUIRE_SAFE(!((read > 1U)), SPL_ERR_PARAM);
    if ((i2c->CR1 & I2C_CR1_PE) == 0U)
    {
        return SPL_ERR_STATE;
    }
    gate = I2c_StartGate(i2c, I2C_ISR_NACKF | I2C_ISR_BERR | I2C_ISR_ARLO);
    if (gate != SPL_OK)
    {
        return gate;
    }

    i2c->ICR = I2C_ICR_STOPCF;
    cr2 = ((uint32_t)address7 << 1U) | ((uint32_t)length << I2C_CR2_NBYTES_Pos) | I2C_CR2_START;
    if (read != 0U)
    {
        cr2 |= I2C_CR2_RD_WRN;
    }
    i2c->CR2 = cr2;
    return SPL_OK;
}

SPL_Status I2C_Restart(I2C_TypeDef *i2c, uint8_t address7, uint8_t length, uint8_t read,
                       uint8_t autoend)
{
    uint32_t cr2;
    SPL_Status gate;

    /* SAFE: gecersiz girdi sessiz register bozulmasi veya yanlis adres riski. */
    SPL_REQUIRE_SAFE(!((I2c_Valid(i2c) == 0U)), SPL_ERR_PARAM);
    /* SAFE: gecersiz girdi sessiz register bozulmasi veya yanlis adres riski. */
    SPL_REQUIRE_SAFE(!((address7 > 127U)), SPL_ERR_PARAM);
    /* SAFE: gecersiz girdi sessiz register bozulmasi veya yanlis adres riski. */
    SPL_REQUIRE_SAFE(!((length == 0U)), SPL_ERR_PARAM);
    /* SAFE: gecersiz girdi sessiz register bozulmasi veya yanlis adres riski. */
    SPL_REQUIRE_SAFE(!((read > 1U)), SPL_ERR_PARAM);
    /* SAFE: gecersiz girdi sessiz register bozulmasi veya yanlis adres riski. */
    SPL_REQUIRE_SAFE(!((autoend > 1U)), SPL_ERR_PARAM);
    if ((i2c->CR1 & I2C_CR1_PE) == 0U)
    {
        return SPL_ERR_STATE;
    }

    /* ERRATA ES0431 2.11.2 / ES0430 2.15.2 / ES0523 2.11.2: a repeated START continues the
     * ongoing transfer; spurious BERR does not abort it. */
    gate = I2c_RestartGate(i2c, I2C_XFER_ABORT_LEGACY);
    if (gate != SPL_OK)
    {
        return gate;
    }

    /* TC is cleared by hardware when a new START is programmed. */
    cr2 = ((uint32_t)address7 << 1U) | ((uint32_t)length << I2C_CR2_NBYTES_Pos) | I2C_CR2_START;
    if (read != 0U)
    {
        cr2 |= I2C_CR2_RD_WRN;
    }
    if (autoend != 0U)
    {
        cr2 |= I2C_CR2_AUTOEND;
    }
    i2c->CR2 = cr2;
    return SPL_OK;
}

SPL_Status I2C_TransferComplete(I2C_TypeDef *i2c, uint8_t *complete)
{
    /* SAFE: gecersiz girdi sessiz register bozulmasi veya yanlis adres riski. */
    SPL_REQUIRE_SAFE(!((I2c_Valid(i2c) == 0U)), SPL_ERR_PARAM);
    /* SAFE: gecersiz girdi sessiz register bozulmasi veya yanlis adres riski. */
    SPL_REQUIRE_SAFE(!((complete == (uint8_t *)0)), SPL_ERR_PARAM);
    /* ERRATA ES0431 2.11.2 / ES0430 2.15.2 / ES0523 2.11.2: spurious BERR does not abort. */
    if ((i2c->ISR & I2C_XFER_ABORT_LEGACY) != 0U)
    {
        return SPL_ERR_HW;
    }
    *complete = (uint8_t)(((i2c->ISR & I2C_ISR_TC) != 0U) ? 1U : 0U);
    return SPL_OK;
}

SPL_Status I2C_StopDetected(I2C_TypeDef *i2c, uint8_t *detected)
{
    /* SAFE: gecersiz girdi sessiz register bozulmasi veya yanlis adres riski. */
    SPL_REQUIRE_SAFE(!((I2c_Valid(i2c) == 0U)), SPL_ERR_PARAM);
    /* SAFE: gecersiz girdi sessiz register bozulmasi veya yanlis adres riski. */
    SPL_REQUIRE_SAFE(!((detected == (uint8_t *)0)), SPL_ERR_PARAM);
    /* A target NACK in an AUTOEND transfer also ends with STOPF: report it as
     * ERR_HW like I2C_TransferComplete (flags kept for I2C_GetErrors). */
    if ((i2c->ISR & I2C_XFER_ABORT_LEGACY) != 0U)
    {
        return SPL_ERR_HW;
    }
    *detected = (uint8_t)(((i2c->ISR & I2C_ISR_STOPF) != 0U) ? 1U : 0U);
    return SPL_OK;
}

/* Acknowledge a STOP event separately from error recovery.
 * ICR is write-one-to-clear; do not read/modify/write this register. */
SPL_Status I2C_ClearStop(I2C_TypeDef *i2c)
{
    /* SAFE: gecersiz girdi sessiz register bozulmasi veya yanlis adres riski. */
    SPL_REQUIRE_SAFE(!(I2c_Valid(i2c) == 0U), SPL_ERR_PARAM);

    i2c->ICR = I2C_ICR_STOPCF;
    /* Read back: the clear reaches the peripheral before return (METHOD 1.7, issue #23). */
    (void)i2c->ISR;
    return SPL_OK;
}

/* ---- v0.10: long transfers, 10-bit addressing, errors, reset, timeout ---- */

#define I2C_CHUNK_MAX 255U
#define I2C_XFER_ALL (I2C_XFER_READ | I2C_XFER_AUTOEND | I2C_XFER_ADDR10 | I2C_XFER_HEAD10R)
/* Reserved bits of I2C_TIMEOUTR: 14:13 and 30:28 (RM0440 39.9.6). */
#define I2C_TIMEOUTR_VALID                                                                         \
    (I2C_TIMEOUTR_TIMEOUTA | I2C_TIMEOUTR_TIDLE | I2C_TIMEOUTR_TIMOUTEN | I2C_TIMEOUTR_TIMEOUTB |  \
     I2C_TIMEOUTR_TEXTEN)
#define I2C_TIMEOUTR_ENABLES (I2C_TIMEOUTR_TIMOUTEN | I2C_TIMEOUTR_TEXTEN)

/* Validates address/options/length shared by StartLong and RestartLong. */
static uint8_t I2c_LongArgsValid(uint16_t address, uint32_t options, uint32_t length,
                                 uint8_t restart)
{
    uint8_t valid = 1U;
    /* 10-bit target: 0..0x3FF; 7-bit target: 0..0x7F. */
    const uint16_t address_max = ((options & I2C_XFER_ADDR10) != 0U) ? 0x3FFU : 0x7FU;

    if (((options & ~I2C_XFER_ALL) != 0U) || (length == 0U) || (address > address_max))
    {
        valid = 0U;
    }

    /* HEAD10R: 10-bit read after a write to the same target, sent with a
     * repeated START only (RM0440 39.4.9, Figure 550). */
    if ((options & I2C_XFER_HEAD10R) != 0U)
    {
        if ((restart == 0U) || ((options & I2C_XFER_ADDR10) == 0U) ||
            ((options & I2C_XFER_READ) == 0U))
        {
            valid = 0U;
        }
    }
    return valid;
}

/* Builds the first-chunk CR2 value (START included) and fills the
 * caller-owned bookkeeping. Called only after every check has passed. */
static uint32_t I2c_LongFirstChunk(I2C_LongTransferTypeDef *xfer, uint16_t address,
                                   uint32_t options, uint32_t length)
{
    uint32_t cr2;
    uint32_t chunk;

    if ((options & I2C_XFER_ADDR10) != 0U)
    {
        cr2 = ((uint32_t)address & I2C_CR2_SADD) | I2C_CR2_ADD10;
        if ((options & I2C_XFER_HEAD10R) != 0U)
        {
            cr2 |= I2C_CR2_HEAD10R;
        }
    }
    else
    {
        cr2 = (uint32_t)address << 1U;
    }
    if ((options & I2C_XFER_READ) != 0U)
    {
        cr2 |= I2C_CR2_RD_WRN;
    }

    xfer->autoend = ((options & I2C_XFER_AUTOEND) != 0U) ? 1U : 0U;
    if (length > I2C_CHUNK_MAX)
    {
        /* RM0440 Figure 552: NBYTES=0xFF, RELOAD=1. AUTOEND has no effect
         * with RELOAD=1 and is applied on the last chunk only. */
        chunk = I2C_CHUNK_MAX;
        cr2 |= I2C_CR2_RELOAD;
    }
    else
    {
        chunk = length;
        if (xfer->autoend != 0U)
        {
            cr2 |= I2C_CR2_AUTOEND;
        }
    }
    xfer->remaining = length - chunk;

    return cr2 | (chunk << I2C_CR2_NBYTES_Pos) | I2C_CR2_START;
}

SPL_Status I2C_StartLong(I2C_TypeDef *i2c, I2C_LongTransferTypeDef *xfer, uint16_t address,
                         uint32_t options, uint32_t length)
{
    SPL_Status gate;

    /* SAFE: gecersiz girdi sessiz register bozulmasi veya yanlis adres riski. */
    SPL_REQUIRE_SAFE(!((I2c_Valid(i2c) == 0U)), SPL_ERR_PARAM);
    /* SAFE: gecersiz girdi sessiz register bozulmasi veya yanlis adres riski. */
    SPL_REQUIRE_SAFE(!((xfer == (I2C_LongTransferTypeDef *)0)), SPL_ERR_PARAM);
    /* SAFE: gecersiz girdi sessiz register bozulmasi veya yanlis adres riski. */
    SPL_REQUIRE_SAFE(!((I2c_LongArgsValid(address, options, length, 0U) == 0U)), SPL_ERR_PARAM);
    if ((i2c->CR1 & I2C_CR1_PE) == 0U)
    {
        return SPL_ERR_STATE;
    }

    /* SADD/ADD10/RD_WRN/NBYTES must not change while START is set. */
    gate = I2c_StartGate(i2c, I2C_ERRORS_ALL);
    if (gate != SPL_OK)
    {
        return gate;
    }

    /* Stale STOPF belongs to the previous transfer (same rule as I2C_Start). */
    i2c->ICR = I2C_ICR_STOPCF;
    i2c->CR2 = I2c_LongFirstChunk(xfer, address, options, length);
    return SPL_OK;
}

SPL_Status I2C_RestartLong(I2C_TypeDef *i2c, I2C_LongTransferTypeDef *xfer, uint16_t address,
                           uint32_t options, uint32_t length)
{
    SPL_Status gate;

    /* SAFE: gecersiz girdi sessiz register bozulmasi veya yanlis adres riski. */
    SPL_REQUIRE_SAFE(!((I2c_Valid(i2c) == 0U)), SPL_ERR_PARAM);
    /* SAFE: gecersiz girdi sessiz register bozulmasi veya yanlis adres riski. */
    SPL_REQUIRE_SAFE(!((xfer == (I2C_LongTransferTypeDef *)0)), SPL_ERR_PARAM);
    /* SAFE: gecersiz girdi sessiz register bozulmasi veya yanlis adres riski. */
    SPL_REQUIRE_SAFE(!((I2c_LongArgsValid(address, options, length, 1U) == 0U)), SPL_ERR_PARAM);
    if ((i2c->CR1 & I2C_CR1_PE) == 0U)
    {
        return SPL_ERR_STATE;
    }

    /* ERRATA ES0431 2.11.2 / ES0430 2.15.2 / ES0523 2.11.2: spurious BERR does not abort. */
    gate = I2c_RestartGate(i2c, I2C_XFER_ABORT_ALL);
    if (gate != SPL_OK)
    {
        return gate;
    }

    /* Setting START clears TC and sends the repeated START (RM0440 39.4.9). */
    i2c->CR2 = I2c_LongFirstChunk(xfer, address, options, length);
    return SPL_OK;
}

SPL_Status I2C_ReloadService(I2C_TypeDef *i2c, I2C_LongTransferTypeDef *xfer)
{
    uint32_t isr;
    uint32_t cr2;
    uint32_t chunk;

    /* SAFE: gecersiz girdi sessiz register bozulmasi veya yanlis adres riski. */
    SPL_REQUIRE_SAFE(!((I2c_Valid(i2c) == 0U)), SPL_ERR_PARAM);
    /* SAFE: gecersiz girdi sessiz register bozulmasi veya yanlis adres riski. */
    SPL_REQUIRE_SAFE(!((xfer == (I2C_LongTransferTypeDef *)0)), SPL_ERR_PARAM);
    /* SAFE: gecersiz girdi sessiz register bozulmasi veya yanlis adres riski. */
    SPL_REQUIRE_SAFE(!((xfer->autoend > 1U)), SPL_ERR_PARAM);
    if ((i2c->CR1 & I2C_CR1_PE) == 0U)
    {
        return SPL_ERR_STATE;
    }

    isr = i2c->ISR;
    /* ERRATA ES0431 2.11.2 / ES0430 2.15.2 / ES0523 2.11.2: spurious BERR does not abort. */
    if ((isr & I2C_XFER_ABORT_ALL) != 0U)
    {
        return SPL_ERR_HW;
    }
    cr2 = i2c->CR2;
    /* Nothing left to reload, or bookkeeping does not match the hardware
     * (RELOAD=0 means the last chunk is already programmed). */
    if ((xfer->remaining == 0U) || ((cr2 & I2C_CR2_RELOAD) == 0U))
    {
        return SPL_ERR_STATE;
    }
    if ((isr & I2C_ISR_TCR) == 0U)
    {
        return SPL_WOULD_BLOCK;
    }

    /* Keep address/direction fields. START, STOP, NACK and PECBYTE are
     * set-only command bits: never write a read-back 1 to them. */
    cr2 &= ~(I2C_CR2_NBYTES | I2C_CR2_RELOAD | I2C_CR2_AUTOEND | I2C_CR2_START | I2C_CR2_STOP |
             I2C_CR2_NACK | I2C_CR2_PECBYTE);
    if (xfer->remaining > I2C_CHUNK_MAX)
    {
        chunk = I2C_CHUNK_MAX;
        cr2 |= I2C_CR2_RELOAD;
    }
    else
    {
        /* Last chunk: RELOAD=0 (RM0440 39.4.7 byte counter), AUTOEND now. */
        chunk = xfer->remaining;
        if (xfer->autoend != 0U)
        {
            cr2 |= I2C_CR2_AUTOEND;
        }
    }
    xfer->remaining -= chunk;

    /* Writing a non-zero NBYTES clears TCR and releases SCL. */
    i2c->CR2 = cr2 | (chunk << I2C_CR2_NBYTES_Pos);
    return SPL_OK;
}

SPL_Status I2C_GetErrors(I2C_TypeDef *i2c, uint32_t *errors)
{
    /* SAFE: gecersiz girdi sessiz register bozulmasi veya yanlis adres riski. */
    SPL_REQUIRE_SAFE(!((I2c_Valid(i2c) == 0U)), SPL_ERR_PARAM);
    /* SAFE: gecersiz girdi sessiz register bozulmasi veya yanlis adres riski. */
    SPL_REQUIRE_SAFE(!((errors == (uint32_t *)0)), SPL_ERR_PARAM);

    *errors = i2c->ISR & I2C_ERRORS_ALL;
    return SPL_OK;
}

SPL_Status I2C_ClearErrorFlags(I2C_TypeDef *i2c, uint32_t errors)
{
    uint32_t icr = 0U;

    /* SAFE: gecersiz girdi sessiz register bozulmasi veya yanlis adres riski. */
    SPL_REQUIRE_SAFE(!((I2c_Valid(i2c) == 0U)), SPL_ERR_PARAM);
    /* SAFE: gecersiz girdi sessiz register bozulmasi veya yanlis adres riski. */
    SPL_REQUIRE_SAFE(!((errors == 0U)), SPL_ERR_PARAM);
    /* SAFE: gecersiz girdi sessiz register bozulmasi veya yanlis adres riski. */
    SPL_REQUIRE_SAFE(!(((errors & ~I2C_ERRORS_ALL) != 0U)), SPL_ERR_PARAM);

    if ((errors & I2C_ISR_NACKF) != 0U)
    {
        icr |= I2C_ICR_NACKCF;
    }
    if ((errors & I2C_ISR_BERR) != 0U)
    {
        icr |= I2C_ICR_BERRCF;
    }
    if ((errors & I2C_ISR_ARLO) != 0U)
    {
        icr |= I2C_ICR_ARLOCF;
    }
    if ((errors & I2C_ISR_OVR) != 0U)
    {
        icr |= I2C_ICR_OVRCF;
    }
    if ((errors & I2C_ISR_PECERR) != 0U)
    {
        icr |= I2C_ICR_PECCF;
    }
    if ((errors & I2C_ISR_TIMEOUT) != 0U)
    {
        icr |= I2C_ICR_TIMOUTCF;
    }

    /* ICR is write-one-to-clear: single write, no read-modify-write. */
    i2c->ICR = icr;
    /* Read back: the clear reaches the peripheral before return (METHOD 1.7, issue #23). */
    (void)i2c->ISR;
    return SPL_OK;
}

SPL_Status I2C_SoftwareReset(I2C_TypeDef *i2c, uint8_t reenable)
{
    uint32_t cr1;

    /* SAFE: gecersiz girdi sessiz register bozulmasi veya yanlis adres riski. */
    SPL_REQUIRE_SAFE(!((I2c_Valid(i2c) == 0U)), SPL_ERR_PARAM);
    /* SAFE: gecersiz girdi sessiz register bozulmasi veya yanlis adres riski. */
    SPL_REQUIRE_SAFE(!((reenable > 1U)), SPL_ERR_PARAM);

    /* RM0440 39.4.6: 1. write PE=0, 2. check PE=0, 3. write PE=1.
     * CR1 has no W1C or self-clearing bits, so read-modify-write is safe. */
    cr1 = i2c->CR1 & ~I2C_CR1_PE;
    i2c->CR1 = cr1;
    if ((i2c->CR1 & I2C_CR1_PE) != 0U)
    {
        /* PE did not read back as 0: do not re-enable. */
        return SPL_ERR_HW;
    }
    if (reenable != 0U)
    {
        i2c->CR1 = cr1 | I2C_CR1_PE;
    }
    return SPL_OK;
}

SPL_Status I2C_TimeoutConfig(I2C_TypeDef *i2c, uint32_t timeoutr)
{
    /* SAFE: gecersiz girdi sessiz register bozulmasi veya yanlis adres riski. */
    SPL_REQUIRE_SAFE(!((I2c_Valid(i2c) == 0U)), SPL_ERR_PARAM);
    /* SAFE: gecersiz girdi sessiz register bozulmasi veya yanlis adres riski. */
    SPL_REQUIRE_SAFE(!(((timeoutr & ~I2C_TIMEOUTR_VALID) != 0U)), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(I2c_HasSmbus(i2c) != 0U, SPL_ERR_UNSUPPORTED);
    if ((i2c->ISR & I2C_ISR_BUSY) != 0U)
    {
        return SPL_BUSY;
    }

    /* RM0440 39.9.6: TIMEOUTA/TIDLE are writable only with TIMOUTEN=0 and
     * TIMEOUTB only with TEXTEN=0. Disable, write fields, then enable. */
    i2c->TIMEOUTR = i2c->TIMEOUTR & ~I2C_TIMEOUTR_ENABLES;
    i2c->TIMEOUTR = timeoutr & ~I2C_TIMEOUTR_ENABLES;
    if ((timeoutr & I2C_TIMEOUTR_ENABLES) != 0U)
    {
        i2c->TIMEOUTR = timeoutr;
    }
    return SPL_OK;
}

/* ---- v0.17: target mode and SMBus/PEC ---- */

SPL_Status I2C_TargetInit(I2C_TypeDef *i2c, const I2C_TargetConfigTypeDef *cfg)
{
    uint32_t oar1;
    uint32_t oar2 = 0U;
    uint32_t cr1 = I2C_CR1_PE;

    SPL_REQUIRE_SAFE(I2c_Valid(i2c) != 0U, SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(cfg != (const I2C_TargetConfigTypeDef *)0, SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE((cfg->timingr != 0U) && ((cfg->timingr & 0x0F000000UL) == 0U) &&
                         ((cfg->addr10 != 0U) || (cfg->own_address <= 127U)) &&
                         (cfg->own_address <= 1023U) && (cfg->own_address2 <= 127U) &&
                         (cfg->oa2_mask <= 7U),
                     SPL_ERR_PARAM);
    SPL_REQUIRE((cfg->addr10 <= 1U) && (cfg->oa2_enable <= 1U) && (cfg->general_call <= 1U) &&
                    (cfg->no_stretch <= 1U) && (cfg->byte_control <= 1U) && (cfg->wakeup <= 1U) &&
                    (cfg->reserved[0] == 0U) && (cfg->reserved[1] == 0U),
                SPL_ERR_PARAM);
    /* RM0440 39.4.8: SBC and NOSTRETCH must not be enabled together. */
    SPL_REQUIRE_SAFE((cfg->byte_control == 0U) || (cfg->no_stretch == 0U), SPL_ERR_PARAM);
    /* Table 352: WUPEN is implemented only on I2C1. Wake-up also requires
     * clock stretching; the CR1 image below leaves DNF at zero. */
    SPL_REQUIRE_SAFE((cfg->wakeup == 0U) || (I2c_HasSmbus(i2c) != 0U), SPL_ERR_UNSUPPORTED);
    SPL_REQUIRE_SAFE((cfg->wakeup == 0U) || (cfg->no_stretch == 0U), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE((i2c->CR1 & I2C_CR1_PE) == 0U, SPL_ERR_STATE);

    oar1 = I2C_OAR1_OA1EN | ((cfg->addr10 != 0U) ? ((uint32_t)cfg->own_address | I2C_OAR1_OA1MODE)
                                                 : ((uint32_t)cfg->own_address << 1U));
    if (cfg->oa2_enable != 0U)
    {
        oar2 = I2C_OAR2_OA2EN | ((uint32_t)cfg->own_address2 << I2C_OAR2_OA2_Pos) |
               ((uint32_t)cfg->oa2_mask << I2C_OAR2_OA2MSK_Pos);
    }
    if (cfg->general_call != 0U)
    {
        cr1 |= I2C_CR1_GCEN;
    }
    if (cfg->no_stretch != 0U)
    {
        cr1 |= I2C_CR1_NOSTRETCH;
    }
    if (cfg->byte_control != 0U)
    {
        cr1 |= I2C_CR1_SBC;
    }
    if (cfg->wakeup != 0U)
    {
        cr1 |= I2C_CR1_WUPEN;
    }

    i2c->TIMINGR = cfg->timingr;
    i2c->OAR1 = 0U;
    i2c->OAR1 = oar1;
    i2c->OAR2 = 0U;
    i2c->OAR2 = oar2;
    i2c->CR1 = cr1;
    return SPL_OK;
}

SPL_Status I2C_TargetPollAddress(I2C_TypeDef *i2c, uint8_t *addcode, uint8_t *controller_reads)
{
    uint32_t isr;

    SPL_REQUIRE_SAFE(I2c_Valid(i2c) != 0U, SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE((addcode != (uint8_t *)0) && (controller_reads != (uint8_t *)0),
                     SPL_ERR_PARAM);
    isr = i2c->ISR;
    if ((isr & I2C_ISR_ADDR) == 0U)
    {
        return SPL_WOULD_BLOCK;
    }
    *addcode = (uint8_t)((isr & I2C_ISR_ADDCODE_Msk) >> I2C_ISR_ADDCODE_Pos);
    *controller_reads = ((isr & I2C_ISR_DIR) != 0U) ? 1U : 0U;
    return SPL_OK;
}

SPL_Status I2C_TargetSetBytes(I2C_TypeDef *i2c, uint8_t nbytes, uint8_t reload)
{
    SPL_REQUIRE_SAFE(I2c_Valid(i2c) != 0U, SPL_ERR_PARAM);
    SPL_REQUIRE(reload <= 1U, SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE((i2c->CR1 & (I2C_CR1_PE | I2C_CR1_SBC)) == (I2C_CR1_PE | I2C_CR1_SBC),
                     SPL_ERR_STATE);
    i2c->CR2 = (i2c->CR2 & ~(I2C_CR2_NBYTES_Msk | I2C_CR2_RELOAD | I2C_CR2_START)) |
               ((uint32_t)nbytes << I2C_CR2_NBYTES_Pos) | ((reload != 0U) ? I2C_CR2_RELOAD : 0U);
    return SPL_OK;
}

SPL_Status I2C_TargetAckAddress(I2C_TypeDef *i2c)
{
    uint32_t cr2;

    SPL_REQUIRE_SAFE(I2c_Valid(i2c) != 0U, SPL_ERR_PARAM);
    SPL_REQUIRE((i2c->ISR & I2C_ISR_ADDR) != 0U, SPL_ERR_STATE);
    /* ES0431 2.11.3 / ES0430 2.15.3: after own-address match, clear ADDR
     * and write CR2 with START low before STOP to prevent a spurious
     * controller transfer. Applying the sequence is harmless on ES0523. */
    cr2 = i2c->CR2 & ~I2C_CR2_START;
    i2c->ICR = I2C_ICR_ADDRCF;
    i2c->CR2 = cr2;
    return SPL_OK;
}

SPL_Status I2C_TargetNack(I2C_TypeDef *i2c)
{
    SPL_REQUIRE_SAFE(I2c_Valid(i2c) != 0U, SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE((i2c->CR1 & I2C_CR1_PE) != 0U, SPL_ERR_STATE);
    i2c->CR2 = (i2c->CR2 & ~I2C_CR2_START) | I2C_CR2_NACK;
    return SPL_OK;
}

SPL_Status I2C_TargetFlushTx(I2C_TypeDef *i2c)
{
    SPL_REQUIRE_SAFE(I2c_Valid(i2c) != 0U, SPL_ERR_PARAM);
    i2c->ISR = I2C_ISR_TXE;
    return SPL_OK;
}

SPL_Status I2C_TargetTryWrite(I2C_TypeDef *i2c, uint8_t byte)
{
    uint32_t isr;

    SPL_REQUIRE_SAFE(I2c_Valid(i2c) != 0U, SPL_ERR_PARAM);
    isr = i2c->ISR;
    if ((isr & (I2C_ISR_OVR | I2C_ISR_BERR)) != 0U)
    {
        return SPL_ERR_HW;
    }
    if ((isr & I2C_ISR_NACKF) != 0U)
    {
        return SPL_ERR_STATE;
    }
    if ((isr & I2C_ISR_TXIS) == 0U)
    {
        return SPL_WOULD_BLOCK;
    }
    i2c->TXDR = byte;
    return SPL_OK;
}

SPL_Status I2C_TargetTryRead(I2C_TypeDef *i2c, uint8_t *byte)
{
    uint32_t isr;

    SPL_REQUIRE_SAFE(I2c_Valid(i2c) != 0U, SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(byte != (uint8_t *)0, SPL_ERR_PARAM);
    isr = i2c->ISR;
    if ((isr & (I2C_ISR_OVR | I2C_ISR_BERR)) != 0U)
    {
        return SPL_ERR_HW;
    }
    if ((isr & I2C_ISR_RXNE) == 0U)
    {
        return SPL_WOULD_BLOCK;
    }
    *byte = (uint8_t)i2c->RXDR;
    return SPL_OK;
}

SPL_Status I2C_TargetTimeoutRecover(I2C_TypeDef *i2c)
{
    uint32_t i;

    SPL_REQUIRE_SAFE(I2c_Valid(i2c) != 0U, SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(I2c_HasSmbus(i2c) != 0U, SPL_ERR_UNSUPPORTED);
    if ((i2c->ISR & I2C_ISR_TIMEOUT) == 0U)
    {
        return SPL_ERR_STATE;
    }
    if ((i2c->ISR & I2C_ISR_STOPF) != 0U)
    {
        return SPL_OK;
    }
    i2c->CR1 = i2c->CR1 & ~I2C_CR1_PE;
    for (i = 0U; i < 3U; ++i)
    {
        (void)i2c->CR1;
    }
    i2c->CR1 = i2c->CR1 | I2C_CR1_PE;
    return SPL_OK;
}

SPL_Status I2C_SmbusConfig(I2C_TypeDef *i2c, uint32_t options)
{
    SPL_REQUIRE_SAFE(I2c_Valid(i2c) != 0U, SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE((options & ~I2C_SMBUS_ALL) == 0U, SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(I2c_HasSmbus(i2c) != 0U, SPL_ERR_UNSUPPORTED);
    SPL_REQUIRE_SAFE((i2c->CR1 & I2C_CR1_PE) == 0U, SPL_ERR_STATE);
    i2c->CR1 = (i2c->CR1 & ~I2C_SMBUS_ALL) | options;
    return SPL_OK;
}

SPL_Status I2C_SetPecByte(I2C_TypeDef *i2c)
{
    SPL_REQUIRE_SAFE(I2c_Valid(i2c) != 0U, SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(I2c_HasSmbus(i2c) != 0U, SPL_ERR_UNSUPPORTED);
    SPL_REQUIRE_SAFE((i2c->CR1 & (I2C_CR1_PE | I2C_CR1_PECEN)) == (I2C_CR1_PE | I2C_CR1_PECEN),
                     SPL_ERR_STATE);
    i2c->CR2 = (i2c->CR2 & ~I2C_CR2_START) | I2C_CR2_PECBYTE;
    return SPL_OK;
}

SPL_Status I2C_GetPec(I2C_TypeDef *i2c, uint8_t *pec)
{
    SPL_REQUIRE_SAFE(I2c_Valid(i2c) != 0U, SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(pec != (uint8_t *)0, SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(I2c_HasSmbus(i2c) != 0U, SPL_ERR_UNSUPPORTED);
    SPL_REQUIRE_SAFE((i2c->CR1 & I2C_CR1_PECEN) != 0U, SPL_ERR_STATE);
    *pec = (uint8_t)(i2c->PECR & I2C_PECR_PEC_Msk);
    return SPL_OK;
}
