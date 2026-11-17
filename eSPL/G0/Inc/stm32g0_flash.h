/* SPDX-License-Identifier: MIT */
#ifndef STM32G0_FLASH_H
#define STM32G0_FLASH_H
#include <stdint.h>
#include "stm32g0xx.h"
#include "stm32g0_status.h"

/* Embedded flash interface (RM0444 Rev 6 section 3, Docs/v0.2_FLASH_CONTRACT.md).
 * Ported from the STM32G4 SPL FLASH driver and adapted: G0 has CFGBSY (writing FLASH_CR while it
 * is set raises a HardFault), BSY1/BSY2, no data cache, 2 KB pages, a size-dependent bank-2 base
 * and ECC per 64 bits. No static state; program/erase are start + FLASH_PollOperation.
 * v0.11 adds mass erase, fast programming, option bytes, WRP / PCROP / securable area and OTP
 * (irreversible ones behind SPL_ENABLE_IRREVERSIBLE + key, see below).
 * Out of scope: ECC interrupt handling (ECCD raises an NMI: application handler; diagnosis via
 * FLASH_GetEcc).
 * Caller duties (all APIs): single owner, not reentrant: serialise FLASH calls with ISRs and other
 * contexts (FLASH_PollOperation and FLASH_GetFlags may be read from an ISR); program, erase and
 * option operations are start + poll: the wait (FLASH_PollOperation) and its timeout belong to
 * the caller; FLASH_Unlock before and FLASH_Lock after a write sequence. */

#ifdef __cplusplus
extern "C"
{
#endif

/* FLASH_KEYR unlock keys (RM0444 3.7.2). Not provided by CMSIS. */
#define FLASH_KEY1 0x45670123UL
#define FLASH_KEY2 0xCDEF89ABUL

/* Physical bank selectors for FLASH_ErasePageStart (erase is not affected by nSWAP_BANK). */
#define FLASH_BANK_1 1U
#define FLASH_BANK_2 2U

#define FLASH_PAGE_BYTES 2048U

/* v0.8 SKU profile: RDERR (PCROP read error) is absent on G030 / G0B0, BSY2 exists only on the
 * dual-bank devices (G0B0 / G0B1 / G0C1). */
#if defined(FLASH_SR_RDERR)
#define FLASH_SR_RDERR_BIT FLASH_SR_RDERR
#else
#define FLASH_SR_RDERR_BIT 0U
#endif
#if defined(FLASH_SR_BSY2)
#define FLASH_SR_BSY2_BIT FLASH_SR_BSY2
#else
#define FLASH_SR_BSY2_BIT 0U
#endif
/* SR error flags that block a new operation (RM0444 3.3.7/3.3.8 step 2) plus RDERR. */
#define FLASH_SR_ERRORS                                                                            \
    (FLASH_SR_OPERR | FLASH_SR_PROGERR | FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_SIZERR |     \
     FLASH_SR_PGSERR | FLASH_SR_MISERR | FLASH_SR_FASTERR | FLASH_SR_RDERR_BIT)
/* All write-1-to-clear SR flags accepted by FLASH_ClearFlags. */
#define FLASH_SR_CLEARABLE (FLASH_SR_EOP | FLASH_SR_ERRORS | FLASH_SR_OPTVERR)
/* Any busy indication: CR must not be written while one is set. */
#define FLASH_SR_ANYBUSY (FLASH_SR_BSY1 | FLASH_SR_BSY2_BIT | FLASH_SR_CFGBSY)

typedef enum
{
    FLASH_RANGE1 = 0, /* VCORE range 1: 0 WS <= 24 MHz, 1 WS <= 48 MHz, 2 WS <= 64 MHz */
    FLASH_RANGE2 = 1  /* VCORE range 2: 0 WS <= 8 MHz, 1 WS <= 16 MHz */
} FLASH_RangeTypeDef;

typedef struct
{
    uint32_t size_kb;        /* flash size data register (FLASHSIZE_BASE) */
    uint32_t banks;          /* 1 or 2 */
    uint32_t pages_per_bank; /* valid page numbers 0 .. pages_per_bank - 1 */
    uint32_t bank2_base;     /* logical start of the upper half when banks = 2, else 0 */
    uint32_t swapped;        /* 1: nSWAP_BANK = 0 in dual-bank mode (bank 2 at FLASH_BASE) */
} FLASH_GeometryTypeDef;

/* Wait states for hclk_hz in the range (RM0444 Table 13). Above the range maximum -> PARAM. */
/* Caller: pass the voltage range actually set in PWR_CR1.VOS. */
/* Level: PARAM[F,r] */
SPL_Status FLASH_CalcLatency(uint32_t hclk_hz, FLASH_RangeTypeDef range, uint32_t *latency);
/* ACR.LATENCY (0..2) with read-back check (mismatch -> ERR_HW). */
/* Caller: raise the latency before HCLK increases and lower it only after HCLK has decreased
 * (RM0444 3.3.4); the value is not compared with the current HCLK. */
/* Level: PARAM[F] HW[r] */
SPL_Status FLASH_SetLatency(uint32_t latency);
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status FLASH_GetLatency(uint32_t *latency);
/* Prefetch and instruction cache enables (0 or 1).
 * ERRATA ES0548 2.2.10: in dual-bank mode (512 KB parts always, 256 KB with DUAL_BANK = 1) the
 * prefetch can fetch wrong code on a branch or call across banks; no workaround. prefetch = 1 in
 * dual-bank mode -> SPL_ERR_STATE, nothing written, unless code_in_one_bank = 1 (the caller
 * guarantees no execution transfer between the banks). Single-bank: code_in_one_bank ignored. */
/* Caller: code_in_one_bank = 1 is the caller's guarantee that no branch or call crosses the
 * banks (not checkable). */
/* Level: PARAM[F] STATE[F] UNSUPPORTED[r] */
SPL_Status FLASH_ConfigAccelerator(uint8_t prefetch, uint8_t icache, uint8_t code_in_one_bank);
/* Instruction cache reset; ICEN must be 0 (RM0444 3.7.1) else STATE. */
/* Caller: none beyond the file-header duties. */
/* Level: STATE[F] */
SPL_Status FLASH_ResetICache(void);
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] UNSUPPORTED[r] */
SPL_Status FLASH_GetGeometry(FLASH_GeometryTypeDef *geometry);
/* Raw FLASH_OPTR (read-only access). */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status FLASH_GetOptionRegister(uint32_t *optr);

/* KEY1/KEY2. Already unlocked -> OK, nothing written. A wrong sequence locks FLASH_CR until reset
 * and raises a HardFault (RM0444 3.3.6). Busy -> BUSY (no KEYR write during an operation). */
/* Caller: none beyond the file-header duties. */
/* Level: HW[r] BUSY[r] */
SPL_Status FLASH_Unlock(void);
/* Clears PG/PER/MER/FSTPG/PNB/BKER and sets LOCK; busy -> BUSY. */
/* Caller: none beyond the file-header duties. */
/* Level: BUSY[r] */
SPL_Status FLASH_Lock(void);
/* Page erase on a physical bank. Locked -> STATE; busy -> BUSY; pending error flags -> ERR_HW
 * (diagnose, then FLASH_ClearFlags). The CPU stalls on reads of the bank being erased. */
/* Caller: the page holds no code, vector table or data still needed; FLASH_PollOperation until
 * OK, then FLASH_EndOperation; code fetched from the busy bank stalls (interrupt latency,
 * watchdog). */
/* Level: PARAM[F] STATE[F,r] UNSUPPORTED[r] HW[r] BUSY[r] */
SPL_Status FLASH_ErasePageStart(uint32_t bank, uint32_t page);
/* One 64-bit standard program at an 8-byte aligned logical main-memory address. The double word
 * must be erased, or data must be 0 (RM0444 3.3.8). v0.3 (backport of G4 v0.13.3 FL-21): data != 0
 * and the target does not read 0xFFFF FFFF FFFF FFFF -> SPL_ERR_STATE before any register write
 * (instead of a PROGERR left in FLASH_SR). The check reads the target double word; an ECC double
 * error there raises the NMI as any other read would.
 * ERRATA ES0548 2.2.3: overwriting a double word programmed with all ones by all zeros fails
 * (PROGERR). data = 0xFFFF FFFF FFFF FFFF is therefore rejected with SPL_ERR_PARAM: it would
 * leave a location that looks erased but can no longer be zeroed. */
/* Caller: FLASH_PollOperation until OK, then FLASH_EndOperation; device layer (record layout,
 * read-back verify): caller. */
/* Level: PARAM[F] STATE[F] UNSUPPORTED[r] HW[r] BUSY[r] */
SPL_Status FLASH_ProgramDoubleWordStart(uint32_t address, uint64_t data);
/* BUSY while any busy flag is set, ERR_HW when error flags are pending, else OK. eop (optional)
 * receives SR.EOP (set only with EOPIE = 1). */
/* Caller: on ERR_HW read FLASH_GetFlags, then FLASH_ClearFlags before a new operation. */
/* Level: HW[r] BUSY[r] */
SPL_Status FLASH_PollOperation(uint8_t *eop);
/* Clears PG/PER/MER/FSTPG/PNB/BKER after the last operation; locked -> STATE, busy -> BUSY. */
/* Caller: none beyond the file-header duties. */
/* Level: STATE[F] BUSY[r] */
SPL_Status FLASH_EndOperation(void);
/* v0.4 (G4 v0.13.4 #12): ECC diagnosis for the application's NMI_Handler (the CMSIS startup
 * NMI_Handler is a weak endless loop). G0 difference: bank 1 reports in FLASH_ECCR, bank 2 in
 * FLASH_ECC2R (RM0444 3.7.6 / 3.7.7). FLASH_GetEcc: reads ECCR, and ECC2R when ECCR holds no
 * event (each register latches only its first error); no write. FLASH_ClearEcc: writes 1 to ECCD
 * and ECCC in both registers, ECCCIE kept, reserved bits 0. A single error on an erased (virgin)
 * double word is corrected and reported as well (RM0444 3.3.3 note). */
typedef struct
{
    uint32_t address;     /* ADDR_ECC: double-word offset in main flash (RM0444 3.7.6) */
    uint8_t detected;     /* ECCD: double error, raises the NMI */
    uint8_t corrected;    /* ECCC: single error corrected */
    uint8_t system_flash; /* SYSF_ECC: error in system flash */
    uint8_t bank;         /* FLASH_BANK_1 (ECCR) or FLASH_BANK_2 (ECC2R) */
} FLASH_EccTypeDef;
/* Caller: call it from the application NMI_Handler (the startup default loops), then
 * FLASH_ClearEcc. */
/* Level: PARAM[F] */
SPL_Status FLASH_GetEcc(FLASH_EccTypeDef *ecc);
/* Caller: read FLASH_GetEcc first: the clear loses the latched address. */
/* Level: - */
SPL_Status FLASH_ClearEcc(void);
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status FLASH_GetFlags(uint32_t *flags);
/* Write-1-to-clear of a non-empty subset of FLASH_SR_CLEARABLE. */
/* Caller: diagnose the error flags (FLASH_GetFlags) before clearing them. */
/* Level: PARAM[F] */
SPL_Status FLASH_ClearFlags(uint32_t mask);

/* ---- v0.11: mass erase, fast programming, option bytes, protections, OTP
 * (Docs/v0.11_TIME_ANALOG_SYSTEM.md) ----
 * Irreversible operations (RDP level 2, PCROP, securable area / BOOT_LOCK, OTP program) are
 * compiled only with SPL_ENABLE_IRREVERSIBLE = 1 and then also need their own key; with the flag
 * at 0 they return SPL_ERR_UNSUPPORTED and write nothing. Destructive but reversible operations
 * (mass erase, RDP 1 -> 0 regression, option reload) need a key in every build. */
#ifndef SPL_ENABLE_IRREVERSIBLE
#define SPL_ENABLE_IRREVERSIBLE 0
#endif

/* Confirmation keys: arbitrary constants that make an accidental call fail with PARAM. */
#define FLASH_KEY_MASS_ERASE 0x4D455241UL     /* "MERA" */
#define FLASH_KEY_RDP_REGRESSION 0x52445030UL /* "RDP0" */
#define FLASH_KEY_OBL_LAUNCH 0x4F424C4CUL     /* "OBLL" */
#define FLASH_KEY_RDP2 0x52445032UL           /* "RDP2" (irreversible) */
#define FLASH_KEY_PCROP 0x5043524FUL          /* "PCRO" (irreversible) */
#define FLASH_KEY_SECURABLE 0x53454341UL      /* "SECA" (irreversible) */
#define FLASH_KEY_OTP 0x4F545021UL            /* "OTP!" (irreversible) */

/* Bank masks for FLASH_MassEraseStart / FLASH_SecureAreaProtect. */
#define FLASH_MASS_BANK1 1U
#define FLASH_MASS_BANK2 2U

/* WRP / PCROP area selectors (A and B per bank). */
#define FLASH_AREA_1A 0U
#define FLASH_AREA_1B 1U
#define FLASH_AREA_2A 2U
#define FLASH_AREA_2B 3U

/* OTP area (RM0444 Table 6): 1 KB, double-word programming, never erased. Not in CMSIS. */
#define FLASH_OTP_BASE 0x1FFF7000UL
#define FLASH_OTP_BYTES 1024U

/* User option bits accepted by FLASH_OptionSetUser (all OPTR fields except RDP; per SKU). */
#if defined(FLASH_OPTR_BOR_EN)
#define FLASH_OPTR_USER_BOR (FLASH_OPTR_BOR_EN | FLASH_OPTR_BORR_LEV | FLASH_OPTR_BORF_LEV)
#else
#define FLASH_OPTR_USER_BOR 0U
#endif
#if defined(FLASH_OPTR_nRST_SHDW)
#define FLASH_OPTR_USER_SHDW FLASH_OPTR_nRST_SHDW
#else
#define FLASH_OPTR_USER_SHDW 0U
#endif
#if defined(FLASH_OPTR_DUAL_BANK)
#define FLASH_OPTR_USER_BANK (FLASH_OPTR_nSWAP_BANK | FLASH_OPTR_DUAL_BANK)
#else
#define FLASH_OPTR_USER_BANK 0U
#endif
#if defined(FLASH_OPTR_IRHEN)
#define FLASH_OPTR_USER_IRH FLASH_OPTR_IRHEN
#else
#define FLASH_OPTR_USER_IRH 0U
#endif
#if defined(FLASH_OPTR_NRST_MODE)
#define FLASH_OPTR_USER_NRST FLASH_OPTR_NRST_MODE
#else
#define FLASH_OPTR_USER_NRST 0U /* x0 parts: no PF2-NRST mode field */
#endif
#define FLASH_OPTR_USER_BITS                                                                       \
    (FLASH_OPTR_USER_BOR | FLASH_OPTR_nRST_STOP | FLASH_OPTR_nRST_STDBY | FLASH_OPTR_USER_SHDW |   \
     FLASH_OPTR_IWDG_SW | FLASH_OPTR_IWDG_STOP | FLASH_OPTR_IWDG_STDBY | FLASH_OPTR_WWDG_SW |      \
     FLASH_OPTR_USER_BANK | FLASH_OPTR_RAM_PARITY_CHECK | FLASH_OPTR_nBOOT_SEL |                   \
     FLASH_OPTR_nBOOT1 | FLASH_OPTR_nBOOT0 | FLASH_OPTR_USER_NRST | FLASH_OPTR_USER_IRH)

/* Mass erase of bank 1 and / or bank 2 (bank_mask FLASH_MASS_BANKx), key FLASH_KEY_MASS_ERASE.
 * Wrong key / mask -> PARAM; bank 2 without bank 2 -> STATE (256 KB, DUAL_BANK = 0) or
 * UNSUPPORTED; then the FLASH_ErasePageStart preconditions. An active WRP or PCROP area makes the
 * hardware abort with WRPERR (FLASH_PollOperation -> ERR_HW). Code must not run from the bank. */
/* Caller: no code, vector table or constant used during the erase may live in an erased bank
 * (not checked); FLASH_PollOperation until OK, then FLASH_EndOperation. */
/* Level: PARAM[F,r] STATE[F,r] UNSUPPORTED[r] HW[r] BUSY[r] */
SPL_Status FLASH_MassEraseStart(uint32_t bank_mask, uint32_t key);
/* Fast programming of one 256-byte row (64 words, 32 double words) at a 256-byte aligned
 * main-memory address (RM0444 3.3.8). Not erased row -> STATE before FSTPG is set; an all-ones
 * double word -> PARAM (ES0548 2.2.3). Caller obligations (not checkable by the driver):
 * HCLK >= 8 MHz; no flash read during the row (code and constants from RAM or the other bank, a
 * read silently aborts the row); interrupts masked (more than ~20 us between two double words
 * sets MISSERR); FLASH_PollOperation, then FLASH_EndOperation clears FSTPG. */
/* Caller: HCLK >= 8 MHz; no flash read during the row (code and data in RAM or the other bank);
 * interrupts masked; then FLASH_PollOperation and FLASH_EndOperation. */
/* Level: PARAM[F] STATE[F] UNSUPPORTED[r] HW[r] BUSY[r] */
SPL_Status FLASH_FastProgramRowStart(uint32_t address, const uint32_t *words);

/* Option bytes (RM0444 3.4.2). Sequence: FLASH_Unlock, FLASH_OptionUnlock, one or more setters,
 * FLASH_OptionStart, FLASH_PollOperation until OK, FLASH_OptionLaunch (system reset).
 * The setters write the option registers only (no flash change until OPTSTRT); they need
 * OPTLOCK = 0 (else STATE) and no running operation (else BUSY). The option registers read the
 * values loaded at the last OBL plus any setter writes since; setters modify only their fields.
 * ERRATA ES0548 2.2.9: an option byte mismatch (for example a reset or supply drop during the
 * OPTSTRT sequence) locks the device (RDP level 1, BOOT_LOCK on early revisions). Caller
 * obligation: stable supply, no watchdog or other reset pending during the sequence. */
/* FLASH_CR must be unlocked (else STATE); already unlocked -> OK; busy -> BUSY; OPTLOCK still
 * set after the keys -> ERR_HW (a wrong sequence locks until reset, RM0444 3.4.2). */
/* Caller: follow the option byte sequence above; FLASH_OptionLock when the sequence is
 * abandoned. */
/* Level: STATE[F] HW[r] BUSY[r] */
SPL_Status FLASH_OptionUnlock(void);
/* Caller: none beyond the file-header duties. */
/* Level: BUSY[r] */
SPL_Status FLASH_OptionLock(void);
/* OPTR fields in mask (subset of FLASH_OPTR_USER_BITS, never RDP) set to value (value within
 * mask). NRST_MODE = 00 (reserved) -> PARAM.
 * ERRATA ES0487 2.2.3 (G030 / G031 / G041 / G070 / G071 / G081, same number in ES0486 / ES0488 /
 * ES0468 / ES0418 / ES0412): RDP level 1 with nBOOT_SEL = 0 (boot from the PA14-BOOT0 pin) cannot
 * boot the user code. The resulting OPTR image with both -> STATE, in FLASH_OptionSetUser and in
 * FLASH_OptionSetRdp: set nBOOT_SEL = 1 (boot by nBOOT0 option bit) first. */
/* Caller: effective only after FLASH_OptionStart + FLASH_OptionLaunch; boot, NRST and watchdog
 * fields change board behaviour: board review is the caller's. */
/* Level: PARAM[F] STATE[F] BUSY[r] */
SPL_Status FLASH_OptionSetUser(uint32_t mask, uint32_t value);
/* RDP level 0 / 1 / 2. Level 0 while OPTR holds level 1 is a regression (mass erase, PCROP area
 * erased when PCROP_RDP = 1) and needs FLASH_KEY_RDP_REGRESSION. Level 2 is permanent: only with
 * SPL_ENABLE_IRREVERSIBLE and FLASH_KEY_RDP2, else UNSUPPORTED. */
/* Caller: effective after FLASH_OptionStart + FLASH_OptionLaunch; level 1 blocks debugger access
 * to flash, the 1 -> 0 regression erases the flash. */
/* Level: PARAM[r] STATE[F] UNSUPPORTED[r] BUSY[r] */
SPL_Status FLASH_OptionSetRdp(uint32_t level, uint32_t key);
/* WRP area (FLASH_AREA_xx): pages start_page .. end_page of that bank; start_page > end_page
 * disables the area. Page numbers above the field width -> PARAM. */
/* Caller: effective after FLASH_OptionStart + FLASH_OptionLaunch; erase / program of a protected
 * page then fails with WRPERR. */
/* Level: PARAM[F] STATE[F,r] UNSUPPORTED[r] BUSY[r] */
SPL_Status FLASH_OptionSetWrp(uint32_t area, uint32_t start_page, uint32_t end_page);
/* PCROP area in 512-byte subpages (irreversible: removed only by an RDP 1 -> 0 regression).
 * Flag + FLASH_KEY_PCROP; start == end (whole bank, RM0444 3.5.2) -> PARAM.
 * ERRATA ES0548 2.2.5: PCROP-protected reads return the previous bus value; RDP level 0 in OPTR
 * -> STATE (set level 1 first), PCROP_RDP is always set. No PCROP on G030 / G050 / G070 / G0B0:
 * UNSUPPORTED. */
/* Caller: irreversible except by an RDP 1 -> 0 regression (full erase): verify the subpages
 * first; PCROP code must not read its own area as data. */
/* Level: PARAM[F,r] STATE[F,r] UNSUPPORTED[r] BUSY[r] */
SPL_Status FLASH_OptionSetPcrop(uint32_t area, uint32_t start_subpage, uint32_t end_subpage,
                                uint32_t key);
/* Securable area size in pages from the bank start (sec_size2: bank 2, dual-bank SKUs only) and
 * BOOT_LOCK. Flag + FLASH_KEY_SECURABLE. Caller notes: on G071 / G081 only in RDP level 0; on
 * G0B1 / G0C1 only with SEC_PROT = SEC_PROT2 = 0; move the vector table out of page 0 first. */
/* Caller: G071 / G081: RDP level 0 only; G0B1 / G0C1: SEC_PROT = SEC_PROT2 = 0 only; move the
 * vector table out of page 0 first. */
/* Level: PARAM[F,r] STATE[F,r] UNSUPPORTED[r] BUSY[r] */
SPL_Status FLASH_OptionSetSecurable(uint32_t sec_size, uint32_t sec_size2, uint8_t boot_lock,
                                    uint32_t key);
/* Closes the securable area of the banks in bank_mask until the next system reset (SEC_PROT /
 * SEC_PROT2). No securable area on the SKU -> UNSUPPORTED; busy -> BUSY. */
/* Caller: the securable area stays inaccessible until the next system reset. */
/* Level: PARAM[F] UNSUPPORTED[r] BUSY[r] */
SPL_Status FLASH_SecureAreaProtect(uint32_t bank_mask);
/* OPTSTRT. OPTLOCK or LOCK set -> STATE; busy -> BUSY; pending error flags -> ERR_HW. */
/* Caller: stable supply, no watchdog or other reset during the sequence (ERRATA ES0548 2.2.9);
 * FLASH_PollOperation until OK, then FLASH_OptionLaunch. */
/* Level: STATE[F] HW[r] BUSY[r] */
SPL_Status FLASH_OptionStart(void);
/* OBL_LAUNCH with FLASH_KEY_OBL_LAUNCH: reloads the option bytes and resets the device; on
 * hardware the call does not return. */
/* Caller: save all state first: the device resets. */
/* Level: PARAM[r] STATE[F] BUSY[r] */
SPL_Status FLASH_OptionLaunch(uint32_t key);
/* One OTP double word (FLASH_OTP_BASE .. + 0x3F8, 8-byte aligned). Flag + FLASH_KEY_OTP; written
 * double word -> STATE; all-ones -> PARAM; then the FLASH_ProgramDoubleWordStart preconditions. */
/* Caller: irreversible (OTP is never erased): verify address and data first; FLASH_PollOperation
 * until OK, then FLASH_EndOperation. */
/* Level: PARAM[F,r] STATE[F] UNSUPPORTED[r] HW[r] BUSY[r] */
SPL_Status FLASH_OtpProgramStart(uint32_t address, uint64_t data, uint32_t key);

#ifdef __cplusplus
}
#endif
#endif /* STM32G0_FLASH_H */
