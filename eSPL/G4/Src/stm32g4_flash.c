#define SPL_LIBRARY_BUILD 1
#include <stddef.h>
#include "stm32g4_flash.h"

/* RM0440 Rev 9: section 3 (category 3), 4 (category 4), 5 (category 2). */

#if defined(FLASH_CR_MER2)
#define FLASH_CR_MER_ALL (FLASH_CR_MER1 | FLASH_CR_MER2)
#else
#define FLASH_CR_MER_ALL FLASH_CR_MER1
#endif
#if defined(FLASH_CR_BKER)
#define FLASH_CR_BKER_BIT FLASH_CR_BKER
#else
#define FLASH_CR_BKER_BIT 0UL
#endif

/* Operation selection bits cleared before a new operation and on lock. */
#define FLASH_CR_OPBITS                                                                            \
    (FLASH_CR_PG | FLASH_CR_PER | FLASH_CR_MER_ALL | FLASH_CR_FSTPG | FLASH_CR_PNB |               \
     FLASH_CR_BKER_BIT)
/* Command bits never copied back by a read-modify-write: STRT/OPTSTRT are
 * set-only and clear with BSY; OBL_LAUNCH=1 forces an option reload. */
#define FLASH_CR_CMDBITS (FLASH_CR_STRT | FLASH_CR_OPTSTRT | FLASH_CR_OBL_LAUNCH)

/* RM0440 Table 7: in dual-bank mode bank 2 starts at 0x0804 0000. */
#define FLASH_BANK2_BASE 0x08040000UL

static SPL_Status Flash_ReadGeometry(FLASH_GeometryTypeDef *g)
{
    const uint32_t max_pages = (FLASH_CR_PNB_Msk >> FLASH_CR_PNB_Pos) + 1U;
    uint32_t size_kb = (uint32_t)(*(const volatile uint16_t *)FLASHSIZE_BASE);
    uint32_t banks = 1U;
    uint32_t page_size = 2048U;
    uint32_t unit;

#if defined(FLASH_OPTR_DBANK)
    /* RM0440 3.3.1: DBANK=1 two banks of 2 KB pages, DBANK=0 one bank of 4 KB. */
    if ((FLASH->OPTR & FLASH_OPTR_DBANK) != 0U)
    {
        banks = 2U;
    }
    else
    {
        page_size = 4096U;
    }
#endif
    unit = (page_size * banks) / 1024U;
    if ((size_kb == 0U) || ((size_kb % unit) != 0U) || ((size_kb / unit) > max_pages))
    {
        return SPL_ERR_UNSUPPORTED;
    }

    g->size_kb = size_kb;
    g->banks = banks;
    g->page_size = page_size;
    g->pages_per_bank = size_kb / unit;
    return SPL_OK;
}

static uint8_t Flash_AddressValid(const FLASH_GeometryTypeDef *g, uint32_t address)
{
    const uint32_t bank_bytes = (g->size_kb * 1024U) / g->banks;
    uint8_t valid = ((address >= FLASH_BASE) && ((address - FLASH_BASE) < bank_bytes)) ? 1U : 0U;

    if ((valid == 0U) && (g->banks == 2U) && (address >= FLASH_BANK2_BASE) &&
        ((address - FLASH_BANK2_BASE) < bank_bytes))
    {
        valid = 1U;
    }
    return valid;
}

/* Common state check for operations that write FLASH_CR. */
static SPL_Status Flash_ReadyForOperation(void)
{
    if ((FLASH->CR & FLASH_CR_LOCK) != 0U)
    {
        return SPL_ERR_STATE;
    }
#if defined(FLASH_OPTR_DBANK)
    /* ERRATA ES0430 2.2.2: a dual-bank write with DCEN = 1 can corrupt the data cache:
     * caller clears DCEN first (no silent auto-disable). Checked before BSY: polling
     * would not help. Applied to every DBANK SKU (G414: no errata sheet available). */
    if (((FLASH->OPTR & FLASH_OPTR_DBANK) != 0U) && ((FLASH->ACR & FLASH_ACR_DCEN) != 0U))
    {
        return SPL_ERR_STATE;
    }
#endif
    /* A CR write while BSY=1 stalls the AHB bus (RM0440 3.3.6). */
    if ((FLASH->SR & FLASH_SR_BSY) != 0U)
    {
        return SPL_BUSY;
    }
    /* Pending programming errors would make the next operation fail with
     * PGSERR (RM0440 3.3.6 step 2): caller must diagnose and clear first. */
    if ((FLASH->SR & FLASH_SR_ERRORS) != 0U)
    {
        return SPL_ERR_HW;
    }
    return SPL_OK;
}

/* Table 9/19/29: maximum HCLK in MHz per wait state, same for all categories. */
static const uint8_t flash_max_mhz[][5] = {
    {34U, 68U, 102U, 136U, 170U}, /* range 1 boost */
    {30U, 60U, 90U, 120U, 150U},  /* range 1 normal */
    {12U, 24U, 26U, 0U, 0U}       /* range 2 */
};

/* Compile-time check (CCL-2 YAP-03): one row per FLASH_RangeTypeDef value 0..FLASH_RANGE2 (the
 * bound 2 of the SAFE check in FLASH_CalcLatency), and the 5 wait states 0..4 fit ACR.LATENCY. */
typedef char Flash_LatencyTableCheck
    [(((sizeof(flash_max_mhz) / sizeof(flash_max_mhz[0])) == ((uint32_t)FLASH_RANGE2 + 1U)) &&
      ((uint32_t)FLASH_RANGE2 == 2U) && ((uint32_t)FLASH_RANGE1_BOOST == 0U) &&
      ((sizeof(flash_max_mhz[0]) / sizeof(flash_max_mhz[0][0])) == 5U) &&
      ((5U - 1U) <= (FLASH_ACR_LATENCY_Msk >> FLASH_ACR_LATENCY_Pos)))
         ? 1
         : -1];

SPL_Status FLASH_CalcLatency(uint32_t hclk_hz, FLASH_RangeTypeDef range, uint32_t *latency)
{
    uint32_t ws;
    uint32_t row;

    /* SAFE: Prevent invalid table index and output pointer; incorrect latency can destabilize core. */
    SPL_REQUIRE_SAFE(!((latency == NULL) || (hclk_hz == 0U) || ((uint32_t)range > 2U)), SPL_ERR_PARAM);

    row = (uint32_t)range;
    for (ws = 0U; ws < 5U; ++ws)
    {
        const uint32_t max_hz = (uint32_t)flash_max_mhz[row][ws] * 1000000UL;
        if ((max_hz != 0U) && (hclk_hz <= max_hz))
        {
            *latency = ws;
            return SPL_OK;
        }
    }
    return SPL_ERR_PARAM;
}

SPL_Status FLASH_SetLatency(uint32_t latency)
{
    /* SAFE: Prevent illegal LATENCY encoding. */
    SPL_REQUIRE_SAFE(!(latency > (FLASH_ACR_LATENCY_Msk >> FLASH_ACR_LATENCY_Pos)), SPL_ERR_PARAM);

    FLASH->ACR = (FLASH->ACR & ~FLASH_ACR_LATENCY) | (latency << FLASH_ACR_LATENCY_Pos);

    /* RM0440 3.3.4 step 2: check the new value is taken into account. */
    if (((FLASH->ACR & FLASH_ACR_LATENCY) >> FLASH_ACR_LATENCY_Pos) != latency)
    {
        return SPL_ERR_HW;
    }
    return SPL_OK;
}

SPL_Status FLASH_ConfigAccelerator(uint8_t prefetch, uint8_t icache, uint8_t dcache)
{
    uint32_t acr;

    /* SAFE: Prevent invalid accelerator control values. */
    SPL_REQUIRE_SAFE(!((prefetch > 1U) || (icache > 1U) || (dcache > 1U)), SPL_ERR_PARAM);

    acr = FLASH->ACR & ~(FLASH_ACR_PRFTEN | FLASH_ACR_ICEN | FLASH_ACR_DCEN);
    if (prefetch != 0U)
    {
        acr |= FLASH_ACR_PRFTEN;
    }
    if (icache != 0U)
    {
        acr |= FLASH_ACR_ICEN;
    }
    if (dcache != 0U)
    {
        acr |= FLASH_ACR_DCEN;
    }
    FLASH->ACR = acr;
    return SPL_OK;
}

SPL_Status FLASH_ResetCaches(void)
{
    /* RM0440 3.7.1: ICRST/DCRST can be written only with the cache disabled. */
    if ((FLASH->ACR & (FLASH_ACR_ICEN | FLASH_ACR_DCEN)) != 0U)
    {
        return SPL_ERR_STATE;
    }

    /* The reset bits are plain rw: assert, then release. */
    FLASH->ACR = FLASH->ACR | FLASH_ACR_ICRST | FLASH_ACR_DCRST;
    FLASH->ACR = FLASH->ACR & ~(FLASH_ACR_ICRST | FLASH_ACR_DCRST);
    return SPL_OK;
}

SPL_Status FLASH_GetGeometry(FLASH_GeometryTypeDef *geometry)
{
    FLASH_GeometryTypeDef g;
    SPL_Status status;

    /* SAFE: Reject invalid output pointer before writing geometry. */
    SPL_REQUIRE_SAFE(!(geometry == NULL), SPL_ERR_PARAM);
    status = Flash_ReadGeometry(&g);
    if (status == SPL_OK)
    {
        *geometry = g;
    }
    return status;
}

SPL_Status FLASH_GetOptionRegister(uint32_t *optr)
{
    /* SAFE: Reject invalid output pointer before reading option bytes. */
    SPL_REQUIRE_SAFE(!(optr == NULL), SPL_ERR_PARAM);
    *optr = FLASH->OPTR;
    return SPL_OK;
}

SPL_Status FLASH_Unlock(void)
{
    if ((FLASH->CR & FLASH_CR_LOCK) == 0U)
    {
        return SPL_OK;
    }

    /* RM0440 3.3.6: any wrong sequence locks FLASH_CR until the next reset. */
    FLASH->KEYR = FLASH_KEY1;
    FLASH->KEYR = FLASH_KEY2;

    if ((FLASH->CR & FLASH_CR_LOCK) != 0U)
    {
        return SPL_ERR_HW;
    }
    return SPL_OK;
}

SPL_Status FLASH_Lock(void)
{
    if ((FLASH->CR & FLASH_CR_LOCK) != 0U)
    {
        return SPL_OK;
    }
    if ((FLASH->SR & FLASH_SR_BSY) != 0U)
    {
        return SPL_BUSY;
    }

    FLASH->CR = (FLASH->CR & ~(FLASH_CR_OPBITS | FLASH_CR_CMDBITS)) | FLASH_CR_LOCK;
    return SPL_OK;
}

SPL_Status FLASH_ErasePageStart(uint32_t bank, uint32_t page)
{
    FLASH_GeometryTypeDef g;
    SPL_Status status;
    uint32_t cr;

    /* SAFE: Prevent erasure of unintended page or bank. */
    SPL_REQUIRE_SAFE(!((bank != FLASH_BANK_1) && (bank != FLASH_BANK_2)), SPL_ERR_PARAM);
    status = Flash_ReadGeometry(&g);
    if (status != SPL_OK)
    {
        return status;
    }
#if defined(FLASH_OPTR_DBANK)
    if ((bank == FLASH_BANK_2) && (g.banks != 2U))
    {
        return SPL_ERR_STATE; /* DBANK=0: option-byte dependent */
    }
#else
    if (bank == FLASH_BANK_2)
    {
        return SPL_ERR_UNSUPPORTED;
    }
#endif
    /* SAFE: Prevent erasure of unintended page or bank. */
    SPL_REQUIRE_SAFE(!(page >= g.pages_per_bank), SPL_ERR_PARAM);
    status = Flash_ReadyForOperation();
    if (status != SPL_OK)
    {
        return status;
    }

    /* RM0440 3.3.6 page erase: PER + PNB (+ BKER in dual-bank mode), then STRT.
     * BKER stays 0 in single-bank mode. */
    cr = (FLASH->CR & ~(FLASH_CR_OPBITS | FLASH_CR_CMDBITS)) | FLASH_CR_PER |
         (page << FLASH_CR_PNB_Pos);
#if defined(FLASH_OPTR_DBANK)
    /* Single-bank SKUs already rejected bank 2 (UNSUPPORTED) above. */
    if (bank == FLASH_BANK_2)
    {
        cr |= FLASH_CR_BKER_BIT;
    }
#endif
    FLASH->CR = cr;
    FLASH->CR = FLASH->CR | FLASH_CR_STRT;
    return SPL_OK;
}

SPL_Status FLASH_ProgramDoubleWordStart(uint32_t address, uint64_t data)
{
    FLASH_GeometryTypeDef g;
    SPL_Status status;

    /* SAFE: Prevent writes to an unintended flash address. */
    SPL_REQUIRE_SAFE(!((address & 7U) != 0U), SPL_ERR_PARAM);
    status = Flash_ReadGeometry(&g);
    if (status != SPL_OK)
    {
        return status;
    }
    /* SAFE: Prevent writes to an unintended flash address. */
    SPL_REQUIRE_SAFE(!(Flash_AddressValid(&g, address) == 0U), SPL_ERR_PARAM);
    status = Flash_ReadyForOperation();
    if (status != SPL_OK)
    {
        return status;
    }
    /* RM0440 3.3.7: the target must be erased unless data == 0 (else PROGERR).
     * Checked before any write so a non-erased target leaves no error flag. */
    if ((data != 0U) && ((*(const volatile uint32_t *)(uintptr_t)address != 0xFFFFFFFFUL) ||
                         (*(const volatile uint32_t *)(uintptr_t)(address + 4U) != 0xFFFFFFFFUL)))
    {
        return SPL_ERR_STATE;
    }

    /* RM0440 3.3.7 standard programming: PG=1, first word at the double-word
     * aligned address, then the second word; the second write launches it. */
    FLASH->CR = (FLASH->CR & ~(FLASH_CR_OPBITS | FLASH_CR_CMDBITS)) | FLASH_CR_PG;
    *(volatile uint32_t *)(uintptr_t)address = (uint32_t)data;
    *(volatile uint32_t *)(uintptr_t)(address + 4U) = (uint32_t)(data >> 32U);
    return SPL_OK;
}

SPL_Status FLASH_PollOperation(uint8_t *eop)
{
    const uint32_t sr = FLASH->SR;

    if ((sr & FLASH_SR_BSY) != 0U)
    {
        return SPL_BUSY;
    }
    if ((sr & FLASH_SR_ERRORS) != 0U)
    {
        return SPL_ERR_HW;
    }
    if (eop != NULL)
    {
        /* EOP is set only when EOPIE=1 (RM0440 3.7.5). */
        *eop = ((sr & FLASH_SR_EOP) != 0U) ? 1U : 0U;
    }
    return SPL_OK;
}

SPL_Status FLASH_EndOperation(void)
{
    if ((FLASH->CR & FLASH_CR_LOCK) != 0U)
    {
        return SPL_ERR_STATE;
    }
    if ((FLASH->SR & FLASH_SR_BSY) != 0U)
    {
        return SPL_BUSY;
    }

    FLASH->CR = FLASH->CR & ~(FLASH_CR_OPBITS | FLASH_CR_CMDBITS);
    return SPL_OK;
}

SPL_Status FLASH_GetFlags(uint32_t *flags)
{
    /* SAFE: Reject invalid output pointer. */
    SPL_REQUIRE_SAFE(!(flags == NULL), SPL_ERR_PARAM);
    *flags = FLASH->SR;
    return SPL_OK;
}

SPL_Status FLASH_ClearFlags(uint32_t mask)
{
    /* SAFE: Prevent clearing undocumented status flags. */
    SPL_REQUIRE_SAFE(!((mask == 0U) || ((mask & ~FLASH_SR_CLEARABLE) != 0U)), SPL_ERR_PARAM);

    /* SR flags are rc_w1: write the mask only, never read-modify-write. */
    FLASH->SR = mask;
    /* Read back: the clear reaches the peripheral before return (METHOD 1.7, issue #23). */
    (void)FLASH->SR;
    return SPL_OK;
}

#if defined(FLASH_ECCR_ECCD2)
#define FLASH_ECC_DETECTED (FLASH_ECCR_ECCD | FLASH_ECCR_ECCD2)
#define FLASH_ECC_CORRECTED (FLASH_ECCR_ECCC | FLASH_ECCR_ECCC2)
#else
#define FLASH_ECC_DETECTED FLASH_ECCR_ECCD
#define FLASH_ECC_CORRECTED FLASH_ECCR_ECCC
#endif

SPL_Status FLASH_GetEcc(FLASH_EccTypeDef *ecc)
{
    uint32_t eccr;

    /* SAFE: Reject invalid ECC output pointer. */
    SPL_REQUIRE_SAFE(!(ecc == (FLASH_EccTypeDef *)0), SPL_ERR_PARAM);
    eccr = FLASH->ECCR; /* single read: the fields belong to the same event */
    ecc->address = eccr & FLASH_ECCR_ADDR_ECC;
    ecc->detected = (uint8_t)(((eccr & FLASH_ECC_DETECTED) != 0U) ? 1U : 0U);
    ecc->corrected = (uint8_t)(((eccr & FLASH_ECC_CORRECTED) != 0U) ? 1U : 0U);
    ecc->system_flash = (uint8_t)(((eccr & FLASH_ECCR_SYSF_ECC) != 0U) ? 1U : 0U);
#if defined(FLASH_ECCR_BK_ECC)
    ecc->bank = (uint8_t)(((eccr & FLASH_ECCR_BK_ECC) != 0U) ? FLASH_BANK_2 : FLASH_BANK_1);
#else
    ecc->bank = (uint8_t)FLASH_BANK_1;
#endif
    return SPL_OK;
}

SPL_Status FLASH_ClearEcc(void)
{
    /* ECC flags are rc_w1: write 1 to them, keep the only rw bit (ECCCIE),
     * write 0 to reserved and read-only fields. */
    FLASH->ECCR = (FLASH->ECCR & FLASH_ECCR_ECCIE) | FLASH_ECC_DETECTED | FLASH_ECC_CORRECTED;
    /* Read back: the clear reaches the peripheral before return (METHOD 1.7, issue #23). */
    (void)FLASH->ECCR;
    return SPL_OK;
}
