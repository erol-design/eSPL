/* SPDX-License-Identifier: MIT */
#ifndef STM32G4_FMC_H
#define STM32G4_FMC_H
#include <stdint.h>
#include "stm32g4xx.h"
#include "stm32g4_status.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* Flexible memory controller driver for STM32G4 (RM0440 Rev 9 section 19).
 * Scope: NOR/PSRAM/SRAM controller (bank 1, four sub-banks NE1..NE4) and NAND controller
 * (bank 3). There is no SDRAM controller on STM32G4. FMC exists only where CMSIS defines
 * FMC_Bank1_R (G473/G474/G483/G484); on every other SKU each API returns
 * SPL_ERR_UNSUPPORTED without any access. RM0440 Rev 9 has no global FMC enable bit in
 * FMC_BCR1 (bits 31:24 are reserved), unlike some other STM32 families.
 * Usage: FMC_NORSRAM_Init (sub-bank disabled), optional FMC_PSRAM_SetCounter, then
 * FMC_NORSRAM_Enable; NAND: FMC_NAND_Init, FMC_NAND_ITConfig, FMC_NAND_Enable, per page
 * FMC_NAND_ECCStart -> data transfer -> FMC_NAND_GetECC.
 * Common rules: no dynamic memory, no static state; no function waits (WOULD_BLOCK is returned
 * instead); parameters are validated before any write; output parameters are written only on
 * OK. The driver never accesses the external memory windows (0x6000 0000..0x8FFF FFFF); it
 * only programs the FMC registers.
 * Caller duties (all APIs; not done by the driver):
 *  - Clock: RCC_AHB3ENR.FMCEN (and the FMC reset if needed).
 *  - Pins: GPIO alternate functions of the FMC pins.
 *  - MPU attributes of the bank windows (the NAND bank must be Device, RM0440 19.5); NVIC
 *    (FMC_IRQn).
 *  - Serialize all FMC calls (single owner or critical section): every write is a
 *    read-modify-write of shared registers.
 *  - Device layer: every access through the memory windows (commands, addresses, data, NAND
 *    R/B waiting) and the external memory timing calculation.
 * Errata: ES0430 Rev 10 (G471/473/474/483/484) section 2.5; contract section
 * "Errata". ES0431 (G431/441) has no FMC items (no FMC on those parts).
 * ERRATA ES0430 2.5.1 (CALLER): a synchronous burst read is followed by two dummy read
 * accesses; read-sensitive synchronous devices (FIFOs) see them.
 * Contract: Docs/v0.12_FMC_CONTRACT.md. */

/* NOR/PSRAM sub-bank (chip select NEx). */
#define FMC_NORSRAM_BANK1 1U
#define FMC_NORSRAM_BANK2 2U
#define FMC_NORSRAM_BANK3 3U
#define FMC_NORSRAM_BANK4 4U

/* FMC_BCRx.MTYP (11 is reserved). */
#define FMC_MEMTYPE_SRAM 0U  /* SRAM / FRAM */
#define FMC_MEMTYPE_PSRAM 1U /* PSRAM (CellularRAM) / FRAM */
#define FMC_MEMTYPE_NOR 2U   /* NOR flash / OneNAND */

/* FMC_BCRx.MWID and FMC_PCR.PWID (10 and 11 are reserved). */
#define FMC_WIDTH_8 0U
#define FMC_WIDTH_16 1U

/* FMC_BCRx.CPSIZE (101..111 are reserved). */
#define FMC_CRAM_PAGE_NONE 0U
#define FMC_CRAM_PAGE_128 1U
#define FMC_CRAM_PAGE_256 2U
#define FMC_CRAM_PAGE_512 3U
#define FMC_CRAM_PAGE_1024 4U

/* FMC_BTRx/BWTRx.ACCMOD (used only with EXTMOD = 1). */
#define FMC_ACCESS_MODE_A 0U
#define FMC_ACCESS_MODE_B 1U
#define FMC_ACCESS_MODE_C 2U
#define FMC_ACCESS_MODE_D 3U

/* FMC_PCR.ECCPS. */
#define FMC_ECC_PAGE_256 0U
#define FMC_ECC_PAGE_512 1U
#define FMC_ECC_PAGE_1024 2U
#define FMC_ECC_PAGE_2048 3U
#define FMC_ECC_PAGE_4096 4U
#define FMC_ECC_PAGE_8192 5U

/* FMC_PSRAM_SetCounter bank mask: bit n enables the counter of sub-bank n+1. */
#define FMC_PSRAM_CNT_BANK1 0x1U
#define FMC_PSRAM_CNT_BANK2 0x2U
#define FMC_PSRAM_CNT_BANK3 0x4U
#define FMC_PSRAM_CNT_BANK4 0x8U
#define FMC_PSRAM_CNT_ALL 0xFU

/* FMC_SR bits (RM0440 19.7.7). Same values as the CMSIS FMC_SR_* masks;
 * literal values only where CMSIS has no FMC (code then compiles but every
 * API returns UNSUPPORTED). */
#if defined(FMC_SR_IRS)
#define FMC_NAND_FLAG_IRS FMC_SR_IRS
#define FMC_NAND_FLAG_ILS FMC_SR_ILS
#define FMC_NAND_FLAG_IFS FMC_SR_IFS
#define FMC_NAND_FLAG_FEMPT FMC_SR_FEMPT
#define FMC_NAND_IT_RISING FMC_SR_IREN
#define FMC_NAND_IT_LEVEL FMC_SR_ILEN
#define FMC_NAND_IT_FALLING FMC_SR_IFEN
#else
#define FMC_NAND_FLAG_IRS 0x01U
#define FMC_NAND_FLAG_ILS 0x02U
#define FMC_NAND_FLAG_IFS 0x04U
#define FMC_NAND_FLAG_FEMPT 0x40U
#define FMC_NAND_IT_RISING 0x08U
#define FMC_NAND_IT_LEVEL 0x10U
#define FMC_NAND_IT_FALLING 0x20U
#endif
#define FMC_NAND_FLAGS_ALL (FMC_NAND_FLAG_IRS | FMC_NAND_FLAG_ILS | FMC_NAND_FLAG_IFS)
#define FMC_NAND_IT_ALL (FMC_NAND_IT_RISING | FMC_NAND_IT_LEVEL | FMC_NAND_IT_FALLING)

/* FMC_GetBankAddress regions (RM0440 19.5, Figure 53, Table 124). */
#define FMC_REGION_NORSRAM1 0U       /* 0x6000 0000, 64 MB, NE1 */
#define FMC_REGION_NORSRAM2 1U       /* 0x6400 0000, 64 MB, NE2 */
#define FMC_REGION_NORSRAM3 2U       /* 0x6800 0000, 64 MB, NE3 */
#define FMC_REGION_NORSRAM4 3U       /* 0x6C00 0000, 64 MB, NE4 */
#define FMC_REGION_NAND_COMMON 4U    /* 0x8000 0000, FMC_PMEM timings */
#define FMC_REGION_NAND_ATTRIBUTE 5U /* 0x8800 0000, FMC_PATT timings */
/* NAND sections inside the common/attribute space (RM0440 Table 125). */
#define FMC_NAND_DATA_OFFSET 0x00000000UL /* HADDR[17:16] = 00 */
#define FMC_NAND_CMD_OFFSET 0x00010000UL  /* HADDR[17:16] = 01 (CLE) */
#define FMC_NAND_ADDR_OFFSET 0x00020000UL /* HADDR[17:16] = 1x (ALE) */

    /* FMC_BCRx control fields. Every field is a register code, not a time.
     * Booleans are 0/1; anything else is SPL_ERR_PARAM. */
    typedef struct
    {
        uint8_t mem_type;           /* FMC_MEMTYPE_* (MTYP) */
        uint8_t data_width;         /* FMC_WIDTH_* (MWID) */
        uint8_t mux;                /* MUXEN: NOR/PSRAM only */
        uint8_t flash_access;       /* FACCEN */
        uint8_t burst;              /* BURSTEN: synchronous read */
        uint8_t wait_polarity;      /* WAITPOL: 1 = NWAIT active high */
        uint8_t wait_config;        /* WAITCFG: 1 = during wait state; not PSRAM */
        uint8_t write_enable;       /* WREN */
        uint8_t wait_enable;        /* WAITEN (synchronous) */
        uint8_t extended_mode;      /* EXTMOD: BWTRx used for writes */
        uint8_t async_wait;         /* ASYNCWAIT */
        uint8_t cram_page;          /* FMC_CRAM_PAGE_* (CPSIZE) */
        uint8_t write_burst;        /* CBURSTRW: synchronous write */
        uint8_t continuous_clock;   /* CCLKEN: sub-bank 1 only */
        uint8_t write_fifo_disable; /* WFDIS: sub-bank 1 only (global) */
        uint8_t nbl_setup;          /* NBLSET 0..3 HCLK */
    } FMC_NORSRAM_InitTypeDef;

    /* FMC_BTRx (read, or read+write without EXTMOD) / FMC_BWTRx (write) codes. */
    typedef struct
    {
        uint8_t addr_setup;     /* ADDSET 0..15 (>= 1 muxed, mode D, PSRAM mode 1) */
        uint8_t addr_hold;      /* ADDHLD 1..15 (0 is reserved) */
        uint8_t data_setup;     /* DATAST 1..255 (0 is reserved) */
        uint8_t bus_turnaround; /* BUSTURN 0..15 */
        uint8_t clk_div;        /* CLKDIV 0..15, >= 1 when synchronous; BWTR: 0 */
        uint8_t data_latency;   /* DATLAT 0..15, PSRAM: 0; BWTR: 0 */
        uint8_t access_mode;    /* FMC_ACCESS_MODE_* (ACCMOD) */
        uint8_t data_hold;      /* DATAHLD 0..3 */
    } FMC_NORSRAM_TimingTypeDef;

    /* FMC_PCR fields (PTYP is always NAND = 1). */
    typedef struct
    {
        uint8_t wait_feature;  /* PWAITEN */
        uint8_t data_width;    /* FMC_WIDTH_* (PWID) */
        uint8_t ecc_page_size; /* FMC_ECC_PAGE_* (ECCPS) */
        uint8_t tclr;          /* TCLR 0..15 (t_clr = (TCLR + SET + 2) HCLK) */
        uint8_t tar;           /* TAR 0..15 (t_ar = (TAR + SET + 2) HCLK) */
    } FMC_NAND_InitTypeDef;

    /* FMC_PMEM / FMC_PATT register codes (RM0440 19.7.7). */
    typedef struct
    {
        uint8_t setup; /* MEMSET/ATTSET 0..254 (0xFF reserved) */
        uint8_t wait;  /* MEMWAIT/ATTWAIT 1..254 */
        uint8_t hold;  /* MEMHOLD/ATTHOLD 1..254 */
        uint8_t hiz;   /* MEMHIZ/ATTHIZ 0..254 */
    } FMC_NAND_TimingTypeDef;

    /* Programs FMC_BTRx, FMC_BWTRx, then FMC_BCRx with MBKEN = 0.
     * write_timing: required (non-NULL) when init->extended_mode = 1; must be
     * NULL otherwise (BWTRx then gets its reset field values).
     * STATE: the sub-bank is enabled (MBKEN = 1; note BCR1 resets with
     * MBKEN = 1), or sub-bank 1 changes CCLKEN/WFDIS (global bits) while
     * another sub-bank or the NAND bank is enabled. Reserved bits are kept.
     * ERRATA ES0430 2.5.3: PSRAM with write_burst = 1 needs the write FIFO
     * disabled: bank 1 PARAM unless write_fifo_disable = 1; bank 2..4 STATE
     * unless BCR1.WFDIS = 1 (init bank 1 first); bank 1 with WFDIS = 0 STATE
     * while bank 2..4 is configured as sync-write PSRAM. */
    /* Caller: timing codes from the external memory data sheet and HCLK (the driver checks ranges
     * only); synchronous burst read on a read-sensitive device: see ERRATA ES0430 2.5.1 above. */
    /* Level: PARAM[F] STATE[r] UNSUPPORTED[r] */
    SPL_Status FMC_NORSRAM_Init(uint32_t bank, const FMC_NORSRAM_InitTypeDef *init,
                                const FMC_NORSRAM_TimingTypeDef *timing,
                                const FMC_NORSRAM_TimingTypeDef *write_timing);
    /* Set / clear FMC_BCRx.MBKEN only (idempotent). */
    /* Caller: initialise the sub-bank (FMC_NORSRAM_Init) and configure the pins and the MPU before
     * the first window access. */
    /* Level: PARAM[F] UNSUPPORTED[r] */
    SPL_Status FMC_NORSRAM_Enable(uint32_t bank);
    /* Caller: no access through the sub-bank window may be in progress, or be made while the
     * sub-bank is disabled. */
    /* Level: PARAM[F] UNSUPPORTED[r] */
    SPL_Status FMC_NORSRAM_Disable(uint32_t bank);
    /* FMC_PCSCNTR: CSCOUNT (0 disables the split) and CNTBxEN from bank_mask
     * (FMC_PSRAM_CNT_*). STATE while any NOR/PSRAM sub-bank is enabled. */
    /* Caller: count from the PSRAM tCEM requirement (not checked). */
    /* Level: PARAM[F] STATE[r] UNSUPPORTED[r] */
    SPL_Status FMC_PSRAM_SetCounter(uint16_t count, uint32_t bank_mask);

    /* Programs FMC_PMEM, FMC_PATT, then FMC_PCR (PTYP = 1, PBKEN = 0,
     * ECCEN = 0). STATE while PBKEN = 1.
     * ERRATA ES0430 2.5.2: PARAM when common->setup (MEMSET) = 0 and
     * attribute->hold (ATTHOLD) <= 1 (busy-NAND wrong read). */
    /* Caller: timing codes from the NAND data sheet and HCLK (the driver checks ranges only). */
    /* Level: PARAM[F] STATE[r] UNSUPPORTED[r] */
    SPL_Status FMC_NAND_Init(const FMC_NAND_InitTypeDef *init, const FMC_NAND_TimingTypeDef *common,
                             const FMC_NAND_TimingTypeDef *attribute);
    /* Caller: configure the interrupt enables (FMC_NAND_ITConfig) before enabling; the NAND window
     * MPU attribute is Device. */
    /* Level: UNSUPPORTED[r] */
    SPL_Status FMC_NAND_Enable(void);
    /* Caller: no access through the NAND window may be in progress. */
    /* Level: UNSUPPORTED[r] */
    SPL_Status FMC_NAND_Disable(void);
    /* ECCEN = 1. If ECCEN is already 1, it is first written 0 (ECC logic and
     * FMC_ECCR reset, RM0440 19.7.6 step 4) and then 1. */
    /* Caller: start it right before the data transfer of each ECC block. */
    /* Level: UNSUPPORTED[r] */
    SPL_Status FMC_NAND_ECCStart(void);
    /* ECCEN = 0: ECC logic disabled and FMC_ECCR cleared. */
    /* Caller: read FMC_NAND_GetECC first: stopping clears FMC_ECCR. */
    /* Level: UNSUPPORTED[r] */
    SPL_Status FMC_NAND_ECCStop(void);
    /* FMC_ECCR masked to the bits valid for ECCPS (RM0440 Table 156).
     * STATE when ECCEN = 0; WOULD_BLOCK while the write FIFO is not empty
     * (FEMPT = 0, RM0440 FMC_SR). Does not stop the ECC logic. */
    /* Caller: call after the last data byte of the ECC block was transferred; bound the WOULD_BLOCK
     * retries. */
    /* Level: PARAM[F] STATE[r] UNSUPPORTED[r] WOULD_BLOCK[r] */
    SPL_Status FMC_NAND_GetECC(uint32_t *ecc);
    /* FMC_SR & (FMC_NAND_FLAGS_ALL | FMC_NAND_FLAG_FEMPT), no side effects. */
    /* Caller: none beyond the file-header duties. */
    /* Level: PARAM[F] UNSUPPORTED[r] */
    SPL_Status FMC_NAND_GetFlags(uint32_t *flags);
    /* Writes 0 to the given IRS/ILS/IFS flags (subset of FMC_NAND_FLAGS_ALL,
     * non-zero). FMC_SR has no atomic clear: a flag set by hardware between
     * the read and the write of the RMW is lost (RM0440 FMC_SR is rw). */
    /* Caller: clear flags from the FMC ISR only (no atomic clear, see above). */
    /* Level: PARAM[F] UNSUPPORTED[r] */
    SPL_Status FMC_NAND_ClearFlags(uint32_t flags);
    /* Sets (enable = 1) or clears (enable = 0) IREN/ILEN/IFEN (subset of
     * FMC_NAND_IT_ALL, non-zero). Same RMW caveat as FMC_NAND_ClearFlags. */
    /* Caller: configure before FMC_NAND_Enable; the FMC ISR clears the flags it services. */
    /* Level: PARAM[F] UNSUPPORTED[r] */
    SPL_Status FMC_NAND_ITConfig(uint32_t its, uint8_t enable);

    /* Base address of a bank window (FMC_REGION_*). No memory access. */
    /* Caller: none beyond the file-header duties. */
    /* Level: PARAM[F] UNSUPPORTED[r] */
    SPL_Status FMC_GetBankAddress(uint32_t region, uint32_t *address);

#ifdef __cplusplus
}
#endif
#endif
