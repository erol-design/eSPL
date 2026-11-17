/* SPDX-License-Identifier: MIT */
#ifndef STM32G4_FLASH_H
#define STM32G4_FLASH_H
#include <stdint.h>
#include "stm32g4xx.h"
#include "stm32g4_status.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* Embedded flash interface for STM32G4 (RM0440 Rev 9 sections 3, 4 and 5).
 * Scope: wait states and accelerator, geometry, unlock / lock, page erase, double-word program
 * and ECC diagnosis. Option-byte programming is out of scope (OPTR is only read).
 * Usage: FLASH_Unlock, FLASH_ClearFlags (pending errors), FLASH_ErasePageStart /
 * FLASH_ProgramDoubleWordStart, poll FLASH_PollOperation until OK, FLASH_EndOperation after the
 * last operation, then FLASH_Lock.
 * Common rules: single FLASH instance, no dynamic memory, no static state; no function waits
 * (program / erase only start the operation, BUSY is returned instead). Every API that writes
 * CR checks BSY first (a CR write while BSY=1 stalls the AHB bus, RM0440 3.3.6). Output
 * parameters are written only on OK. FLASH_PollOperation and FLASH_GetFlags are read-only.
 * Executing code or reading data from the bank that is being erased or
 * programmed stalls the bus (CPU) until the operation ends (RM0440 3.3.6).
 * Single-bank parts always stall unless the caller runs from RAM.
 * Caller duties (all APIs; not done by the driver):
 *  - Single owner: not reentrant; serialise all FLASH calls (ISR included).
 *  - Program / erase only in VCORE range 1 (RM0440 3.3.6; PWR is not checked here).
 *  - Code location (CCL-2 FL-09a): budget the bus stall against the watchdog and the interrupt
 *    latency, or place the wait loop, the handlers and the vector table in RAM (DBANK=1: in
 *    the other bank).
 *  - Device layer: record layout, wear levelling and power-fail handling are the caller's.
 * Contract: Docs/v0.10_FLASH_CONTRACT.md. */

/* Unlock keys for FLASH_KEYR (RM0440 3.7.3). Not provided by CMSIS. */
#define FLASH_KEY1 0x45670123UL
#define FLASH_KEY2 0xCDEF89ABUL

/* Bank selectors for FLASH_ErasePageStart(). */
#define FLASH_BANK_1 1U
#define FLASH_BANK_2 2U

/* SR error flags that block a new operation (RM0440 3.3.6/3.3.7: "check and
 * clear all error programming flags ... If not, PGSERR is set") plus RDERR. */
#define FLASH_SR_ERRORS                                                                            \
    (FLASH_SR_OPERR | FLASH_SR_PROGERR | FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_SIZERR |     \
     FLASH_SR_PGSERR | FLASH_SR_MISERR | FLASH_SR_FASTERR | FLASH_SR_RDERR)

/* All W1C (rc_w1) SR flags accepted by FLASH_ClearFlags(). */
#define FLASH_SR_CLEARABLE (FLASH_SR_EOP | FLASH_SR_ERRORS | FLASH_SR_OPTVERR)

    /* VCORE range for the wait-state table (RM0440 Table 9/19/29). */
    typedef enum
    {
        FLASH_RANGE1_BOOST = 0,
        FLASH_RANGE1_NORMAL = 1,
        FLASH_RANGE2 = 2
    } FLASH_RangeTypeDef;

    /* Main-memory geometry derived from the SKU, OPTR.DBANK and the flash size
     * data register (RM0440 48.2). */
    typedef struct
    {
        uint32_t size_kb;        /* FLASH_SIZE register value. */
        uint32_t banks;          /* 1 or 2. */
        uint32_t page_size;      /* Bytes. */
        uint32_t pages_per_bank; /* Valid PNB values are 0..pages_per_bank-1. */
    } FLASH_GeometryTypeDef;

    /* FLASH_ECCR snapshot (RM0440 3.7.7 / 4.7.7 / 5.7.7). */
    typedef struct
    {
        uint32_t address;     /* ADDR_ECC field, raw (fail address as defined by the RM) */
        uint8_t detected;     /* ECCD (| ECCD2): double error, raises NMI */
        uint8_t corrected;    /* ECCC (| ECCC2): single error corrected */
        uint8_t system_flash; /* SYSF_ECC: error in system flash */
        uint8_t bank;         /* FLASH_BANK_1, or FLASH_BANK_2 when BK_ECC = 1 (DBANK SKUs) */
    } FLASH_EccTypeDef;

    /* Wait states required for hclk_hz in the given range (pure calculation). */
    /* Caller: pass the VCORE range actually set in PWR (not read here). */
    /* Level: PARAM[F,r] */
    SPL_Status FLASH_CalcLatency(uint32_t hclk_hz, FLASH_RangeTypeDef range, uint32_t *latency);
    /* Program ACR.LATENCY (0..15) and verify by read-back (ERR_HW on mismatch). */
    /* Caller: raise the latency before HCLK increases and lower it only after HCLK has decreased
     * (RM0440 3.3.4). */
    /* Level: PARAM[F] HW[r] */
    SPL_Status FLASH_SetLatency(uint32_t latency);
    /* Prefetch / instruction cache / data cache enable bits (each 0 or 1). */
    /* Caller: DBANK=1 parts: DCEN must be 0 during program / erase (ES0430 2.2.2, see below). */
    /* Level: PARAM[F] */
    SPL_Status FLASH_ConfigAccelerator(uint8_t prefetch, uint8_t icache, uint8_t dcache);
    /* Reset both caches; requires ICEN=0 and DCEN=0 (else ERR_STATE). */
    /* Caller: re-enable the caches afterwards with FLASH_ConfigAccelerator. */
    /* Level: STATE[r] */
    SPL_Status FLASH_ResetCaches(void);

    /* Caller: none beyond the file-header duties. */
    /* Level: PARAM[F] UNSUPPORTED[r] */
    SPL_Status FLASH_GetGeometry(FLASH_GeometryTypeDef *geometry);
    /* Raw FLASH_OPTR value (read-only access). */
    /* Caller: none beyond the file-header duties. */
    /* Level: PARAM[F] */
    SPL_Status FLASH_GetOptionRegister(uint32_t *optr);

    /* KEY1/KEY2 sequence. Already unlocked: OK, no write. A wrong sequence
     * locks FLASH_CR until the next reset (and raises a HardFault).
     * Caller duty (CCL-2 FL-03a): this driver never locks by itself; after every FLASH_Unlock
     * the caller calls FLASH_Lock on every exit path of its sequence, including error and
     * timeout branches (retry FLASH_Lock while it returns BUSY, or end with a reset). */
    /* Level: HW[r] */
    SPL_Status FLASH_Unlock(void);
    /* Clears the operation bits and sets LOCK. BUSY while BSY=1. */
    /* Caller: bound the BUSY retries (end with a reset if BSY never clears). */
    /* Level: BUSY[r] */
    SPL_Status FLASH_Lock(void);

    /* ERRATA ES0430 2.2.2 (ErasePageStart, ProgramDoubleWordStart): OPTR.DBANK = 1 with
     * ACR.DCEN = 1 -> SPL_ERR_STATE, nothing written (any SKU with DBANK). Sequence:
     * FLASH_ConfigAccelerator(p, i, 0) -> operations -> FLASH_ConfigAccelerator(p, 0, 0)
     * -> FLASH_ResetCaches -> FLASH_ConfigAccelerator(p, i, 1).
     * ERRATA ES0431 2.2.2 / ES0430 2.2.3 / ES0523 2.2.2: after a reset or power-down during
     * program/erase, FLASH_ECCR may be corrupted when that location is read: erase the page(s)
     * again before trusting ECC information (FLASH_GetEcc). */
    /* Start a page erase. bank is FLASH_BANK_1/FLASH_BANK_2.
     * Caller duty (CCL-2 FL-07b): one page per call, no range service; for a range the caller
     * takes page_size from FLASH_GetGeometry, first page = start / page_size, last page =
     * (start + length - 1) / page_size (rounds up), and erases first..last inclusive. */
    /* Caller: the page holds no code, vector table or data still needed; clear pending error flags
     * first (else ERR_HW); then FLASH_PollOperation until OK. */
    /* Level: PARAM[F] STATE[r] UNSUPPORTED[r] HW[r] BUSY[r] */
    SPL_Status FLASH_ErasePageStart(uint32_t bank, uint32_t page);
    /* Start one 64-bit program at an 8-byte aligned main-memory address.
     * The double word must be erased (both words 0xFFFFFFFF) unless data is
     * all zero: otherwise SPL_ERR_STATE before any write (v0.13.3, would give
     * PROGERR, RM0440 3.3.7). The check reads the target double word. */
    /* Caller: clear pending error flags first (else ERR_HW); FLASH_PollOperation until OK before
     * the next start, then FLASH_EndOperation; device layer (record layout, verify-after-write) is
     * the caller's. */
    /* Level: PARAM[F] STATE[r] UNSUPPORTED[r] HW[r] BUSY[r] */
    SPL_Status FLASH_ProgramDoubleWordStart(uint32_t address, uint64_t data);
    /* BUSY while BSY=1, ERR_HW if error flags are pending, else OK.
     * eop (optional, may be NULL) receives SR.EOP (set only if EOPIE=1). */
    /* Caller: bound the BUSY retries; on ERR_HW read FLASH_GetFlags, then FLASH_ClearFlags before a
     * new operation. */
    /* Level: HW[r] BUSY[r] */
    SPL_Status FLASH_PollOperation(uint8_t *eop);
    /* Clear PG/PER/MER/FSTPG/PNB/BKER after the last operation. */
    /* Caller: call after the last FLASH_PollOperation OK, before FLASH_Lock. */
    /* Level: STATE[r] BUSY[r] */
    SPL_Status FLASH_EndOperation(void);

    /* Raw SR (BSY and all flags). */
    /* Caller: none beyond the file-header duties. */
    /* Level: PARAM[F] */
    SPL_Status FLASH_GetFlags(uint32_t *flags);
    /* W1C clear of a non-empty subset of FLASH_SR_CLEARABLE. */
    /* Caller: diagnose the error flags (FLASH_GetFlags) before clearing them. */
    /* Level: PARAM[F] */
    SPL_Status FLASH_ClearFlags(uint32_t mask);

    /* v0.13.4 (issue #12): ECC diagnosis. A double error (ECCD) raises the NMI; the CMSIS
     * startup NMI_Handler is a weak endless loop, so the application defines NMI_Handler and
     * calls these from it (read-only / single write, no wait). ECCR latches only the first
     * error: read it, then clear it.
     * FLASH_GetEcc: one ECCR read, no write. FLASH_ClearEcc: writes 1 to every ECC flag
     * (ECCD, ECCC and on DBANK SKUs ECCD2, ECCC2), ECCCIE kept, reserved bits 0. */
    /* Caller: call it from the application NMI_Handler (the startup default loops); after an
     * interrupted program / erase erase the page(s) again before trusting ECCR (errata above). */
    /* Level: PARAM[F] */
    SPL_Status FLASH_GetEcc(FLASH_EccTypeDef *ecc);
    /* Caller: read FLASH_GetEcc first: the clear loses the latched address. */
    /* Level: - */
    SPL_Status FLASH_ClearEcc(void);

#ifdef __cplusplus
}
#endif
#endif
