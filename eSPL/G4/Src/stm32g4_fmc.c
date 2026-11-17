/* SPDX-License-Identifier: MIT */
/* FMC driver (NOR/PSRAM/SRAM and NAND). RM0440 Rev 9 section 19 (FSMC/FMC). Register
 * descriptions: 19.6.6 (NOR/PSRAM), 19.7.7 (NAND); parameter ranges: Table 126, Table 152.
 * Without FMC_Bank1_R in CMSIS every API returns SPL_ERR_UNSUPPORTED.
 * Public contract: stm32g4_fmc.h. */
#define SPL_LIBRARY_BUILD 1
#include <stddef.h>
#include "stm32g4_fmc.h"

#if defined(FMC_Bank1_R)

/* Keep declarations visible in sizeof-only FAST guards; no code emitted if unused. */
#if SPL_CHECK_LEVEL == SPL_CHECK_FAST
#define FMC_CHECK_UNUSED __attribute__((unused))
#else
#define FMC_CHECK_UNUSED
#endif

/* FMC_BCRx fields written by FMC_NORSRAM_Init. All other bits are reserved
 * ("must be kept at reset value"; bit 7 resets to 1) and are preserved. In
 * FMC_BCR2..4 the CCLKEN/WFDIS positions are don't care and are written 0. */
#define FMC_BCR_FIELDS                                                                             \
    (FMC_BCRx_MBKEN | FMC_BCRx_MUXEN | FMC_BCRx_MTYP | FMC_BCRx_MWID | FMC_BCRx_FACCEN |           \
     FMC_BCRx_BURSTEN | FMC_BCRx_WAITPOL | FMC_BCRx_WAITCFG | FMC_BCRx_WREN | FMC_BCRx_WAITEN |    \
     FMC_BCRx_EXTMOD | FMC_BCRx_ASYNCWAIT | FMC_BCRx_CPSIZE | FMC_BCRx_CBURSTRW |                  \
     FMC_BCR1_CCLKEN | FMC_BCR1_WFDIS | FMC_BCRx_NBLSET)
/* FMC_BWTRx fields; bits 27:20 are reserved (reset value 1s) and preserved. */
#define FMC_BWTR_FIELDS                                                                            \
    (FMC_BWTRx_ADDSET | FMC_BWTRx_ADDHLD | FMC_BWTRx_DATAST | FMC_BWTRx_BUSTURN |                  \
     FMC_BWTRx_ACCMOD | FMC_BWTRx_DATAHLD)
/* FMC_BWTRx field reset values (reset value 0x0FFF FFFF without bits 27:20). */
#define FMC_BWTR_FIELDS_RESET                                                                      \
    (FMC_BWTRx_ADDSET | FMC_BWTRx_ADDHLD | FMC_BWTRx_DATAST | FMC_BWTRx_BUSTURN)
#define FMC_PCSCNTR_FIELDS                                                                         \
    (FMC_PCSCNTR_CSCOUNT | FMC_PCSCNTR_CNTB1EN | FMC_PCSCNTR_CNTB2EN | FMC_PCSCNTR_CNTB3EN |       \
     FMC_PCSCNTR_CNTB4EN)
#define FMC_PCR_FIELDS                                                                             \
    (FMC_PCR_PWAITEN | FMC_PCR_PBKEN | FMC_PCR_PTYP | FMC_PCR_PWID | FMC_PCR_ECCEN |               \
     FMC_PCR_TCLR | FMC_PCR_TAR | FMC_PCR_ECCPS)
/* FMC_SR read/write bits; FEMPT is read-only, bits 31:7 reserved (0). */
#define FMC_SR_RW (FMC_NAND_FLAGS_ALL | FMC_NAND_IT_ALL)

#define FMC_ADDSET_MAX 15U
#define FMC_FIELD4_MAX 15U
#define FMC_FIELD2_MAX 3U
#define FMC_NAND_TIMING_MAX 254U
#define FMC_NAND_ATTRIBUTE_BASE (FMC_BANK3 + 0x08000000UL)

static FMC_CHECK_UNUSED uint8_t Fmc_BankValid(uint32_t bank)
{
    return (uint8_t)(((bank >= FMC_NORSRAM_BANK1) && (bank <= FMC_NORSRAM_BANK4)) ? 1U : 0U);
}

/* BTCR[2(x-1)] = FMC_BCRx, BTCR[2(x-1)+1] = FMC_BTRx, BWTR[2(x-1)] =
 * FMC_BWTRx (RM0440 19.6.6 address offsets). */
static uint32_t Fmc_BcrIndex(uint32_t bank)
{
    return (bank - 1U) * 2U;
}

/* Compile-time check (CCL-2 YAP-03): for the highest bank, BTR index idx + 1 fits CMSIS BTCR[]
 * and BWTR index idx fits CMSIS BWTR[] (idx = Fmc_BcrIndex(FMC_NORSRAM_BANK4)). */
typedef char Fmc_BankIndexCheck
    [(((((FMC_NORSRAM_BANK4 - 1U) * 2U) + 1U) <
       (sizeof(((FMC_Bank1_TypeDef *)0)->BTCR) / sizeof(((FMC_Bank1_TypeDef *)0)->BTCR[0]))) &&
      (((FMC_NORSRAM_BANK4 - 1U) * 2U) <
       (sizeof(((FMC_Bank1E_TypeDef *)0)->BWTR) / sizeof(((FMC_Bank1E_TypeDef *)0)->BWTR[0]))) &&
      (FMC_NORSRAM_BANK1 == 1U))
         ? 1
         : -1];

static FMC_CHECK_UNUSED uint8_t Fmc_Bool(uint8_t value)
{
    return (uint8_t)((value <= 1U) ? 1U : 0U);
}

static FMC_CHECK_UNUSED uint8_t Fmc_InitValid(uint32_t bank, const FMC_NORSRAM_InitTypeDef *init)
{
    uint8_t ok = 1U;

    if ((init->mem_type > FMC_MEMTYPE_NOR) || (init->data_width > FMC_WIDTH_16) ||
        (init->cram_page > FMC_CRAM_PAGE_1024) || (init->nbl_setup > FMC_FIELD2_MAX))
    {
        ok = 0U;
    }
    if ((Fmc_Bool(init->mux) == 0U) || (Fmc_Bool(init->flash_access) == 0U) ||
        (Fmc_Bool(init->burst) == 0U) || (Fmc_Bool(init->wait_polarity) == 0U) ||
        (Fmc_Bool(init->wait_config) == 0U) || (Fmc_Bool(init->write_enable) == 0U) ||
        (Fmc_Bool(init->wait_enable) == 0U) || (Fmc_Bool(init->extended_mode) == 0U) ||
        (Fmc_Bool(init->async_wait) == 0U) || (Fmc_Bool(init->write_burst) == 0U) ||
        (Fmc_Bool(init->continuous_clock) == 0U) || (Fmc_Bool(init->write_fifo_disable) == 0U))
    {
        ok = 0U;
    }
    /* MUXEN: "valid only with NOR and PSRAM memories". */
    if ((init->mux != 0U) && (init->mem_type == FMC_MEMTYPE_SRAM))
    {
        ok = 0U;
    }
    /* WAITCFG = 1: "not used for PSRAM". */
    if ((init->wait_config != 0U) && (init->mem_type == FMC_MEMTYPE_PSRAM))
    {
        ok = 0U;
    }
    /* CCLKEN/WFDIS exist in FMC_BCR1 only (don't care in BCR2..4). CCLKEN
     * needs bank 1 in synchronous mode (FMC_BCR1 CCLKEN notes). */
    if (bank != FMC_NORSRAM_BANK1)
    {
        if ((init->continuous_clock != 0U) || (init->write_fifo_disable != 0U))
        {
            ok = 0U;
        }
    }
    else if ((init->continuous_clock != 0U) && (init->burst == 0U) && (init->write_burst == 0U))
    {
        ok = 0U;
    }
    /* ERRATA ES0430 2.5.3: sync PSRAM write with the write FIFO enabled may corrupt data. */
    else if ((init->mem_type == FMC_MEMTYPE_PSRAM) && (init->write_burst != 0U) &&
             (init->write_fifo_disable == 0U))
    {
        ok = 0U;
    }
    else
    {
        /* Sub-bank 1 with valid CCLKEN/WFDIS. */
    }
    return ok;
}

/* Field ranges common to FMC_BTRx and FMC_BWTRx (RM0440 19.6.6). */
static FMC_CHECK_UNUSED uint8_t Fmc_TimingValid(const FMC_NORSRAM_TimingTypeDef *t,
                                                uint8_t addset_min)
{
    uint8_t ok = 1U;

    if ((t->addr_setup > FMC_ADDSET_MAX) || (t->addr_setup < addset_min))
    {
        ok = 0U;
    }
    /* ADDHLD = 0 and DATAST = 0 are reserved codes. */
    if ((t->addr_hold == 0U) || (t->addr_hold > FMC_FIELD4_MAX) || (t->data_setup == 0U))
    {
        ok = 0U;
    }
    if ((t->bus_turnaround > FMC_FIELD4_MAX) || (t->clk_div > FMC_FIELD4_MAX) ||
        (t->data_latency > FMC_FIELD4_MAX) || (t->access_mode > FMC_ACCESS_MODE_D) ||
        (t->data_hold > FMC_FIELD2_MAX))
    {
        ok = 0U;
    }
    return ok;
}

/* ADDSET minimum 1: muxed mode, mode D, mode 1 with PSRAM (ADDSET notes). */
static FMC_CHECK_UNUSED uint8_t Fmc_AddsetMin(const FMC_NORSRAM_InitTypeDef *init,
                                              uint8_t access_mode)
{
    uint8_t min = 0U;

    if (init->mux != 0U)
    {
        min = 1U;
    }
    if ((init->extended_mode != 0U) && (access_mode == FMC_ACCESS_MODE_D))
    {
        min = 1U;
    }
    if ((init->extended_mode == 0U) && (init->mem_type == FMC_MEMTYPE_PSRAM))
    {
        min = 1U;
    }
    return min;
}

static FMC_CHECK_UNUSED uint8_t Fmc_TimingsValid(const FMC_NORSRAM_InitTypeDef *init,
                                                 const FMC_NORSRAM_TimingTypeDef *timing,
                                                 const FMC_NORSRAM_TimingTypeDef *write_timing)
{
    uint8_t ok = Fmc_TimingValid(timing, Fmc_AddsetMin(init, timing->access_mode));
    uint8_t sync = 0U;

    if ((init->burst != 0U) || (init->write_burst != 0U) || (init->continuous_clock != 0U))
    {
        sync = 1U;
    }

    /* Table 126: clock divide ratio min 2 HCLK, i.e. CLKDIV >= 1. */
    if ((sync != 0U) && (timing->clk_div == 0U))
    {
        ok = 0U;
    }
    /* "With PSRAMs (CRAMs) the filled DATLAT must be set to 0". */
    if ((init->mem_type == FMC_MEMTYPE_PSRAM) && (timing->data_latency != 0U))
    {
        ok = 0U;
    }
    if (init->extended_mode != 0U)
    {
        /* FMC_BWTRx has no CLKDIV/DATLAT (bits 27:20 reserved): must be 0. */
        if ((write_timing == NULL) ||
            (Fmc_TimingValid(write_timing, Fmc_AddsetMin(init, write_timing->access_mode)) == 0U) ||
            (write_timing->clk_div != 0U) || (write_timing->data_latency != 0U))
        {
            ok = 0U;
        }
    }
    else if (write_timing != NULL)
    {
        ok = 0U;
    }
    else
    {
        /* Read/write timings come from FMC_BTRx only. */
    }
    return ok;
}

static uint32_t Fmc_BcrValue(const FMC_NORSRAM_InitTypeDef *init)
{
    uint32_t v = ((uint32_t)init->mux << FMC_BCRx_MUXEN_Pos);

    v |= ((uint32_t)init->mem_type << FMC_BCRx_MTYP_Pos);
    v |= ((uint32_t)init->data_width << FMC_BCRx_MWID_Pos);
    v |= ((uint32_t)init->flash_access << FMC_BCRx_FACCEN_Pos);
    v |= ((uint32_t)init->burst << FMC_BCRx_BURSTEN_Pos);
    v |= ((uint32_t)init->wait_polarity << FMC_BCRx_WAITPOL_Pos);
    v |= ((uint32_t)init->wait_config << FMC_BCRx_WAITCFG_Pos);
    v |= ((uint32_t)init->write_enable << FMC_BCRx_WREN_Pos);
    v |= ((uint32_t)init->wait_enable << FMC_BCRx_WAITEN_Pos);
    v |= ((uint32_t)init->extended_mode << FMC_BCRx_EXTMOD_Pos);
    v |= ((uint32_t)init->async_wait << FMC_BCRx_ASYNCWAIT_Pos);
    v |= ((uint32_t)init->cram_page << FMC_BCRx_CPSIZE_Pos);
    v |= ((uint32_t)init->write_burst << FMC_BCRx_CBURSTRW_Pos);
    v |= ((uint32_t)init->continuous_clock << FMC_BCR1_CCLKEN_Pos);
    v |= ((uint32_t)init->write_fifo_disable << FMC_BCR1_WFDIS_Pos);
    v |= ((uint32_t)init->nbl_setup << FMC_BCRx_NBLSET_Pos);
    return v;
}

/* FMC_BTRx layout; for FMC_BWTRx clk_div and data_latency are 0 (checked). */
static uint32_t Fmc_TimingValue(const FMC_NORSRAM_TimingTypeDef *t)
{
    uint32_t v = ((uint32_t)t->addr_setup << FMC_BTRx_ADDSET_Pos);

    v |= ((uint32_t)t->addr_hold << FMC_BTRx_ADDHLD_Pos);
    v |= ((uint32_t)t->data_setup << FMC_BTRx_DATAST_Pos);
    v |= ((uint32_t)t->bus_turnaround << FMC_BTRx_BUSTURN_Pos);
    v |= ((uint32_t)t->clk_div << FMC_BTRx_CLKDIV_Pos);
    v |= ((uint32_t)t->data_latency << FMC_BTRx_DATLAT_Pos);
    v |= ((uint32_t)t->access_mode << FMC_BTRx_ACCMOD_Pos);
    v |= ((uint32_t)t->data_hold << FMC_BTRx_DATAHLD_Pos);
    return v;
}

/* ERRATA ES0430 2.5.3: sub-bank configured as PSRAM with synchronous write
 * (MTYP = 01, CBURSTRW = 1), enabled or not. */
static uint8_t Fmc_IsSyncWritePsram(uint32_t bcr)
{
    uint32_t mtyp = (bcr & FMC_BCRx_MTYP_Msk) >> FMC_BCRx_MTYP_Pos;

    return (uint8_t)(((mtyp == FMC_MEMTYPE_PSRAM) && ((bcr & FMC_BCRx_CBURSTRW) != 0U)) ? 1U : 0U);
}

/* Any NOR/PSRAM sub-bank enabled (optionally ignoring one). */
static uint8_t Fmc_AnyNorEnabled(uint32_t skip_bank)
{
    uint8_t any = 0U;
    uint32_t bank;

    for (bank = FMC_NORSRAM_BANK1; bank <= FMC_NORSRAM_BANK4; ++bank)
    {
        if ((bank != skip_bank) && ((FMC_Bank1_R->BTCR[Fmc_BcrIndex(bank)] & FMC_BCRx_MBKEN) != 0U))
        {
            any = 1U;
        }
    }
    return any;
}

SPL_Status FMC_NORSRAM_Init(uint32_t bank, const FMC_NORSRAM_InitTypeDef *init,
                            const FMC_NORSRAM_TimingTypeDef *timing,
                            const FMC_NORSRAM_TimingTypeDef *write_timing)
{
    uint32_t idx;
    uint32_t bcr;
    uint32_t value;
    uint32_t bwtr;

    /* Invalid input: silent register corruption or wrong address risk. */
    SPL_REQUIRE(!((Fmc_BankValid(bank) == 0U)), SPL_ERR_PARAM);
    /* Invalid input: silent register corruption or wrong address risk. */
    SPL_REQUIRE(!((init == NULL)), SPL_ERR_PARAM);
    /* Invalid input: silent register corruption or wrong address risk. */
    SPL_REQUIRE(!((timing == NULL)), SPL_ERR_PARAM);
    /* Invalid input: silent register corruption or wrong address risk. */
    SPL_REQUIRE(!((Fmc_InitValid(bank, init) == 0U)), SPL_ERR_PARAM);
    /* Invalid input: silent register corruption or wrong address risk. */
    SPL_REQUIRE(!((Fmc_TimingsValid(init, timing, write_timing) == 0U)), SPL_ERR_PARAM);
    idx = Fmc_BcrIndex(bank);
    bcr = FMC_Bank1_R->BTCR[idx];
    /* Driver rule: configure a sub-bank only while it is disabled. */
    if ((bcr & FMC_BCRx_MBKEN) != 0U)
    {
        return SPL_ERR_STATE;
    }
    value = Fmc_BcrValue(init);
    /* ERRATA ES0430 2.5.3: WFDIS (BCR1) acts on every sub-bank: sync-write PSRAM needs WFDIS=1. */
    if ((bank != FMC_NORSRAM_BANK1) && (Fmc_IsSyncWritePsram(value) != 0U) &&
        ((FMC_Bank1_R->BTCR[0] & FMC_BCR1_WFDIS) == 0U))
    {
        return SPL_ERR_STATE;
    }
    if ((bank == FMC_NORSRAM_BANK1) && ((value & FMC_BCR1_WFDIS) == 0U))
    {
        uint32_t other;

        for (other = FMC_NORSRAM_BANK2; other <= FMC_NORSRAM_BANK4; ++other)
        {
            if (Fmc_IsSyncWritePsram(FMC_Bank1_R->BTCR[Fmc_BcrIndex(other)]) != 0U)
            {
                return SPL_ERR_STATE;
            }
        }
    }
    /* CCLKEN/WFDIS in BCR1 act on every bank: change them only while all
     * other banks (NOR/PSRAM 2..4 and NAND) are disabled. */
    if ((bank == FMC_NORSRAM_BANK1) && ((bcr & (FMC_BCR1_CCLKEN | FMC_BCR1_WFDIS)) !=
                                        (value & (FMC_BCR1_CCLKEN | FMC_BCR1_WFDIS))))
    {
        if ((Fmc_AnyNorEnabled(FMC_NORSRAM_BANK1) != 0U) ||
            ((FMC_Bank3_R->PCR & FMC_PCR_PBKEN) != 0U))
        {
            return SPL_ERR_STATE;
        }
    }

    FMC_Bank1_R->BTCR[idx + 1U] = Fmc_TimingValue(timing);
    bwtr = FMC_Bank1E_R->BWTR[idx] & ~FMC_BWTR_FIELDS;
    if (init->extended_mode != 0U)
    {
        bwtr |= Fmc_TimingValue(write_timing);
    }
    else
    {
        bwtr |= FMC_BWTR_FIELDS_RESET;
    }
    FMC_Bank1E_R->BWTR[idx] = bwtr;
    /* MBKEN = 0 (value never contains it). */
    FMC_Bank1_R->BTCR[idx] = (bcr & ~FMC_BCR_FIELDS) | value;
    return SPL_OK;
}

SPL_Status FMC_NORSRAM_Enable(uint32_t bank)
{
    uint32_t idx;

    /* Invalid input: silent register corruption or wrong address risk. */
    SPL_REQUIRE(!(Fmc_BankValid(bank) == 0U), SPL_ERR_PARAM);
    idx = Fmc_BcrIndex(bank);
    /* FMC_BCRx has no W1C or self-clearing bits: RMW is safe. */
    FMC_Bank1_R->BTCR[idx] = FMC_Bank1_R->BTCR[idx] | FMC_BCRx_MBKEN;
    return SPL_OK;
}

SPL_Status FMC_NORSRAM_Disable(uint32_t bank)
{
    uint32_t idx;

    /* Invalid input: silent register corruption or wrong address risk. */
    SPL_REQUIRE(!(Fmc_BankValid(bank) == 0U), SPL_ERR_PARAM);
    idx = Fmc_BcrIndex(bank);
    FMC_Bank1_R->BTCR[idx] = FMC_Bank1_R->BTCR[idx] & ~FMC_BCRx_MBKEN;
    return SPL_OK;
}

SPL_Status FMC_PSRAM_SetCounter(uint16_t count, uint32_t bank_mask)
{
    uint32_t value;

    /* Invalid input: silent register corruption or wrong address risk. */
    SPL_REQUIRE(!((bank_mask & ~FMC_PSRAM_CNT_ALL) != 0U), SPL_ERR_PARAM);
    /* The counter is common to all sub-banks: no sub-bank may be active. */
    if (Fmc_AnyNorEnabled(0U) != 0U)
    {
        return SPL_ERR_STATE;
    }
    value = ((uint32_t)count << FMC_PCSCNTR_CSCOUNT_Pos) | (bank_mask << FMC_PCSCNTR_CNTB1EN_Pos);
    FMC_Bank1_R->PCSCNTR = (FMC_Bank1_R->PCSCNTR & ~FMC_PCSCNTR_FIELDS) | value;
    return SPL_OK;
}

static FMC_CHECK_UNUSED uint8_t Fmc_NandTimingValid(const FMC_NAND_TimingTypeDef *t)
{
    uint8_t ok = 1U;

    /* 0xFF is reserved in every field; 0 is reserved for WAIT and HOLD. */
    if ((t->setup > FMC_NAND_TIMING_MAX) || (t->hiz > FMC_NAND_TIMING_MAX))
    {
        ok = 0U;
    }
    if ((t->wait == 0U) || (t->wait > FMC_NAND_TIMING_MAX) || (t->hold == 0U) ||
        (t->hold > FMC_NAND_TIMING_MAX))
    {
        ok = 0U;
    }
    return ok;
}

/* ERRATA ES0430 2.5.2: MEMSET = 0 with ATTHOLD <= 1 can read wrong data from a busy NAND.
 * Literal workaround: MEMSET > 0 or ATTHOLD > 1. */
static FMC_CHECK_UNUSED uint8_t Fmc_NandBusyReadSafe(const FMC_NAND_TimingTypeDef *common,
                                                     const FMC_NAND_TimingTypeDef *attribute)
{
    return (uint8_t)(((common->setup > 0U) || (attribute->hold > 1U)) ? 1U : 0U);
}

/* FMC_PMEM and FMC_PATT share the layout (MEMSET/ATTSET at bit 0 ...). */
static uint32_t Fmc_NandTimingValue(const FMC_NAND_TimingTypeDef *t)
{
    uint32_t v = ((uint32_t)t->setup << FMC_PMEM_MEMSET_Pos);

    v |= ((uint32_t)t->wait << FMC_PMEM_MEMWAIT_Pos);
    v |= ((uint32_t)t->hold << FMC_PMEM_MEMHOLD_Pos);
    v |= ((uint32_t)t->hiz << FMC_PMEM_MEMHIZ_Pos);
    return v;
}

SPL_Status FMC_NAND_Init(const FMC_NAND_InitTypeDef *init, const FMC_NAND_TimingTypeDef *common,
                         const FMC_NAND_TimingTypeDef *attribute)
{
    uint32_t pcr;
    uint32_t value;

    /* Invalid input: silent register corruption or wrong address risk. */
    SPL_REQUIRE(!((init == NULL)), SPL_ERR_PARAM);
    /* Invalid input: silent register corruption or wrong address risk. */
    SPL_REQUIRE(!((common == NULL)), SPL_ERR_PARAM);
    /* Invalid input: silent register corruption or wrong address risk. */
    SPL_REQUIRE(!((attribute == NULL)), SPL_ERR_PARAM);
    /* Invalid input: silent register corruption or wrong address risk. */
    SPL_REQUIRE(!((Fmc_Bool(init->wait_feature) == 0U)), SPL_ERR_PARAM);
    /* Invalid input: silent register corruption or wrong address risk. */
    SPL_REQUIRE(!((init->data_width > FMC_WIDTH_16)), SPL_ERR_PARAM);
    /* Invalid input: silent register corruption or wrong address risk. */
    SPL_REQUIRE(!((init->ecc_page_size > FMC_ECC_PAGE_8192)), SPL_ERR_PARAM);
    /* Invalid input: silent register corruption or wrong address risk. */
    SPL_REQUIRE(!((init->tclr > FMC_FIELD4_MAX)), SPL_ERR_PARAM);
    /* Invalid input: silent register corruption or wrong address risk. */
    SPL_REQUIRE(!((init->tar > FMC_FIELD4_MAX)), SPL_ERR_PARAM);
    /* ERRATA ES0430 2.5.2: refuse MEMSET = 0 together with ATTHOLD <= 1. */
    /* Invalid input: silent register corruption or wrong address risk. */
    SPL_REQUIRE(!((Fmc_NandTimingValid(common) == 0U)), SPL_ERR_PARAM);
    /* Invalid input: silent register corruption or wrong address risk. */
    SPL_REQUIRE(!((Fmc_NandTimingValid(attribute) == 0U)), SPL_ERR_PARAM);
    /* Invalid input: silent register corruption or wrong address risk. */
    SPL_REQUIRE(!((Fmc_NandBusyReadSafe(common, attribute) == 0U)), SPL_ERR_PARAM);
    pcr = FMC_Bank3_R->PCR;
    if ((pcr & FMC_PCR_PBKEN) != 0U)
    {
        return SPL_ERR_STATE;
    }

    FMC_Bank3_R->PMEM = Fmc_NandTimingValue(common);
    FMC_Bank3_R->PATT = Fmc_NandTimingValue(attribute);
    value = FMC_PCR_PTYP | ((uint32_t)init->wait_feature << FMC_PCR_PWAITEN_Pos);
    value |= ((uint32_t)init->data_width << FMC_PCR_PWID_Pos);
    value |= ((uint32_t)init->tclr << FMC_PCR_TCLR_Pos);
    value |= ((uint32_t)init->tar << FMC_PCR_TAR_Pos);
    value |= ((uint32_t)init->ecc_page_size << FMC_PCR_ECCPS_Pos);
    FMC_Bank3_R->PCR = (pcr & ~FMC_PCR_FIELDS) | value;
    return SPL_OK;
}

SPL_Status FMC_NAND_Enable(void)
{
    /* FMC_PCR has no W1C or self-clearing bits: RMW is safe. */
    FMC_Bank3_R->PCR = FMC_Bank3_R->PCR | FMC_PCR_PBKEN;
    return SPL_OK;
}

SPL_Status FMC_NAND_Disable(void)
{
    FMC_Bank3_R->PCR = FMC_Bank3_R->PCR & ~FMC_PCR_PBKEN;
    return SPL_OK;
}

SPL_Status FMC_NAND_ECCStart(void)
{
    uint32_t pcr = FMC_Bank3_R->PCR;

    /* RM0440 19.7.6: ECCR is cleared only by ECCEN = 0; a new block needs
     * ECCEN = 0 then ECCEN = 1. */
    if ((pcr & FMC_PCR_ECCEN) != 0U)
    {
        pcr &= ~FMC_PCR_ECCEN;
        FMC_Bank3_R->PCR = pcr;
    }
    FMC_Bank3_R->PCR = pcr | FMC_PCR_ECCEN;
    return SPL_OK;
}

SPL_Status FMC_NAND_ECCStop(void)
{
    FMC_Bank3_R->PCR = FMC_Bank3_R->PCR & ~FMC_PCR_ECCEN;
    return SPL_OK;
}

SPL_Status FMC_NAND_GetECC(uint32_t *ecc)
{
    uint32_t pcr;
    uint32_t bits;
    uint32_t value;

    /* Invalid input: silent register corruption or wrong address risk. */
    SPL_REQUIRE(!(ecc == NULL), SPL_ERR_PARAM);
    pcr = FMC_Bank3_R->PCR;
    if ((pcr & FMC_PCR_ECCEN) == 0U)
    {
        return SPL_ERR_STATE;
    }
    /* FMC_SR: "To read the correct ECC, the software must wait until the
     * FIFO is empty." */
    if ((FMC_Bank3_R->SR & FMC_SR_FEMPT) == 0U)
    {
        return SPL_WOULD_BLOCK;
    }
    /* Table 156: ECCPS 0..5 -> ECC[21:0] .. ECC[31:0]. */
    bits = 22U + (2U * ((pcr & FMC_PCR_ECCPS) >> FMC_PCR_ECCPS_Pos));
    value = FMC_Bank3_R->ECCR;
    if (bits < 32U)
    {
        value &= ((uint32_t)1U << bits) - 1U;
    }
    *ecc = value;
    return SPL_OK;
}

SPL_Status FMC_NAND_GetFlags(uint32_t *flags)
{
    /* Invalid input: silent register corruption or wrong address risk. */
    SPL_REQUIRE(!(flags == NULL), SPL_ERR_PARAM);
    *flags = FMC_Bank3_R->SR & (FMC_NAND_FLAGS_ALL | FMC_NAND_FLAG_FEMPT);
    return SPL_OK;
}

SPL_Status FMC_NAND_ClearFlags(uint32_t flags)
{
    /* Invalid input: silent register corruption or wrong address risk. */
    SPL_REQUIRE(!((flags == 0U)), SPL_ERR_PARAM);
    /* Invalid input: silent register corruption or wrong address risk. */
    SPL_REQUIRE(!(((flags & ~FMC_NAND_FLAGS_ALL) != 0U)), SPL_ERR_PARAM);
    /* IRS/IFS: writing 1 sets the flag, so only the read value is written
     * back for the other flags; the cleared ones are written 0. */
    FMC_Bank3_R->SR = FMC_Bank3_R->SR & FMC_SR_RW & ~flags;
    return SPL_OK;
}

SPL_Status FMC_NAND_ITConfig(uint32_t its, uint8_t enable)
{
    uint32_t sr;

    /* Invalid input: silent register corruption or wrong address risk. */
    SPL_REQUIRE(!((its == 0U)), SPL_ERR_PARAM);
    /* Invalid input: silent register corruption or wrong address risk. */
    SPL_REQUIRE(!(((its & ~FMC_NAND_IT_ALL) != 0U)), SPL_ERR_PARAM);
    /* Invalid input: silent register corruption or wrong address risk. */
    SPL_REQUIRE(!((Fmc_Bool(enable) == 0U)), SPL_ERR_PARAM);
    sr = FMC_Bank3_R->SR & FMC_SR_RW;
    if (enable != 0U)
    {
        sr |= its;
    }
    else
    {
        sr &= ~its;
    }
    FMC_Bank3_R->SR = sr;
    return SPL_OK;
}

/* Bank window base per FMC_REGION_* code. */
static const uint32_t fmc_region_base[] = {FMC_BANK1_1, FMC_BANK1_2, FMC_BANK1_3,
                                           FMC_BANK1_4, FMC_BANK3,   FMC_NAND_ATTRIBUTE_BASE};

/* Compile-time check (CCL-2 YAP-03): one entry per region code 0..FMC_REGION_NAND_ATTRIBUTE, the
 * bound used by the check in FMC_GetBankAddress. */
typedef char Fmc_RegionTableCheck[((sizeof(fmc_region_base) / sizeof(fmc_region_base[0])) ==
                                   (FMC_REGION_NAND_ATTRIBUTE + 1U))
                                      ? 1
                                      : -1];

SPL_Status FMC_GetBankAddress(uint32_t region, uint32_t *address)
{
    /* Invalid input: silent register corruption or wrong address risk. */
    SPL_REQUIRE(!((region > FMC_REGION_NAND_ATTRIBUTE)), SPL_ERR_PARAM);
    /* Invalid input: silent register corruption or wrong address risk. */
    SPL_REQUIRE(!((address == NULL)), SPL_ERR_PARAM);
    *address = fmc_region_base[region];
    return SPL_OK;
}

#else /* No FMC on this SKU (CMSIS FMC_Bank1_R not defined). */

SPL_Status FMC_NORSRAM_Init(uint32_t bank, const FMC_NORSRAM_InitTypeDef *init,
                            const FMC_NORSRAM_TimingTypeDef *timing,
                            const FMC_NORSRAM_TimingTypeDef *write_timing)
{
    (void)bank;
    (void)init;
    (void)timing;
    (void)write_timing;
    return SPL_ERR_UNSUPPORTED;
}

SPL_Status FMC_NORSRAM_Enable(uint32_t bank)
{
    (void)bank;
    return SPL_ERR_UNSUPPORTED;
}

SPL_Status FMC_NORSRAM_Disable(uint32_t bank)
{
    (void)bank;
    return SPL_ERR_UNSUPPORTED;
}

SPL_Status FMC_PSRAM_SetCounter(uint16_t count, uint32_t bank_mask)
{
    (void)count;
    (void)bank_mask;
    return SPL_ERR_UNSUPPORTED;
}

SPL_Status FMC_NAND_Init(const FMC_NAND_InitTypeDef *init, const FMC_NAND_TimingTypeDef *common,
                         const FMC_NAND_TimingTypeDef *attribute)
{
    (void)init;
    (void)common;
    (void)attribute;
    return SPL_ERR_UNSUPPORTED;
}

SPL_Status FMC_NAND_Enable(void)
{
    return SPL_ERR_UNSUPPORTED;
}

SPL_Status FMC_NAND_Disable(void)
{
    return SPL_ERR_UNSUPPORTED;
}

SPL_Status FMC_NAND_ECCStart(void)
{
    return SPL_ERR_UNSUPPORTED;
}

SPL_Status FMC_NAND_ECCStop(void)
{
    return SPL_ERR_UNSUPPORTED;
}

SPL_Status FMC_NAND_GetECC(uint32_t *ecc)
{
    (void)ecc;
    return SPL_ERR_UNSUPPORTED;
}

SPL_Status FMC_NAND_GetFlags(uint32_t *flags)
{
    (void)flags;
    return SPL_ERR_UNSUPPORTED;
}

SPL_Status FMC_NAND_ClearFlags(uint32_t flags)
{
    (void)flags;
    return SPL_ERR_UNSUPPORTED;
}

SPL_Status FMC_NAND_ITConfig(uint32_t its, uint8_t enable)
{
    (void)its;
    (void)enable;
    return SPL_ERR_UNSUPPORTED;
}

SPL_Status FMC_GetBankAddress(uint32_t region, uint32_t *address)
{
    (void)region;
    (void)address;
    return SPL_ERR_UNSUPPORTED;
}

#endif
