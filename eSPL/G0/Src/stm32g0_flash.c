/* Ported from STM32G4 SPL Src/stm32g4_flash.c (G4 v0.13.2) and adapted to RM0444 Rev 6
 * section 3: busy = BSY1 | BSY2 | CFGBSY, no DCEN, geometry Table 9..12, latency Table 13,
 * ES0548 2.2.3 and 2.2.10. */
/* Kutuphane birimi: spl_check.h karisma korumasi referansini bu birim de uretir; uretmeyen tek
 * birim stm32g0_check.c (SPL_CHECK_LEVEL_DEFINER). BULGU-02, CHECK_LEVEL.md 3.2. */
#define SPL_LIBRARY_BUILD 1
#include <stddef.h>
#include "stm32g0_flash.h"
#include "stm32g0_sku.h"

/* Operation selection bits cleared before a new operation and on lock. */
#if SPL_G0_HAS_FLASH_DUAL_BANK
#define FLASH_CR_BANK2_OPBITS (FLASH_CR_MER2 | FLASH_CR_BKER)
#else
#define FLASH_CR_BANK2_OPBITS 0U
#endif
#define FLASH_CR_OPBITS                                                                            \
    (FLASH_CR_PG | FLASH_CR_PER | FLASH_CR_MER1 | FLASH_CR_FSTPG | FLASH_CR_PNB |                  \
     FLASH_CR_BANK2_OPBITS)
/* Never copied back by read-modify-write: STRT/OPTSTRT are set-only and clear with BSY;
 * OBL_LAUNCH = 1 reloads the option bytes (system reset). */
#define FLASH_CR_CMDBITS (FLASH_CR_STRT | FLASH_CR_OPTSTRT | FLASH_CR_OBL_LAUNCH)

#define FLASH_SIZE_KB_REG (*(const volatile uint16_t *)FLASHSIZE_BASE)

/* Denetim seviyesi (A4 dalga 1, Docs/CHECK_LEVEL.md 5.4 FLASH tablosu): SPL_REQUIRE = normal sinif
 * (yalniz FULL), SPL_REQUIRE_SAFE = SAFE sinif (FULL ve MIX). Her seviyede kalanlar: SPL_BUSY,
 * bekleyen hata (ERR_HW), geri okuma, geri alinamaz islem anahtarlari (FLASH_KEY_*), yapisal
 * dallar (#if, SPL_ENABLE_IRREVERSIBLE), FLASH_CalcLatency'nin cevabi. Program / silme / koruma
 * API'lerinde geometri yalniz SAFE adres / bank denetimi icin okunur: FAST'ta okunmaz.
 * Butun makrolar ilk MMIO yazimindan once (METHOD 1.3). */

static SPL_Status Flash_ReadGeometry(FLASH_GeometryTypeDef *g);

#if SPL_CHECK_LEVEL <= SPL_CHECK_MIX
/* Bank 2 request on a device or configuration without bank 2: same rule as page erase. SAFE
 * denetimi (BKER / MER2 / WRP2 yok sayilir, bank 1 etkilenir); FAST'ta derlenmez. */
static SPL_Status Flash_Bank2Status(const FLASH_GeometryTypeDef *g)
{
    if (g->banks == 2U)
    {
        return SPL_OK;
    }
    return ((SPL_G0_HAS_FLASH_DUAL_BANK != 0) && (g->size_kb == 256U)) ? SPL_ERR_STATE
                                                                       : SPL_ERR_UNSUPPORTED;
}
#endif

static SPL_Status Flash_ReadGeometry(FLASH_GeometryTypeDef *g)
{
    const uint32_t size_kb = (uint32_t)FLASH_SIZE_KB_REG;
    const uint32_t max_pages = (FLASH_CR_PNB_Msk >> FLASH_CR_PNB_Pos) + 1U; /* G0B1: 128 */
    uint32_t banks = 1U;
#if SPL_G0_HAS_FLASH_DUAL_BANK
    const uint32_t optr = FLASH->OPTR;

    /* RM0444 Table 12: 512 KB is always dual-bank, 256 KB follows DUAL_BANK, <= 128 KB single. */
    if ((size_kb == 512U) || ((size_kb == 256U) && ((optr & FLASH_OPTR_DUAL_BANK) != 0U)))
    {
        banks = 2U;
    }
#endif
    /* v0.8: single-bank SKUs (G03x..G08x) never report two banks; PNB width is per SKU. */
    if ((size_kb == 0U) || ((size_kb % (2U * banks)) != 0U) || (size_kb > 512U) ||
        ((size_kb / (2U * banks)) > max_pages))
    {
        return SPL_ERR_UNSUPPORTED;
    }
    g->size_kb = size_kb;
    g->banks = banks;
    g->pages_per_bank = size_kb / (2U * banks);
    g->bank2_base = (banks == 2U) ? (FLASH_BASE + ((size_kb * 1024U) / 2U)) : 0U;
#if SPL_G0_HAS_FLASH_DUAL_BANK
    g->swapped = ((banks == 2U) && ((optr & FLASH_OPTR_nSWAP_BANK) == 0U)) ? 1U : 0U;
#else
    g->swapped = 0U;
#endif
    return SPL_OK;
}

/* Every CR-writing operation: locked -> STATE, busy -> BUSY (a CR write with CFGBSY = 1 is a
 * HardFault on G0, RM0444 3.7.5), pending errors -> ERR_HW (else PGSERR, RM0444 3.3.8). */
static SPL_Status Flash_ReadyForOperation(void)
{
    /* SAFE: kilitliyken CR yazimi yok sayilir ve OK donerdi; programlama yazimi bus hatasi. */
    SPL_REQUIRE_SAFE((FLASH->CR & FLASH_CR_LOCK) == 0U, SPL_ERR_STATE);
    if ((FLASH->SR & FLASH_SR_ANYBUSY) != 0U)
    {
        return SPL_BUSY;
    }
    if ((FLASH->SR & FLASH_SR_ERRORS) != 0U)
    {
        return SPL_ERR_HW;
    }
    return SPL_OK;
}

SPL_Status FLASH_CalcLatency(uint32_t hclk_hz, FLASH_RangeTypeDef range, uint32_t *latency)
{
    static const uint8_t max_mhz[2][3] = {{24U, 48U, 64U}, {8U, 16U, 0U}};
    uint32_t ws;

    SPL_REQUIRE_SAFE(latency != NULL, SPL_ERR_PARAM);
    SPL_REQUIRE(hclk_hz != 0U, SPL_ERR_PARAM); /* normal */
    /* SAFE: range > Range 2 tablo disi okur, yanlis bekleme durumu doner. */
    SPL_REQUIRE_SAFE((uint32_t)range <= (uint32_t)FLASH_RANGE2, SPL_ERR_PARAM);
    for (ws = 0U; ws < 3U; ++ws)
    {
        const uint32_t max_hz = (uint32_t)max_mhz[(uint32_t)range][ws] * 1000000UL;
        if ((max_hz != 0U) && (hclk_hz <= max_hz))
        {
            *latency = ws;
            return SPL_OK;
        }
    }
    return SPL_ERR_PARAM; /* result: bu frekans bu aralikta olmaz (her seviyede) */
}

SPL_Status FLASH_SetLatency(uint32_t latency)
{
    /* SAFE: LATENCY'ye ayrilmis kod ya da ACR'de tasma. */
    SPL_REQUIRE_SAFE(latency <= 2U, SPL_ERR_PARAM);
    FLASH->ACR = (FLASH->ACR & ~FLASH_ACR_LATENCY_Msk) | (latency << FLASH_ACR_LATENCY_Pos);
    /* RM0444 3.3.4: the new value is effective when it reads back. */
    if (((FLASH->ACR & FLASH_ACR_LATENCY_Msk) >> FLASH_ACR_LATENCY_Pos) != latency)
    {
        return SPL_ERR_HW;
    }
    return SPL_OK;
}

SPL_Status FLASH_GetLatency(uint32_t *latency)
{
    SPL_REQUIRE_SAFE(latency != NULL, SPL_ERR_PARAM);
    *latency = (FLASH->ACR & FLASH_ACR_LATENCY_Msk) >> FLASH_ACR_LATENCY_Pos;
    return SPL_OK;
}

SPL_Status FLASH_ConfigAccelerator(uint8_t prefetch, uint8_t icache, uint8_t code_in_one_bank)
{
    uint32_t acr;

    SPL_REQUIRE((prefetch <= 1U) && (icache <= 1U) && (code_in_one_bank <= 1U),
                SPL_ERR_PARAM); /* normal: 1 gibi davranir */
#if SPL_CHECK_LEVEL <= SPL_CHECK_MIX
    {
        FLASH_GeometryTypeDef g;
        const SPL_Status st = Flash_ReadGeometry(&g);

        if (st != SPL_OK)
        {
            return st;
        }
        /* ERRATA ES0548 2.2.10: prefetch across banks may fetch wrong code; refuse unless the
         * caller states that execution never crosses the banks (no silent prefetch disable).
         * SAFE (errata); geometri yalniz bu denetim icin okunur. */
        SPL_REQUIRE_SAFE((prefetch == 0U) || (g.banks != 2U) || (code_in_one_bank != 0U),
                         SPL_ERR_STATE);
    }
#else
    (void)code_in_one_bank;
#endif
    acr = FLASH->ACR & ~(FLASH_ACR_PRFTEN | FLASH_ACR_ICEN | FLASH_ACR_ICRST);
    if (prefetch != 0U)
    {
        acr |= FLASH_ACR_PRFTEN;
    }
    if (icache != 0U)
    {
        acr |= FLASH_ACR_ICEN;
    }
    FLASH->ACR = acr;
    return SPL_OK;
}

SPL_Status FLASH_ResetICache(void)
{
    /* SAFE: ICRST acik onbellekte tanimsiz / yok sayilir. */
    SPL_REQUIRE_SAFE((FLASH->ACR & FLASH_ACR_ICEN) == 0U, SPL_ERR_STATE);
    FLASH->ACR = FLASH->ACR | FLASH_ACR_ICRST;
    FLASH->ACR = FLASH->ACR & ~FLASH_ACR_ICRST;
    return SPL_OK;
}

SPL_Status FLASH_GetGeometry(FLASH_GeometryTypeDef *geometry)
{
    FLASH_GeometryTypeDef g;
    SPL_Status st;

    SPL_REQUIRE_SAFE(geometry != NULL, SPL_ERR_PARAM);
    st = Flash_ReadGeometry(&g);
    if (st == SPL_OK)
    {
        *geometry = g;
    }
    return st;
}

SPL_Status FLASH_GetOptionRegister(uint32_t *optr)
{
    SPL_REQUIRE_SAFE(optr != NULL, SPL_ERR_PARAM);
    *optr = FLASH->OPTR;
    return SPL_OK;
}

SPL_Status FLASH_Unlock(void)
{
    if ((FLASH->CR & FLASH_CR_LOCK) == 0U)
    {
        return SPL_OK;
    }
    if ((FLASH->SR & FLASH_SR_ANYBUSY) != 0U)
    {
        return SPL_BUSY;
    }
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
    if ((FLASH->SR & FLASH_SR_ANYBUSY) != 0U)
    {
        return SPL_BUSY;
    }
    FLASH->CR = (FLASH->CR & ~(FLASH_CR_OPBITS | FLASH_CR_CMDBITS)) | FLASH_CR_LOCK;
    return SPL_OK;
}

SPL_Status FLASH_ErasePageStart(uint32_t bank, uint32_t page)
{
    SPL_Status st;
    uint32_t cr;

    /* SAFE: baska bank / sayfa silinir ya da PNB tasar (CHECK_LEVEL.md 5.3). */
    SPL_REQUIRE_SAFE((bank == FLASH_BANK_1) || (bank == FLASH_BANK_2), SPL_ERR_PARAM);
#if SPL_CHECK_LEVEL <= SPL_CHECK_MIX
    {
        FLASH_GeometryTypeDef g;

        st = Flash_ReadGeometry(&g);
        if (st != SPL_OK)
        {
            return st;
        }
        /* SAFE: bank 2 yoksa BKER yok sayilir, bank 1'deki ayni sayfa silinir. 256 KB with
         * DUAL_BANK = 0 -> STATE (option byte); single-bank device -> UNSUPPORTED. */
        if (bank == FLASH_BANK_2)
        {
            st = Flash_Bank2Status(&g);
            if (st != SPL_OK)
            {
                return st;
            }
        }
        SPL_REQUIRE_SAFE(page < g.pages_per_bank, SPL_ERR_PARAM);
    }
#endif
    st = Flash_ReadyForOperation();
    if (st != SPL_OK)
    {
        return st;
    }
    /* RM0444 3.3.7: PER + PNB (+ BKER for physical bank 2), then STRT. */
    cr = (FLASH->CR & ~(FLASH_CR_OPBITS | FLASH_CR_CMDBITS)) | FLASH_CR_PER |
         (page << FLASH_CR_PNB_Pos);
#if SPL_G0_HAS_FLASH_DUAL_BANK
    if (bank == FLASH_BANK_2)
    {
        cr |= FLASH_CR_BKER;
    }
#endif
    FLASH->CR = cr;
    FLASH->CR = cr | FLASH_CR_STRT;
    return SPL_OK;
}

SPL_Status FLASH_ProgramDoubleWordStart(uint32_t address, uint64_t data)
{
    SPL_Status st;

    SPL_REQUIRE_SAFE((address & 7U) == 0U, SPL_ERR_PARAM); /* SAFE: yanlis adres programlanir */
    /* ERRATA ES0548 2.2.3: an all-ones program can never be overwritten by zero later. SAFE. */
    SPL_REQUIRE_SAFE(data != 0xFFFFFFFFFFFFFFFFULL, SPL_ERR_PARAM);
#if SPL_CHECK_LEVEL <= SPL_CHECK_MIX
    {
        FLASH_GeometryTypeDef g;

        st = Flash_ReadGeometry(&g);
        if (st != SPL_OK)
        {
            return st;
        }
        /* Logical addresses: the hardware routes to the physical bank (RM0444 3.3.2). SAFE:
         * ana bellek disi adres. */
        SPL_REQUIRE_SAFE((address >= FLASH_BASE) && ((address - FLASH_BASE) < (g.size_kb * 1024U)),
                         SPL_ERR_PARAM);
    }
#endif
    st = Flash_ReadyForOperation();
    if (st != SPL_OK)
    {
        return st;
    }
    /* v0.3 (G4 v0.13.3 FL-21 backport): a non-erased target accepts only data == 0 (RM0444 3.3.8);
     * refuse before PG is set so no PROGERR is left behind. normal (K-A4-2): denetim olmasa donanim
     * PROGERR ile reddeder, hicbir sey yazilmaz, FLASH_PollOperation ERR_HW doner. */
    SPL_REQUIRE((data == 0U) ||
                    ((*(const volatile uint32_t *)(uintptr_t)address == 0xFFFFFFFFUL) &&
                     (*(const volatile uint32_t *)(uintptr_t)(address + 4U) == 0xFFFFFFFFUL)),
                SPL_ERR_STATE);
    /* RM0444 3.3.8: PG = 1, first word at the aligned address, then the second word. Word
     * accesses only (a byte or half-word write sets SIZERR); both halves are 32-bit stores. */
    FLASH->CR = (FLASH->CR & ~(FLASH_CR_OPBITS | FLASH_CR_CMDBITS)) | FLASH_CR_PG;
    *(volatile uint32_t *)(uintptr_t)address = (uint32_t)data;
    *(volatile uint32_t *)(uintptr_t)(address + 4U) = (uint32_t)(data >> 32U);
    return SPL_OK;
}

SPL_Status FLASH_PollOperation(uint8_t *eop)
{
    const uint32_t sr = FLASH->SR;

    if ((sr & FLASH_SR_ANYBUSY) != 0U)
    {
        return SPL_BUSY;
    }
    if ((sr & FLASH_SR_ERRORS) != 0U)
    {
        return SPL_ERR_HW;
    }
    if (eop != NULL)
    {
        *eop = ((sr & FLASH_SR_EOP) != 0U) ? 1U : 0U;
    }
    return SPL_OK;
}

SPL_Status FLASH_EndOperation(void)
{
    /* normal: kilitliyken islem biti kurulamaz, yazim etkisiz. */
    SPL_REQUIRE((FLASH->CR & FLASH_CR_LOCK) == 0U, SPL_ERR_STATE);
    if ((FLASH->SR & FLASH_SR_ANYBUSY) != 0U)
    {
        return SPL_BUSY;
    }
    FLASH->CR = FLASH->CR & ~(FLASH_CR_OPBITS | FLASH_CR_CMDBITS);
    return SPL_OK;
}

#define FLASH_ECC_EVENTS (FLASH_ECCR_ECCD | FLASH_ECCR_ECCC)

SPL_Status FLASH_GetEcc(FLASH_EccTypeDef *ecc)
{
    uint32_t eccr;
    uint8_t bank = (uint8_t)FLASH_BANK_1;

    SPL_REQUIRE_SAFE(ecc != NULL, SPL_ERR_PARAM);
    eccr = FLASH->ECCR; /* single read: the fields belong to the same event */
#if SPL_G0_HAS_FLASH_DUAL_BANK
    if ((eccr & FLASH_ECC_EVENTS) == 0U)
    {
        const uint32_t ecc2r = FLASH->ECC2R;
        if ((ecc2r & (FLASH_ECC2R_ECCD | FLASH_ECC2R_ECCC)) != 0U)
        {
            eccr = ecc2r; /* same bit layout as ECCR */
            bank = (uint8_t)FLASH_BANK_2;
        }
    }
#endif
    ecc->address = eccr & FLASH_ECCR_ADDR_ECC;
    ecc->detected = (uint8_t)(((eccr & FLASH_ECCR_ECCD) != 0U) ? 1U : 0U);
    ecc->corrected = (uint8_t)(((eccr & FLASH_ECCR_ECCC) != 0U) ? 1U : 0U);
    ecc->system_flash = (uint8_t)(((eccr & FLASH_ECCR_SYSF_ECC) != 0U) ? 1U : 0U);
    ecc->bank = bank;
    return SPL_OK;
}

SPL_Status FLASH_ClearEcc(void)
{
    /* ECCD / ECCC are rc_w1: write 1 to them, keep the only rw bit (ECCCIE), 0 elsewhere. */
    FLASH->ECCR = (FLASH->ECCR & FLASH_ECCR_ECCCIE) | FLASH_ECC_EVENTS;
#if SPL_G0_HAS_FLASH_DUAL_BANK
    FLASH->ECC2R = (FLASH->ECC2R & FLASH_ECC2R_ECCCIE) | FLASH_ECC2R_ECCD | FLASH_ECC2R_ECCC;
#endif
    /* Read back: the clear reaches the peripheral before return (METHOD 1.7, issue #23). */
    (void)FLASH->ECCR;
    return SPL_OK;
}

SPL_Status FLASH_GetFlags(uint32_t *flags)
{
    SPL_REQUIRE_SAFE(flags != NULL, SPL_ERR_PARAM);
    *flags = FLASH->SR;
    return SPL_OK;
}

SPL_Status FLASH_ClearFlags(uint32_t mask)
{
    /* normal: etkisiz yazim (SR'de komsu birim yok). */
    SPL_REQUIRE(mask != 0U, SPL_ERR_PARAM); /* normal: etkisiz */
    /* SAFE (EDR-003): ayrilmis bite yazim. */
    SPL_REQUIRE_SAFE((mask & ~FLASH_SR_CLEARABLE) == 0U, SPL_ERR_PARAM);
    FLASH->SR = mask; /* rc_w1: write the mask only */
    /* Read back: the clear reaches the peripheral before return (METHOD 1.7, issue #23). */
    (void)FLASH->SR;
    return SPL_OK;
}

/* ---- v0.11: mass erase, fast programming, option bytes, protections, OTP (RM0444 3.3, 3.4) ----
 */

/* RM0444 3.7.4: option unlock keys. Not provided by CMSIS. */
#define FLASH_OPTKEY1 0x08192A3BUL
#define FLASH_OPTKEY2 0x4C5D6E7FUL

#define FLASH_RDP_LEVEL0 0xAAU
#define FLASH_RDP_LEVEL1 0xBBU /* any value other than 0xAA / 0xCC is level 1 */
#define FLASH_RDP_LEVEL2 0xCCU

#define FLASH_ROW_WORDS 64U /* 32 double words = 256 bytes (RM0444 3.3.8) */

/* Option registers are written only with OPTLOCK = 0 and no operation running. */
static SPL_Status Flash_OptionWritable(void)
{
    /* SAFE: OPTLOCK = 1 iken option register yazimi sessizce yok sayilir. */
    SPL_REQUIRE_SAFE((FLASH->CR & FLASH_CR_OPTLOCK) == 0U, SPL_ERR_STATE);
    if ((FLASH->SR & FLASH_SR_ANYBUSY) != 0U)
    {
        return SPL_BUSY;
    }
    return SPL_OK;
}

SPL_Status FLASH_MassEraseStart(uint32_t bank_mask, uint32_t key)
{
    SPL_Status st;
    uint32_t cr;

    if (key != FLASH_KEY_MASS_ERASE)
    {
        return SPL_ERR_PARAM; /* geri alinamaz islem anahtari: her seviyede */
    }
    /* normal: islem biti yok (PGSERR) / fazla bit atilir. */
    SPL_REQUIRE((bank_mask != 0U) && ((bank_mask & ~(FLASH_MASS_BANK1 | FLASH_MASS_BANK2)) == 0U),
                SPL_ERR_PARAM);
#if SPL_CHECK_LEVEL <= SPL_CHECK_MIX
    if ((bank_mask & FLASH_MASS_BANK2) != 0U)
    {
        FLASH_GeometryTypeDef g;

        st = Flash_ReadGeometry(&g);
        if (st != SPL_OK)
        {
            return st;
        }
        st = Flash_Bank2Status(&g); /* SAFE */
        if (st != SPL_OK)
        {
            return st;
        }
    }
#endif
    st = Flash_ReadyForOperation();
    if (st != SPL_OK)
    {
        return st;
    }
    /* RM0444 3.3.7: MER1 (and MER2), then STRT. An active WRP or PCROP area aborts the mass erase
     * with WRPERR (RM0444 3.5.2 / 3.5.3): reported by FLASH_PollOperation as ERR_HW. */
    cr = (FLASH->CR & ~(FLASH_CR_OPBITS | FLASH_CR_CMDBITS));
    if ((bank_mask & FLASH_MASS_BANK1) != 0U)
    {
        cr |= FLASH_CR_MER1;
    }
#if SPL_G0_HAS_FLASH_DUAL_BANK
    if ((bank_mask & FLASH_MASS_BANK2) != 0U)
    {
        cr |= FLASH_CR_MER2;
    }
#endif
    FLASH->CR = cr;
    FLASH->CR = cr | FLASH_CR_STRT;
    return SPL_OK;
}

SPL_Status FLASH_FastProgramRowStart(uint32_t address, const uint32_t *words)
{
    SPL_Status st;
    uint32_t i;

    SPL_REQUIRE_SAFE(words != NULL, SPL_ERR_PARAM); /* SAFE: G0'da NULL sessiz (BULGU-03) */
    /* SAFE: hizasiz satir yanlis adresi programlar. */
    SPL_REQUIRE_SAFE((address & ((FLASH_ROW_WORDS * 4U) - 1U)) == 0U, SPL_ERR_PARAM);
#if SPL_CHECK_LEVEL <= SPL_CHECK_MIX
    /* ERRATA ES0548 2.2.3: no all-ones double word (same rule as the standard program). SAFE. */
    for (i = 0U; i < FLASH_ROW_WORDS; i += 2U)
    {
        SPL_REQUIRE_SAFE((words[i] != 0xFFFFFFFFUL) || (words[i + 1U] != 0xFFFFFFFFUL),
                         SPL_ERR_PARAM);
    }
    {
        FLASH_GeometryTypeDef g;

        st = Flash_ReadGeometry(&g);
        if (st != SPL_OK)
        {
            return st;
        }
        /* Main memory only (RM0444 3.3.8). SAFE. */
        SPL_REQUIRE_SAFE((address >= FLASH_BASE) && ((address - FLASH_BASE) < (g.size_kb * 1024U)),
                         SPL_ERR_PARAM);
    }
#endif
    st = Flash_ReadyForOperation();
    if (st != SPL_OK)
    {
        return st;
    }
#if SPL_CHECK_LEVEL == SPL_CHECK_FULL
    /* Step 1: the row must be erased, else PGSERR; refuse before FSTPG is set. normal (K-A4-2):
     * denetim olmasa donanim PGSERR ile reddeder, FLASH_PollOperation ERR_HW doner. */
    for (i = 0U; i < FLASH_ROW_WORDS; ++i)
    {
        SPL_REQUIRE(*(const volatile uint32_t *)(uintptr_t)(address + (4U * i)) == 0xFFFFFFFFUL,
                    SPL_ERR_STATE);
    }
#endif
    /* Steps 5 and 6: FSTPG, then 32 double words written successively (word stores). */
    FLASH->CR = (FLASH->CR & ~(FLASH_CR_OPBITS | FLASH_CR_CMDBITS)) | FLASH_CR_FSTPG;
    for (i = 0U; i < FLASH_ROW_WORDS; ++i)
    {
        *(volatile uint32_t *)(uintptr_t)(address + (4U * i)) = words[i];
    }
    return SPL_OK;
}

SPL_Status FLASH_OptionUnlock(void)
{
    /* RM0444 3.4.2: FLASH_CR must be unlocked first. SAFE: yanlis sira OPTLOCK'u reset'e kadar
     * kilitleyebilir. */
    SPL_REQUIRE_SAFE((FLASH->CR & FLASH_CR_LOCK) == 0U, SPL_ERR_STATE);
    if ((FLASH->CR & FLASH_CR_OPTLOCK) == 0U)
    {
        return SPL_OK;
    }
    if ((FLASH->SR & FLASH_SR_ANYBUSY) != 0U)
    {
        return SPL_BUSY;
    }
    FLASH->OPTKEYR = FLASH_OPTKEY1;
    FLASH->OPTKEYR = FLASH_OPTKEY2;
    if ((FLASH->CR & FLASH_CR_OPTLOCK) != 0U)
    {
        return SPL_ERR_HW;
    }
    return SPL_OK;
}

SPL_Status FLASH_OptionLock(void)
{
    if ((FLASH->CR & FLASH_CR_OPTLOCK) != 0U)
    {
        return SPL_OK;
    }
    if ((FLASH->SR & FLASH_SR_ANYBUSY) != 0U)
    {
        return SPL_BUSY;
    }
    FLASH->CR = (FLASH->CR & ~FLASH_CR_CMDBITS) | FLASH_CR_OPTLOCK;
    return SPL_OK;
}

/* ERRATA ES0487 2.2.3 (ES0486 / ES0488 / ES0468 / ES0418 / ES0412 2.2.3): with RDP level 1, a boot
 * from main flash selected by the PA14-BOOT0 pin (nBOOT_SEL = 0) is taken as an intrusion and the
 * user code never runs; no workaround. An OPTR image with both -> refused (1). */
#if SPL_CHECK_LEVEL <= SPL_CHECK_MIX
/* SAFE denetimi (kart bir daha kullanici kodunu calistirmaz); FAST'ta derlenmez. Saf. */
static uint8_t Flash_BootPinConflict(uint32_t optr)
{
#if SPL_G0_ERRATA_RDP1_BOOT0_PIN
    const uint32_t rdp = optr & FLASH_OPTR_RDP_Msk;

    return (((rdp != FLASH_RDP_LEVEL0) && (rdp != FLASH_RDP_LEVEL2) &&
             ((optr & FLASH_OPTR_nBOOT_SEL) == 0U))
                ? 1U
                : 0U);
#else
    (void)optr;
    return 0U;
#endif
}
#define FLASH_BOOT_PIN_CONFLICT(optr) (Flash_BootPinConflict(optr) != 0U)
#else
#define FLASH_BOOT_PIN_CONFLICT(optr) (0) /* kapali seviyede degerlendirilmez */
#endif

SPL_Status FLASH_OptionSetUser(uint32_t mask, uint32_t value)
{
    SPL_Status st;

    SPL_REQUIRE(mask != 0U, SPL_ERR_PARAM); /* normal: etkisiz yazim */
    /* SAFE: kullanici bitleri disinda RDP dahil OPTR'nin baska alanlari degisir. */
    SPL_REQUIRE_SAFE(((mask & ~FLASH_OPTR_USER_BITS) == 0U) && ((value & ~mask) == 0U),
                     SPL_ERR_PARAM);
#if defined(FLASH_OPTR_NRST_MODE)
    /* RM0444 3.7.8: NRST_MODE = 00 is reserved. SAFE: reset pini tanimsiz, kart erisilemez. */
    SPL_REQUIRE_SAFE(((mask & FLASH_OPTR_NRST_MODE) == 0U) ||
                         ((value & FLASH_OPTR_NRST_MODE) != 0U),
                     SPL_ERR_PARAM);
#endif
    st = Flash_OptionWritable();
    if (st != SPL_OK)
    {
        return st;
    }
    SPL_REQUIRE_SAFE(!FLASH_BOOT_PIN_CONFLICT((FLASH->OPTR & ~mask) | value), SPL_ERR_STATE);
    FLASH->OPTR = (FLASH->OPTR & ~mask) | value;
    return SPL_OK;
}

SPL_Status FLASH_OptionSetRdp(uint32_t level, uint32_t key)
{
    uint32_t rdp;
    SPL_Status st;

    if (level == 0U)
    {
        /* Level 1 -> 0 is a regression: mass erase of the main memory (RM0444 3.5.1). */
        if (((FLASH->OPTR & FLASH_OPTR_RDP_Msk) != FLASH_RDP_LEVEL0) &&
            (key != FLASH_KEY_RDP_REGRESSION))
        {
            return SPL_ERR_PARAM;
        }
        rdp = FLASH_RDP_LEVEL0;
    }
    else if (level == 1U)
    {
        rdp = FLASH_RDP_LEVEL1;
    }
    else if (level == 2U)
    {
#if SPL_ENABLE_IRREVERSIBLE
        if (key != FLASH_KEY_RDP2)
        {
            return SPL_ERR_PARAM;
        }
        rdp = FLASH_RDP_LEVEL2; /* permanent: no debug, no option change ever again */
#else
        return SPL_ERR_UNSUPPORTED;
#endif
    }
    else
    {
        return SPL_ERR_PARAM;
    }
    st = Flash_OptionWritable();
    if (st != SPL_OK)
    {
        return st;
    }
    SPL_REQUIRE_SAFE(!FLASH_BOOT_PIN_CONFLICT((FLASH->OPTR & ~FLASH_OPTR_RDP_Msk) | rdp),
                     SPL_ERR_STATE);
    FLASH->OPTR = (FLASH->OPTR & ~FLASH_OPTR_RDP_Msk) | rdp;
    return SPL_OK;
}

/* WRP area register: 1A / 1B always, 2A / 2B only with bank 2. */
static SPL_Status Flash_WrpRegister(uint32_t area, volatile uint32_t **reg)
{
    SPL_REQUIRE_SAFE(area <= FLASH_AREA_2B, SPL_ERR_PARAM); /* SAFE: yanlis alan register'i */
#if SPL_CHECK_LEVEL <= SPL_CHECK_MIX
    if (area >= FLASH_AREA_2A)
    {
        FLASH_GeometryTypeDef g;
        SPL_Status st = Flash_ReadGeometry(&g);

        if (st == SPL_OK)
        {
            st = Flash_Bank2Status(&g); /* SAFE */
        }
        if (st != SPL_OK)
        {
            return st;
        }
    }
#endif
    if (area == FLASH_AREA_1A)
    {
        *reg = &FLASH->WRP1AR;
    }
    else if (area == FLASH_AREA_1B)
    {
        *reg = &FLASH->WRP1BR;
    }
#if SPL_G0_HAS_FLASH_DUAL_BANK
    else if (area == FLASH_AREA_2A)
    {
        *reg = &FLASH->WRP2AR;
    }
    else
    {
        *reg = &FLASH->WRP2BR;
    }
#else
    else
    {
        return SPL_ERR_UNSUPPORTED; /* FULL / MIX: Flash_Bank2Status refuses first; FAST: yapisal */
    }
#endif
    return SPL_OK;
}

SPL_Status FLASH_OptionSetWrp(uint32_t area, uint32_t start_page, uint32_t end_page)
{
    volatile uint32_t *reg = NULL;
    SPL_Status st;
    const uint32_t width = FLASH_WRP1AR_WRP1A_STRT_Msk >> FLASH_WRP1AR_WRP1A_STRT_Pos;

    /* SAFE: END alanina tasma, yanlis sayfa araligi korunur. */
    SPL_REQUIRE_SAFE((start_page <= width) && (end_page <= width), SPL_ERR_PARAM);
    st = Flash_WrpRegister(area, &reg);
    if (st != SPL_OK)
    {
        return st;
    }
    st = Flash_OptionWritable();
    if (st != SPL_OK)
    {
        return st;
    }
    /* RM0444 3.5.3: pages start .. end protected; start > end disables the area. */
    *reg = (start_page << FLASH_WRP1AR_WRP1A_STRT_Pos) | (end_page << FLASH_WRP1AR_WRP1A_END_Pos);
    return SPL_OK;
}

#if SPL_ENABLE_IRREVERSIBLE && defined(FLASH_PCROP1ASR_PCROP1A_STRT)
static SPL_Status Flash_PcropRegisters(uint32_t area, volatile uint32_t **sr,
                                       volatile uint32_t **er)
{
    SPL_REQUIRE_SAFE(area <= FLASH_AREA_2B, SPL_ERR_PARAM); /* SAFE: yanlis alan register'i */
#if SPL_CHECK_LEVEL <= SPL_CHECK_MIX
    if (area >= FLASH_AREA_2A)
    {
        FLASH_GeometryTypeDef g;
        SPL_Status st = Flash_ReadGeometry(&g);

        if (st == SPL_OK)
        {
            st = Flash_Bank2Status(&g); /* SAFE */
        }
        if (st != SPL_OK)
        {
            return st;
        }
    }
#endif
    if (area == FLASH_AREA_1A)
    {
        *sr = &FLASH->PCROP1ASR;
        *er = &FLASH->PCROP1AER;
    }
    else if (area == FLASH_AREA_1B)
    {
        *sr = &FLASH->PCROP1BSR;
        *er = &FLASH->PCROP1BER;
    }
#if SPL_G0_HAS_FLASH_DUAL_BANK
    else if (area == FLASH_AREA_2A)
    {
        *sr = &FLASH->PCROP2ASR;
        *er = &FLASH->PCROP2AER;
    }
    else
    {
        *sr = &FLASH->PCROP2BSR;
        *er = &FLASH->PCROP2BER;
    }
#else
    else
    {
        return SPL_ERR_UNSUPPORTED;
    }
#endif
    return SPL_OK;
}
#endif

SPL_Status FLASH_OptionSetPcrop(uint32_t area, uint32_t start_subpage, uint32_t end_subpage,
                                uint32_t key)
{
#if SPL_ENABLE_IRREVERSIBLE && defined(FLASH_PCROP1ASR_PCROP1A_STRT)
    volatile uint32_t *sr = NULL;
    volatile uint32_t *er = NULL;
    SPL_Status st;
    const uint32_t width = FLASH_PCROP1ASR_PCROP1A_STRT_Msk >> FLASH_PCROP1ASR_PCROP1A_STRT_Pos;

    if (key != FLASH_KEY_PCROP)
    {
        return SPL_ERR_PARAM; /* geri alinamaz islem anahtari: her seviyede */
    }
    /* SAFE: alan genisligine tasma. RM0444 3.5.2: END = STRT protects the whole bank, not one
     * subpage: refused (geri alinamaz). */
    SPL_REQUIRE_SAFE((start_subpage <= width) && (end_subpage <= width) &&
                         (start_subpage != end_subpage),
                     SPL_ERR_PARAM);
    st = Flash_PcropRegisters(area, &sr, &er);
    if (st != SPL_OK)
    {
        return st;
    }
    /* ERRATA ES0548 2.2.5: a PCROP read returns the previous bus value; ST recommends PCROP only
     * with RDP level 1 + PCROP_RDP = 1 (or level 2). RDP level 0 in OPTR -> STATE; PCROP_RDP is
     * always set (rs bit) so the area is erased on a level 1 -> 0 regression. */
    SPL_REQUIRE_SAFE((FLASH->OPTR & FLASH_OPTR_RDP_Msk) != FLASH_RDP_LEVEL0, SPL_ERR_STATE);
    st = Flash_OptionWritable();
    if (st != SPL_OK)
    {
        return st;
    }
    *sr = start_subpage;
    *er = end_subpage;
    FLASH->PCROP1AER = FLASH->PCROP1AER | FLASH_PCROP1AER_PCROP_RDP;
    return SPL_OK;
#else
    (void)area;
    (void)start_subpage;
    (void)end_subpage;
    (void)key;
    return SPL_ERR_UNSUPPORTED;
#endif
}

SPL_Status FLASH_OptionSetSecurable(uint32_t sec_size, uint32_t sec_size2, uint8_t boot_lock,
                                    uint32_t key)
{
#if SPL_ENABLE_IRREVERSIBLE && defined(FLASH_SECR_SEC_SIZE)
    SPL_Status st;
    uint32_t secr;

    if (key != FLASH_KEY_SECURABLE)
    {
        return SPL_ERR_PARAM; /* geri alinamaz islem anahtari: her seviyede */
    }
    /* SAFE: boot_lock > 1 BOOT_LOCK'u kurar (yalniz RDP 0'da geri alinir); SEC_SIZE tasmasi. */
    SPL_REQUIRE_SAFE((boot_lock <= 1U) &&
                         (sec_size <= (FLASH_SECR_SEC_SIZE_Msk >> FLASH_SECR_SEC_SIZE_Pos)),
                     SPL_ERR_PARAM);
#if SPL_CHECK_LEVEL <= SPL_CHECK_MIX
    {
        FLASH_GeometryTypeDef g;

        st = Flash_ReadGeometry(&g);
        if (st != SPL_OK)
        {
            return st;
        }
        SPL_REQUIRE_SAFE(sec_size <= g.pages_per_bank, SPL_ERR_PARAM); /* yanlis bolge guvenli */
#if defined(FLASH_SECR_SEC_SIZE2)
        if (sec_size2 != 0U)
        {
            st = Flash_Bank2Status(&g); /* SAFE */
            if (st != SPL_OK)
            {
                return st;
            }
            SPL_REQUIRE_SAFE(sec_size2 <= g.pages_per_bank, SPL_ERR_PARAM);
        }
#endif
    }
#endif
#if defined(FLASH_SECR_SEC_SIZE2)
#else
    if (sec_size2 != 0U)
    {
        return SPL_ERR_UNSUPPORTED;
    }
#endif
    st = Flash_OptionWritable();
    if (st != SPL_OK)
    {
        return st;
    }
    /* RM0444 3.5.5: SEC_SIZE pages from the bank start; BOOT_LOCK forces the boot from main
     * flash. Both are changeable only in RDP level 0 on G071 / G081 (caller note). */
    secr = (sec_size << FLASH_SECR_SEC_SIZE_Pos) | ((boot_lock != 0U) ? FLASH_SECR_BOOT_LOCK : 0U);
#if defined(FLASH_SECR_SEC_SIZE2)
    secr |= sec_size2 << FLASH_SECR_SEC_SIZE2_Pos;
#endif
    FLASH->SECR = secr;
    return SPL_OK;
#else
    (void)sec_size;
    (void)sec_size2;
    (void)boot_lock;
    (void)key;
    return SPL_ERR_UNSUPPORTED;
#endif
}

SPL_Status FLASH_SecureAreaProtect(uint32_t bank_mask)
{
#if defined(FLASH_CR_SEC_PROT)
    uint32_t set = 0U;

    /* normal: etkisiz / fazla bit atilir. */
    SPL_REQUIRE((bank_mask != 0U) && ((bank_mask & ~(FLASH_MASS_BANK1 | FLASH_MASS_BANK2)) == 0U),
                SPL_ERR_PARAM);
    if ((bank_mask & FLASH_MASS_BANK1) != 0U)
    {
        set |= FLASH_CR_SEC_PROT;
    }
    if ((bank_mask & FLASH_MASS_BANK2) != 0U)
    {
#if defined(FLASH_CR_SEC_PROT2)
        set |= FLASH_CR_SEC_PROT2;
#else
        return SPL_ERR_UNSUPPORTED;
#endif
    }
    if ((FLASH->SR & FLASH_SR_ANYBUSY) != 0U)
    {
        return SPL_BUSY;
    }
    /* RM0444 3.5.5: SEC_PROT is cleared only by a system reset; no unlock is needed. */
    FLASH->CR = (FLASH->CR & ~FLASH_CR_CMDBITS) | set;
    return SPL_OK;
#else
    (void)bank_mask;
    return SPL_ERR_UNSUPPORTED;
#endif
}

SPL_Status FLASH_OptionStart(void)
{
    SPL_Status st = Flash_OptionWritable();

    if (st != SPL_OK)
    {
        return st;
    }
    /* SAFE: kilitliyken OPTSTRT yok sayilir, OK donerdi. */
    SPL_REQUIRE_SAFE((FLASH->CR & FLASH_CR_LOCK) == 0U, SPL_ERR_STATE);
    if ((FLASH->SR & FLASH_SR_ERRORS) != 0U)
    {
        return SPL_ERR_HW;
    }
    /* RM0444 3.4.2: OPTSTRT; BSY1 stays set until the option bytes are written. */
    FLASH->CR = (FLASH->CR & ~(FLASH_CR_OPBITS | FLASH_CR_CMDBITS)) | FLASH_CR_OPTSTRT;
    return SPL_OK;
}

SPL_Status FLASH_OptionLaunch(uint32_t key)
{
    SPL_Status st;

    if (key != FLASH_KEY_OBL_LAUNCH)
    {
        return SPL_ERR_PARAM;
    }
    st = Flash_OptionWritable();
    if (st != SPL_OK)
    {
        return st;
    }
    /* RM0444 3.4.2: OBL_LAUNCH reloads the option bytes and resets the device. */
    FLASH->CR = (FLASH->CR & ~(FLASH_CR_OPBITS | FLASH_CR_CMDBITS)) | FLASH_CR_OBL_LAUNCH;
    return SPL_OK;
}

SPL_Status FLASH_OtpProgramStart(uint32_t address, uint64_t data, uint32_t key)
{
#if SPL_ENABLE_IRREVERSIBLE
    SPL_Status st;

    if (key != FLASH_KEY_OTP)
    {
        return SPL_ERR_PARAM; /* geri alinamaz islem anahtari: her seviyede */
    }
    /* SAFE: ana bellege ya da yanlis OTP konumuna kalici yazim. */
    SPL_REQUIRE_SAFE(((address & 7U) == 0U) && (address >= FLASH_OTP_BASE) &&
                         (address <= (FLASH_OTP_BASE + FLASH_OTP_BYTES - 8U)),
                     SPL_ERR_PARAM);
    /* ERRATA ES0548 2.2.3 rule kept: an all-ones OTP double word is a wasted location. SAFE. */
    SPL_REQUIRE_SAFE(data != 0xFFFFFFFFFFFFFFFFULL, SPL_ERR_PARAM);
    st = Flash_ReadyForOperation();
    if (st != SPL_OK)
    {
        return st;
    }
    /* OTP cannot be erased: a double word already written -> STATE (RM0444 3.3.8). SAFE: data = 0
     * yazimi konumu kalici olarak sifirlardi. */
    SPL_REQUIRE_SAFE((*(const volatile uint32_t *)(uintptr_t)address == 0xFFFFFFFFUL) &&
                         (*(const volatile uint32_t *)(uintptr_t)(address + 4U) == 0xFFFFFFFFUL),
                     SPL_ERR_STATE);
    FLASH->CR = (FLASH->CR & ~(FLASH_CR_OPBITS | FLASH_CR_CMDBITS)) | FLASH_CR_PG;
    *(volatile uint32_t *)(uintptr_t)address = (uint32_t)data;
    *(volatile uint32_t *)(uintptr_t)(address + 4U) = (uint32_t)(data >> 32U);
    return SPL_OK;
#else
    (void)address;
    (void)data;
    (void)key;
    return SPL_ERR_UNSUPPORTED;
#endif
}
