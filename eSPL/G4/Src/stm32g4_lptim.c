/* SPDX-License-Identifier: MIT */
/* Low-power timer (LPTIM1) driver, RM0440 Rev 9 section 33.
 * Public contract: stm32g4_lptim.h. */
#define SPL_LIBRARY_BUILD 1
#include "stm32g4_lptim.h"

#define LPTIM_CKPOL_MAX LPTIM_CKPOL_BOTH
#define LPTIM_FILTER_MAX 3U
#define LPTIM_PRESC_MAX 7U
#define LPTIM_REG16_MAX 0xFFFFUL
#define LPTIM_TRIGSEL_LOW_BITS 0x7U
#define LPTIM_TRIGSEL_HIGH_BIT 0x8U

static uint8_t Lptim_ValidInstance(const LPTIM_TypeDef *lptim)
{
    return (uint8_t)((lptim == LPTIM1) ? 1U : 0U);
}

static uint8_t Lptim_IsEnabled(const LPTIM_TypeDef *lptim)
{
    return (uint8_t)(((lptim->CR & LPTIM_CR_ENABLE) != 0U) ? 1U : 0U);
}

static uint8_t Lptim_ValidInit(const LPTIM_InitTypeDef *init)
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
    /* RM0440 33.4.11: with an external clock only one edge can count. */
    if ((init->clock_source == LPTIM_CLOCK_EXTERNAL) && (init->clock_polarity == LPTIM_CKPOL_BOTH))
    {
        return 0U;
    }
    /* RM0440 33.4.14: encoder mode needs CKSEL=0 and PRESC=000. */
    if ((init->encoder != 0U) &&
        ((init->clock_source != LPTIM_CLOCK_INTERNAL) || (init->prescaler != 0U)))
    {
        return 0U;
    }
    /* RM0440 33.4.11: CKSEL=0, COUNTMODE=1 needs an unprescaled clock. */
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
           ((init->trigger_source & LPTIM_TRIGSEL_LOW_BITS) << LPTIM_CFGR_TRIGSEL_Pos) |
           (init->trigger_edge << LPTIM_CFGR_TRIGEN_Pos) |
           (init->timeout << LPTIM_CFGR_TIMOUT_Pos) | (init->set_once << LPTIM_CFGR_WAVE_Pos) |
           (init->polarity << LPTIM_CFGR_WAVPOL_Pos) | (init->preload << LPTIM_CFGR_PRELOAD_Pos) |
           (init->count_external << LPTIM_CFGR_COUNTMODE_Pos) |
           (init->encoder << LPTIM_CFGR_ENC_Pos);
    /* TRIGSEL[3] is CFGR bit 29 (RM0440 33.7.4, CMSIS LPTIM_CFGR_TRIGSEL_3). */
    if ((init->trigger_source & LPTIM_TRIGSEL_HIGH_BIT) != 0U)
    {
        cfgr |= LPTIM_CFGR_TRIGSEL_3;
    }
    return cfgr;
}

SPL_Status LPTIM_Init(LPTIM_TypeDef *lptim, const LPTIM_InitTypeDef *init)
{
    /* Reject invalid parameters before hardware access. */
    SPL_REQUIRE(!((Lptim_ValidInstance(lptim) == 0U) || (init == (const LPTIM_InitTypeDef *)0)),
                SPL_ERR_PARAM);
    /* Reject invalid parameters before hardware access. */
    SPL_REQUIRE(!(Lptim_ValidInit(init) == 0U), SPL_ERR_PARAM);
    /* RM0440 33.4.12 / 33.7.4: CFGR only with ENABLE=0. */
    if (Lptim_IsEnabled(lptim) != 0U)
    {
        return SPL_ERR_STATE;
    }

    lptim->CFGR = Lptim_EncodeCfgr(init);
    return SPL_OK;
}

SPL_Status LPTIM_InterruptConfig(LPTIM_TypeDef *lptim, uint32_t mask)
{
    /* Reject invalid parameters before hardware access. */
    SPL_REQUIRE(!((Lptim_ValidInstance(lptim) == 0U) || ((mask & ~LPTIM_IT_ALLOWED) != 0U)),
                SPL_ERR_PARAM);
    /* RM0440 33.4.12 / 33.7.3: IER only with ENABLE=0. */
    if (Lptim_IsEnabled(lptim) != 0U)
    {
        return SPL_ERR_STATE;
    }

    lptim->IER = mask;
    return SPL_OK;
}

SPL_Status LPTIM_Enable(LPTIM_TypeDef *lptim)
{
    /* Reject invalid parameters before hardware access. */
    SPL_REQUIRE(!(Lptim_ValidInstance(lptim) == 0U), SPL_ERR_PARAM);
    /* COUNTRST, CNTSTRT, SNGSTRT are set-only/self-clearing: never copied. */
    lptim->CR = (lptim->CR & LPTIM_CR_RSTARE) | LPTIM_CR_ENABLE;
    return SPL_OK;
}

SPL_Status LPTIM_Disable(LPTIM_TypeDef *lptim)
{
    /* Reject invalid parameters before hardware access. */
    SPL_REQUIRE(!(Lptim_ValidInstance(lptim) == 0U), SPL_ERR_PARAM);
    if (lptim->IER != 0U)
    {
        /* ERRATA ES0431 2.9.1 / ES0430 2.13.1 / ES0523 2.9.1: clearing ENABLE near an LPTIM
         * interrupt can freeze the wakeup IRQ; disable via RCC LPTIM1RST. */
        RCC->APB1RSTR1 |= RCC_APB1RSTR1_LPTIM1RST;
        RCC->APB1RSTR1 &= ~RCC_APB1RSTR1_LPTIM1RST;
        return SPL_OK;
    }
    lptim->CR = 0U;
    return SPL_OK;
}

SPL_Status LPTIM_Start(LPTIM_TypeDef *lptim, uint32_t mode)
{
    uint32_t start;

    /* Reject invalid parameters before hardware access. */
    SPL_REQUIRE(!(Lptim_ValidInstance(lptim) == 0U), SPL_ERR_PARAM);
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
        return SPL_ERR_PARAM;
    }
    /* RM0440 33.4.6: start bits written with ENABLE=0 are discarded. */
    if (Lptim_IsEnabled(lptim) == 0U)
    {
        return SPL_ERR_STATE;
    }
    /* RM0440 33.4.14: encoder mode runs in continuous mode only. */
    if ((start == LPTIM_CR_SNGSTRT) && ((lptim->CFGR & LPTIM_CFGR_ENC) != 0U))
    {
        return SPL_ERR_STATE;
    }

    lptim->CR = (lptim->CR & (LPTIM_CR_ENABLE | LPTIM_CR_RSTARE)) | start;
    return SPL_OK;
}

/* 1 when the STATE gates of Lptim_SyncedWrite pass (ENABLE=1, IER=0). */
static uint32_t Lptim_WriteGateOpen(const LPTIM_TypeDef *lptim)
{
    return (((lptim->CR & LPTIM_CR_ENABLE) != 0U) && (lptim->IER == 0U)) ? 1U : 0U;
}

/* Shared ARR/CMP write rule (RM0440 33.4.10): a new write only after the
 * previous one is acknowledged by ARROK/CMPOK. */
static SPL_Status Lptim_SyncedWrite(LPTIM_TypeDef *lptim, uint32_t *pending, uint32_t ok_flag,
                                    volatile uint32_t *reg, uint32_t value)
{
    if (Lptim_IsEnabled(lptim) == 0U)
    {
        return SPL_ERR_STATE;
    }
    /* ERRATA ES0431 2.9.2 / ES0430 2.13.2 / ES0523 2.9.2: with an LPTIM interrupt enabled, the
     * thread-context ICR clear of ARROK/CMPOK (their IE is never set) can leave
     * the IRQ stuck high. ARR/CMP are written only while IER = 0. */
    if (lptim->IER != 0U)
    {
        return SPL_ERR_STATE;
    }
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
    /* Reject invalid parameters before hardware access. */
    SPL_REQUIRE(!((Lptim_ValidInstance(lptim) == 0U) || (sync == (LPTIM_SyncTypeDef *)0) ||
                  (arr == 0U) || (arr > LPTIM_REG16_MAX)),
                SPL_ERR_PARAM);
    /* RM0440 33.4.9: ARR strictly greater than CMP (checked only when the
     * write gate is open, so ENABLE=0 and IER != 0 still report STATE first). */
    /* Reject invalid parameters before hardware access. */
    SPL_REQUIRE(!((Lptim_WriteGateOpen(lptim) != 0U) && (arr <= (lptim->CMP & LPTIM_CMP_CMP))),
                SPL_ERR_PARAM);
    /* RM0440 33.4.10: PRELOAD=0 updates ARR at once; an ARR at or below the
     * running counter makes the period run to 0xFFFF. Comparing with a counter
     * read races the moving counter (CCL-2 ZMN-02), so a running LPTIM with
     * PRELOAD=0 refuses every shrink: STATE, no write. Use PRELOAD=1 (update at
     * the period end) or disable the timer first. Growing ARR is safe. */
    if (((lptim->CFGR & LPTIM_CFGR_PRELOAD) == 0U) && (arr < (lptim->ARR & LPTIM_ARR_ARR)) &&
        (Lptim_WriteGateOpen(lptim) != 0U))
    {
        return SPL_ERR_STATE;
    }
    return Lptim_SyncedWrite(lptim, &sync->arr_pending, LPTIM_ISR_ARROK, &lptim->ARR, arr);
}

SPL_Status LPTIM_SetCompare(LPTIM_TypeDef *lptim, LPTIM_SyncTypeDef *sync, uint32_t cmp)
{
    /* Reject invalid parameters before hardware access. */
    SPL_REQUIRE(!((Lptim_ValidInstance(lptim) == 0U) || (sync == (LPTIM_SyncTypeDef *)0) ||
                  (cmp > LPTIM_REG16_MAX)),
                SPL_ERR_PARAM);
    /* RM0440 33.4.9: CMP strictly below ARR (STATE gates of Lptim_SyncedWrite first). */
    /* Reject invalid parameters before hardware access. */
    SPL_REQUIRE(!((Lptim_WriteGateOpen(lptim) != 0U) && (cmp >= (lptim->ARR & LPTIM_ARR_ARR))),
                SPL_ERR_PARAM);
    return Lptim_SyncedWrite(lptim, &sync->cmp_pending, LPTIM_ISR_CMPOK, &lptim->CMP, cmp);
}

SPL_Status LPTIM_SyncStatus(LPTIM_TypeDef *lptim, LPTIM_SyncTypeDef *sync)
{
    uint32_t isr;

    /* Reject invalid parameters before hardware access. */
    SPL_REQUIRE(!((Lptim_ValidInstance(lptim) == 0U) || (sync == (LPTIM_SyncTypeDef *)0)),
                SPL_ERR_PARAM);
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

    /* Reject invalid parameters before hardware access. */
    SPL_REQUIRE(!((Lptim_ValidInstance(lptim) == 0U) || (count == (uint32_t *)0)), SPL_ERR_PARAM);
    /* RM0440 33.4.13: with RSTARE=1 every CNT read resets the counter. */
    if ((lptim->CR & LPTIM_CR_RSTARE) != 0U)
    {
        return SPL_ERR_STATE;
    }

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
    /* Reject invalid parameters before hardware access. */
    SPL_REQUIRE(!((Lptim_ValidInstance(lptim) == 0U) || (flags == (uint32_t *)0)), SPL_ERR_PARAM);
    *flags = lptim->ISR & LPTIM_FLAGS_ALL;
    return SPL_OK;
}

SPL_Status LPTIM_ClearFlags(LPTIM_TypeDef *lptim, uint32_t flags)
{
    uint32_t ier;

    /* Reject invalid parameters before hardware access. */
    SPL_REQUIRE(!((Lptim_ValidInstance(lptim) == 0U) || (flags == 0U) ||
                  ((flags & ~LPTIM_FLAGS_CLEARABLE) != 0U)),
                SPL_ERR_PARAM);
    /* ERRATA ES0431 2.9.2 / ES0430 2.13.2 / ES0523 2.9.2: in interrupt mode (IER != 0) clear
     * only flags whose interrupt is enabled (ISR and IER bit positions match). */
    ier = lptim->IER;
    if ((ier != 0U) && ((flags & ~ier) != 0U))
    {
        return SPL_ERR_STATE;
    }
    /* W1C register: write only the requested clear bits. */
    lptim->ICR = flags;
    /* Read back: the clear reaches the peripheral before return (METHOD 1.7, issue #23). */
    (void)lptim->ISR;
    return SPL_OK;
}
