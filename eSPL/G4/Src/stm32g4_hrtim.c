#define SPL_LIBRARY_BUILD 1
#include "stm32g4_hrtim.h"

/* High-resolution timer HRTIM1, RM0440 Rev 9 section 28. Contract:
 * Docs/v0.14_HRTIM_CONTRACT.md. Every register write happens only after all
 * parameter and state checks passed. */

#if defined(HRTIM1)

#define HRTIM_CKPSC_MAX 7U
#define HRTIM_REP_MAX 255U
#define HRTIM_DT_MAX 511U
#define HRTIM_DTPRSC_MAX 7U
#define HRTIM_CALRTE_MAX 3U
#define HRTIM_FAULT_MAX 3U
/* SETxyR.SST / RSTxyR.SRT: software trigger, set by software, cleared by hardware. */
#define HRTIM_SW_TRIGGER (1UL << 0)
/* MCR TxCEN: timer A at bit 17 .. timer F at bit 22. */
#define HRTIM_MCR_TCEN_POS HRTIM_MCR_TACEN_Pos
/* CR2: TxSWU at 1..6, TxRST at 9..14 (self-clearing); SWPx at 16..21 (read/write). */
#define HRTIM_CR2_SWU_POS HRTIM_CR2_TASWU_Pos
#define HRTIM_CR2_RST_POS HRTIM_CR2_TARST_Pos
#define HRTIM_CR2_SWP_ALL                                                                          \
    (HRTIM_CR2_SWPA | HRTIM_CR2_SWPB | HRTIM_CR2_SWPC | HRTIM_CR2_SWPD | HRTIM_CR2_SWPE |          \
     HRTIM_CR2_SWPF)
#define HRTIM_COMMON_CLEARABLE (HRTIM_FLAG_FLT_ALL | HRTIM_FLAG_BMPER)
#define HRTIM_DT_LOCKS (HRTIM_DTR_DTRSLK | HRTIM_DTR_DTRLK | HRTIM_DTR_DTFSLK | HRTIM_DTR_DTFLK)
/* OUTxR: output 2 fields = output 1 fields << 16 (RM0440 28.5.52). */
#define HRTIM_OUTR_OUT2_SHIFT 16U

/* Flag masks of the header must match the CMSIS bit positions. */
typedef char Hrtim_FlagCheck
    [((HRTIM_FLAG_DLLRDY == HRTIM_ISR_DLLRDY) && (HRTIM_FLAG_BMPER == HRTIM_ISR_BMPER) &&
      (HRTIM_TIMFLAG_UPD == HRTIM_TIMISR_UPD) && (HRTIM_SRC_PER == HRTIM_SET1R_PER) &&
      (HRTIM_SRC_UPDATE == HRTIM_SET1R_UPDATE) && (HRTIM_SW_TRIGGER == HRTIM_SET1R_SST) &&
      ((HRTIM_OUTR_POL1 << HRTIM_OUTR_OUT2_SHIFT) == HRTIM_OUTR_POL2) &&
      ((HRTIM_OUTR_IDLES1 << HRTIM_OUTR_OUT2_SHIFT) == HRTIM_OUTR_IDLES2) &&
      ((HRTIM_OUTR_FAULT1 << HRTIM_OUTR_OUT2_SHIFT) == HRTIM_OUTR_FAULT2) &&
      (HRTIM_OUTPUT_ALL ==
       (HRTIM_OENR_TA1OEN | HRTIM_OENR_TA2OEN | HRTIM_OENR_TB1OEN | HRTIM_OENR_TB2OEN |
        HRTIM_OENR_TC1OEN | HRTIM_OENR_TC2OEN | HRTIM_OENR_TD1OEN | HRTIM_OENR_TD2OEN |
        HRTIM_OENR_TE1OEN | HRTIM_OENR_TE2OEN | HRTIM_OENR_TF1OEN | HRTIM_OENR_TF2OEN)))
         ? 1
         : -1];

static HRTIM_Timerx_TypeDef *Hrtim_Timer(uint32_t timer)
{
    return &HRTIM1->sTimerxRegs[timer];
}

static uint32_t Hrtim_Running(uint32_t timer)
{
    return ((HRTIM1->sMasterRegs.MCR & (1UL << (HRTIM_MCR_TCEN_POS + timer))) != 0U) ? 1U : 0U;
}

static uint32_t Hrtim_Ckpsc(const HRTIM_Timerx_TypeDef *t)
{
    return (t->TIMxCR & HRTIM_TIMCR_CK_PSC_Msk) >> HRTIM_TIMCR_CK_PSC_Pos;
}

/* ckpsc 0..7, checked by the caller. */
static uint32_t Hrtim_InRange(uint32_t ckpsc, uint32_t value)
{
    /* RM0440 Table 232: period and compare limits per CKPSC. The table lists CKPSC 0..5;
     * 6 and 7 use the CKPSC 5 limits (no high-resolution bits left). */
    static const uint16_t hrtim_min[8] = {0x0060U, 0x0030U, 0x0018U, 0x000CU,
                                          0x0006U, 0x0003U, 0x0003U, 0x0003U};
    static const uint16_t hrtim_max[8] = {0xFFDFU, 0xFFEFU, 0xFFF7U, 0xFFFBU,
                                          0xFFFDU, 0xFFFDU, 0xFFFDU, 0xFFFDU};

    return ((value >= hrtim_min[ckpsc]) && (value <= hrtim_max[ckpsc])) ? 1U : 0U;
}

static uint32_t Hrtim_DllReadyFlag(void)
{
    return ((HRTIM1->sCommonRegs.ISR & HRTIM_ISR_DLLRDY) != 0U) ? 1U : 0U;
}

SPL_Status HRTIM_DllCalibrationStart(void)
{
    const uint32_t dllcr = HRTIM1->sCommonRegs.DLLCR;

    /* RM0440 28.5.75: CAL must not be set simultaneously with CALEN. */
    if ((dllcr & HRTIM_DLLCR_CALEN) != 0U)
    {
        return SPL_ERR_STATE;
    }
    /* ICR is W1C: retire the old readiness so this calibration is observable. */
    HRTIM1->sCommonRegs.ICR = HRTIM_ICR_DLLRDYC;
    HRTIM1->sCommonRegs.DLLCR = (dllcr & HRTIM_DLLCR_CALRTE) | HRTIM_DLLCR_CAL;
    return SPL_OK;
}

SPL_Status HRTIM_DllReady(uint8_t *ready)
{
    /* SAFE: NULL isaretci okuma/yazma etkisi hedef mimaride guvenilir bir hata degildir. */
    SPL_REQUIRE_SAFE(!(ready == (uint8_t *)0), SPL_ERR_PARAM);
    *ready = (uint8_t)Hrtim_DllReadyFlag();
    return SPL_OK;
}

SPL_Status HRTIM_DllPeriodic(uint8_t enable, uint32_t rate)
{
    /* SAFE: gecersiz alan / maske register semantigini bozabilir veya yanlis donanim durumuna yol acar. */
    SPL_REQUIRE_SAFE(!((enable > 1U) || ((enable != 0U) && (rate > HRTIM_CALRTE_MAX))), SPL_ERR_PARAM);
    if (enable == 0U)
    {
        HRTIM1->sCommonRegs.DLLCR = 0U;
        return SPL_OK;
    }
    /* RM0440 28.3.24: CALEN is set once the first calibration is done. */
    if (Hrtim_DllReadyFlag() == 0U)
    {
        return SPL_ERR_STATE;
    }
    HRTIM1->sCommonRegs.DLLCR = (rate << HRTIM_DLLCR_CALRTE_Pos) | HRTIM_DLLCR_CALEN;
    return SPL_OK;
}

/* 1 when every non-zero CMP1..4 of t fits the Table 232 limits of ckpsc.
 * Issue #27 politikasi: CMP ile PER birbirinden bagimsizdir; CMP > PER gecerli bir degerdir
 * (periyot icinde olay uretmez). Sinanan yalniz on bolucu tablosudur. */
static uint32_t Hrtim_ComparesFit(const HRTIM_Timerx_TypeDef *t, uint32_t ckpsc)
{
    const uint32_t cmp[4] = {t->CMP1xR, t->CMP2xR, t->CMP3xR, t->CMP4xR};
    uint32_t i;

    for (i = 0U; i < 4U; i++)
    {
        uint32_t v = cmp[i] & HRTIM_CMP1R_CMP1R_Msk;

        if ((v != 0U) && (Hrtim_InRange(ckpsc, v) == 0U))
        {
            return 0U;
        }
    }
    return 1U;
}

SPL_Status HRTIM_TimerInit(uint32_t timer, const HRTIM_TimerInitTypeDef *cfg)
{
    HRTIM_Timerx_TypeDef *t;
    uint32_t cr;

    /* SAFE: NULL isaretci okuma/yazma etkisi hedef mimaride guvenilir bir hata degildir. */
    SPL_REQUIRE_SAFE(!((timer >= HRTIM_TIMER_COUNT) || (cfg == (const HRTIM_TimerInitTypeDef *)0)), SPL_ERR_PARAM);
    /* SAFE: gecersiz alan / maske register semantigini bozabilir veya yanlis donanim durumuna yol acar. */
    SPL_REQUIRE_SAFE(!((cfg->prescaler > HRTIM_CKPSC_MAX) || (cfg->repetition > HRTIM_REP_MAX) ||
        (cfg->continuous > 1U) || (cfg->retrigger > 1U) || (cfg->preload > 1U) ||
        (cfg->update_on_rep > 1U) || (Hrtim_InRange(cfg->prescaler, cfg->period) == 0U)), SPL_ERR_PARAM);
    /* RM0440 28.3.3: the prescaler cannot be modified once the timer is enabled. */
    if (Hrtim_Running(timer) != 0U)
    {
        return SPL_ERR_STATE;
    }
    t = Hrtim_Timer(timer);
    /* v0.14 (CCL-2 ZMN-16): compare values already written must stay valid under the new
     * prescaler (Table 232 limits); 0 = reset value / skipped pulse. Issue #27: no relation to
     * the period (CMP > PER is valid). */
    if (Hrtim_ComparesFit(t, cfg->prescaler) == 0U)
    {
        return SPL_ERR_STATE;
    }
    cr = (cfg->prescaler << HRTIM_TIMCR_CK_PSC_Pos) | (cfg->continuous << HRTIM_TIMCR_CONT_Pos) |
         (cfg->retrigger << HRTIM_TIMCR_RETRIG_Pos) | (cfg->preload << HRTIM_TIMCR_PREEN_Pos) |
         (cfg->update_on_rep << HRTIM_TIMCR_TREPU_Pos);
    /* Prescaler first (RM0440 28.3.3), then period and repetition. TIMxCR2 = 0: up-counting,
     * no greater-than comparison, no roll-over update (ES0430 2.11.1..2.11.3 not reachable). */
    t->TIMxCR = cr;
    t->TIMxCR2 = 0U;
    t->PERxR = cfg->period;
    t->REPxR = cfg->repetition;
    return SPL_OK;
}

SPL_Status HRTIM_SetPeriod(uint32_t timer, uint32_t period)
{
    HRTIM_Timerx_TypeDef *t;

    /* SAFE: gecersiz timer secimi register erisimini / olay maskesini bozabilir. */
    SPL_REQUIRE_SAFE(!(timer >= HRTIM_TIMER_COUNT), SPL_ERR_PARAM);
    t = Hrtim_Timer(timer);
    /* SAFE: gecersiz alan / maske register semantigini bozabilir veya yanlis donanim durumuna yol acar. */
    SPL_REQUIRE_SAFE(!(Hrtim_InRange(Hrtim_Ckpsc(t), period) == 0U), SPL_ERR_PARAM);
    /* Issue #27 politikasi: HRTIM_SetCompare ile ayni kural, yalniz Tablo 232. Yazilmis CMP1..4
     * sinanmaz; CMP > PER gecerlidir (periyot icinde olay yok). Iki ayri register yazimi atomik
     * degildir: PREEN = 1 iken etkin degerler birlikte bir sonraki update olayinda yuklenir. */
    t->PERxR = period;
    return SPL_OK;
}

SPL_Status HRTIM_SetCompare(uint32_t timer, uint32_t unit, uint32_t value)
{
    HRTIM_Timerx_TypeDef *t;
    volatile uint32_t *reg;
    uint32_t ok;

    /* SAFE: gecersiz timer secimi register erisimini / olay maskesini bozabilir. */
    SPL_REQUIRE_SAFE(!((timer >= HRTIM_TIMER_COUNT) || (unit < 1U) || (unit > 4U)), SPL_ERR_PARAM);
    t = Hrtim_Timer(timer);
    /* RM0440 28.3.4: 0 skips a pulse, allowed in CMP1 and CMP3 only. */
    ok = ((value == 0U) && ((unit == 1U) || (unit == 3U))) ? 1U
                                                           : Hrtim_InRange(Hrtim_Ckpsc(t), value);
    /* Issue #27 politikasi: PER ile iliski sinanmaz; CMP > PER gecerli (periyot icinde
     * eslesme olmaz, olay uretilmez). HRTIM_SetPeriod ile ayni kural. */
    /* SAFE: gecersiz alan / maske register semantigini bozabilir veya yanlis donanim durumuna yol acar. */
    SPL_REQUIRE_SAFE(!(ok == 0U), SPL_ERR_PARAM);
    reg = (unit == 1U)   ? &t->CMP1xR
          : (unit == 2U) ? &t->CMP2xR
          : (unit == 3U) ? &t->CMP3xR
                         : &t->CMP4xR;
    *reg = value;
    return SPL_OK;
}

/* Output bits of timer t in OENR (2 bits). */
static uint32_t Hrtim_TimerOutputs(uint32_t timer)
{
    return (uint32_t)(3UL << (2U * timer));
}

SPL_Status HRTIM_OutputConfig(uint32_t timer, uint32_t output, const HRTIM_OutputTypeDef *cfg)
{
    HRTIM_Timerx_TypeDef *t;
    uint32_t value;
    uint32_t field;

    /* SAFE: NULL isaretci okuma/yazma etkisi hedef mimaride guvenilir bir hata degildir. */
    SPL_REQUIRE_SAFE(!((timer >= HRTIM_TIMER_COUNT) || (output < 1U) || (output > 2U) ||
        (cfg == (const HRTIM_OutputTypeDef *)0)), SPL_ERR_PARAM);
    /* SAFE: gecersiz alan / maske register semantigini bozabilir veya yanlis donanim durumuna yol acar. */
    SPL_REQUIRE_SAFE(!(((cfg->set_sources & HRTIM_SW_TRIGGER) != 0U) ||
        ((cfg->reset_sources & HRTIM_SW_TRIGGER) != 0U) || (cfg->polarity > 1U) ||
        (cfg->idle_level > 1U) || (cfg->fault_state > HRTIM_FAULT_MAX)), SPL_ERR_PARAM);
    /* RM0440 28.5.52: POL and FAULT are fixed once the timer runs, IDLES before the HRTIM
     * controls the output. */
    if ((Hrtim_Running(timer) != 0U) ||
        ((HRTIM1->sCommonRegs.OENR & HRTIM_OUTPUT_BIT(timer, output)) != 0U))
    {
        return SPL_ERR_STATE;
    }
    t = Hrtim_Timer(timer);
    /* Output 1 fields; output 2 has the same layout 16 bits higher (constant shifts only). */
    value = (cfg->polarity << HRTIM_OUTR_POL1_Pos) | (cfg->idle_level << HRTIM_OUTR_IDLES1_Pos) |
            (cfg->fault_state << HRTIM_OUTR_FAULT1_Pos);
    if (output == 1U)
    {
        field = HRTIM_OUTR_POL1 | HRTIM_OUTR_IDLES1 | HRTIM_OUTR_FAULT1;
        t->SETx1R = cfg->set_sources;
        t->RSTx1R = cfg->reset_sources;
    }
    else
    {
        field = HRTIM_OUTR_POL2 | HRTIM_OUTR_IDLES2 | HRTIM_OUTR_FAULT2;
        value <<= HRTIM_OUTR_OUT2_SHIFT;
        t->SETx2R = cfg->set_sources;
        t->RSTx2R = cfg->reset_sources;
    }
    t->OUTxR = (t->OUTxR & ~field) | value;
    return SPL_OK;
}

SPL_Status HRTIM_DeadTimeConfig(uint32_t timer, const HRTIM_DeadTimeTypeDef *cfg)
{
    HRTIM_Timerx_TypeDef *t;

    /* SAFE: NULL isaretci okuma/yazma etkisi hedef mimaride guvenilir bir hata degildir. */
    SPL_REQUIRE_SAFE(!((timer >= HRTIM_TIMER_COUNT) || (cfg == (const HRTIM_DeadTimeTypeDef *)0)), SPL_ERR_PARAM);
    /* SAFE: gecersiz alan / maske register semantigini bozabilir veya yanlis donanim durumuna yol acar. */
    SPL_REQUIRE_SAFE(!((cfg->enable > 1U) ||
        ((cfg->enable != 0U) && ((cfg->prescaler > HRTIM_DTPRSC_MAX) ||
                                 (cfg->rising > HRTIM_DT_MAX) || (cfg->falling > HRTIM_DT_MAX)))), SPL_ERR_PARAM);
    t = Hrtim_Timer(timer);
    /* RM0440 28.5.52 DTEN: not while the timer runs or its outputs are enabled; 28.5.26: the
     * lock bits make value, sign and prescaler read-only. */
    if ((Hrtim_Running(timer) != 0U) ||
        ((HRTIM1->sCommonRegs.OENR & Hrtim_TimerOutputs(timer)) != 0U) ||
        ((t->DTxR & HRTIM_DT_LOCKS) != 0U))
    {
        return SPL_ERR_STATE;
    }
    if (cfg->enable == 0U)
    {
        t->OUTxR &= ~HRTIM_OUTR_DTEN;
        return SPL_OK;
    }
    t->DTxR = (cfg->rising << HRTIM_DTR_DTR_Pos) | (cfg->prescaler << HRTIM_DTR_DTPRSC_Pos) |
              (cfg->falling << HRTIM_DTR_DTF_Pos);
    t->OUTxR |= HRTIM_OUTR_DTEN;
    return SPL_OK;
}

SPL_Status HRTIM_ForceOutput(uint32_t timer, uint32_t output, uint8_t active)
{
    HRTIM_Timerx_TypeDef *t;
    volatile uint32_t *reg;

    /* SAFE: gecersiz timer secimi register erisimini / olay maskesini bozabilir. */
    SPL_REQUIRE_SAFE(!((timer >= HRTIM_TIMER_COUNT) || (output < 1U) || (output > 2U) || (active > 1U)), SPL_ERR_PARAM);
    t = Hrtim_Timer(timer);
    if (output == 1U)
    {
        reg = (active != 0U) ? &t->SETx1R : &t->RSTx1R;
    }
    else
    {
        reg = (active != 0U) ? &t->SETx2R : &t->RSTx2R;
    }
    /* The source bits are kept; SST/SRT is the only trigger in the register. */
    *reg |= HRTIM_SW_TRIGGER;
    return SPL_OK;
}

SPL_Status HRTIM_OutputEnable(uint32_t mask)
{
    /* SAFE: gecersiz bayrak/maske ayrilmis bitlere yazim yapabilir. */
    SPL_REQUIRE_SAFE(!((mask == 0U) || ((mask & ~HRTIM_OUTPUT_ALL) != 0U)), SPL_ERR_PARAM);
    HRTIM1->sCommonRegs.OENR = mask; /* writing 0 has no effect: no read-modify-write */
    return SPL_OK;
}

SPL_Status HRTIM_OutputDisable(uint32_t mask)
{
    /* SAFE: gecersiz bayrak/maske ayrilmis bitlere yazim yapabilir. */
    SPL_REQUIRE_SAFE(!((mask == 0U) || ((mask & ~HRTIM_OUTPUT_ALL) != 0U)), SPL_ERR_PARAM);
    HRTIM1->sCommonRegs.ODISR = mask;
    return SPL_OK;
}

SPL_Status HRTIM_OutputStatus(uint32_t *enabled, uint32_t *disable_status)
{
    /* SAFE: NULL isaretci okuma/yazma etkisi hedef mimaride guvenilir bir hata degildir. */
    SPL_REQUIRE_SAFE(!((enabled == (uint32_t *)0) || (disable_status == (uint32_t *)0)), SPL_ERR_PARAM);
    *enabled = HRTIM1->sCommonRegs.OENR & HRTIM_OUTPUT_ALL;
    *disable_status = HRTIM1->sCommonRegs.ODSR & HRTIM_OUTPUT_ALL;
    return SPL_OK;
}

SPL_Status HRTIM_CounterEnable(uint32_t timer_mask, uint8_t enable)
{
    const uint32_t bits = timer_mask << HRTIM_MCR_TCEN_POS;

    /* SAFE: gecersiz timer secimi register erisimini / olay maskesini bozabilir. */
    SPL_REQUIRE_SAFE(!((timer_mask == 0U) || ((timer_mask & ~HRTIM_TIMER_ALL) != 0U) || (enable > 1U)), SPL_ERR_PARAM);
    if (enable == 0U)
    {
        HRTIM1->sMasterRegs.MCR &= ~bits;
        return SPL_OK;
    }
    /* RM0440 28.3.24: timing units start only once the DLL is ready. */
    if (Hrtim_DllReadyFlag() == 0U)
    {
        return SPL_ERR_STATE;
    }
    HRTIM1->sMasterRegs.MCR |= bits;
    return SPL_OK;
}

static SPL_Status Hrtim_Cr2Trigger(uint32_t timer_mask, uint32_t pos)
{
    /* SAFE: gecersiz timer secimi register erisimini / olay maskesini bozabilir. */
    SPL_REQUIRE_SAFE(!((timer_mask == 0U) || ((timer_mask & ~HRTIM_TIMER_ALL) != 0U)), SPL_ERR_PARAM);
    /* Only the SWPx bits are read/write state; no pending trigger is copied back. */
    HRTIM1->sCommonRegs.CR2 = (HRTIM1->sCommonRegs.CR2 & HRTIM_CR2_SWP_ALL) | (timer_mask << pos);
    return SPL_OK;
}

SPL_Status HRTIM_SoftwareUpdate(uint32_t timer_mask)
{
    return Hrtim_Cr2Trigger(timer_mask, HRTIM_CR2_SWU_POS);
}

SPL_Status HRTIM_SoftwareReset(uint32_t timer_mask)
{
    return Hrtim_Cr2Trigger(timer_mask, HRTIM_CR2_RST_POS);
}

SPL_Status HRTIM_GetFlags(uint32_t timer, uint32_t *flags)
{
    /* SAFE: NULL isaretci okuma/yazma etkisi hedef mimaride guvenilir bir hata degildir. */
    SPL_REQUIRE_SAFE(!((timer >= HRTIM_TIMER_COUNT) || (flags == (uint32_t *)0)), SPL_ERR_PARAM);
    *flags = Hrtim_Timer(timer)->TIMxISR;
    return SPL_OK;
}

SPL_Status HRTIM_ClearFlags(uint32_t timer, uint32_t flags)
{
    /* SAFE: gecersiz timer secimi register erisimini / olay maskesini bozabilir. */
    SPL_REQUIRE_SAFE(!((timer >= HRTIM_TIMER_COUNT) || (flags == 0U) || ((flags & ~HRTIM_TIMFLAG_ALL) != 0U)), SPL_ERR_PARAM);
    Hrtim_Timer(timer)->TIMxICR = flags; /* W1C: no read-modify-write */
    /* Read back: the clear reaches the peripheral before return (METHOD 1.7, issue #23). */
    (void)Hrtim_Timer(timer)->TIMxISR;
    return SPL_OK;
}

SPL_Status HRTIM_GetCommonFlags(uint32_t *flags)
{
    /* SAFE: NULL isaretci okuma/yazma etkisi hedef mimaride guvenilir bir hata degildir. */
    SPL_REQUIRE_SAFE(!(flags == (uint32_t *)0), SPL_ERR_PARAM);
    *flags = HRTIM1->sCommonRegs.ISR;
    return SPL_OK;
}

SPL_Status HRTIM_ClearCommonFlags(uint32_t flags)
{
    /* SAFE: gecersiz bayrak/maske ayrilmis bitlere yazim yapabilir. */
    SPL_REQUIRE_SAFE(!((flags == 0U) || ((flags & ~HRTIM_COMMON_CLEARABLE) != 0U)), SPL_ERR_PARAM);
    HRTIM1->sCommonRegs.ICR = flags;
    /* Read back: the clear reaches the peripheral before return (METHOD 1.7, issue #23). */
    (void)HRTIM1->sCommonRegs.ISR;
    return SPL_OK;
}

#else /* no HRTIM1 on this SKU */

SPL_Status HRTIM_DllCalibrationStart(void)
{
    return SPL_ERR_UNSUPPORTED;
}

SPL_Status HRTIM_DllReady(uint8_t *ready)
{
    (void)ready;
    return SPL_ERR_UNSUPPORTED;
}

SPL_Status HRTIM_DllPeriodic(uint8_t enable, uint32_t rate)
{
    (void)enable;
    (void)rate;
    return SPL_ERR_UNSUPPORTED;
}

SPL_Status HRTIM_TimerInit(uint32_t timer, const HRTIM_TimerInitTypeDef *cfg)
{
    (void)timer;
    (void)cfg;
    return SPL_ERR_UNSUPPORTED;
}

SPL_Status HRTIM_SetPeriod(uint32_t timer, uint32_t period)
{
    (void)timer;
    (void)period;
    return SPL_ERR_UNSUPPORTED;
}

SPL_Status HRTIM_SetCompare(uint32_t timer, uint32_t unit, uint32_t value)
{
    (void)timer;
    (void)unit;
    (void)value;
    return SPL_ERR_UNSUPPORTED;
}

SPL_Status HRTIM_OutputConfig(uint32_t timer, uint32_t output, const HRTIM_OutputTypeDef *cfg)
{
    (void)timer;
    (void)output;
    (void)cfg;
    return SPL_ERR_UNSUPPORTED;
}

SPL_Status HRTIM_DeadTimeConfig(uint32_t timer, const HRTIM_DeadTimeTypeDef *cfg)
{
    (void)timer;
    (void)cfg;
    return SPL_ERR_UNSUPPORTED;
}

SPL_Status HRTIM_ForceOutput(uint32_t timer, uint32_t output, uint8_t active)
{
    (void)timer;
    (void)output;
    (void)active;
    return SPL_ERR_UNSUPPORTED;
}

SPL_Status HRTIM_OutputEnable(uint32_t mask)
{
    (void)mask;
    return SPL_ERR_UNSUPPORTED;
}

SPL_Status HRTIM_OutputDisable(uint32_t mask)
{
    (void)mask;
    return SPL_ERR_UNSUPPORTED;
}

SPL_Status HRTIM_OutputStatus(uint32_t *enabled, uint32_t *disable_status)
{
    (void)enabled;
    (void)disable_status;
    return SPL_ERR_UNSUPPORTED;
}

SPL_Status HRTIM_CounterEnable(uint32_t timer_mask, uint8_t enable)
{
    (void)timer_mask;
    (void)enable;
    return SPL_ERR_UNSUPPORTED;
}

SPL_Status HRTIM_SoftwareUpdate(uint32_t timer_mask)
{
    (void)timer_mask;
    return SPL_ERR_UNSUPPORTED;
}

SPL_Status HRTIM_SoftwareReset(uint32_t timer_mask)
{
    (void)timer_mask;
    return SPL_ERR_UNSUPPORTED;
}

SPL_Status HRTIM_GetFlags(uint32_t timer, uint32_t *flags)
{
    (void)timer;
    (void)flags;
    return SPL_ERR_UNSUPPORTED;
}

SPL_Status HRTIM_ClearFlags(uint32_t timer, uint32_t flags)
{
    (void)timer;
    (void)flags;
    return SPL_ERR_UNSUPPORTED;
}

SPL_Status HRTIM_GetCommonFlags(uint32_t *flags)
{
    (void)flags;
    return SPL_ERR_UNSUPPORTED;
}

SPL_Status HRTIM_ClearCommonFlags(uint32_t flags)
{
    (void)flags;
    return SPL_ERR_UNSUPPORTED;
}

#endif /* HRTIM1 */
