/* SPDX-License-Identifier: MIT */
#ifndef STM32G4_QSPI_H
#define STM32G4_QSPI_H
#include <stdint.h>
#include "stm32g4xx.h"
#include "stm32g4_status.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* Quad-SPI interface driver (RM0440 Rev 9 section 20). Single instance (CMSIS QUADSPI,
 * 0xA000 1000), present only on SKUs whose CMSIS header defines QUADSPI
 * (G471/473/474/483/484/491/4A1). On other SKUs (G431/441/411/414) the module still compiles
 * and EVERY API returns SPL_ERR_UNSUPPORTED without any register access.
 * Common rules: no handle, no static state, no function waits; every register write happens
 * only after all parameter and state checks passed; CR read-modify-writes never copy the
 * self-clearing ABORT back (while ABORT is pending a CR writer returns BUSY). The driver is a
 * register-level command engine: flash commands, page / sector boundaries, write-enable,
 * busy polling of the memory and its timing belong to the device layer (caller).
 * Usage: QSPI_Init (interrupts masked on G47x/G48x, ERRATA below), QSPI_Enable, then per
 * command QSPI_CommConfig (plus QSPI_SetAddress / QSPI_TryWrite* / QSPI_TryRead* as
 * QSPI_GetStartTrigger reports), QSPI_GetStatus / QSPI_ClearFlags; stop with QSPI_Abort +
 * QSPI_AbortPoll.
 * Caller duties (all APIs): RCC AHB3 clock enable (QSPIEN) and kernel clock QSPISEL (RCC
 * module); GPIO alternate functions of CLK / NCS / IO pins; DMA channel setup; with DMAEN = 1
 * disable the DMA channel before changing FTHRES or FMODE, and in indirect write write AR
 * before enabling the DMA (RM0440 20.3.11); never access the 0x9000 0000 bank before
 * memory-mapped mode is configured; FSIZE must cover the memory (TEF / bus fault otherwise);
 * NVIC and handler (the handler clears TE / TC / SM / TO with QSPI_ClearFlags); serialize all
 * callers (one owner; an ISR that services the FIFO must be the only DR user while a command
 * runs); bound every retry loop on BUSY / WOULD_BLOCK (a FIFO that nobody services never
 * drains, the driver cannot tell a stalled transfer from a running one).
 * Contract: Docs/v0.12_QSPI_CONTRACT.md.
 * Errata: ES0430 Rev 10 (G471/473/474/483/484) section 2.6 and ES0523 Rev 7
 * (G491/G4A1) section 2.5, contract section "Errata". ES0523 lists only the
 * last-byte item (2.5.1); the ES0430 2.6.1 / 2.6.3 rules below are built for
 * the ES0430 dies only (v0.13.2).
 * ERRATA ES0430 2.6.2: CCR = 0 hangs the peripheral; every accepted command
 * has CCR != 0 (empty command refused on every SKU). Recovery if it ever
 * happens: EN 0->1.
 * ERRATA ES0430 2.6.4 / ES0523 2.5.1: a memory-mapped read of the last byte of the FSIZE
 * region returns 0x00, a repeated read stalls the AXI bus: keep margin in
 * flash_size or read that byte in indirect mode (caller). */

/* QUADSPI_CCR field codes (RM0440 20.5.6). */
#define QSPI_FMODE_INDIRECT_WRITE 0U
#define QSPI_FMODE_INDIRECT_READ 1U
#define QSPI_FMODE_AUTO_POLLING 2U
#define QSPI_FMODE_MEMORY_MAPPED 3U

/* IMODE / ADMODE / ABMODE / DMODE. */
#define QSPI_LINES_NONE 0U
#define QSPI_LINES_1 1U
#define QSPI_LINES_2 2U
#define QSPI_LINES_4 3U

/* ADSIZE / ABSIZE. */
#define QSPI_SIZE_8 0U
#define QSPI_SIZE_16 1U
#define QSPI_SIZE_24 2U
#define QSPI_SIZE_32 3U

/* Register write that starts the programmed command (RM0440 20.3.6,
 * 20.3.11), returned by QSPI_GetStartTrigger. */
#define QSPI_TRIGGER_CCR 0U /* CCR write (QSPI_CommConfig) starts it. */
#define QSPI_TRIGGER_AR 1U  /* AR write (QSPI_SetAddress) starts it. */
#define QSPI_TRIGGER_DR 2U  /* First DR write (QSPI_TryWrite*) starts it. */
#define QSPI_TRIGGER_AHB 3U /* First memory-mapped read access starts it. */

/* QUADSPI_SR flags (QSPI_GetStatus). TE/TC/SM/TO are cleared through
 * QSPI_ClearFlags (FCR bit at the same position, RM0440 20.5.4); FT is
 * hardware-managed. */
#if defined(QUADSPI)
#define QSPI_FLAG_TE QUADSPI_SR_TEF
#define QSPI_FLAG_TC QUADSPI_SR_TCF
#define QSPI_FLAG_FT QUADSPI_SR_FTF
#define QSPI_FLAG_SM QUADSPI_SR_SMF
#define QSPI_FLAG_TO QUADSPI_SR_TOF
#define QSPI_FLAG_BUSY QUADSPI_SR_BUSY
#define QSPI_SR_FLEVEL_MASK QUADSPI_SR_FLEVEL_Msk
#define QSPI_SR_FLEVEL_SHIFT QUADSPI_SR_FLEVEL_Pos
/* QUADSPI_CR interrupt enables (QSPI_InterruptConfig). */
#define QSPI_IT_TE QUADSPI_CR_TEIE
#define QSPI_IT_TC QUADSPI_CR_TCIE
#define QSPI_IT_FT QUADSPI_CR_FTIE
#define QSPI_IT_SM QUADSPI_CR_SMIE
#define QSPI_IT_TO QUADSPI_CR_TOIE
#else
/* No QUADSPI on this SKU: same values so portable code compiles. */
#define QSPI_FLAG_TE 0x01UL
#define QSPI_FLAG_TC 0x02UL
#define QSPI_FLAG_FT 0x04UL
#define QSPI_FLAG_SM 0x08UL
#define QSPI_FLAG_TO 0x10UL
#define QSPI_FLAG_BUSY 0x20UL
#define QSPI_SR_FLEVEL_MASK 0x1F00UL
#define QSPI_SR_FLEVEL_SHIFT 8U
#define QSPI_IT_TE 0x00010000UL
#define QSPI_IT_TC 0x00020000UL
#define QSPI_IT_FT 0x00040000UL
#define QSPI_IT_SM 0x00080000UL
#define QSPI_IT_TO 0x00100000UL
#endif
#define QSPI_FLAGS_CLEARABLE (QSPI_FLAG_TE | QSPI_FLAG_TC | QSPI_FLAG_SM | QSPI_FLAG_TO)
#define QSPI_IT_ALL (QSPI_IT_TE | QSPI_IT_TC | QSPI_IT_FT | QSPI_IT_SM | QSPI_IT_TO)

    /* Raw RM field values (RM0440 20.5.1 CR, 20.5.2 DCR). */
    typedef struct
    {
        uint8_t prescaler;      /* CR.PRESCALER 0..255: CLK = F / (prescaler + 1). */
        uint8_t fifo_threshold; /* CR.FTHRES 0..15: FTF at fifo_threshold + 1 bytes. */
        uint8_t sample_shift;   /* CR.SSHIFT 0/1 (must be 0 for DDR commands). */
        uint8_t dual_flash;     /* CR.DFM 0/1. */
        uint8_t flash_select;   /* CR.FSEL 0: FLASH 1, 1: FLASH 2 (ignored when DFM=1). */
        uint8_t flash_size;     /* DCR.FSIZE 0..31: 2^(flash_size + 1) bytes. */
        uint8_t cs_high_time;   /* DCR.CSHT 0..7: NCS high >= cs_high_time + 1 CLK. */
        uint8_t clock_mode;     /* DCR.CKMODE 0: mode 0, 1: mode 3. */
    } QSPI_InitTypeDef;

    /* One command (RM0440 20.5.5 DLR, 20.5.8 ABR, 20.5.6 CCR). */
    typedef struct
    {
        uint32_t dlr;        /* DLR: bytes - 1; 0xFFFFFFFF = undefined; <= 3 for polling. */
        uint32_t abr;        /* ABR: alternate bytes (used when abmode != NONE). */
        uint8_t fmode;       /* QSPI_FMODE_x. */
        uint8_t instruction; /* INSTRUCTION 0..255. */
        uint8_t imode;       /* QSPI_LINES_x. */
        uint8_t admode;      /* QSPI_LINES_x. */
        uint8_t adsize;      /* QSPI_SIZE_x. */
        uint8_t abmode;      /* QSPI_LINES_x. */
        uint8_t absize;      /* QSPI_SIZE_x. */
        uint8_t dcyc;        /* Dummy cycles 0..31. */
        uint8_t dmode;       /* QSPI_LINES_x; NONE only for indirect write. */
        uint8_t sioo;        /* 1: send instruction only once. */
        uint8_t ddrm;        /* 1: DDR address/alternate/data phases. */
        uint8_t dhhc;        /* 1: DDR hold 1/4 CLK (needs prescaler > 0). */
    } QSPI_CommTypeDef;

    typedef struct
    {
        uint32_t mask;         /* PSMKR: 1 = bit compared. */
        uint32_t match;        /* PSMAR. */
        uint16_t interval;     /* PIR: CLK cycles between two status reads. */
        uint8_t match_or;      /* CR.PMM 0: AND match, 1: OR match. */
        uint8_t stop_on_match; /* CR.APMS 1: polling stops at the first match. */
    } QSPI_AutoPollTypeDef;

    /* Requires EN=0 (else STATE) and BUSY=0 (else BUSY). Writes DCR, then the
     * whole CR (EN=0, ABORT=0, DMAEN/TCEN/APMS/PMM and all interrupt enables
     * cleared).
     * ERRATA ES0430 2.6.3 (G47x/G48x only, not ES0523): first runs the erratum timing workaround
     * (CR = 0, CR = 0xFF000001, CCR = 0x20000000 twice, CR = 0) unless CCR already holds 0x20000000
     * (workaround done, no command since). If BUSY is still high after it: BUSY, nothing else
     * written; call again (no repeat). Call after reset and after leaving memory-mapped mode
     * (Abort/Disable, then Init), with interrupts masked: the last CR write must follow the first
     * CCR write within 127 QUADSPI kernel clocks. Leaves CCR = 0x20000000.
     * Caller: mask interrupts around the call on G47x/G48x (127-kernel-clock window above); clear
     * the SR flags after QSPI_Enable (contract O-6: the workaround may set SR flags). */
    /* Level: PARAM[F] STATE[r] UNSUPPORTED[r] BUSY[r] */
    SPL_Status QSPI_Init(const QSPI_InitTypeDef *init);
    /* Sets / clears CR.EN (read-modify-write that never copies ABORT back).
     * ABORT still pending => BUSY. Disable is allowed while BUSY=1: it is the
     * RM stop path for auto-polling and memory-mapped mode.
     * Caller: QSPI_Init first. */
    /* Level: UNSUPPORTED[r] BUSY[r] */
    SPL_Status QSPI_Enable(void);
    /* Clears CR.EN (same rules as QSPI_Enable).
     * Caller: none beyond the file-header duties. */
    /* Level: UNSUPPORTED[r] BUSY[r] */
    SPL_Status QSPI_Disable(void);

    /* Writes DLR, ABR, then CCR (CCR last: it may be the start trigger).
     * Needs BUSY=0 (else BUSY or STATE, see contract). When the CCR write is
     * the trigger: EN=1, TEF=0 (else ERR_HW), TCF=0 and SMF=0 (else STATE).
     * ERRATA ES0430 2.6.1 (G47x/G48x only, not ES0523): PARAM for an indirect
     * read with no instruction, address or alternate phase and dcyc < 2 (it
     * would hang with BUSY=1).
     * ERRATA ES0430 2.6.3 (G47x/G48x only, not ES0523): STATE when the current
     * CCR is memory-mapped and cmd is not: run QSPI_Disable + QSPI_Init
     * (workaround) first.
     * Caller: the command content (opcode, dummy cycles, line modes) must match the memory
     * datasheet (device layer); for auto-polling call QSPI_AutoPollingConfig first. */
    /* Level: PARAM[F] STATE[r] UNSUPPORTED[r] HW[r] BUSY[r] */
    SPL_Status QSPI_CommConfig(const QSPI_CommTypeDef *cmd);
    /* Pure: which register write starts cmd (QSPI_TRIGGER_x). No register access. */
    /* Caller: none beyond the file-header duties. */
    /* Level: PARAM[F] UNSUPPORTED[r] */
    SPL_Status QSPI_GetStartTrigger(const QSPI_CommTypeDef *cmd, uint32_t *trigger);
    /* DLR / ABR setters between commands: BUSY=0 required; never a trigger. With DFM = 1 DL[0]
     * reads back 1 (RM0440 20.5.5).
     * Caller: write them before the trigger of the command that uses them. */
    /* Level: PARAM[F] STATE[r] UNSUPPORTED[r] BUSY[r] */
    SPL_Status QSPI_SetDataLength(uint32_t dlr);
    /* ABR write; same rules as QSPI_SetDataLength.
     * Caller: write it before the trigger of the command that uses it. */
    /* Level: STATE[r] UNSUPPORTED[r] BUSY[r] */
    SPL_Status QSPI_SetAlternateBytes(uint32_t abr);
    /* AR write. STATE when the current CCR has no address phase or is in
     * memory-mapped mode (the RM ignores AR writes there). Trigger rules as
     * for QSPI_CommConfig when AR starts the command.
     * Caller: the address must respect the memory's page / sector boundaries (device layer). */
    /* Level: PARAM[F] STATE[r] UNSUPPORTED[r] HW[r] BUSY[r] */
    SPL_Status QSPI_SetAddress(uint32_t address);

    /* Indirect write FIFO (FMODE=00). Byte/half-word/word DR access adds
     * 1/2/4 bytes. With BUSY=0 the write is the start trigger (trigger gate).
     * FIFO has fewer free bytes than the access width: WOULD_BLOCK while BUSY=1 (the transfer
     * drains it); STATE while BUSY=0 (nothing drains it, waiting cannot help).
     * Caller: write the DLR + 1 bytes of the command (DLR undefined: end with QSPI_Abort) and
     * keep servicing the FIFO until TCF (a FIFO not refilled stalls the transfer with
     * BUSY = 1). */
    /* Level: STATE[r] UNSUPPORTED[r] HW[r] BUSY[r] WOULD_BLOCK[r] */
    SPL_Status QSPI_TryWrite8(uint8_t data);
    /* Caller: as QSPI_TryWrite8. */
    /* Level: STATE[r] UNSUPPORTED[r] HW[r] BUSY[r] WOULD_BLOCK[r] */
    SPL_Status QSPI_TryWrite16(uint16_t data);
    /* Caller: as QSPI_TryWrite8. */
    /* Level: STATE[r] UNSUPPORTED[r] HW[r] BUSY[r] WOULD_BLOCK[r] */
    SPL_Status QSPI_TryWrite32(uint32_t data);
    /* Indirect read FIFO (FMODE=01) or last polled status (FMODE=10).
     * WOULD_BLOCK while fewer than width bytes are available and more are
     * coming; STATE when they will never come (read the tail narrower).
     * DESTRUCTIVE: on OK the bytes are popped from the FIFO (FMODE=01) or FTF is cleared
     * (FMODE=10).
     * Caller: keep reading until all DLR + 1 bytes are taken (a full FIFO that is not emptied
     * stalls the transfer with BUSY = 1). */
    /* Level: PARAM[F] STATE[r] UNSUPPORTED[r] HW[r] BUSY[r] WOULD_BLOCK[r] */
    SPL_Status QSPI_TryRead8(uint8_t *data);
    /* Caller: as QSPI_TryRead8. */
    /* Level: PARAM[F] STATE[r] UNSUPPORTED[r] HW[r] BUSY[r] WOULD_BLOCK[r] */
    SPL_Status QSPI_TryRead16(uint16_t *data);
    /* Caller: as QSPI_TryRead8. */
    /* Level: PARAM[F] STATE[r] UNSUPPORTED[r] HW[r] BUSY[r] WOULD_BLOCK[r] */
    SPL_Status QSPI_TryRead32(uint32_t *data);

    /* Raw SR read (no side effects). */
    /* Caller: none beyond the file-header duties. */
    /* Level: PARAM[F] UNSUPPORTED[r] */
    SPL_Status QSPI_GetStatus(uint32_t *sr);
    /* Writes FCR = flags; flags: non-zero subset of QSPI_FLAGS_CLEARABLE. */
    /* Caller: none beyond the file-header duties. */
    /* Level: PARAM[F] UNSUPPORTED[r] */
    SPL_Status QSPI_ClearFlags(uint32_t flags);

    /* Requests CR.ABORT (self-clearing). OK without a write when BUSY=0 or
     * ABORT already pending. Then poll QSPI_AbortPoll; TCF is set by the
     * abort (clear it with QSPI_ClearFlags).
     * Caller: the memory may be left mid-command (e.g. a program or erase that was started):
     * recover it at the device layer. */
    /* Level: UNSUPPORTED[r] */
    SPL_Status QSPI_Abort(void);
    /* OK: no abort pending and BUSY=0. WOULD_BLOCK: ABORT still set.
     * STATE: BUSY=1 without an abort request (call QSPI_Abort).
     * Caller: bound the poll loop (no timeout here). */
    /* Level: STATE[r] UNSUPPORTED[r] WOULD_BLOCK[r] */
    SPL_Status QSPI_AbortPoll(void);

    /* PSMKR, PSMAR, PIR, then CR.PMM/APMS. BUSY=0 required. Call before the
     * QSPI_CommConfig (FMODE=10) that may start polling.
     * Caller: mask / match must suit the memory's status register (device layer); with
     * stop_on_match = 0 polling runs until QSPI_Abort. */
    /* Level: PARAM[F] STATE[r] UNSUPPORTED[r] BUSY[r] */
    SPL_Status QSPI_AutoPollingConfig(const QSPI_AutoPollTypeDef *cfg);
    /* LPTR = timeout, then CR.TCEN = enable. BUSY=0 required.
     * QSPI_Init leaves the timeout off (TCEN = 0): in memory-mapped mode NCS then stays active
     * (memory selected) after the last access until QSPI_Abort, which costs supply current and
     * keeps the memory out of its standby state (CCL-2 DBL-02). Enable it here when the memory
     * should be released between accesses.
     * Caller: decide whether NCS may stay active after memory-mapped accesses (timeout off). */
    /* Level: PARAM[F] STATE[r] UNSUPPORTED[r] BUSY[r] */
    SPL_Status QSPI_MemoryMappedTimeout(uint16_t timeout, uint8_t enable);
    /* CR.DMAEN = enable. Allowed in any BUSY state (no RM restriction).
     * Caller: configure and enable the DMA channel (DMAMUX request, buffer that lives until the
     * transfer ends); disable that channel before changing FTHRES or FMODE. */
    /* Level: PARAM[F] UNSUPPORTED[r] */
    SPL_Status QSPI_DmaConfig(uint8_t enable);
    /* Sets (enable=1) or clears (enable=0) the CR enables in mask
     * (non-zero subset of QSPI_IT_ALL).
     * Caller: clear stale SR flags (QSPI_ClearFlags) before enabling, else the IRQ fires at once;
     * NVIC and handler are the caller's. */
    /* Level: PARAM[F] UNSUPPORTED[r] */
    SPL_Status QSPI_InterruptConfig(uint32_t mask, uint8_t enable);

#ifdef __cplusplus
}
#endif
#endif
