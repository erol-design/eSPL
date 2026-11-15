#define SPL_LIBRARY_BUILD 1
#include "stm32g4_qspi.h"

/* Quad-SPI driver, RM0440 Rev 9 section 20. Contract:
 * Docs/v0.12_QSPI_CONTRACT.md. Every register write happens only after all
 * parameter and state checks passed. */

#if defined(QUADSPI)

#define QSPI_FIFO_BYTES 16U

/* ERRATA ES0430 2.6.1 / 2.6.3 apply to the ES0430 dies only (G471/473/474/483/484). ES0523
 * Rev 7 (G491/G4A1) lists one QUADSPI item, ES0523 2.5.1 (= ES0430 2.6.4, caller duty), and
 * never listed 2.6.1..2.6.3 (revision history Rev 1..7): the workarounds below are not built
 * for G491/G4A1. ES0430 2.6.2 needs no code (the empty command is refused on every SKU). */
#if defined(STM32G471xx) || defined(STM32G473xx) || defined(STM32G474xx) ||                        \
    defined(STM32G483xx) || defined(STM32G484xx)
#define QSPI_ES0430_WORKAROUNDS
#endif

#if defined(QSPI_ES0430_WORKAROUNDS)
/* ERRATA ES0430 2.6.1: minimum dummy cycles of a data-only indirect read. */
#define QSPI_ERRATA_DATA_ONLY_DCYC_MIN 2U
/* ERRATA ES0430 2.6.3: literal register values of the workaround sequence
 * (CR: PRESCALER = 255, EN = 1; CCR: bit 29, "activate the free-running
 * clock"; RM0440 Rev 9 and CMSIS mark CCR bit 29 reserved, so the erratum
 * value is used as given). Qspi_CcrValue never sets bit 29, so CCR equal to
 * QSPI_ERRATA_WA_CCR means: workaround applied, no command programmed since. */
#define QSPI_ERRATA_WA_CR 0xFF000001UL
#define QSPI_ERRATA_WA_CCR 0x20000000UL

/* ERRATA ES0430 2.6.3: workaround core, from the first CR write to the
 * disable. Caller (QSPI_Init) has checked EN = 0, ABORT = 0 and BUSY = 0, so
 * the erratum's first "wait for BUSY to fall" after CR = 0 is already met
 * (writing CR = 0 to a disabled idle peripheral starts nothing). The disable
 * must follow the first CCR write within 127 kernel clocks: four
 * back-to-back stores, nothing in between; interrupts are a caller duty. */
static void Qspi_TimingWorkaround(void)
{
    QUADSPI->CR = 0U;
    QUADSPI->CR = QSPI_ERRATA_WA_CR;
    QUADSPI->CCR = QSPI_ERRATA_WA_CCR;
    QUADSPI->CCR = QSPI_ERRATA_WA_CCR;
    QUADSPI->CR = 0U;
}
#endif /* QSPI_ES0430_WORKAROUNDS */

static uint32_t Qspi_Fmode(uint32_t ccr)
{
    return (ccr & QUADSPI_CCR_FMODE_Msk) >> QUADSPI_CCR_FMODE_Pos;
}

static uint32_t Qspi_Flevel(uint32_t sr)
{
    return (sr & QUADSPI_SR_FLEVEL_Msk) >> QUADSPI_SR_FLEVEL_Pos;
}

/* RM0440 20.3.6 / 20.3.11: the command starts with the last register write
 * that carries information the command needs.
 * - FMODE=11: first memory-mapped access.
 * - FMODE=00 and DMODE!=00: first DR write (AR, if any, is written before).
 * - ADMODE!=00 (and no firmware data): AR write.
 * - otherwise: CCR write. ABR writes never start a command. */
static uint32_t Qspi_TriggerOf(uint32_t fmode, uint32_t admode, uint32_t dmode)
{
    uint32_t trigger = QSPI_TRIGGER_CCR;

    if (fmode == QSPI_FMODE_MEMORY_MAPPED)
    {
        trigger = QSPI_TRIGGER_AHB;
    }
    else if ((fmode == QSPI_FMODE_INDIRECT_WRITE) && (dmode != QSPI_LINES_NONE))
    {
        trigger = QSPI_TRIGGER_DR;
    }
    else if (admode != QSPI_LINES_NONE)
    {
        trigger = QSPI_TRIGGER_AR;
    }
    else
    {
        /* CCR write starts the command. */
    }
    return trigger;
}

/* Gate for registers that are writable only when BUSY=0 (CR PRESCALER/PMM/
 * APMS/SSHIFT/TCEN/DFM/FSEL, DCR, DLR, CCR, AR, ABR, PSMKR, PSMAR, PIR,
 * LPTR; RM0440 20.5). RM0440 20.3.14: BUSY falls by itself in indirect mode
 * (when the transfer is serviced), in auto-polling only with APMS=1 (match)
 * and in memory-mapped mode only on a timeout (TCEN=1). Otherwise only an
 * abort or EN=0 clears it: STATE. A pending ABORT always completes: BUSY. */
static SPL_Status Qspi_BusyGate(uint32_t cr, uint32_t sr, uint32_t ccr)
{
    uint32_t fmode = Qspi_Fmode(ccr);

    if ((cr & QUADSPI_CR_ABORT) != 0U)
    {
        return SPL_BUSY;
    }
    if ((sr & QUADSPI_SR_BUSY) == 0U)
    {
        return SPL_OK;
    }
    if (fmode == QSPI_FMODE_MEMORY_MAPPED)
    {
        return ((cr & QUADSPI_CR_TCEN) != 0U) ? SPL_BUSY : SPL_ERR_STATE;
    }
    if (fmode == QSPI_FMODE_AUTO_POLLING)
    {
        return ((cr & QUADSPI_CR_APMS) != 0U) ? SPL_BUSY : SPL_ERR_STATE;
    }
    return SPL_BUSY;
}

/* Gate for the write that starts a command: the peripheral must be enabled
 * and no completion or error flag of an earlier command may still be set
 * (stale TCF/SMF would look like completion of the new command; TEF is a
 * diagnostic that the caller captures and clears explicitly). */
static SPL_Status Qspi_TriggerGate(uint32_t cr, uint32_t sr)
{
    if ((cr & QUADSPI_CR_EN) == 0U)
    {
        return SPL_ERR_STATE;
    }
    if ((sr & QUADSPI_SR_TEF) != 0U)
    {
        return SPL_ERR_HW;
    }
    if ((sr & (QUADSPI_SR_TCF | QUADSPI_SR_SMF)) != 0U)
    {
        return SPL_ERR_STATE;
    }
    return SPL_OK;
}

/* Read-modify-write of CR for the run-time bits. ABORT is a self-clearing
 * command bit: never copied back; a pending abort makes the call BUSY. */
static SPL_Status Qspi_CrUpdate(uint32_t clear, uint32_t set)
{
    uint32_t cr = QUADSPI->CR;

    if ((cr & QUADSPI_CR_ABORT) != 0U)
    {
        return SPL_BUSY;
    }
    QUADSPI->CR = (cr & ~(clear | QUADSPI_CR_ABORT)) | set;
    return SPL_OK;
}

static uint8_t Qspi_CommValid(const QSPI_CommTypeDef *cmd)
{
    uint8_t ok = 1U;

    if ((cmd->fmode > 3U) || (cmd->imode > 3U) || (cmd->admode > 3U) || (cmd->adsize > 3U) ||
        (cmd->abmode > 3U) || (cmd->absize > 3U) || (cmd->dcyc > 31U) || (cmd->dmode > 3U) ||
        (cmd->sioo > 1U) || (cmd->ddrm > 1U) || (cmd->dhhc > 1U))
    {
        ok = 0U;
    }
    /* RM0440 20.3.3 data phase: DMODE=00 only in indirect-write mode. */
    else if ((cmd->dmode == QSPI_LINES_NONE) && (cmd->fmode != QSPI_FMODE_INDIRECT_WRITE))
    {
        ok = 0U;
    }
    /* RM0440 20.5.5: DL <= 3 (4 bytes) in automatic status-polling mode. */
    else if ((cmd->fmode == QSPI_FMODE_AUTO_POLLING) && (cmd->dlr > 3U))
    {
        ok = 0U;
    }
    /* A command with no phase at all would only toggle NCS. */
    /* ERRATA ES0430 2.6.2: this check also keeps CCR != 0 (CCR = 0 hangs QUADSPI). */
    else if ((cmd->imode == QSPI_LINES_NONE) && (cmd->admode == QSPI_LINES_NONE) &&
             (cmd->abmode == QSPI_LINES_NONE) && (cmd->dmode == QSPI_LINES_NONE) &&
             (cmd->dcyc == 0U))
    {
        ok = 0U;
    }
    else
    {
        /* RM0440 20.3.3 dummy phase: at least one dummy cycle when data is
         * received on two or four lines (turn-around). */
        uint8_t receive = (uint8_t)((cmd->fmode != QSPI_FMODE_INDIRECT_WRITE) ? 1U : 0U);
        if ((receive != 0U) && (cmd->dmode >= QSPI_LINES_2) && (cmd->dcyc == 0U))
        {
            ok = 0U;
        }
#if defined(QSPI_ES0430_WORKAROUNDS)
        /* ERRATA ES0430 2.6.1: data-only indirect read hangs (BUSY stuck): >= 2 dummy cycles.
         * Not in ES0523 (G491/G4A1): not built there. */
        if ((cmd->fmode == QSPI_FMODE_INDIRECT_READ) && (cmd->imode == QSPI_LINES_NONE) &&
            (cmd->admode == QSPI_LINES_NONE) && (cmd->abmode == QSPI_LINES_NONE) &&
            (cmd->dcyc < QSPI_ERRATA_DATA_ONLY_DCYC_MIN))
        {
            ok = 0U;
        }
#endif
    }
    return ok;
}

static uint32_t Qspi_CcrValue(const QSPI_CommTypeDef *cmd)
{
    uint32_t ccr = ((uint32_t)cmd->instruction << QUADSPI_CCR_INSTRUCTION_Pos) |
                   ((uint32_t)cmd->imode << QUADSPI_CCR_IMODE_Pos) |
                   ((uint32_t)cmd->admode << QUADSPI_CCR_ADMODE_Pos) |
                   ((uint32_t)cmd->adsize << QUADSPI_CCR_ADSIZE_Pos) |
                   ((uint32_t)cmd->abmode << QUADSPI_CCR_ABMODE_Pos) |
                   ((uint32_t)cmd->absize << QUADSPI_CCR_ABSIZE_Pos) |
                   ((uint32_t)cmd->dcyc << QUADSPI_CCR_DCYC_Pos) |
                   ((uint32_t)cmd->dmode << QUADSPI_CCR_DMODE_Pos) |
                   ((uint32_t)cmd->fmode << QUADSPI_CCR_FMODE_Pos) |
                   ((uint32_t)cmd->sioo << QUADSPI_CCR_SIOO_Pos) |
                   ((uint32_t)cmd->dhhc << QUADSPI_CCR_DHHC_Pos) |
                   ((uint32_t)cmd->ddrm << QUADSPI_CCR_DDRM_Pos);
    return ccr;
}

/* Indirect-write data gate (FMODE=00, DMODE!=00). width = 1, 2 or 4. */
static SPL_Status Qspi_WriteGate(uint32_t width)
{
    uint32_t cr = QUADSPI->CR;
    uint32_t sr = QUADSPI->SR;
    uint32_t ccr = QUADSPI->CCR;
    uint32_t used = Qspi_Flevel(sr);

    if ((cr & QUADSPI_CR_EN) == 0U)
    {
        return SPL_ERR_STATE;
    }
    if ((cr & QUADSPI_CR_ABORT) != 0U)
    {
        return SPL_BUSY;
    }
    if ((Qspi_Fmode(ccr) != QSPI_FMODE_INDIRECT_WRITE) || ((ccr & QUADSPI_CCR_DMODE_Msk) == 0U))
    {
        return SPL_ERR_STATE;
    }
    if ((sr & QUADSPI_SR_BUSY) == 0U)
    {
        /* This DR write starts the command (RM0440 20.3.6). */
        SPL_Status status = Qspi_TriggerGate(cr, sr);

        if (status != SPL_OK)
        {
            return status;
        }
        return ((used + width) > QSPI_FIFO_BYTES) ? SPL_ERR_STATE : SPL_OK;
    }
    if ((sr & QUADSPI_SR_TEF) != 0U)
    {
        return SPL_ERR_HW;
    }
    /* A DR write to a full FIFO stalls the bus (RM0440 20.5.9): never do it.
     * The FIFO drains by itself while BUSY=1. */
    return ((used + width) > QSPI_FIFO_BYTES) ? SPL_WOULD_BLOCK : SPL_OK;
}

/* Indirect-read / auto-polling data gate. width = 1, 2 or 4. */
static SPL_Status Qspi_ReadGate(uint32_t width)
{
    uint32_t cr = QUADSPI->CR;
    uint32_t sr = QUADSPI->SR;
    uint32_t fmode = Qspi_Fmode(QUADSPI->CCR);
    SPL_Status status = SPL_ERR_STATE;

    if ((cr & QUADSPI_CR_EN) == 0U)
    {
        return SPL_ERR_STATE;
    }
    if ((cr & QUADSPI_CR_ABORT) != 0U)
    {
        return SPL_BUSY;
    }
    if (fmode == QSPI_FMODE_INDIRECT_READ)
    {
        if ((sr & QUADSPI_SR_TEF) != 0U)
        {
            status = SPL_ERR_HW;
        }
        else if (Qspi_Flevel(sr) >= width)
        {
            status = SPL_OK;
        }
        /* RM0440 20.5.9: a DR read with too few bytes stalls the bus while
         * BUSY=1. After the last byte (TCF=1) or with no transfer (BUSY=0)
         * the missing bytes never arrive: read the tail narrower. */
        else if (((sr & QUADSPI_SR_TCF) != 0U) || ((sr & QUADSPI_SR_BUSY) == 0U))
        {
            status = SPL_ERR_STATE;
        }
        else
        {
            status = SPL_WOULD_BLOCK;
        }
    }
    else if (fmode == QSPI_FMODE_AUTO_POLLING)
    {
        /* RM0440 20.3.7: FTF set at each new status, cleared by the DR read. */
        if ((sr & QUADSPI_SR_FTF) != 0U)
        {
            status = SPL_OK;
        }
        else if ((sr & QUADSPI_SR_BUSY) != 0U)
        {
            status = SPL_WOULD_BLOCK;
        }
        else
        {
            status = SPL_ERR_STATE;
        }
    }
    else
    {
        /* Indirect write or memory-mapped (DR reads return 0): no data. */
    }
    return status;
}

SPL_Status QSPI_Init(const QSPI_InitTypeDef *init)
{
    uint32_t cr;
    uint32_t sr;

    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!(init == (const QSPI_InitTypeDef *)0), SPL_ERR_PARAM);
    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!(init->fifo_threshold > 15U), SPL_ERR_PARAM);
    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!(init->sample_shift > 1U), SPL_ERR_PARAM);
    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!(init->dual_flash > 1U), SPL_ERR_PARAM);
    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!(init->flash_select > 1U), SPL_ERR_PARAM);
    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!(init->flash_size > 31U), SPL_ERR_PARAM);
    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!(init->cs_high_time > 7U), SPL_ERR_PARAM);
    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!(init->clock_mode > 1U), SPL_ERR_PARAM);
    cr = QUADSPI->CR;
    sr = QUADSPI->SR;
    if ((cr & QUADSPI_CR_EN) != 0U)
    {
        return SPL_ERR_STATE;
    }
    if (((cr & QUADSPI_CR_ABORT) != 0U) || ((sr & QUADSPI_SR_BUSY) != 0U))
    {
        return SPL_BUSY;
    }
#if defined(QSPI_ES0430_WORKAROUNDS)
    /* ERRATA ES0430 2.6.3: apply the timing workaround after reset and after memory-mapped mode;
     * skipped when CCR still holds the workaround value (applied, no command since). Not in
     * ES0523 (G491/G4A1): not built there. */
    if (QUADSPI->CCR != QSPI_ERRATA_WA_CCR)
    {
        Qspi_TimingWorkaround();
        /* Erratum's final "wait for BUSY to fall": not in this call. BUSY
         * then means "call QSPI_Init again"; the retry sees CCR =
         * QSPI_ERRATA_WA_CCR and finishes without repeating the sequence. */
        if ((QUADSPI->SR & QUADSPI_SR_BUSY) != 0U)
        {
            return SPL_BUSY;
        }
    }
#endif

    QUADSPI->DCR = ((uint32_t)init->flash_size << QUADSPI_DCR_FSIZE_Pos) |
                   ((uint32_t)init->cs_high_time << QUADSPI_DCR_CSHT_Pos) |
                   ((uint32_t)init->clock_mode << QUADSPI_DCR_CKMODE_Pos);
    QUADSPI->CR = ((uint32_t)init->prescaler << QUADSPI_CR_PRESCALER_Pos) |
                  ((uint32_t)init->fifo_threshold << QUADSPI_CR_FTHRES_Pos) |
                  ((uint32_t)init->flash_select << QUADSPI_CR_FSEL_Pos) |
                  ((uint32_t)init->dual_flash << QUADSPI_CR_DFM_Pos) |
                  ((uint32_t)init->sample_shift << QUADSPI_CR_SSHIFT_Pos);
    return SPL_OK;
}

SPL_Status QSPI_Enable(void)
{
    if ((QUADSPI->CR & QUADSPI_CR_EN) != 0U)
    {
        return SPL_OK;
    }
    return Qspi_CrUpdate(0U, QUADSPI_CR_EN);
}

SPL_Status QSPI_Disable(void)
{
    if ((QUADSPI->CR & QUADSPI_CR_EN) == 0U)
    {
        return SPL_OK;
    }
    return Qspi_CrUpdate(QUADSPI_CR_EN, 0U);
}

SPL_Status QSPI_CommConfig(const QSPI_CommTypeDef *cmd)
{
    uint32_t cr;
    uint32_t sr;
    uint32_t ccr;
    uint32_t prescaler;
    SPL_Status status;

    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!(cmd == (const QSPI_CommTypeDef *)0), SPL_ERR_PARAM);
    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!(Qspi_CommValid(cmd) == 0U), SPL_ERR_PARAM);
    cr = QUADSPI->CR;
    sr = QUADSPI->SR;
    ccr = QUADSPI->CCR;
    status = Qspi_BusyGate(cr, sr, ccr);
    if (status != SPL_OK)
    {
        return status;
    }
#if defined(QSPI_ES0430_WORKAROUNDS)
    /* ERRATA ES0430 2.6.3: leaving memory-mapped mode needs the workaround first (QSPI_Init).
     * Not in ES0523 (G491/G4A1): not built there. */
    if ((Qspi_Fmode(ccr) == QSPI_FMODE_MEMORY_MAPPED) && (cmd->fmode != QSPI_FMODE_MEMORY_MAPPED))
    {
        return SPL_ERR_STATE;
    }
#endif
    /* RM0440 20.3.4 / 20.5.1 / 20.5.6: DDR needs SSHIFT=0 and a prescaler
     * division of two or more; DHHC=1 needs PRESCALER > 0. */
    prescaler = (cr & QUADSPI_CR_PRESCALER_Msk) >> QUADSPI_CR_PRESCALER_Pos;
    if ((cmd->ddrm != 0U) && (((cr & QUADSPI_CR_SSHIFT) != 0U) || (prescaler == 0U)))
    {
        return SPL_ERR_STATE;
    }
    if ((cmd->dhhc != 0U) && (prescaler == 0U))
    {
        return SPL_ERR_STATE;
    }
    if (Qspi_TriggerOf(cmd->fmode, cmd->admode, cmd->dmode) == QSPI_TRIGGER_CCR)
    {
        status = Qspi_TriggerGate(cr, sr);
        if (status != SPL_OK)
        {
            return status;
        }
    }

    /* ABR must be programmed before the trigger (RM0440 20.3.6); CCR last. */
    QUADSPI->DLR = cmd->dlr;
    QUADSPI->ABR = cmd->abr;
    QUADSPI->CCR = Qspi_CcrValue(cmd);
    return SPL_OK;
}

SPL_Status QSPI_GetStartTrigger(const QSPI_CommTypeDef *cmd, uint32_t *trigger)
{
    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!(cmd == (const QSPI_CommTypeDef *)0), SPL_ERR_PARAM);
    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!(trigger == (uint32_t *)0), SPL_ERR_PARAM);
    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!(cmd->fmode > 3U), SPL_ERR_PARAM);
    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!(cmd->admode > 3U), SPL_ERR_PARAM);
    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!(cmd->dmode > 3U), SPL_ERR_PARAM);
    *trigger = Qspi_TriggerOf(cmd->fmode, cmd->admode, cmd->dmode);
    return SPL_OK;
}

SPL_Status QSPI_SetDataLength(uint32_t dlr)
{
    uint32_t ccr = QUADSPI->CCR;
    SPL_Status status = Qspi_BusyGate(QUADSPI->CR, QUADSPI->SR, ccr);

    if (status != SPL_OK)
    {
        return status;
    }
    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!((Qspi_Fmode(ccr) == QSPI_FMODE_AUTO_POLLING) && (dlr > 3U)), SPL_ERR_PARAM);
    QUADSPI->DLR = dlr;
    return SPL_OK;
}

SPL_Status QSPI_SetAlternateBytes(uint32_t abr)
{
    SPL_Status status = Qspi_BusyGate(QUADSPI->CR, QUADSPI->SR, QUADSPI->CCR);

    if (status != SPL_OK)
    {
        return status;
    }
    QUADSPI->ABR = abr;
    return SPL_OK;
}

SPL_Status QSPI_SetAddress(uint32_t address)
{
    uint32_t cr = QUADSPI->CR;
    uint32_t sr = QUADSPI->SR;
    uint32_t ccr = QUADSPI->CCR;
    uint32_t fmode = Qspi_Fmode(ccr);
    uint32_t admode = (ccr & QUADSPI_CCR_ADMODE_Msk) >> QUADSPI_CCR_ADMODE_Pos;
    uint32_t dmode = (ccr & QUADSPI_CCR_DMODE_Msk) >> QUADSPI_CCR_DMODE_Pos;
    SPL_Status status = Qspi_BusyGate(cr, sr, ccr);

    if (status != SPL_OK)
    {
        return status;
    }
    /* RM0440 20.5.7: AR writes are ignored in memory-mapped mode. */
    if ((fmode == QSPI_FMODE_MEMORY_MAPPED) || (admode == QSPI_LINES_NONE))
    {
        return SPL_ERR_STATE;
    }
    /* RM0440 20.5.7: ADDRESS[0] is stuck at 0 in dual-flash mode. */
    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!(((cr & QUADSPI_CR_DFM) != 0U) && ((address & 1U) != 0U)), SPL_ERR_PARAM);
    if (Qspi_TriggerOf(fmode, admode, dmode) == QSPI_TRIGGER_AR)
    {
        status = Qspi_TriggerGate(cr, sr);
        if (status != SPL_OK)
        {
            return status;
        }
    }
    QUADSPI->AR = address;
    return SPL_OK;
}

/* Byte/half-word DR accesses must target the least significant byte/half-word
 * (RM0440 20.3.6, 20.5.9); the access width selects how many FIFO bytes move
 * (deviation class D-06). */
SPL_Status QSPI_TryWrite8(uint8_t data)
{
    SPL_Status status = Qspi_WriteGate(1U);

    if (status == SPL_OK)
    {
        *(volatile uint8_t *)(volatile void *)&QUADSPI->DR = data;
    }
    return status;
}

SPL_Status QSPI_TryWrite16(uint16_t data)
{
    SPL_Status status = Qspi_WriteGate(2U);

    if (status == SPL_OK)
    {
        *(volatile uint16_t *)(volatile void *)&QUADSPI->DR = data;
    }
    return status;
}

SPL_Status QSPI_TryWrite32(uint32_t data)
{
    SPL_Status status = Qspi_WriteGate(4U);

    if (status == SPL_OK)
    {
        QUADSPI->DR = data;
    }
    return status;
}

SPL_Status QSPI_TryRead8(uint8_t *data)
{
    SPL_Status status;

    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!(data == (uint8_t *)0), SPL_ERR_PARAM);
    status = Qspi_ReadGate(1U);
    if (status == SPL_OK)
    {
        *data = *(volatile uint8_t *)(volatile void *)&QUADSPI->DR;
    }
    return status;
}

SPL_Status QSPI_TryRead16(uint16_t *data)
{
    SPL_Status status;

    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!(data == (uint16_t *)0), SPL_ERR_PARAM);
    status = Qspi_ReadGate(2U);
    if (status == SPL_OK)
    {
        *data = *(volatile uint16_t *)(volatile void *)&QUADSPI->DR;
    }
    return status;
}

SPL_Status QSPI_TryRead32(uint32_t *data)
{
    SPL_Status status;

    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!(data == (uint32_t *)0), SPL_ERR_PARAM);
    status = Qspi_ReadGate(4U);
    if (status == SPL_OK)
    {
        *data = QUADSPI->DR;
    }
    return status;
}

SPL_Status QSPI_GetStatus(uint32_t *sr)
{
    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!(sr == (uint32_t *)0), SPL_ERR_PARAM);
    *sr = QUADSPI->SR;
    return SPL_OK;
}

SPL_Status QSPI_ClearFlags(uint32_t flags)
{
    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!(flags == 0U), SPL_ERR_PARAM);
    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!((flags & ~QSPI_FLAGS_CLEARABLE) != 0U), SPL_ERR_PARAM);
    /* FCR is write-only W1C (RM0440 20.5.4): plain write, no RMW. */
    QUADSPI->FCR = flags;
    /* Read back: the clear reaches the peripheral before return (METHOD 1.7, issue #23). */
    (void)QUADSPI->SR;
    return SPL_OK;
}

SPL_Status QSPI_Abort(void)
{
    uint32_t cr = QUADSPI->CR;

    if (((cr & QUADSPI_CR_ABORT) != 0U) || ((QUADSPI->SR & QUADSPI_SR_BUSY) == 0U))
    {
        return SPL_OK;
    }
    QUADSPI->CR = cr | QUADSPI_CR_ABORT;
    return SPL_OK;
}

SPL_Status QSPI_AbortPoll(void)
{
    if ((QUADSPI->CR & QUADSPI_CR_ABORT) != 0U)
    {
        return SPL_WOULD_BLOCK;
    }
    if ((QUADSPI->SR & QUADSPI_SR_BUSY) != 0U)
    {
        return SPL_ERR_STATE;
    }
    return SPL_OK;
}

SPL_Status QSPI_AutoPollingConfig(const QSPI_AutoPollTypeDef *cfg)
{
    uint32_t cr;
    uint32_t set = 0U;
    SPL_Status status;

    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!(cfg == (const QSPI_AutoPollTypeDef *)0), SPL_ERR_PARAM);
    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!(cfg->match_or > 1U), SPL_ERR_PARAM);
    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!(cfg->stop_on_match > 1U), SPL_ERR_PARAM);
    cr = QUADSPI->CR;
    status = Qspi_BusyGate(cr, QUADSPI->SR, QUADSPI->CCR);
    if (status != SPL_OK)
    {
        return status;
    }
    if (cfg->match_or != 0U)
    {
        set |= QUADSPI_CR_PMM;
    }
    if (cfg->stop_on_match != 0U)
    {
        set |= QUADSPI_CR_APMS;
    }
    QUADSPI->PSMKR = cfg->mask;
    QUADSPI->PSMAR = cfg->match;
    QUADSPI->PIR = (uint32_t)cfg->interval;
    QUADSPI->CR = (cr & ~(QUADSPI_CR_PMM | QUADSPI_CR_APMS)) | set;
    return SPL_OK;
}

SPL_Status QSPI_MemoryMappedTimeout(uint16_t timeout, uint8_t enable)
{
    uint32_t cr;
    SPL_Status status;

    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!(enable > 1U), SPL_ERR_PARAM);
    cr = QUADSPI->CR;
    status = Qspi_BusyGate(cr, QUADSPI->SR, QUADSPI->CCR);
    if (status != SPL_OK)
    {
        return status;
    }
    QUADSPI->LPTR = (uint32_t)timeout;
    if (enable != 0U)
    {
        cr |= QUADSPI_CR_TCEN;
    }
    else
    {
        cr &= ~QUADSPI_CR_TCEN;
    }
    QUADSPI->CR = cr;
    return SPL_OK;
}

SPL_Status QSPI_DmaConfig(uint8_t enable)
{
    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!(enable > 1U), SPL_ERR_PARAM);
    return (enable != 0U) ? Qspi_CrUpdate(0U, QUADSPI_CR_DMAEN)
                          : Qspi_CrUpdate(QUADSPI_CR_DMAEN, 0U);
}

SPL_Status QSPI_InterruptConfig(uint32_t mask, uint8_t enable)
{
    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!(mask == 0U), SPL_ERR_PARAM);
    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!((mask & ~QSPI_IT_ALL) != 0U), SPL_ERR_PARAM);
    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!(enable > 1U), SPL_ERR_PARAM);
    return (enable != 0U) ? Qspi_CrUpdate(0U, mask) : Qspi_CrUpdate(mask, 0U);
}

#else /* !QUADSPI: SKU without Quad-SPI, every API is UNSUPPORTED. */

SPL_Status QSPI_Init(const QSPI_InitTypeDef *init)
{
    (void)init;
    return SPL_ERR_UNSUPPORTED;
}

SPL_Status QSPI_Enable(void)
{
    return SPL_ERR_UNSUPPORTED;
}

SPL_Status QSPI_Disable(void)
{
    return SPL_ERR_UNSUPPORTED;
}

SPL_Status QSPI_CommConfig(const QSPI_CommTypeDef *cmd)
{
    (void)cmd;
    return SPL_ERR_UNSUPPORTED;
}

SPL_Status QSPI_GetStartTrigger(const QSPI_CommTypeDef *cmd, uint32_t *trigger)
{
    (void)cmd;
    (void)trigger;
    return SPL_ERR_UNSUPPORTED;
}

SPL_Status QSPI_SetDataLength(uint32_t dlr)
{
    (void)dlr;
    return SPL_ERR_UNSUPPORTED;
}

SPL_Status QSPI_SetAlternateBytes(uint32_t abr)
{
    (void)abr;
    return SPL_ERR_UNSUPPORTED;
}

SPL_Status QSPI_SetAddress(uint32_t address)
{
    (void)address;
    return SPL_ERR_UNSUPPORTED;
}

SPL_Status QSPI_TryWrite8(uint8_t data)
{
    (void)data;
    return SPL_ERR_UNSUPPORTED;
}

SPL_Status QSPI_TryWrite16(uint16_t data)
{
    (void)data;
    return SPL_ERR_UNSUPPORTED;
}

SPL_Status QSPI_TryWrite32(uint32_t data)
{
    (void)data;
    return SPL_ERR_UNSUPPORTED;
}

SPL_Status QSPI_TryRead8(uint8_t *data)
{
    (void)data;
    return SPL_ERR_UNSUPPORTED;
}

SPL_Status QSPI_TryRead16(uint16_t *data)
{
    (void)data;
    return SPL_ERR_UNSUPPORTED;
}

SPL_Status QSPI_TryRead32(uint32_t *data)
{
    (void)data;
    return SPL_ERR_UNSUPPORTED;
}

SPL_Status QSPI_GetStatus(uint32_t *sr)
{
    (void)sr;
    return SPL_ERR_UNSUPPORTED;
}

SPL_Status QSPI_ClearFlags(uint32_t flags)
{
    (void)flags;
    return SPL_ERR_UNSUPPORTED;
}

SPL_Status QSPI_Abort(void)
{
    return SPL_ERR_UNSUPPORTED;
}

SPL_Status QSPI_AbortPoll(void)
{
    return SPL_ERR_UNSUPPORTED;
}

SPL_Status QSPI_AutoPollingConfig(const QSPI_AutoPollTypeDef *cfg)
{
    (void)cfg;
    return SPL_ERR_UNSUPPORTED;
}

SPL_Status QSPI_MemoryMappedTimeout(uint16_t timeout, uint8_t enable)
{
    (void)timeout;
    (void)enable;
    return SPL_ERR_UNSUPPORTED;
}

SPL_Status QSPI_DmaConfig(uint8_t enable)
{
    (void)enable;
    return SPL_ERR_UNSUPPORTED;
}

SPL_Status QSPI_InterruptConfig(uint32_t mask, uint8_t enable)
{
    (void)mask;
    (void)enable;
    return SPL_ERR_UNSUPPORTED;
}

#endif /* QUADSPI */
