/* Ported from STM32G4 SPL v0.13.4 (Autometry_Mcu_Spl 4994f2b); reviewed against RM0444 Rev 6
 * section 26 (Table 135) and ES0548 2.9.1 / 2.9.2. G0 changes: LPTIM1 and LPTIM2, encoder on
 * LPTIM1 only, TRIGSEL[2:0], RCC reset through RCC_APBRSTR1 per instance. */
/* Kutuphane birimi: spl_check.h karisma korumasi referansini bu birim de uretir; uretmeyen tek
 * birim stm32g0_check.c (SPL_CHECK_LEVEL_DEFINER). BULGU-02, CHECK_LEVEL.md 3.2. */
#define SPL_LIBRARY_BUILD 1
#include "stm32g0_lptim.h"

#if SPL_G0_HAS_LPTIM

#define LPTIM_CKPOL_MAX LPTIM_CKPOL_BOTH
#define LPTIM_FILTER_MAX 3U
#define LPTIM_PRESC_MAX 7U
#define LPTIM_REG16_MAX 0xFFFFUL

/* Denetim seviyesi (A4 dalga 3, Docs/CHECK_LEVEL.md 5.4 LPTIM tablosu): SPL_REQUIRE = normal sinif
 * (yalniz FULL), SPL_REQUIRE_SAFE = SAFE sinif (FULL ve MIX). CFGR alanlari (0 / 1 bayraklari
 * dahil) dogrudan kaydirilir: hepsi SAFE (Lptim_ValidInit tek SAFE denetimi). Yardimcilar static
 * inline (saf). ARROK / CMPOK beklemesi ve kararsiz sayac (BUSY) result: her seviyede. */

static inline uint8_t Lptim_ValidInstance(const LPTIM_TypeDef *lptim)
{
    return (uint8_t)(((lptim == LPTIM1) || (lptim == LPTIM2)) ? 1U : 0U);
}

static inline uint8_t Lptim_IsEnabled(const LPTIM_TypeDef *lptim)
{
    return (uint8_t)(((lptim->CR & LPTIM_CR_ENABLE) != 0U) ? 1U : 0U);
}

/* RM0444 Tables 138 / 139: lptim_ext_trig5 = COMP3_OUT (LPTIM1) / TAMP_TRG3 (LPTIM2), both only on
 * G0B1 / G0C1; trig6 / trig7 = COMP1_OUT / COMP2_OUT, absent on G031 / G041. A trigger whose
 * source is absent never fires (2026-11-14 capability audit). Software trigger: always valid. */
static inline uint8_t Lptim_TriggerExists(const LPTIM_TypeDef *lptim, const LPTIM_InitTypeDef *init)
{
    uint8_t exists = 1U;

    (void)lptim;
    if (init->trigger_edge == LPTIM_TRIGGER_SOFTWARE)
    {
        return 1U;
    }
#if !defined(COMP3)
    if ((init->trigger_source == 5U) && (lptim == LPTIM1))
    {
        exists = 0U;
    }
#endif
#if !defined(TAMP_CR1_TAMP3E)
    if ((init->trigger_source == 5U) && (lptim == LPTIM2))
    {
        exists = 0U;
    }
#endif
#if !defined(COMP1)
    if ((init->trigger_source == 6U) || (init->trigger_source == 7U))
    {
        exists = 0U;
    }
#endif
    return exists;
}

static inline uint8_t Lptim_ValidInit(const LPTIM_InitTypeDef *init)
{
    if ((init->clock_source > LPTIM_CLOCK_EXTERNAL) || (init->clock_polarity > LPTIM_CKPOL_MAX) ||
        (init->clock_filter > LPTIM_FILTER_MAX) || (init->trigger_filter > LPTIM_FILTER_MAX) ||
        (init->prescaler > LPTIM_PRESC_MAX) || (init->trigger_source > LPTIM_TRIGSEL_MAX) ||
        (init->trigger_edge > LPTIM_TRIGGER_BOTH) || (init->timeout > 1U) ||
        (init->set_once > 1U) || (init->polarity > 1U) || (init->preload > 1U) ||
        (init->count_external > 1U) || (init->encoder > 1U))
    {
        return 0U;
    }
    /* RM0444 26.4.12: with an external clock only one edge can count. */
    if ((init->clock_source == LPTIM_CLOCK_EXTERNAL) && (init->clock_polarity == LPTIM_CKPOL_BOTH))
    {
        return 0U;
    }
    /* RM0444 26.4.15: encoder mode needs CKSEL=0 and PRESC=000. */
    if ((init->encoder != 0U) &&
        ((init->clock_source != LPTIM_CLOCK_INTERNAL) || (init->prescaler != 0U)))
    {
        return 0U;
    }
    /* RM0444 26.4.12: CKSEL=0, COUNTMODE=1 needs an unprescaled clock. */
    if ((init->clock_source == LPTIM_CLOCK_INTERNAL) && (init->count_external != 0U) &&
        (init->prescaler != 0U))
    {
        return 0U;
    }
    return 1U;
}

static uint32_t Lptim_EncodeCfgr(const LPTIM_InitTypeDef *init)
{
    uint32_t cfgr;

    cfgr = (init->clock_source << LPTIM_CFGR_CKSEL_Pos) |
           (init->clock_polarity << LPTIM_CFGR_CKPOL_Pos) |
           (init->clock_filter << LPTIM_CFGR_CKFLT_Pos) |
           (init->trigger_filter << LPTIM_CFGR_TRGFLT_Pos) |
           (init->prescaler << LPTIM_CFGR_PRESC_Pos) |
           (init->trigger_source << LPTIM_CFGR_TRIGSEL_Pos) |
           (init->trigger_edge << LPTIM_CFGR_TRIGEN_Pos) |
           (init->timeout << LPTIM_CFGR_TIMOUT_Pos) | (init->set_once << LPTIM_CFGR_WAVE_Pos) |
           (init->polarity << LPTIM_CFGR_WAVPOL_Pos) | (init->preload << LPTIM_CFGR_PRELOAD_Pos) |
           (init->count_external << LPTIM_CFGR_COUNTMODE_Pos) |
           (init->encoder << LPTIM_CFGR_ENC_Pos);
    return cfgr;
}

SPL_Status LPTIM_Init(LPTIM_TypeDef *lptim, const LPTIM_InitTypeDef *init)
{
    SPL_REQUIRE_SAFE(Lptim_ValidInstance(lptim) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE_SAFE(init != (const LPTIM_InitTypeDef *)0, SPL_ERR_PARAM);
    /* SAFE: alan tasmasi (bayraklar da kaydirilir) ve RM0444 26.4.12 / 26.4.15 yasak birlesimleri.
     */
    SPL_REQUIRE_SAFE(Lptim_ValidInit(init) != 0U, SPL_ERR_PARAM);
    /* RM0444 Table 135: encoder mode on LPTIM1 only (ENC is reserved on LPTIM2). SAFE. */
    SPL_REQUIRE_SAFE((lptim != LPTIM2) || (init->encoder == 0U), SPL_ERR_UNSUPPORTED);
    /* SAFE (ornek yetenegi): kaynagi SKU'da olmayan tetik hic gelmez. */
    SPL_REQUIRE_SAFE(Lptim_TriggerExists(lptim, init) != 0U, SPL_ERR_UNSUPPORTED);
    /* RM0444 26.4.13 / 26.7.4: CFGR only with ENABLE=0. SAFE: yazim sessizce yok sayilir. */
    SPL_REQUIRE_SAFE(Lptim_IsEnabled(lptim) == 0U, SPL_ERR_STATE);

    lptim->CFGR = Lptim_EncodeCfgr(init);
    return SPL_OK;
}

SPL_Status LPTIM_InterruptConfig(LPTIM_TypeDef *lptim, uint32_t mask)
{
    SPL_REQUIRE_SAFE(Lptim_ValidInstance(lptim) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    /* SAFE: ARROK / CMPOK IE (ERRATA ES0548 2.9.2: kesme takili kalir) ya da ayrilmis bit. */
    SPL_REQUIRE_SAFE((mask & ~LPTIM_IT_ALLOWED) == 0U, SPL_ERR_PARAM);
    /* RM0444 26.7.3: UPIE / DOWNIE are reserved on an LPTIM without encoder mode (LPTIM2, Table
     * 135). SAFE (ornek yetenegi). 2026-11-14 yetenek taramasi. */
    SPL_REQUIRE_SAFE((lptim != LPTIM2) || ((mask & (LPTIM_IER_UPIE | LPTIM_IER_DOWNIE)) == 0U),
                     SPL_ERR_UNSUPPORTED);
    /* RM0444 26.4.13 / 26.7.3: IER only with ENABLE=0. SAFE. */
    SPL_REQUIRE_SAFE(Lptim_IsEnabled(lptim) == 0U, SPL_ERR_STATE);

    lptim->IER = mask;
    return SPL_OK;
}

SPL_Status LPTIM_Enable(LPTIM_TypeDef *lptim)
{
    SPL_REQUIRE_SAFE(Lptim_ValidInstance(lptim) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    /* COUNTRST, CNTSTRT, SNGSTRT are set-only/self-clearing: never copied. */
    lptim->CR = (lptim->CR & LPTIM_CR_RSTARE) | LPTIM_CR_ENABLE;
    return SPL_OK;
}

SPL_Status LPTIM_Disable(LPTIM_TypeDef *lptim)
{
    SPL_REQUIRE_SAFE(Lptim_ValidInstance(lptim) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    if (lptim->IER != 0U)
    {
        /* ERRATA ES0548 2.9.1: clearing ENABLE near an LPTIM interrupt can freeze the wake-up
         * IRQ; disable through the instance's RCC reset (LPTIM1RST / LPTIM2RST). */
        const uint32_t rst = (lptim == LPTIM1) ? RCC_APBRSTR1_LPTIM1RST : RCC_APBRSTR1_LPTIM2RST;
        RCC->APBRSTR1 |= rst;
        RCC->APBRSTR1 &= ~rst;
        return SPL_OK;
    }
    lptim->CR = 0U;
    return SPL_OK;
}

SPL_Status LPTIM_Start(LPTIM_TypeDef *lptim, uint32_t mode)
{
    uint32_t start;

    SPL_REQUIRE_SAFE(Lptim_ValidInstance(lptim) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    if (mode == LPTIM_START_CONTINUOUS)
    {
        start = LPTIM_CR_CNTSTRT;
    }
    else if (mode == LPTIM_START_SINGLE)
    {
        start = LPTIM_CR_SNGSTRT;
    }
    else
    {
        return SPL_ERR_PARAM; /* yapisal: gecersiz kip (start tanimsiz kalirdi) */
    }
    /* RM0444 26.4.8: start bits written with ENABLE=0 are discarded. SAFE. */
    SPL_REQUIRE_SAFE(Lptim_IsEnabled(lptim) != 0U, SPL_ERR_STATE);
    /* RM0444 26.4.15: encoder mode runs in continuous mode only. SAFE. */
    SPL_REQUIRE_SAFE((start != LPTIM_CR_SNGSTRT) || ((lptim->CFGR & LPTIM_CFGR_ENC) == 0U),
                     SPL_ERR_STATE);

    lptim->CR = (lptim->CR & (LPTIM_CR_ENABLE | LPTIM_CR_RSTARE)) | start;
    return SPL_OK;
}

/* 1 when the STATE gates of Lptim_SyncedWrite pass (ENABLE=1, IER=0). */
static inline uint32_t Lptim_WriteGateOpen(const LPTIM_TypeDef *lptim)
{
    return (((lptim->CR & LPTIM_CR_ENABLE) != 0U) && (lptim->IER == 0U)) ? 1U : 0U;
}

/* Shared ARR/CMP write rule (RM0444 26.4.11): a new write only after the
 * previous one is acknowledged by ARROK/CMPOK. */
static SPL_Status Lptim_SyncedWrite(LPTIM_TypeDef *lptim, uint32_t *pending, uint32_t ok_flag,
                                    volatile uint32_t *reg, uint32_t value)
{
    SPL_REQUIRE_SAFE(Lptim_IsEnabled(lptim) != 0U, SPL_ERR_STATE); /* SAFE: yazim yok sayilir */
    /* ERRATA ES0548 2.9.2: with an LPTIM interrupt enabled, the
     * thread-context ICR clear of ARROK/CMPOK (their IE is never set) can leave
     * the IRQ stuck high. ARR/CMP are written only while IER = 0. SAFE (errata). */
    SPL_REQUIRE_SAFE(lptim->IER == 0U, SPL_ERR_STATE);
    if ((*pending != 0U) && ((lptim->ISR & ok_flag) == 0U))
    {
        return SPL_BUSY;
    }

    /* ARROKCF/CMPOKCF sit at the ARROK/CMPOK positions. Clear first so the
     * acknowledgement of this write is observable. */
    lptim->ICR = ok_flag;
    *reg = value;
    *pending = 1U;
    return SPL_OK;
}

SPL_Status LPTIM_SetAutoReload(LPTIM_TypeDef *lptim, LPTIM_SyncTypeDef *sync, uint32_t arr)
{
    SPL_REQUIRE_SAFE(Lptim_ValidInstance(lptim) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE_SAFE(sync != (LPTIM_SyncTypeDef *)0, SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE((arr != 0U) && (arr <= LPTIM_REG16_MAX), SPL_ERR_PARAM); /* SAFE */
    /* RM0444 26.4.10: ARR strictly greater than CMP (checked only when the
     * write gate is open, so ENABLE=0 and IER != 0 still report STATE first). SAFE. */
    SPL_REQUIRE_SAFE((Lptim_WriteGateOpen(lptim) == 0U) || (arr > (lptim->CMP & LPTIM_CMP_CMP)),
                     SPL_ERR_PARAM);
#if SPL_CHECK_LEVEL <= SPL_CHECK_MIX
    /* RM0444 26.4.11: PRELOAD=0 updates ARR at once; an ARR at or below the
     * running counter makes the period run to 0xFFFF. With RSTARE=1 the
     * counter cannot be read (a read resets it): shrinking is refused. SAFE; blok yalniz bu
     * denetim icin (sayac okumasi dahil): FAST'ta derlenmez. */
    if (((lptim->CFGR & LPTIM_CFGR_PRELOAD) == 0U) && (arr < (lptim->ARR & LPTIM_ARR_ARR)) &&
        (Lptim_WriteGateOpen(lptim) != 0U))
    {
        uint32_t count;

        SPL_REQUIRE_SAFE((lptim->CR & LPTIM_CR_RSTARE) == 0U, SPL_ERR_STATE);
        if (LPTIM_ReadCounter(lptim, &count) != SPL_OK)
        {
            return SPL_BUSY; /* counter moving faster than three reads */
        }
        SPL_REQUIRE_SAFE(arr > count, SPL_ERR_STATE);
    }
#endif
    return Lptim_SyncedWrite(lptim, &sync->arr_pending, LPTIM_ISR_ARROK, &lptim->ARR, arr);
}

SPL_Status LPTIM_SetCompare(LPTIM_TypeDef *lptim, LPTIM_SyncTypeDef *sync, uint32_t cmp)
{
    SPL_REQUIRE_SAFE(Lptim_ValidInstance(lptim) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE_SAFE(sync != (LPTIM_SyncTypeDef *)0, SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(cmp <= LPTIM_REG16_MAX, SPL_ERR_PARAM); /* SAFE */
    /* RM0444 26.4.10: CMP strictly below ARR (STATE gates of Lptim_SyncedWrite first). SAFE. */
    SPL_REQUIRE_SAFE((Lptim_WriteGateOpen(lptim) == 0U) || (cmp < (lptim->ARR & LPTIM_ARR_ARR)),
                     SPL_ERR_PARAM);
    return Lptim_SyncedWrite(lptim, &sync->cmp_pending, LPTIM_ISR_CMPOK, &lptim->CMP, cmp);
}

SPL_Status LPTIM_SyncStatus(LPTIM_TypeDef *lptim, LPTIM_SyncTypeDef *sync)
{
    uint32_t isr;

    SPL_REQUIRE_SAFE(Lptim_ValidInstance(lptim) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE_SAFE(sync != (LPTIM_SyncTypeDef *)0, SPL_ERR_PARAM);
    isr = lptim->ISR;
    if ((isr & LPTIM_ISR_ARROK) != 0U)
    {
        sync->arr_pending = 0U;
    }
    if ((isr & LPTIM_ISR_CMPOK) != 0U)
    {
        sync->cmp_pending = 0U;
    }
    if ((sync->arr_pending != 0U) || (sync->cmp_pending != 0U))
    {
        return SPL_BUSY;
    }
    return SPL_OK;
}

SPL_Status LPTIM_ReadCounter(LPTIM_TypeDef *lptim, uint32_t *count)
{
    uint32_t first;
    uint32_t second;
    uint32_t third;

    SPL_REQUIRE_SAFE(Lptim_ValidInstance(lptim) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE_SAFE(count != (uint32_t *)0, SPL_ERR_PARAM);
    /* RM0444 26.4.14: with RSTARE=1 every CNT read resets the counter. SAFE (yikici okuma). */
    SPL_REQUIRE_SAFE((lptim->CR & LPTIM_CR_RSTARE) == 0U, SPL_ERR_STATE);

    first = lptim->CNT & LPTIM_CNT_CNT;
    second = lptim->CNT & LPTIM_CNT_CNT;
    if (first == second)
    {
        *count = second;
        return SPL_OK;
    }
    third = lptim->CNT & LPTIM_CNT_CNT;
    if (second == third)
    {
        *count = third;
        return SPL_OK;
    }
    return SPL_BUSY;
}

SPL_Status LPTIM_GetFlags(LPTIM_TypeDef *lptim, uint32_t *flags)
{
    SPL_REQUIRE_SAFE(Lptim_ValidInstance(lptim) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE_SAFE(flags != (uint32_t *)0, SPL_ERR_PARAM);
    *flags = lptim->ISR & LPTIM_FLAGS_ALL;
    return SPL_OK;
}

SPL_Status LPTIM_ClearFlags(LPTIM_TypeDef *lptim, uint32_t flags)
{
    uint32_t ier;

    SPL_REQUIRE_SAFE(Lptim_ValidInstance(lptim) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE(flags != 0U, SPL_ERR_PARAM);                           /* normal: etkisiz */
    /* SAFE (EDR-003): ayrilmis bite yazim. */
    SPL_REQUIRE_SAFE((flags & ~LPTIM_FLAGS_CLEARABLE) == 0U, SPL_ERR_PARAM);
    /* RM0444 26.7.2: UPCF / DOWNCF are reserved on LPTIM2 (no encoder mode). SAFE. */
    SPL_REQUIRE_SAFE((lptim != LPTIM2) || ((flags & (LPTIM_ISR_UP | LPTIM_ISR_DOWN)) == 0U),
                     SPL_ERR_UNSUPPORTED);
    /* ERRATA ES0548 2.9.2: in interrupt mode (IER != 0) clear
     * only flags whose interrupt is enabled (ISR and IER bit positions match). SAFE. */
    ier = lptim->IER;
    SPL_REQUIRE_SAFE((ier == 0U) || ((flags & ~ier) == 0U), SPL_ERR_STATE);
    /* W1C register: write only the requested clear bits. */
    lptim->ICR = flags;
    /* Read back: the clear reaches the peripheral before return (METHOD 1.7, issue #23). */
    (void)lptim->ISR;
    return SPL_OK;
}

#else
/* ISO C forbids an empty translation unit. */
typedef int stm32g0_lptim_absent;
#endif /* SPL_G0_HAS_LPTIM */
