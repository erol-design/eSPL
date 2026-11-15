#define SPL_LIBRARY_BUILD 1
#include "stm32g4_tim.h"

static uint8_t Tim_ChannelCount(const TIM_TypeDef *t)
{
    if ((t == TIM1) || (t == TIM2) || (t == TIM3)
#if defined(TIM4)
        || (t == TIM4)
#endif
#if defined(TIM5)
        || (t == TIM5)
#endif
#if defined(TIM8)
        || (t == TIM8)
#endif
#if defined(TIM20)
        || (t == TIM20)
#endif
    )
    {
        return 4U;
    }
    if (t == TIM15)
    {
        return 2U;
    }
    if ((t == TIM16) || (t == TIM17))
    {
        return 1U;
    }
    return 0U;
}
static uint8_t Tim_IsBase(const TIM_TypeDef *t)
{
    return (uint8_t)(((Tim_ChannelCount(t) != 0U) || (t == TIM6) || (t == TIM7)) ? 1U : 0U);
}
static uint8_t Tim_Is32(const TIM_TypeDef *t)
{
    return (uint8_t)(((t == TIM2)
#if defined(TIM5)
                      || (t == TIM5)
#endif
                          )
                         ? 1U
                         : 0U);
}
static uint8_t Tim_ChannelValid(const TIM_TypeDef *t, uint8_t ch)
{
    return (uint8_t)(((ch >= 1U) && (ch <= Tim_ChannelCount(t))) ? 1U : 0U);
}
static uint8_t Tim_WidthValid(const TIM_TypeDef *t, uint32_t value)
{
    return (uint8_t)(((Tim_Is32(t) != 0U) || (value <= 0xFFFFU)) ? 1U : 0U);
}
/* Timers with a BDTR register (MOE, break, LOCK): TIM1/8/20/15/16/17. */
static uint8_t Tim_HasBdtr(const TIM_TypeDef *t)
{
    return (uint8_t)(((t == TIM1)
#if defined(TIM8)
                      || (t == TIM8)
#endif
#if defined(TIM20)
                      || (t == TIM20)
#endif
                      || (t == TIM15) || (t == TIM16) || (t == TIM17))
                         ? 1U
                         : 0U);
}
/* BKDSRM/BK2DSRM: set by software, cleared by hardware (RM0440 29.6.20).
 * The legacy minimal host mock has no such bits. */
#if defined(TIM_BDTR_BK2DSRM)
#define TIM_BDTR_DSRM_BITS (TIM_BDTR_BKDSRM | TIM_BDTR_BK2DSRM)
#else
#define TIM_BDTR_DSRM_BITS 0U
#endif
/* LOCK level >= 2 freezes CCxP of output channels; level 3 also OCxM/OCxPE
 * (RM0440 29.6.20 LOCK[1:0]). Timers without BDTR have no LOCK. */
static uint8_t Tim_PolarityLocked(const TIM_TypeDef *t)
{
#if defined(TIM_BDTR_LOCK_Msk)
    return (uint8_t)(((Tim_HasBdtr(t) != 0U) &&
                      (((t->BDTR & TIM_BDTR_LOCK_Msk) >> TIM_BDTR_LOCK_Pos) >= 2U))
                         ? 1U
                         : 0U);
#else
    (void)t;
    return 0U;
#endif
}
#if defined(TIM_SPL_EXTENDED)
static uint32_t Tim_FlagMask(const TIM_TypeDef *t); /* valid SR flags per timer type */
#endif
static volatile uint32_t *Tim_Ccr(TIM_TypeDef *t, uint8_t ch)
{
    switch (ch)
    {
    case 1U:
        return &t->CCR1;
    case 2U:
        return &t->CCR2;
    case 3U:
        return &t->CCR3;
    default:
        return &t->CCR4;
    }
}
SPL_Status TIM_BaseInit(TIM_TypeDef *t, const TIM_BaseInitTypeDef *cfg)
{
    uint32_t cr1;
    /* SAFE: reject invalid parameters before hardware access. */
    SPL_REQUIRE_SAFE(!((Tim_IsBase(t) == 0U) || (cfg == (const TIM_BaseInitTypeDef *)0) ||
        (cfg->auto_reload_preload > 1U) || (cfg->prescaler > 0xFFFFU) || (cfg->period == 0U) ||
        (Tim_WidthValid(t, cfg->period) == 0U)), SPL_ERR_PARAM);
    /* Issue #27 politikasi: yazilmis CCRx sinanmaz; periyot karsilastirmanin altina inebilir. */
    if ((t->CR1 & TIM_CR1_CEN) != 0U)
    {
        return SPL_ERR_STATE;
    }
    /* Preserve direction, one-pulse and unrelated CR1 bits. */
    cr1 = t->CR1 & (uint32_t)~TIM_CR1_ARPE;
    if (cfg->auto_reload_preload != 0U)
    {
        cr1 |= TIM_CR1_ARPE;
    }
    t->CR1 = cr1;
    t->PSC = cfg->prescaler;
    t->ARR = cfg->period;
    /* Caller decides when to generate UG, avoiding implicit update interrupt. */
    return SPL_OK;
}
SPL_Status TIM_Enable(TIM_TypeDef *t, uint8_t enable)
{
    /* SAFE: reject invalid parameters before hardware access. */
    SPL_REQUIRE_SAFE(!((Tim_IsBase(t) == 0U) || (enable > 1U)), SPL_ERR_PARAM);
#if defined(TIM_CR1_OPM)
    /* One-pulse mode: CEN=1 means a pulse is running; a new request would be
     * lost (hardware clears CEN at the end of the pulse). */
    if ((enable != 0U) && ((t->CR1 & (TIM_CR1_OPM | TIM_CR1_CEN)) == (TIM_CR1_OPM | TIM_CR1_CEN)))
    {
        return SPL_BUSY;
    }
#endif
    if (enable != 0U)
    {
        t->CR1 |= TIM_CR1_CEN;
    }
    else
    {
        t->CR1 &= (uint32_t)~TIM_CR1_CEN;
    }
    return SPL_OK;
}
SPL_Status TIM_GenerateUpdate(TIM_TypeDef *t)
{
    /* SAFE: reject invalid parameters before hardware access. */
    SPL_REQUIRE_SAFE(!(Tim_IsBase(t) == 0U), SPL_ERR_PARAM);
    t->EGR = TIM_EGR_UG;
    return SPL_OK;
}
SPL_Status TIM_UpdateInterrupt(TIM_TypeDef *t, uint8_t enable)
{
    /* SAFE: reject invalid parameters before hardware access. */
    SPL_REQUIRE_SAFE(!((Tim_IsBase(t) == 0U) || (enable > 1U)), SPL_ERR_PARAM);
    if (enable != 0U)
    {
        t->DIER |= TIM_DIER_UIE;
    }
    else
    {
        t->DIER &= (uint32_t)~TIM_DIER_UIE;
    }
    return SPL_OK;
}
SPL_Status TIM_UpdatePending(TIM_TypeDef *t, uint8_t *pending)
{
    /* SAFE: reject invalid parameters before hardware access. */
    SPL_REQUIRE_SAFE(!((Tim_IsBase(t) == 0U) || (pending == (uint8_t *)0)), SPL_ERR_PARAM);
    *pending = (uint8_t)(((t->SR & TIM_SR_UIF) != 0U) ? 1U : 0U);
    return SPL_OK;
}
SPL_Status TIM_ClearUpdate(TIM_TypeDef *t)
{
    /* SAFE: reject invalid parameters before hardware access. */
    SPL_REQUIRE_SAFE(!(Tim_IsBase(t) == 0U), SPL_ERR_PARAM);
#if defined(TIM_SPL_EXTENDED)
    /* TIM SR is rc_w0: zero on UIF, ones on the other flags of this timer type,
     * reserved bits at their reset value 0 (same value as TIM_ClearFlags). */
    t->SR = Tim_FlagMask(t) & ~(uint32_t)TIM_SR_UIF;
#else
    /* Host mock header without the full SR map (never a target build). */
    t->SR = ~(uint32_t)TIM_SR_UIF;
#endif
    /* Read back: the clear reaches the peripheral before return (METHOD 1.7, issue #23). */
    (void)t->SR;
    return SPL_OK;
}
SPL_Status TIM_OutputCompareInit(TIM_TypeDef *t, uint8_t ch, const TIM_OCInitTypeDef *cfg)
{
    volatile uint32_t *ccmr;
    uint32_t shift;
    uint32_t mask;
    uint32_t field;
    uint32_t ccer_mask;
    uint32_t ccmr_value;
    /* SAFE: reject invalid parameters before hardware access. */
    SPL_REQUIRE_SAFE(!((Tim_ChannelValid(t, ch) == 0U) || (cfg == (const TIM_OCInitTypeDef *)0) ||
        (cfg->inverted > 1U) || (cfg->preload > 1U) || (Tim_WidthValid(t, cfg->compare) == 0U)), SPL_ERR_PARAM);
    /* SAFE: reject invalid parameters before hardware access. */
    SPL_REQUIRE_SAFE(!(!((cfg->mode == (uint32_t)TIM_OC_FROZEN) ||
          (cfg->mode == (uint32_t)TIM_OC_ACTIVE_ON_MATCH) ||
          (cfg->mode == (uint32_t)TIM_OC_INACTIVE_ON_MATCH) ||
          (cfg->mode == (uint32_t)TIM_OC_TOGGLE) || (cfg->mode == (uint32_t)TIM_OC_PWM1) ||
          (cfg->mode == (uint32_t)TIM_OC_PWM2))), SPL_ERR_PARAM);
    /* Issue #27 politikasi: compare ARR'ye gore sinanmaz (CCR > ARR gecerli), yalniz genislik. */
    ccer_mask = (uint32_t)1U << (4U * ((uint32_t)ch - 1U));
    if (((t->CR1 & TIM_CR1_CEN) != 0U) || ((t->CCER & ccer_mask) != 0U))
    {
        return SPL_ERR_STATE;
    }
    /* The CCMR write below makes the channel an output before CCxP is
     * written, so LOCK >= 2 would silently drop CCxP (and OCxM/OCxPE at 3). */
    if (Tim_PolarityLocked(t) != 0U)
    {
        return SPL_ERR_STATE;
    }
    ccmr = (ch <= 2U) ? &t->CCMR1 : &t->CCMR2;
    shift = (((uint32_t)ch - 1U) % 2U) * 8U;
    /* OCxM[3] is at bit 16 (odd channel) / 24 (even channel).
     * Clear it too, or an old extended PWM mode can survive re-init. */
    mask = ((uint32_t)0xFFU << shift) | ((uint32_t)1U << (((ch & 1U) != 0U) ? 16U : 24U));
    field = (((uint32_t)cfg->mode << 4U) | ((uint32_t)cfg->preload << 3U)) << shift;
    ccmr_value = *ccmr;
    *ccmr = (ccmr_value & ~mask) | field;
    t->CCER = (t->CCER & ~(ccer_mask << 1U)) |
              ((uint32_t)cfg->inverted << (4U * ((uint32_t)ch - 1U) + 1U));
    *Tim_Ccr(t, ch) = cfg->compare;
    return SPL_OK;
}
SPL_Status TIM_InputCaptureInit(TIM_TypeDef *t, uint8_t ch, const TIM_ICInitTypeDef *cfg)
{
    volatile uint32_t *ccmr;
    uint32_t shift;
    uint32_t ccer_shift;
    uint32_t ccmr_value;
    uint32_t mask;
    /* SAFE: reject invalid parameters before hardware access. */
    SPL_REQUIRE_SAFE(!((Tim_ChannelValid(t, ch) == 0U) || (cfg == (const TIM_ICInitTypeDef *)0) ||
        (cfg->falling_edge > 1U) || (cfg->filter > 15U) || (cfg->prescaler > 3U)), SPL_ERR_PARAM);
    ccer_shift = 4U * ((uint32_t)ch - 1U);
    if (((t->CR1 & TIM_CR1_CEN) != 0U) || ((t->CCER & ((uint32_t)1U << ccer_shift)) != 0U))
    {
        return SPL_ERR_STATE;
    }
    ccmr = (ch <= 2U) ? &t->CCMR1 : &t->CCMR2;
    shift = (((uint32_t)ch - 1U) % 2U) * 8U;
    ccmr_value = *ccmr;
    mask = ((uint32_t)0xFFU << shift) | ((uint32_t)1U << (((ch & 1U) != 0U) ? 16U : 24U));
    *ccmr = (ccmr_value & ~mask) |
            ((1U | ((uint32_t)cfg->prescaler << 2U) | ((uint32_t)cfg->filter << 4U)) << shift);
    /* Preserve enable and complementary outputs, clear both capture polarity bits. */
    t->CCER = (t->CCER & ~((uint32_t)0xAU << ccer_shift)) |
              ((uint32_t)cfg->falling_edge << (ccer_shift + 1U));
    return SPL_OK;
}
SPL_Status TIM_ChannelEnable(TIM_TypeDef *t, uint8_t ch, uint8_t enable)
{
    uint32_t mask;
    /* SAFE: reject invalid parameters before hardware access. */
    SPL_REQUIRE_SAFE(!((Tim_ChannelValid(t, ch) == 0U) || (enable > 1U)), SPL_ERR_PARAM);
    mask = (uint32_t)1U << (4U * ((uint32_t)ch - 1U));
    if (enable != 0U)
    {
        t->CCER |= mask;
    }
    else
    {
        t->CCER &= ~mask;
    }
    return SPL_OK;
}
SPL_Status TIM_SetCompare(TIM_TypeDef *t, uint8_t ch, uint32_t compare)
{
    /* SAFE: reject invalid parameters before hardware access. */
    SPL_REQUIRE_SAFE(!((Tim_ChannelValid(t, ch) == 0U) || (Tim_WidthValid(t, compare) == 0U)), SPL_ERR_PARAM);
    /* Issue #27 politikasi (HRTIM ile ayni): ARR ile iliski sinanmaz, yalniz register
     * genisligi. CCR > ARR gecerli bir degerdir: PWM kipinde OCxREF sabit kalir, karsilastirma
     * kipinde periyot icinde eslesme olmaz. Iki ayri register yazimi atomik degildir; OCxPE /
     * ARPE = 1 iken etkin degerler bir sonraki update olayinda birlikte yuklenir. */
    *Tim_Ccr(t, ch) = compare;
    return SPL_OK;
}
SPL_Status TIM_GetCapture(TIM_TypeDef *t, uint8_t ch, uint32_t *capture)
{
    uint32_t ccmr;

    /* SAFE: reject invalid parameters before hardware access. */
    SPL_REQUIRE_SAFE(!((Tim_ChannelValid(t, ch) == 0U) || (capture == (uint32_t *)0)), SPL_ERR_PARAM);
    /* Input channel (CCxS != 00): report a lost capture (CCxOF, SR bit ch + 8)
     * and a missing new one (CCxIF, SR bit ch) instead of returning a stale
     * value. Reading CCRx clears CCxIF; the flags are otherwise kept. */
    ccmr = (ch <= 2U) ? t->CCMR1 : t->CCMR2;
    if (((ccmr >> ((((uint32_t)ch - 1U) % 2U) * 8U)) & 3U) != 0U)
    {
        const uint32_t sr = t->SR;

        if ((sr & ((uint32_t)1U << ((uint32_t)ch + 8U))) != 0U)
        {
            return SPL_ERR_HW;
        }
        if ((sr & ((uint32_t)1U << (uint32_t)ch)) == 0U)
        {
            return SPL_WOULD_BLOCK;
        }
    }
    *capture = *Tim_Ccr(t, ch);
    return SPL_OK;
}
/* v0.15 (CCL-2 ZMN-23): MOE geri okumasinda en cok okuma sayisi. Olculen gecikme 1 okuma
 * (G431 Rev X); 4 okuma payli ust sinirdir, kirilma girisi etkinse hepsi 0 gorur. */
#define TIM_MOE_READBACK_READS 4U
/* v0.15 (CCL-2 ZMN-23): bu zamanlayicinin kirilma bayraklari (BIF; varsa B2IF ve SBIF).
 * Bayrak kuruluyken kirilma olmus ve ele alinmamis demektir; MOE acilmaz. Eski kucuk host
 * mock'unda bu bitler yoksa maske 0 olur. */
static uint32_t Tim_BreakFlags(const TIM_TypeDef *t)
{
#if defined(TIM_SPL_EXTENDED)
    return Tim_FlagMask(t) & (TIM_SR_BIF | TIM_SR_B2IF | TIM_SR_SBIF);
#elif defined(TIM_SR_BIF)
    (void)t;
    return TIM_SR_BIF;
#else
    (void)t;
    return 0U;
#endif
}
SPL_Status TIM_MainOutputEnable(TIM_TypeDef *t, uint8_t enable)
{
    uint32_t bdtr;

    /* SAFE: reject invalid parameters before hardware access. */
    SPL_REQUIRE_SAFE(!((enable > 1U) || (Tim_HasBdtr(t) == 0U)), SPL_ERR_PARAM);
    bdtr = t->BDTR;
    if (enable != 0U)
    {
        /* Break re-arm still in progress: MOE may be set only after hardware
         * has cleared BKDSRM/BK2DSRM (RM0440 29.3.19 re-arm procedure). */
        if ((bdtr & TIM_BDTR_DSRM_BITS) != 0U)
        {
            return SPL_BUSY;
        }
        /* v0.15 (CCL-2 ZMN-23): kirilma bayragi kuruluyken hicbir sey yazilmaz. Cagiran once
         * kirilmanin sebebini giderir, bayragi TIM_ClearFlags ile temizler (giris hala etkinse
         * bayrak temizlenmez, RM0440 29.6.5) ve sonra tekrar dener. Otomatik temizleme yok. */
        if ((t->SR & Tim_BreakFlags(t)) != 0U)
        {
            return SPL_ERR_HW;
        }
        bdtr |= TIM_BDTR_MOE;
    }
    else
    {
        bdtr &= (uint32_t)~TIM_BDTR_MOE;
    }
    /* Never copy a stale BKDSRM/BK2DSRM=1 back: hardware may have cleared it
     * between read and write, and with MOE=0 a 1 disarms the break (RM0440
     * Table 276). Writing 0 keeps the break armed. Any BDTR write also
     * freezes LOCK (RM0440 29.6.20): run TIM_BreakDeadTimeInit first. */
    t->BDTR = bdtr & (uint32_t)~TIM_BDTR_DSRM_BITS;
    /* v0.15 (CCL-2 ZMN-23): kirilma girisi etkinken donanim MOE'yi kurmaz (RM0440 29.3.16);
     * bayrak okumasi ile yazim arasinda gelen kirilma da burada gorulur. Geri okuma: MOE
     * tutmadiysa basari donulmez. MOE yazimi bir esitleme gecikmesiyle gorunur: NUCLEO-G431RB
     * Rev X olcumu (2026-11-12) yazimdan hemen sonraki ilk okuma 0, ikinci okuma 1. Bu yuzden
     * sabit sayida okuma yapilir (sinirli, dis olay beklenmez); MOE gorulunce hemen OK. */
    if (enable != 0U)
    {
        uint32_t i;

        for (i = 0U; i < TIM_MOE_READBACK_READS; i++)
        {
            if ((t->BDTR & TIM_BDTR_MOE) != 0U)
            {
                return SPL_OK;
            }
        }
        return SPL_ERR_HW;
    }
    return SPL_OK;
}

#if defined(TIM_SPL_EXTENDED)
/* ---- v0.10 extension, see Docs/v0.10_TIM_CONTRACT.md ---- */

/* SR flags per timer family (RM0440 29.6.5, 30.5.5, 31.7.5, 31.8.4, 32.4.4). */
#define TIM_FLAGS_ENCODER (TIM_SR_IDXF | TIM_SR_DIRF | TIM_SR_IERRF | TIM_SR_TERRF)
#define TIM_FLAGS_GENERAL                                                                          \
    (TIM_SR_UIF | TIM_SR_CC1IF | TIM_SR_CC2IF | TIM_SR_CC3IF | TIM_SR_CC4IF | TIM_SR_TIF |         \
     TIM_SR_CC1OF | TIM_SR_CC2OF | TIM_SR_CC3OF | TIM_SR_CC4OF | TIM_FLAGS_ENCODER)
#define TIM_FLAGS_ADVANCED                                                                         \
    (TIM_FLAGS_GENERAL | TIM_SR_COMIF | TIM_SR_BIF | TIM_SR_B2IF | TIM_SR_SBIF | TIM_SR_CC5IF |    \
     TIM_SR_CC6IF)
#define TIM_FLAGS_TIM15                                                                            \
    (TIM_SR_UIF | TIM_SR_CC1IF | TIM_SR_CC2IF | TIM_SR_COMIF | TIM_SR_TIF | TIM_SR_BIF |           \
     TIM_SR_CC1OF | TIM_SR_CC2OF)
#define TIM_FLAGS_TIM16_17 (TIM_SR_UIF | TIM_SR_CC1IF | TIM_SR_COMIF | TIM_SR_BIF | TIM_SR_CC1OF)

/* TIM1/8/20: advanced-control timers with break2 and four complementary channels. */
static uint8_t Tim_IsAdvanced(const TIM_TypeDef *t)
{
    uint8_t result = 0U;

    if (t == TIM1)
    {
        result = 1U;
    }
#if defined(TIM8)
    if (t == TIM8)
    {
        result = 1U;
    }
#endif
#if defined(TIM20)
    if (t == TIM20)
    {
        result = 1U;
    }
#endif
    return result;
}

/* TIM2/3/4/5: general-purpose timers with counter modes and encoder interface. */
static uint8_t Tim_IsGeneral(const TIM_TypeDef *t)
{
    uint8_t result = 0U;

    if ((t == TIM2) || (t == TIM3))
    {
        result = 1U;
    }
#if defined(TIM4)
    if (t == TIM4)
    {
        result = 1U;
    }
#endif
#if defined(TIM5)
    if (t == TIM5)
    {
        result = 1U;
    }
#endif
    return result;
}

/* Timers with CR1 DIR/CMS and encoder interface (RM0440 29.3.25, 30.4; TIM15 SMS
 * encoder codes are reserved, RM0440 31.7.3). */
static uint8_t Tim_HasCounterMode(const TIM_TypeDef *t)
{
    return (uint8_t)(((Tim_IsAdvanced(t) != 0U) || (Tim_IsGeneral(t) != 0U)) ? 1U : 0U);
}

static uint8_t Tim_IsSmall(const TIM_TypeDef *t)
{
    return (uint8_t)(((t == TIM15) || (t == TIM16) || (t == TIM17)) ? 1U : 0U);
}

/* Timers with BDTR, RCR and complementary outputs. */
static uint8_t Tim_HasBreak(const TIM_TypeDef *t)
{
    return (uint8_t)(((Tim_IsAdvanced(t) != 0U) || (Tim_IsSmall(t) != 0U)) ? 1U : 0U);
}

static uint8_t Tim_IsBasic(const TIM_TypeDef *t)
{
    return (uint8_t)(((t == TIM6) || (t == TIM7)) ? 1U : 0U);
}

static uint32_t Tim_FlagMask(const TIM_TypeDef *t)
{
    uint32_t mask = 0U;

    if (Tim_IsAdvanced(t) != 0U)
    {
        mask = TIM_FLAGS_ADVANCED;
    }
    else if (Tim_IsGeneral(t) != 0U)
    {
        mask = TIM_FLAGS_GENERAL;
    }
    else if (t == TIM15)
    {
        mask = TIM_FLAGS_TIM15;
    }
    else if ((t == TIM16) || (t == TIM17))
    {
        mask = TIM_FLAGS_TIM16_17;
    }
    else if (Tim_IsBasic(t) != 0U)
    {
        mask = TIM_SR_UIF;
    }
    else
    {
        mask = 0U;
    }
    return mask;
}

static uint8_t Tim_ComplementaryValid(const TIM_TypeDef *t, uint8_t ch)
{
    uint8_t count = 0U;

    if (Tim_IsAdvanced(t) != 0U)
    {
        count = 4U;
    }
    else if (Tim_IsSmall(t) != 0U)
    {
        count = 1U;
    }
    else
    {
        count = 0U;
    }
    return (uint8_t)(((ch >= 1U) && (ch <= count)) ? 1U : 0U);
}

static uint32_t Tim_LockLevel(const TIM_TypeDef *t)
{
    return (t->BDTR & TIM_BDTR_LOCK_Msk) >> TIM_BDTR_LOCK_Pos;
}

/* CCxS of channel 1..4: 0 means the channel is configured as output. */
static uint32_t Tim_ChannelSelection(const TIM_TypeDef *t, uint8_t ch)
{
    uint32_t ccmr = (ch <= 2U) ? t->CCMR1 : t->CCMR2;
    uint32_t shift = (((uint32_t)ch - 1U) % 2U) * 8U;

    return (ccmr >> shift) & TIM_CCMR1_CC1S_Msk;
}

static uint32_t Tim_SlaveMode(const TIM_TypeDef *t)
{
    uint32_t smcr = t->SMCR;
    uint32_t sms = smcr & (TIM_SMCR_SMS_0 | TIM_SMCR_SMS_1 | TIM_SMCR_SMS_2);

    if ((smcr & TIM_SMCR_SMS_3) != 0U)
    {
        sms |= 8U;
    }
    return sms;
}

SPL_Status TIM_CounterInit(TIM_TypeDef *t, const TIM_CounterInitTypeDef *cfg)
{
    uint32_t cr1;
    uint32_t bits = 0U;

    /* SAFE: reject invalid parameters before hardware access. */
    SPL_REQUIRE_SAFE(!((Tim_IsBase(t) == 0U) || (cfg == (const TIM_CounterInitTypeDef *)0)), SPL_ERR_PARAM);
    /* SAFE: reject invalid parameters before hardware access. */
    SPL_REQUIRE_SAFE(!((cfg->mode > (uint32_t)TIM_COUNTER_CENTER3) || (cfg->clock_division > 2U) ||
        (cfg->one_pulse > 1U) || (cfg->uif_remap > 1U)), SPL_ERR_PARAM);
    /* SAFE: reject invalid parameters before hardware access. */
    SPL_REQUIRE_SAFE(!((Tim_HasCounterMode(t) == 0U) && (cfg->mode != (uint32_t)TIM_COUNTER_UP)), SPL_ERR_PARAM);
    /* SAFE: reject invalid parameters before hardware access. */
    SPL_REQUIRE_SAFE(!((Tim_IsBasic(t) != 0U) && (cfg->clock_division != 0U)), SPL_ERR_PARAM);
    if ((t->CR1 & TIM_CR1_CEN) != 0U)
    {
        return SPL_ERR_STATE;
    }
    if (cfg->mode == (uint32_t)TIM_COUNTER_DOWN)
    {
        bits |= TIM_CR1_DIR;
    }
    else if (cfg->mode >= (uint32_t)TIM_COUNTER_CENTER1)
    {
        /* CENTER1..3 -> CMS = 01..11 */
        bits |= ((cfg->mode - 1U) << TIM_CR1_CMS_Pos) & TIM_CR1_CMS_Msk;
    }
    else
    {
        bits |= 0U;
    }
    bits |= (cfg->clock_division << TIM_CR1_CKD_Pos) & TIM_CR1_CKD_Msk;
    if (cfg->one_pulse != 0U)
    {
        bits |= TIM_CR1_OPM;
    }
    if (cfg->uif_remap != 0U)
    {
        bits |= TIM_CR1_UIFREMAP;
    }
    /* Preserve ARPE, URS, UDIS, DITHEN; CEN is known to be 0. */
    cr1 = t->CR1 &
          (uint32_t)~(TIM_CR1_DIR | TIM_CR1_CMS | TIM_CR1_CKD | TIM_CR1_OPM | TIM_CR1_UIFREMAP);
    t->CR1 = cr1 | bits;
    return SPL_OK;
}

SPL_Status TIM_SetRepetition(TIM_TypeDef *t, uint32_t repetition)
{
    uint32_t max = 0xFFU;

    /* SAFE: reject invalid parameters before hardware access. */
    SPL_REQUIRE_SAFE(!(Tim_HasBreak(t) == 0U), SPL_ERR_PARAM);
    if (Tim_IsAdvanced(t) != 0U)
    {
        max = 0xFFFFU;
    }
    /* SAFE: reject invalid parameters before hardware access. */
    SPL_REQUIRE_SAFE(!(repetition > max), SPL_ERR_PARAM);
    if ((t->CR1 & TIM_CR1_CEN) != 0U)
    {
        return SPL_ERR_STATE;
    }
    t->RCR = repetition;
    return SPL_OK;
}

SPL_Status TIM_GetCounter(TIM_TypeDef *t, uint32_t *count, uint8_t *uif_copy)
{
    uint32_t cnt;
    uint8_t remap;

    /* SAFE: reject invalid parameters before hardware access. */
    SPL_REQUIRE_SAFE(!((Tim_IsBase(t) == 0U) || (count == (uint32_t *)0)), SPL_ERR_PARAM);
    /* Read CR1 first, then CNT once, so value and UIF copy come from one sample. */
    remap = (uint8_t)(((t->CR1 & TIM_CR1_UIFREMAP) != 0U) ? 1U : 0U);
    cnt = t->CNT;
    if (Tim_Is32(t) == 0U)
    {
        *count = cnt & 0xFFFFU;
    }
    else if (remap != 0U)
    {
        *count = cnt & (uint32_t)~TIM_CNT_UIFCPY;
    }
    else
    {
        *count = cnt;
    }
    if (uif_copy != (uint8_t *)0)
    {
        *uif_copy = (uint8_t)(((remap != 0U) && ((cnt & TIM_CNT_UIFCPY) != 0U)) ? 1U : 0U);
    }
    return SPL_OK;
}

SPL_Status TIM_SetCounter(TIM_TypeDef *t, uint32_t count)
{
    /* SAFE: reject invalid parameters before hardware access. */
    SPL_REQUIRE_SAFE(!(Tim_IsBase(t) == 0U), SPL_ERR_PARAM);
    /* SAFE: reject invalid parameters before hardware access. */
    SPL_REQUIRE_SAFE(!(Tim_WidthValid(t, count) == 0U), SPL_ERR_PARAM);
    /* With UIFREMAP=1, bit 31 of a 32-bit CNT is the read-only UIF copy. */
    /* SAFE: reject invalid parameters before hardware access. */
    SPL_REQUIRE_SAFE(!(((t->CR1 & TIM_CR1_UIFREMAP) != 0U) && ((count & TIM_CNT_UIFCPY) != 0U)), SPL_ERR_PARAM);
    t->CNT = count;
    return SPL_OK;
}

SPL_Status TIM_EncoderInit(TIM_TypeDef *t, const TIM_EncoderInitTypeDef *cfg)
{
    uint32_t ccmr;
    uint32_t ccer;
    uint32_t smcr;

    /* SAFE: reject invalid parameters before hardware access. */
    SPL_REQUIRE_SAFE(!((Tim_HasCounterMode(t) == 0U) || (cfg == (const TIM_EncoderInitTypeDef *)0)), SPL_ERR_PARAM);
    /* SAFE: reject invalid parameters before hardware access. */
    SPL_REQUIRE_SAFE(!((cfg->mode < (uint32_t)TIM_ENCODER_X2_TI1) || (cfg->mode > (uint32_t)TIM_ENCODER_X4) ||
        (cfg->ti1_inverted > 1U) || (cfg->ti2_inverted > 1U) || (cfg->ti1_filter > 15U) ||
        (cfg->ti2_filter > 15U)), SPL_ERR_PARAM);
    /* CCxS is writable only with CCxE=0; encoder and external clock mode 2 are exclusive. */
    if (((t->CR1 & TIM_CR1_CEN) != 0U) || ((t->CCER & (TIM_CCER_CC1E | TIM_CCER_CC2E)) != 0U) ||
        ((t->SMCR & TIM_SMCR_ECE) != 0U))
    {
        return SPL_ERR_STATE;
    }
    /* CC1S = CC2S = 01, prescalers 0, filters; clear OC1M[3]/OC2M[3] too. */
    ccmr = t->CCMR1 & (uint32_t)~(0xFFFFU | TIM_CCMR1_OC1M_3 | TIM_CCMR1_OC2M_3);
    ccmr |= TIM_CCMR1_CC1S_0 | TIM_CCMR1_CC2S_0;
    ccmr |= (cfg->ti1_filter << TIM_CCMR1_IC1F_Pos) & TIM_CCMR1_IC1F_Msk;
    ccmr |= (cfg->ti2_filter << TIM_CCMR1_IC2F_Pos) & TIM_CCMR1_IC2F_Msk;
    t->CCMR1 = ccmr;
    /* Polarity via CC1P/CC2P; CC1NP and CC2NP must be kept low (RM0440 29.3.25). */
    ccer = t->CCER & (uint32_t)~(TIM_CCER_CC1P | TIM_CCER_CC1NP | TIM_CCER_CC2P | TIM_CCER_CC2NP);
    if (cfg->ti1_inverted != 0U)
    {
        ccer |= TIM_CCER_CC1P;
    }
    if (cfg->ti2_inverted != 0U)
    {
        ccer |= TIM_CCER_CC2P;
    }
    t->CCER = ccer;
    smcr = t->SMCR & (uint32_t)~TIM_SMCR_SMS;
    t->SMCR = smcr | cfg->mode;
    return SPL_OK;
}

SPL_Status TIM_GetDirection(TIM_TypeDef *t, uint8_t *down)
{
    /* SAFE: reject invalid parameters before hardware access. */
    SPL_REQUIRE_SAFE(!((Tim_HasCounterMode(t) == 0U) || (down == (uint8_t *)0)), SPL_ERR_PARAM);
    *down = (uint8_t)(((t->CR1 & TIM_CR1_DIR) != 0U) ? 1U : 0U);
    return SPL_OK;
}

SPL_Status TIM_ComplementaryOutputInit(TIM_TypeDef *t, uint8_t ch, uint8_t inverted)
{
    uint32_t shift;
    uint32_t ccer;

    /* SAFE: reject invalid parameters before hardware access. */
    SPL_REQUIRE_SAFE(!((Tim_ComplementaryValid(t, ch) == 0U) || (inverted > 1U)), SPL_ERR_PARAM);
    shift = 4U * ((uint32_t)ch - 1U);
    if ((t->CCER & ((uint32_t)TIM_CCER_CC1NE << shift)) != 0U)
    {
        return SPL_ERR_STATE;
    }
    /* LOCK level 2 freezes CCxNP while the channel is an output (CCxS=00). */
    if ((Tim_LockLevel(t) >= 2U) && (Tim_ChannelSelection(t, ch) == 0U))
    {
        return SPL_ERR_STATE;
    }
    ccer = t->CCER & ~((uint32_t)TIM_CCER_CC1NP << shift);
    t->CCER = ccer | ((uint32_t)inverted << (TIM_CCER_CC1NP_Pos + shift));
    return SPL_OK;
}

SPL_Status TIM_ComplementaryEnable(TIM_TypeDef *t, uint8_t ch, uint8_t enable)
{
    uint32_t mask;

    /* SAFE: reject invalid parameters before hardware access. */
    SPL_REQUIRE_SAFE(!((Tim_ComplementaryValid(t, ch) == 0U) || (enable > 1U)), SPL_ERR_PARAM);
    /* DTG = 0: CCx and CCxN switch at the same edge, a bridge would conduct
     * through both switches. Program the dead time (TIM_BreakDeadTimeInit) first. */
    if ((enable != 0U) && ((t->BDTR & TIM_BDTR_DTG_Msk) == 0U))
    {
        return SPL_ERR_STATE;
    }
    mask = (uint32_t)TIM_CCER_CC1NE << (4U * ((uint32_t)ch - 1U));
    if (enable != 0U)
    {
        t->CCER |= mask;
    }
    else
    {
        t->CCER &= ~mask;
    }
    return SPL_OK;
}

/* RM0440 29.6.20 DTG encoding; rounded UP to the next representable dead time. */
SPL_Status TIM_DeadTimeToDtg(uint32_t dts_hz, uint32_t dead_time_ns, uint8_t *dtg)
{
    uint64_t ticks;
    uint32_t n;
    uint32_t value;

    /* SAFE: reject invalid parameters before hardware access. */
    SPL_REQUIRE_SAFE(!((dts_hz == 0U) || (dtg == (uint8_t *)0)), SPL_ERR_PARAM);
    ticks = (((uint64_t)dead_time_ns * dts_hz) + 999999999ULL) / 1000000000ULL;
    /* SAFE: reject invalid parameters before hardware access. */
    SPL_REQUIRE_SAFE(!(ticks > 1008U), SPL_ERR_PARAM);
    n = (uint32_t)ticks;
    if (n <= 127U)
    {
        value = n;
    }
    else if (((n + 1U) / 2U) <= 127U)
    {
        value = 0x80U | (((n + 1U) / 2U) - 64U);
    }
    else if (((n + 7U) / 8U) <= 63U)
    {
        value = 0xC0U | (((n + 7U) / 8U) - 32U);
    }
    else
    {
        value = 0xE0U | (((n + 15U) / 16U) - 32U);
    }
    *dtg = (uint8_t)value;
    return SPL_OK;
}

SPL_Status TIM_BreakDeadTimeInit(TIM_TypeDef *t, const TIM_BreakDeadTimeInitTypeDef *cfg)
{
    uint32_t bdtr;

    /* SAFE: reject invalid parameters before hardware access. */
    SPL_REQUIRE_SAFE(!((Tim_HasBreak(t) == 0U) || (cfg == (const TIM_BreakDeadTimeInitTypeDef *)0)), SPL_ERR_PARAM);
    /* SAFE: reject invalid parameters before hardware access. */
    SPL_REQUIRE_SAFE(!((cfg->dead_time > 0xFFU) || (cfg->lock_level > 3U) || (cfg->off_state_run > 1U) ||
        (cfg->off_state_idle > 1U) || (cfg->auto_output_enable > 1U) || (cfg->break_enable > 1U) ||
        (cfg->break_active_high > 1U) || (cfg->break_filter > 15U) || (cfg->break2_enable > 1U) ||
        (cfg->break2_active_high > 1U) || (cfg->break2_filter > 15U)), SPL_ERR_PARAM);
    /* SAFE: reject invalid parameters before hardware access. */
    SPL_REQUIRE_SAFE(!((Tim_IsAdvanced(t) == 0U) &&
        ((cfg->break2_enable != 0U) || (cfg->break2_active_high != 0U) ||
         (cfg->break2_filter != 0U))), SPL_ERR_PARAM);
    /* RM0440 29.6.20: BRKIN2 must only be used with OSSR = OSSI = 1. */
    /* SAFE: reject invalid parameters before hardware access. */
    SPL_REQUIRE_SAFE(!((cfg->break2_enable != 0U) && ((cfg->off_state_run == 0U) || (cfg->off_state_idle == 0U))), SPL_ERR_PARAM);
    /* Outputs must be off; LOCK >= 1 freezes DTG/BKE/BKP/AOE/BKF/BK2x and LOCK itself. */
    if (((t->BDTR & TIM_BDTR_MOE) != 0U) || (Tim_LockLevel(t) != 0U))
    {
        return SPL_ERR_STATE;
    }
    bdtr = (cfg->dead_time << TIM_BDTR_DTG_Pos) & TIM_BDTR_DTG_Msk;
    bdtr |= (cfg->lock_level << TIM_BDTR_LOCK_Pos) & TIM_BDTR_LOCK_Msk;
    bdtr |= (cfg->break_filter << TIM_BDTR_BKF_Pos) & TIM_BDTR_BKF_Msk;
    bdtr |= (cfg->break2_filter << TIM_BDTR_BK2F_Pos) & TIM_BDTR_BK2F_Msk;
    if (cfg->off_state_idle != 0U)
    {
        bdtr |= TIM_BDTR_OSSI;
    }
    if (cfg->off_state_run != 0U)
    {
        bdtr |= TIM_BDTR_OSSR;
    }
    if (cfg->break_enable != 0U)
    {
        bdtr |= TIM_BDTR_BKE;
    }
    if (cfg->break_active_high != 0U)
    {
        bdtr |= TIM_BDTR_BKP;
    }
    if (cfg->auto_output_enable != 0U)
    {
        bdtr |= TIM_BDTR_AOE;
    }
    if (cfg->break2_enable != 0U)
    {
        bdtr |= TIM_BDTR_BK2E;
    }
    if (cfg->break2_active_high != 0U)
    {
        bdtr |= TIM_BDTR_BK2P;
    }
    /* Single write: MOE, BKBID/BK2BID and BKDSRM/BK2DSRM are written as 0. */
    t->BDTR = bdtr;
    /* v0.14 (CCL-2 ZMN-13, as G0 af576c6): LOCK is write-once after reset (RM0440 29.6.20).
     * An earlier BDTR write (e.g. TIM_MainOutputEnable) froze it at 0, so the requested level
     * was dropped; LOCK 0 locks nothing, so the other fields above were written. Report it. */
    if (Tim_LockLevel(t) != (uint32_t)cfg->lock_level)
    {
        return SPL_ERR_STATE;
    }
    return SPL_OK;
}

SPL_Status TIM_GetLockLevel(TIM_TypeDef *t, uint8_t *level)
{
    /* SAFE: reject invalid parameters before hardware access. */
    SPL_REQUIRE_SAFE(!((Tim_HasBreak(t) == 0U) || (level == (uint8_t *)0)), SPL_ERR_PARAM);
    *level = (uint8_t)Tim_LockLevel(t);
    return SPL_OK;
}

SPL_Status TIM_GetFlags(TIM_TypeDef *t, uint32_t *flags)
{
    /* SAFE: reject invalid parameters before hardware access. */
    SPL_REQUIRE_SAFE(!((Tim_IsBase(t) == 0U) || (flags == (uint32_t *)0)), SPL_ERR_PARAM);
    *flags = t->SR & Tim_FlagMask(t);
    return SPL_OK;
}

SPL_Status TIM_ClearFlags(TIM_TypeDef *t, uint32_t flags)
{
    uint32_t valid;

    /* SAFE: reject invalid parameters before hardware access. */
    SPL_REQUIRE_SAFE(!(Tim_IsBase(t) == 0U), SPL_ERR_PARAM);
    valid = Tim_FlagMask(t);
    /* SAFE: reject invalid parameters before hardware access. */
    SPL_REQUIRE_SAFE(!((flags == 0U) || ((flags & ~valid) != 0U)), SPL_ERR_PARAM);
    /* rc_w0: zeros clear the requested flags, ones leave the other flags untouched,
     * reserved bits are written at their reset value 0. No read-modify-write. */
    t->SR = valid & ~flags;
    /* Read back: the clear reaches the peripheral before return (METHOD 1.7, issue #23). */
    (void)t->SR;
    return SPL_OK;
}

SPL_Status TIM_SetMasterTrigger(TIM_TypeDef *t, uint32_t source)
{
    uint32_t max;
    uint32_t cr2;

    if ((Tim_IsAdvanced(t) != 0U) || (Tim_IsGeneral(t) != 0U))
    {
        max = (uint32_t)TIM_TRGO_ENCODER_CLOCK;
    }
    else if (t == TIM15)
    {
        max = (uint32_t)TIM_TRGO_OC2REF;
    }
    else if (Tim_IsBasic(t) != 0U)
    {
        max = (uint32_t)TIM_TRGO_UPDATE;
    }
    else
    {
        return SPL_ERR_PARAM;
    }
    /* SAFE: reject invalid parameters before hardware access. */
    SPL_REQUIRE_SAFE(!(source > max), SPL_ERR_PARAM);
    if ((t->CR1 & TIM_CR1_CEN) != 0U)
    {
        return SPL_ERR_STATE;
    }
    /* RM0440 29.6.2: MMS=1000 is valid only with SMS = 0001/0010/0011/1010..1111. */
    if (source == (uint32_t)TIM_TRGO_ENCODER_CLOCK)
    {
        const uint32_t sms = Tim_SlaveMode(t);

        if (!(((sms >= 1U) && (sms <= 3U)) || (sms >= 10U)))
        {
            return SPL_ERR_STATE;
        }
    }
    cr2 = t->CR2 & (uint32_t)~TIM_CR2_MMS;
    cr2 |= (source & 7U) << TIM_CR2_MMS_Pos;
    if ((source & 8U) != 0U)
    {
        cr2 |= TIM_CR2_MMS_3;
    }
    t->CR2 = cr2;
    return SPL_OK;
}
#endif /* TIM_SPL_EXTENDED */
