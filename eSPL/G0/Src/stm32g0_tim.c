/* SPDX-License-Identifier: MIT */
/* TIM driver implementation; public contract: Inc/stm32g0_tim.h. Ported from STM32G4 SPL
 * v0.13.4 (Autometry_Mcu_Spl 4994f2b); reviewed against RM0444 Rev 6 sections 21..25 and ES0548
 * 2.8.x. G0 changes: timer set (TIM14 added; TIM5/8/20 removed), no encoder index flags, MMS[2:0]
 * only (no encoder-clock TRGO). */
/* Library unit: this unit also emits the spl_check.h mismatch-guard reference; the only unit
 * that does not is stm32g0_check.c (SPL_CHECK_LEVEL_DEFINER). BULGU-02, CHECK_LEVEL.md 3.2. */
#define SPL_LIBRARY_BUILD 1
#include "stm32g0_tim.h"
#include "stm32g0_sku.h"
#include "stm32g0_sku_inst.h"

/* v0.8: TIM2/4/6/7/15 are tested through SPL_G0_IS_TIMx (0 where the SKU lacks them).
 * G0B1 timers (RM0444 sections 21..25): TIM1 advanced, TIM2 (32 bit) / TIM3 / TIM4 general purpose,
 * TIM6 / TIM7 basic, TIM14 one channel (no CR2, SMCR, RCR, BDTR, DMA), TIM15 two channels,
 * TIM16 / TIM17 one channel with complementary output and break. */
/* Check level (A4 wave 3, Docs/CHECK_LEVEL.md 5.4 TIM table): SPL_REQUIRE = checked at FULL only
 * (not compiled at FAST). Type / capability helpers are static inline: those used only in checks
 * are dropped at FAST without a warning (pure, Tools/pure_checks.txt). Hardware events (CCxOF, OPM,
 * BUSY, MOE / LOCK read-back) are result: at every level. All macros come before the first MMIO
 * write (METHOD 1.3). */
static inline uint8_t Tim_ChannelCount(const TIM_TypeDef *t)
{
    if ((t == TIM1) || SPL_G0_IS_TIM2(t) || (t == TIM3) || SPL_G0_IS_TIM4(t))
    {
        return 4U;
    }
    if SPL_G0_IS_TIM15 (t)
    {
        return 2U;
    }
    if ((t == TIM14) || (t == TIM16) || (t == TIM17))
    {
        return 1U;
    }
    return 0U;
}
static inline uint8_t Tim_IsBase(const TIM_TypeDef *t)
{
    return (uint8_t)(((Tim_ChannelCount(t) != 0U) || SPL_G0_IS_TIM6(t) || SPL_G0_IS_TIM7(t)) ? 1U
                                                                                             : 0U);
}
static inline uint8_t Tim_Is32(const TIM_TypeDef *t)
{
    (void)t; /* v0.8: unused where the SKU has no TIM2 */
    return (uint8_t)(SPL_G0_IS_TIM2(t) ? 1U : 0U);
}
static inline uint8_t Tim_ChannelValid(const TIM_TypeDef *t, uint8_t ch)
{
    return (uint8_t)(((ch >= 1U) && (ch <= Tim_ChannelCount(t))) ? 1U : 0U);
}
static inline uint8_t Tim_WidthValid(const TIM_TypeDef *t, uint32_t value)
{
    return (uint8_t)(((Tim_Is32(t) != 0U) || (value <= 0xFFFFU)) ? 1U : 0U);
}
/* Timers with a BDTR register (MOE, break, LOCK): TIM1/15/16/17. */
static inline uint8_t Tim_HasBdtr(const TIM_TypeDef *t)
{
    return (uint8_t)(((t == TIM1) || SPL_G0_IS_TIM15(t) || (t == TIM16) || (t == TIM17)) ? 1U : 0U);
}
/* BKDSRM/BK2DSRM: set by software, cleared by hardware (RM0444 21.4.20). */
#define TIM_BDTR_DSRM_BITS (TIM_BDTR_BKDSRM | TIM_BDTR_BK2DSRM)
/* LOCK level >= 2 freezes CCxP of output channels; level 3 also OCxM/OCxPE
 * (RM0444 21.4.20 LOCK[1:0]). Timers without BDTR have no LOCK. */
static inline uint8_t Tim_PolarityLocked(const TIM_TypeDef *t)
{
    return (uint8_t)(((Tim_HasBdtr(t) != 0U) &&
                      (((t->BDTR & TIM_BDTR_LOCK_Msk) >> TIM_BDTR_LOCK_Pos) >= 2U))
                         ? 1U
                         : 0U);
}
static uint32_t Tim_FlagMask(const TIM_TypeDef *t); /* valid SR flags per timer type */
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
    SPL_REQUIRE(Tim_IsBase(t) != 0U, SPL_ERR_PARAM); /* arbitrary address */
    SPL_REQUIRE(cfg != (const TIM_BaseInitTypeDef *)0, SPL_ERR_PARAM);
    /* the ARPE flag acts as 1; period = 0 stops the counter (visible). */
    SPL_REQUIRE((cfg->auto_reload_preload <= 1U) && (cfg->period != 0U), SPL_ERR_PARAM);
    /* a value wider than PSC / ARR is silently truncated. */
    SPL_REQUIRE((cfg->prescaler <= 0xFFFFU) && (Tim_WidthValid(t, cfg->period) != 0U),
                SPL_ERR_PARAM);
    /* Issue #27 policy: the written CCRx are not checked; the period may drop below a compare
     * value. on a running counter the period would change silently. */
    SPL_REQUIRE((t->CR1 & TIM_CR1_CEN) == 0U, SPL_ERR_STATE);
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
    SPL_REQUIRE(Tim_IsBase(t) != 0U, SPL_ERR_PARAM); /* arbitrary address */
    SPL_REQUIRE(enable <= 1U, SPL_ERR_PARAM);        /* acts as 1 */
    /* One-pulse mode: CEN=1 means a pulse is running; a new request would be
     * lost (hardware clears CEN at the end of the pulse). */
    if ((enable != 0U) && ((t->CR1 & (TIM_CR1_OPM | TIM_CR1_CEN)) == (TIM_CR1_OPM | TIM_CR1_CEN)))
    {
        return SPL_BUSY;
    }
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
    SPL_REQUIRE(Tim_IsBase(t) != 0U, SPL_ERR_PARAM); /* arbitrary address */
    t->EGR = TIM_EGR_UG;
    return SPL_OK;
}
SPL_Status TIM_UpdateInterrupt(TIM_TypeDef *t, uint8_t enable)
{
    SPL_REQUIRE(Tim_IsBase(t) != 0U, SPL_ERR_PARAM); /* arbitrary address */
    SPL_REQUIRE(enable <= 1U, SPL_ERR_PARAM);        /* acts as 1 */
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
    SPL_REQUIRE(Tim_IsBase(t) != 0U, SPL_ERR_PARAM); /* arbitrary address */
    SPL_REQUIRE(pending != (uint8_t *)0, SPL_ERR_PARAM);
    *pending = (uint8_t)(((t->SR & TIM_SR_UIF) != 0U) ? 1U : 0U);
    return SPL_OK;
}
SPL_Status TIM_ClearUpdate(TIM_TypeDef *t)
{
    SPL_REQUIRE(Tim_IsBase(t) != 0U, SPL_ERR_PARAM); /* arbitrary address */
    /* v0.13.5 (G4 #20): TIM SR is rc_w0: zero on UIF, ones on the other flags of this timer
     * type, reserved bits at their reset value 0 (same value as TIM_ClearFlags). */
    t->SR = Tim_FlagMask(t) & ~(uint32_t)TIM_SR_UIF;
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
    SPL_REQUIRE(Tim_ChannelValid(t, ch) != 0U, SPL_ERR_PARAM); /* bad channel/address */
    SPL_REQUIRE(cfg != (const TIM_OCInitTypeDef *)0, SPL_ERR_PARAM);
    /* inverted / preload are written shifted (would set 2 adjacent bits); CCR width. */
    SPL_REQUIRE((cfg->inverted <= 1U) && (cfg->preload <= 1U) &&
                    (Tim_WidthValid(t, cfg->compare) != 0U),
                SPL_ERR_PARAM);
    /* Issue #27 policy: compare is not checked against ARR (CCR > ARR is valid), width only.
     * reserved OCxM code. */
    SPL_REQUIRE((cfg->mode == (uint32_t)TIM_OC_FROZEN) ||
                    (cfg->mode == (uint32_t)TIM_OC_ACTIVE_ON_MATCH) ||
                    (cfg->mode == (uint32_t)TIM_OC_INACTIVE_ON_MATCH) ||
                    (cfg->mode == (uint32_t)TIM_OC_TOGGLE) ||
                    (cfg->mode == (uint32_t)TIM_OC_PWM1) || (cfg->mode == (uint32_t)TIM_OC_PWM2),
                SPL_ERR_PARAM);
    ccer_mask = (uint32_t)1U << (4U * ((uint32_t)ch - 1U));
    /* reconfiguration on a running counter / enabled channel. */
    SPL_REQUIRE(((t->CR1 & TIM_CR1_CEN) == 0U) && ((t->CCER & ccer_mask) == 0U), SPL_ERR_STATE);
    /* The CCMR write below makes the channel an output before CCxP is
     * written, so LOCK >= 2 would silently drop CCxP (and OCxM/OCxPE at 3). */
    SPL_REQUIRE(Tim_PolarityLocked(t) == 0U, SPL_ERR_STATE);
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
    SPL_REQUIRE(Tim_ChannelValid(t, ch) != 0U, SPL_ERR_PARAM);
    SPL_REQUIRE(cfg != (const TIM_ICInitTypeDef *)0, SPL_ERR_PARAM);
    /* falling_edge is shifted; filter / prescaler would spill into the adjacent field. */
    SPL_REQUIRE((cfg->falling_edge <= 1U) && (cfg->filter <= 15U) && (cfg->prescaler <= 3U),
                SPL_ERR_PARAM);
    ccer_shift = 4U * ((uint32_t)ch - 1U);
    /* CCxS is writable only with CCxE = 0; the write would be silently ignored. */
    SPL_REQUIRE(((t->CR1 & TIM_CR1_CEN) == 0U) && ((t->CCER & ((uint32_t)1U << ccer_shift)) == 0U),
                SPL_ERR_STATE);
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
    SPL_REQUIRE(Tim_ChannelValid(t, ch) != 0U, SPL_ERR_PARAM);
    SPL_REQUIRE(enable <= 1U, SPL_ERR_PARAM);
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
    /* other channel; a value beyond the width is silently truncated. */
    SPL_REQUIRE((Tim_ChannelValid(t, ch) != 0U) && (Tim_WidthValid(t, compare) != 0U),
                SPL_ERR_PARAM);
    /* Issue #27 policy (same as HRTIM): the relation to ARR is not checked, only the register
     * width. CCR > ARR is a valid value: in PWM mode OCxREF stays constant, in compare mode there
     * is no match within the period. Two separate register writes are not atomic; with OCxPE /
     * ARPE = 1 the effective values are loaded together at the next update event. */
    *Tim_Ccr(t, ch) = compare;
    return SPL_OK;
}
SPL_Status TIM_GetCapture(TIM_TypeDef *t, uint8_t ch, uint32_t *capture)
{
    uint32_t ccmr;

    SPL_REQUIRE(Tim_ChannelValid(t, ch) != 0U, SPL_ERR_PARAM);
    SPL_REQUIRE(capture != (uint32_t *)0, SPL_ERR_PARAM);
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
/* v0.15 (CCL-2 ZMN-23): maximum number of reads in the MOE read-back (delay measured on G4: 1
 * read; 4 is an upper bound with margin; with the break input active every read sees 0). */
#define TIM_MOE_READBACK_READS 4U
SPL_Status TIM_MainOutputEnable(TIM_TypeDef *t, uint8_t enable)
{
    uint32_t bdtr;

    SPL_REQUIRE(enable <= 1U, SPL_ERR_PARAM);
    SPL_REQUIRE(Tim_HasBdtr(t) != 0U, SPL_ERR_PARAM); /* timer without BDTR */
    bdtr = t->BDTR;
    if (enable != 0U)
    {
        /* Break re-arm still in progress: MOE may be set only after hardware
         * has cleared BKDSRM/BK2DSRM (RM0444 21.3.16 re-arm procedure). */
        if ((bdtr & TIM_BDTR_DSRM_BITS) != 0U)
        {
            return SPL_BUSY;
        }
        /* v0.15 (CCL-2 ZMN-23): nothing is written while a break flag (BIF; B2IF and SBIF where
         * present) is set. The caller first removes the cause of the break, clears the flag with
         * TIM_ClearFlags (the flag is not cleared while the input is still active) and then
         * retries. No automatic clearing. */
        if ((t->SR & Tim_FlagMask(t) & (TIM_SR_BIF | TIM_SR_B2IF | TIM_SR_SBIF)) != 0U)
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
     * between read and write, and with MOE=0 a 1 disarms the break (RM0444
     * Table 116). Writing 0 keeps the break armed. Any BDTR write also
     * freezes LOCK (RM0444 21.4.20): run TIM_BreakDeadTimeInit first. */
    t->BDTR = bdtr & (uint32_t)~TIM_BDTR_DSRM_BITS;
    /* v0.15 (CCL-2 ZMN-23): hardware does not set MOE while the break input is active (RM0444
     * 21.3.16); a break arriving between the flag read and the write is also seen here. Read-back:
     * no success is returned if MOE did not latch. The MOE write becomes visible after a
     * synchronization delay (G4 NUCLEO-G431RB measurement 2026-11-12: first read 0, second 1; not
     * measured on a G0 board). Therefore a fixed number of reads is made (bounded, no external
     * event is awaited); OK as soon as MOE is seen. */
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

/* ---- extension (from G4 v0.10), see Docs/v0.4_TIM_CONTRACT.md ---- */

/* SR flags per timer family (RM0444 21.4.5, 22.4.5, 24.4.3, 25.5.5, 25.6.4, 23.4.4). */
/* G0: no encoder index / direction / error flags (IDXF, DIRF, IERRF, TERRF are G4 only). */
#define TIM_FLAGS_GENERAL                                                                          \
    (TIM_SR_UIF | TIM_SR_CC1IF | TIM_SR_CC2IF | TIM_SR_CC3IF | TIM_SR_CC4IF | TIM_SR_TIF |         \
     TIM_SR_CC1OF | TIM_SR_CC2OF | TIM_SR_CC3OF | TIM_SR_CC4OF)
#define TIM_FLAGS_ADVANCED                                                                         \
    (TIM_FLAGS_GENERAL | TIM_SR_COMIF | TIM_SR_BIF | TIM_SR_B2IF | TIM_SR_SBIF | TIM_SR_CC5IF |    \
     TIM_SR_CC6IF)
#define TIM_FLAGS_TIM15                                                                            \
    (TIM_SR_UIF | TIM_SR_CC1IF | TIM_SR_CC2IF | TIM_SR_COMIF | TIM_SR_TIF | TIM_SR_BIF |           \
     TIM_SR_CC1OF | TIM_SR_CC2OF)
#define TIM_FLAGS_TIM16_17 (TIM_SR_UIF | TIM_SR_CC1IF | TIM_SR_COMIF | TIM_SR_BIF | TIM_SR_CC1OF)
/* RM0444 24.4.3: TIM14_SR has UIF, CC1IF, CC1OF only. */
#define TIM_FLAGS_TIM14 (TIM_SR_UIF | TIM_SR_CC1IF | TIM_SR_CC1OF)

/* TIM1: advanced-control timer with break2 and complementary channels. */
static inline uint8_t Tim_IsAdvanced(const TIM_TypeDef *t)
{
    return (uint8_t)((t == TIM1) ? 1U : 0U);
}

/* TIM2/3/4: general-purpose timers with counter modes and encoder interface. */
static inline uint8_t Tim_IsGeneral(const TIM_TypeDef *t)
{
    return (uint8_t)((SPL_G0_IS_TIM2(t) || (t == TIM3) || SPL_G0_IS_TIM4(t)) ? 1U : 0U);
}

/* Timers with CR1 DIR/CMS and encoder interface (RM0444 21.3.22, 22.3.15; TIM15 SMS
 * encoder codes are reserved, RM0444 25.5.3). */
static inline uint8_t Tim_HasCounterMode(const TIM_TypeDef *t)
{
    return (uint8_t)(((Tim_IsAdvanced(t) != 0U) || (Tim_IsGeneral(t) != 0U)) ? 1U : 0U);
}

static inline uint8_t Tim_IsSmall(const TIM_TypeDef *t)
{
    return (uint8_t)((SPL_G0_IS_TIM15(t) || (t == TIM16) || (t == TIM17)) ? 1U : 0U);
}

/* Timers with BDTR, RCR and complementary outputs. */
static inline uint8_t Tim_HasBreak(const TIM_TypeDef *t)
{
    return (uint8_t)(((Tim_IsAdvanced(t) != 0U) || (Tim_IsSmall(t) != 0U)) ? 1U : 0U);
}

static inline uint8_t Tim_IsBasic(const TIM_TypeDef *t)
{
    (void)t; /* v0.8: unused where the SKU has no TIM6 / TIM7 */
    return (uint8_t)((SPL_G0_IS_TIM6(t) || SPL_G0_IS_TIM7(t)) ? 1U : 0U);
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
    else if SPL_G0_IS_TIM15 (t)
    {
        mask = TIM_FLAGS_TIM15;
    }
    else if ((t == TIM16) || (t == TIM17))
    {
        mask = TIM_FLAGS_TIM16_17;
    }
    else if (t == TIM14)
    {
        mask = TIM_FLAGS_TIM14;
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

/* Number of channels with a complementary (CHxN) output. TIM1: CH1N..CH3N; no CH4N (RM0444
 * 21.4.11 TIM1_CCER: bit 14 reserved, no CC4NE; none of the 12 G0 SKU CMSIS headers defines
 * TIM_CCER_CC4NE, the SVD agrees). CC4NP (bit 15) exists but only for edge selection in input
 * mode. The port from G4 (TIM8 has CH4N) had left 4: ch = 4 would write to a reserved bit
 * (2026-11-14 external review). */
static inline uint8_t Tim_ComplementaryValid(const TIM_TypeDef *t, uint8_t ch)
{
    uint8_t count = 0U;

    if (Tim_IsAdvanced(t) != 0U)
    {
        count = 3U;
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

static inline uint32_t Tim_LockLevel(const TIM_TypeDef *t)
{
    return (t->BDTR & TIM_BDTR_LOCK_Msk) >> TIM_BDTR_LOCK_Pos;
}

/* CCxS of channel 1..4: 0 means the channel is configured as output. */
static inline uint32_t Tim_ChannelSelection(const TIM_TypeDef *t, uint8_t ch)
{
    uint32_t ccmr = (ch <= 2U) ? t->CCMR1 : t->CCMR2;
    uint32_t shift = (((uint32_t)ch - 1U) % 2U) * 8U;

    return (ccmr >> shift) & TIM_CCMR1_CC1S_Msk;
}

SPL_Status TIM_CounterInit(TIM_TypeDef *t, const TIM_CounterInitTypeDef *cfg)
{
    uint32_t cr1;
    uint32_t bits = 0U;

    SPL_REQUIRE(Tim_IsBase(t) != 0U, SPL_ERR_PARAM); /* arbitrary address */
    SPL_REQUIRE(cfg != (const TIM_CounterInitTypeDef *)0, SPL_ERR_PARAM);
    /* reserved CMS / CKD code. */
    SPL_REQUIRE((cfg->mode <= (uint32_t)TIM_COUNTER_CENTER3) && (cfg->clock_division <= 2U),
                SPL_ERR_PARAM);
    SPL_REQUIRE((cfg->one_pulse <= 1U) && (cfg->uif_remap <= 1U), SPL_ERR_PARAM);
    /* reserved bits on a timer without counter mode / CKD. */
    SPL_REQUIRE((Tim_HasCounterMode(t) != 0U) || (cfg->mode == (uint32_t)TIM_COUNTER_UP),
                SPL_ERR_PARAM);
    SPL_REQUIRE((Tim_IsBasic(t) == 0U) || (cfg->clock_division == 0U), SPL_ERR_PARAM);
    /* CMS is forbidden on a running counter; the direction would change silently. */
    SPL_REQUIRE((t->CR1 & TIM_CR1_CEN) == 0U, SPL_ERR_STATE);
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
    SPL_REQUIRE(Tim_HasBreak(t) != 0U, SPL_ERR_PARAM); /* timer without RCR */
    /* RCR width (TIM1 16 bit, others 8 bit); the value is silently truncated. */
    SPL_REQUIRE(repetition <= ((Tim_IsAdvanced(t) != 0U) ? 0xFFFFU : 0xFFU), SPL_ERR_PARAM);
    SPL_REQUIRE((t->CR1 & TIM_CR1_CEN) == 0U, SPL_ERR_STATE);
    t->RCR = repetition;
    return SPL_OK;
}

SPL_Status TIM_GetCounter(TIM_TypeDef *t, uint32_t *count, uint8_t *uif_copy)
{
    uint32_t cnt;
    uint8_t remap;

    SPL_REQUIRE(Tim_IsBase(t) != 0U, SPL_ERR_PARAM); /* arbitrary address */
    SPL_REQUIRE(count != (uint32_t *)0, SPL_ERR_PARAM);
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
    SPL_REQUIRE(Tim_IsBase(t) != 0U, SPL_ERR_PARAM);            /* arbitrary address */
    SPL_REQUIRE(Tim_WidthValid(t, count) != 0U, SPL_ERR_PARAM); /* silently truncated */
    /* With UIFREMAP=1, bit 31 of a 32-bit CNT is the read-only UIF copy. the write is lost.
     */
    SPL_REQUIRE(((t->CR1 & TIM_CR1_UIFREMAP) == 0U) || ((count & TIM_CNT_UIFCPY) == 0U),
                SPL_ERR_PARAM);
    t->CNT = count;
    return SPL_OK;
}

SPL_Status TIM_EncoderInit(TIM_TypeDef *t, const TIM_EncoderInitTypeDef *cfg)
{
    uint32_t ccmr;
    uint32_t ccer;
    uint32_t smcr;

    SPL_REQUIRE(Tim_HasCounterMode(t) != 0U, SPL_ERR_PARAM); /* timer without encoder */
    SPL_REQUIRE(cfg != (const TIM_EncoderInitTypeDef *)0, SPL_ERR_PARAM);
    /* other slave mode; the filter is silently truncated by the mask. */
    SPL_REQUIRE((cfg->mode >= (uint32_t)TIM_ENCODER_X2_TI1) &&
                    (cfg->mode <= (uint32_t)TIM_ENCODER_X4) && (cfg->ti1_filter <= 15U) &&
                    (cfg->ti2_filter <= 15U),
                SPL_ERR_PARAM);
    SPL_REQUIRE((cfg->ti1_inverted <= 1U) && (cfg->ti2_inverted <= 1U), SPL_ERR_PARAM);
    /* CCxS is writable only with CCxE=0; encoder and external clock mode 2 are exclusive. */
    SPL_REQUIRE(((t->CR1 & TIM_CR1_CEN) == 0U) &&
                    ((t->CCER & (TIM_CCER_CC1E | TIM_CCER_CC2E)) == 0U) &&
                    ((t->SMCR & TIM_SMCR_ECE) == 0U),
                SPL_ERR_STATE);
    /* CC1S = CC2S = 01, prescalers 0, filters; clear OC1M[3]/OC2M[3] too. */
    ccmr = t->CCMR1 & (uint32_t)~(0xFFFFU | TIM_CCMR1_OC1M_3 | TIM_CCMR1_OC2M_3);
    ccmr |= TIM_CCMR1_CC1S_0 | TIM_CCMR1_CC2S_0;
    ccmr |= (cfg->ti1_filter << TIM_CCMR1_IC1F_Pos) & TIM_CCMR1_IC1F_Msk;
    ccmr |= (cfg->ti2_filter << TIM_CCMR1_IC2F_Pos) & TIM_CCMR1_IC2F_Msk;
    t->CCMR1 = ccmr;
    /* Polarity via CC1P/CC2P; CC1NP and CC2NP must be kept low (RM0444 21.3.22). */
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
    SPL_REQUIRE(Tim_HasCounterMode(t) != 0U, SPL_ERR_PARAM); /* reserved bit silently 0 */
    SPL_REQUIRE(down != (uint8_t *)0, SPL_ERR_PARAM);
    *down = (uint8_t)(((t->CR1 & TIM_CR1_DIR) != 0U) ? 1U : 0U);
    return SPL_OK;
}

SPL_Status TIM_ComplementaryOutputInit(TIM_TypeDef *t, uint8_t ch, uint8_t inverted)
{
    uint32_t shift;
    uint32_t ccer;

    /* channel without complementary output; inverted is shifted (adjacent bit). */
    SPL_REQUIRE((Tim_ComplementaryValid(t, ch) != 0U) && (inverted <= 1U), SPL_ERR_PARAM);
    shift = 4U * ((uint32_t)ch - 1U);
    /* polarity change on an enabled output (glitch). */
    SPL_REQUIRE((t->CCER & ((uint32_t)TIM_CCER_CC1NE << shift)) == 0U, SPL_ERR_STATE);
    /* LOCK level 2 freezes CCxNP while the channel is an output (CCxS=00). */
    SPL_REQUIRE((Tim_LockLevel(t) < 2U) || (Tim_ChannelSelection(t, ch) != 0U), SPL_ERR_STATE);
    ccer = t->CCER & ~((uint32_t)TIM_CCER_CC1NP << shift);
    t->CCER = ccer | ((uint32_t)inverted << (TIM_CCER_CC1NP_Pos + shift));
    return SPL_OK;
}

SPL_Status TIM_ComplementaryEnable(TIM_TypeDef *t, uint8_t ch, uint8_t enable)
{
    uint32_t mask;

    SPL_REQUIRE(Tim_ComplementaryValid(t, ch) != 0U, SPL_ERR_PARAM);
    SPL_REQUIRE(enable <= 1U, SPL_ERR_PARAM);
    /* v0.13.5 (G4 #16): DTG = 0: CCx and CCxN switch at the same edge, a bridge would conduct
     * through both switches. Program the dead time (TIM_BreakDeadTimeInit) first. */
    SPL_REQUIRE((enable == 0U) || ((t->BDTR & TIM_BDTR_DTG_Msk) != 0U), SPL_ERR_STATE);
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

/* RM0444 21.4.20 DTG encoding; rounded UP to the next representable dead time. */
SPL_Status TIM_DeadTimeToDtg(uint32_t dts_hz, uint32_t dead_time_ns, uint8_t *dtg)
{
    uint64_t ticks;
    uint32_t n;
    uint32_t value;

    SPL_REQUIRE(dts_hz != 0U, SPL_ERR_PARAM); /* dead time silently 0 */
    SPL_REQUIRE(dtg != (uint8_t *)0, SPL_ERR_PARAM);
    ticks = (((uint64_t)dead_time_ns * dts_hz) + 999999999ULL) / 1000000000ULL;
    if (ticks > 1008U)
    {
        return SPL_ERR_PARAM; /* result: not representable, at every level */
    }
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

    SPL_REQUIRE(Tim_HasBreak(t) != 0U, SPL_ERR_PARAM); /* timer without BDTR */
    SPL_REQUIRE(cfg != (const TIM_BreakDeadTimeInitTypeDef *)0, SPL_ERR_PARAM);
    /* silently truncated by the mask. */
    SPL_REQUIRE((cfg->dead_time <= 0xFFU) && (cfg->lock_level <= 3U) &&
                    (cfg->break_filter <= 15U) && (cfg->break2_filter <= 15U),
                SPL_ERR_PARAM);
    /* the 0 / 1 flags act as 1 (!= 0 ? BIT : 0). */
    SPL_REQUIRE((cfg->off_state_run <= 1U) && (cfg->off_state_idle <= 1U) &&
                    (cfg->auto_output_enable <= 1U) && (cfg->break_enable <= 1U) &&
                    (cfg->break_active_high <= 1U) && (cfg->break2_enable <= 1U) &&
                    (cfg->break2_active_high <= 1U),
                SPL_ERR_PARAM);
    /* the break2 fields are reserved outside TIM1. */
    SPL_REQUIRE((Tim_IsAdvanced(t) != 0U) ||
                    ((cfg->break2_enable == 0U) && (cfg->break2_active_high == 0U) &&
                     (cfg->break2_filter == 0U)),
                SPL_ERR_PARAM);
    /* RM0444 21.4.20: BRKIN2 must only be used with OSSR = OSSI = 1. */
    SPL_REQUIRE((cfg->break2_enable == 0U) ||
                    ((cfg->off_state_run != 0U) && (cfg->off_state_idle != 0U)),
                SPL_ERR_PARAM);
    /* Outputs must be off; LOCK >= 1 freezes DTG/BKE/BKP/AOE/BKF/BK2x and LOCK itself. */
    SPL_REQUIRE(((t->BDTR & TIM_BDTR_MOE) == 0U) && (Tim_LockLevel(t) == 0U), SPL_ERR_STATE);
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
    /* LOCK is write-once after reset (RM0444 21.4.20): an earlier BDTR write (for example
     * TIM_MainOutputEnable) froze it at 0 and the requested level was dropped. Report it. */
    if (Tim_LockLevel(t) != cfg->lock_level)
    {
        return SPL_ERR_STATE; /* result: hardware read-back, at every level */
    }
    return SPL_OK;
}

SPL_Status TIM_GetLockLevel(TIM_TypeDef *t, uint8_t *level)
{
    SPL_REQUIRE(Tim_HasBreak(t) != 0U, SPL_ERR_PARAM);
    SPL_REQUIRE(level != (uint8_t *)0, SPL_ERR_PARAM);
    *level = (uint8_t)Tim_LockLevel(t);
    return SPL_OK;
}

SPL_Status TIM_GetFlags(TIM_TypeDef *t, uint32_t *flags)
{
    SPL_REQUIRE(Tim_IsBase(t) != 0U, SPL_ERR_PARAM); /* arbitrary address */
    SPL_REQUIRE(flags != (uint32_t *)0, SPL_ERR_PARAM);
    *flags = t->SR & Tim_FlagMask(t);
    return SPL_OK;
}

SPL_Status TIM_ClearFlags(TIM_TypeDef *t, uint32_t flags)
{
    uint32_t valid;

    SPL_REQUIRE(Tim_IsBase(t) != 0U, SPL_ERR_PARAM); /* arbitrary address */
    valid = Tim_FlagMask(t);
    /* no effect / extra bits are dropped by the mask. */
    SPL_REQUIRE(flags != 0U, SPL_ERR_PARAM); /* no effect */
    /* (EDR-003): write to a reserved bit. */
    SPL_REQUIRE((flags & ~valid) == 0U, SPL_ERR_PARAM);
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

    /* G0: MMS[2:0] only (RM0444 21.4.2 / 22.4.2): no encoder-clock TRGO (G4 MMS = 1000). */
    if ((Tim_IsAdvanced(t) != 0U) || (Tim_IsGeneral(t) != 0U))
    {
        max = (uint32_t)TIM_TRGO_OC4REF;
    }
    else if SPL_G0_IS_TIM15 (t)
    {
        max = (uint32_t)TIM_TRGO_OC2REF;
    }
    else if (Tim_IsBasic(t) != 0U)
    {
        max = (uint32_t)TIM_TRGO_UPDATE;
    }
    else
    {
        return SPL_ERR_PARAM; /* structural: timer without MMS (max would stay undefined) */
    }
    SPL_REQUIRE(source <= max, SPL_ERR_PARAM); /* other TRGO */
    SPL_REQUIRE((t->CR1 & TIM_CR1_CEN) == 0U, SPL_ERR_STATE);
    cr2 = t->CR2 & (uint32_t)~TIM_CR2_MMS;
    cr2 |= (source << TIM_CR2_MMS_Pos) & TIM_CR2_MMS;
    t->CR2 = cr2;
    return SPL_OK;
}

/* ---- v0.11: slave modes, output compare clear, DMA requests and burst (RM0444 21..25) ---- */

/* SMS[3:0] / TS[4:0] are split fields: bit 3 of SMS at bit 16, TS[4:3] at bits 21:20. */
static uint32_t Tim_SmsBits(uint32_t mode)
{
    return (mode & 7U) | (((mode & 8U) != 0U) ? TIM_SMCR_SMS_3 : 0U);
}

static inline uint32_t Tim_SmsValue(uint32_t smcr)
{
    return (smcr & 7U) | (((smcr & TIM_SMCR_SMS_3) != 0U) ? 8U : 0U);
}

static inline uint8_t Tim_SmsIsReset(uint32_t smcr)
{
    const uint32_t sms = Tim_SmsValue(smcr);

    return (
        uint8_t)(((sms == (uint32_t)TIM_SLAVE_RESET) || (sms == (uint32_t)TIM_SLAVE_RESET_TRIGGER))
                     ? 1U
                     : 0U);
}

/* Any OCxCE set (channels 1..4; bits 7 / 15 of CCMR1 / CCMR2). */
static inline uint8_t Tim_OcClearActive(const TIM_TypeDef *t)
{
    return (uint8_t)((((t->CCMR1 | t->CCMR2) & (TIM_CCMR1_OC1CE | TIM_CCMR1_OC2CE)) != 0U) ? 1U
                                                                                           : 0U);
}

/* Timers with a slave mode controller: TIM1, TIM2..4, TIM15 (no ETRF trigger on TIM15). */
static inline uint8_t Tim_HasSlave(const TIM_TypeDef *t)
{
    return (uint8_t)(((Tim_HasCounterMode(t) != 0U) || SPL_G0_IS_TIM15(t)) ? 1U : 0U);
}

SPL_Status TIM_SlaveConfig(TIM_TypeDef *t, const TIM_SlaveTypeDef *cfg)
{
    uint32_t smcr;
    uint32_t max_trigger;

    SPL_REQUIRE(Tim_HasSlave(t) != 0U, SPL_ERR_PARAM); /* no slave controller */
    SPL_REQUIRE(cfg != (const TIM_SlaveTypeDef *)0, SPL_ERR_PARAM);
    max_trigger =
        (Tim_HasCounterMode(t) != 0U) ? (uint32_t)TIM_TRIGGER_ETRF : (uint32_t)TIM_TRIGGER_TI2FP2;
    /* Codes 1..3 are the encoder modes (TIM_EncoderInit), above 8 reserved (RM0444 21.4.3).
     */
    SPL_REQUIRE(
        ((cfg->mode == (uint32_t)TIM_SLAVE_DISABLED) || (cfg->mode >= (uint32_t)TIM_SLAVE_RESET)) &&
            (cfg->mode <= (uint32_t)TIM_SLAVE_RESET_TRIGGER) && (cfg->trigger <= max_trigger),
        SPL_ERR_PARAM);
    SPL_REQUIRE(cfg->msm <= 1U, SPL_ERR_PARAM); /* acts as 1 */
    /* RM0444 21.4.3 note: gated mode must not use TI1F_ED (one pulse per edge, not a level).
     */
    SPL_REQUIRE((cfg->mode != (uint32_t)TIM_SLAVE_GATED) ||
                    (cfg->trigger != (uint32_t)TIM_TRIGGER_TI1F_ED),
                SPL_ERR_PARAM);
    /* ERRATA ES0548 2.8.1: one-pulse master in combined reset + trigger mode with MSM = 1 misses a
     * trigger arriving at CNT = ARR; no workaround, ST advises MSM = 0. Refused while OPM = 1.
     * */
    SPL_REQUIRE((cfg->mode != (uint32_t)TIM_SLAVE_RESET_TRIGGER) || (cfg->msm == 0U) ||
                    ((t->CR1 & TIM_CR1_OPM) == 0U),
                SPL_ERR_STATE);
    /* ERRATA ES0548 2.8.3: ocref_clr is wrong in reset and combined reset + trigger modes. */
    SPL_REQUIRE(((cfg->mode != (uint32_t)TIM_SLAVE_RESET) &&
                 (cfg->mode != (uint32_t)TIM_SLAVE_RESET_TRIGGER)) ||
                    (Tim_OcClearActive(t) == 0U),
                SPL_ERR_STATE);
    /* TS changes only with SMS = 000 (RM0444 21.4.3 note): slave mode off, TS / MSM, then SMS. */
    smcr = t->SMCR & (uint32_t)~(TIM_SMCR_SMS | TIM_SMCR_TS | TIM_SMCR_MSM);
    t->SMCR = t->SMCR & (uint32_t)~TIM_SMCR_SMS;
    smcr |= ((cfg->trigger & 7U) << TIM_SMCR_TS_Pos) |
            ((cfg->trigger & 0x18U) << 17U) | /* TS[4:3] */
            ((cfg->msm != 0U) ? TIM_SMCR_MSM : 0U);
    t->SMCR = smcr;
    t->SMCR = smcr | Tim_SmsBits(cfg->mode);
    return SPL_OK;
}

/* Timers with OCCS / OCxCE (ETR and the OR1 OCREF_CLR selector): TIM1, TIM2..4. */
SPL_Status TIM_OcClearSource(TIM_TypeDef *t, uint32_t source)
{
    /* OCREF_CLR is 1 bit (COMP1 / COMP2) or 2 bits with COMP3 (G0B1 / G0C1). */
    const uint32_t comp_max = ((TIM1_OR1_OCREF_CLR_Msk >> TIM1_OR1_OCREF_CLR_Pos) == 3U) ? 2U : 1U;

    /* timer without OCCS / reserved OR1 code. */
    SPL_REQUIRE((Tim_HasCounterMode(t) != 0U) &&
                    ((source == TIM_OCCLR_ETRF) || (source <= comp_max)),
                SPL_ERR_PARAM);
    /* ERRATA ES0545 2.8.6 / ES0546 2.9.6: TIM2_CHx unstable with OCREF_CLR on TIM2_ETRF. */
    SPL_REQUIRE((SPL_G0_ERRATA_TIM2_ETRF_OCCLR == 0) || (source != TIM_OCCLR_ETRF) ||
                    !SPL_G0_IS_TIM2(t),
                SPL_ERR_UNSUPPORTED);
    /* (instance capability): on a SKU without a comparator (G031 / G041, value line: no
     * OR1, OCCS = 0 reserved, RM0454 15.4.3) a COMP source never arrives. 2026-11-14 capability
     * audit. */
    SPL_REQUIRE((source == TIM_OCCLR_ETRF) || (SPL_G0_HAS_COMP != 0), SPL_ERR_UNSUPPORTED);
    if (source == TIM_OCCLR_ETRF)
    {
        t->SMCR = t->SMCR | TIM_SMCR_OCCS;
    }
    else
    {
        /* OR1.OCREF_CLR: 00 COMP1, 01 COMP2, 10 COMP3 (G0B1 / G0C1), 11 reserved (RM0444 21.4.23).
         */
        t->OR1 = (t->OR1 & (uint32_t)~TIM1_OR1_OCREF_CLR_Msk) | (source << TIM1_OR1_OCREF_CLR_Pos);
        t->SMCR = t->SMCR & (uint32_t)~TIM_SMCR_OCCS;
    }
    return SPL_OK;
}

SPL_Status TIM_OcClearEnable(TIM_TypeDef *t, uint8_t ch, uint8_t enable)
{
    volatile uint32_t *ccmr;
    uint32_t bit;

    SPL_REQUIRE((Tim_HasCounterMode(t) != 0U) && (Tim_ChannelValid(t, ch) != 0U), SPL_ERR_PARAM);
    SPL_REQUIRE(enable <= 1U, SPL_ERR_PARAM);
    /* input channel: OCxCE has no meaning. bit 7 is ICxF in input mode, the filter would
     * change silently. */
    SPL_REQUIRE(Tim_ChannelSelection(t, ch) == 0U, SPL_ERR_STATE);
    /* ERRATA ES0548 2.8.3 (see TIM_SlaveConfig). */
    SPL_REQUIRE((enable == 0U) || (Tim_SmsIsReset(t->SMCR) == 0U), SPL_ERR_STATE);
    /* ERRATA ES0545 2.8.6 / ES0546 2.9.6 also when OCCS was set outside TIM_OcClearSource. */
    SPL_REQUIRE((SPL_G0_ERRATA_TIM2_ETRF_OCCLR == 0) || (enable == 0U) || !SPL_G0_IS_TIM2(t) ||
                    ((t->SMCR & TIM_SMCR_OCCS) == 0U),
                SPL_ERR_UNSUPPORTED);
    ccmr = (ch <= 2U) ? &t->CCMR1 : &t->CCMR2;
    bit = ((ch % 2U) != 0U) ? TIM_CCMR1_OC1CE : TIM_CCMR1_OC2CE;
    *ccmr = (*ccmr & ~bit) | ((enable != 0U) ? bit : 0U);
    return SPL_OK;
}

/* DIER DMA request bits per timer class (RM0444 21.4.4, 22.4.4, 23.4.3, 25.5.4, 25.6.3). */
static uint32_t Tim_DmaMask(const TIM_TypeDef *t)
{
    uint32_t mask = 0U;

    if (Tim_IsAdvanced(t) != 0U)
    {
        mask = TIM_DIER_UDE | TIM_DIER_CC1DE | TIM_DIER_CC2DE | TIM_DIER_CC3DE | TIM_DIER_CC4DE |
               TIM_DIER_COMDE | TIM_DIER_TDE;
    }
    else if (Tim_IsGeneral(t) != 0U)
    {
        mask = TIM_DIER_UDE | TIM_DIER_CC1DE | TIM_DIER_CC2DE | TIM_DIER_CC3DE | TIM_DIER_CC4DE |
               TIM_DIER_TDE;
    }
    else if SPL_G0_IS_TIM15 (t)
    {
        mask = TIM_DIER_UDE | TIM_DIER_CC1DE | TIM_DIER_COMDE | TIM_DIER_TDE;
    }
    else if ((t == TIM16) || (t == TIM17))
    {
        mask = TIM_DIER_UDE | TIM_DIER_CC1DE;
    }
    else if (Tim_IsBasic(t) != 0U)
    {
        mask = TIM_DIER_UDE;
    }
    else
    {
        mask = 0U; /* TIM14: no DMA */
    }
    return mask;
}

SPL_Status TIM_DmaRequestConfig(TIM_TypeDef *t, uint32_t requests)
{
    const uint32_t valid = Tim_DmaMask(t);

    /* timer without DMA / interrupt enable bits in DIER would be set. */
    SPL_REQUIRE((valid != 0U) && ((requests & ~valid) == 0U), SPL_ERR_PARAM);
    t->DIER = (t->DIER & ~valid) | requests;
    return SPL_OK;
}

/* DCR / DMAR exist on TIM1..4, TIM15..17 (not TIM6 / TIM7 / TIM14). Register index 18 = DCR,
 * 19 = DMAR: a burst over them is refused; indexes above 19 reach OR1 .. TISEL (0x50 .. 0x68). */
#define TIM_DMAR_INDEX 19U
#define TIM_DCR_INDEX 18U
#define TIM_LAST_INDEX 26U /* TISEL, offset 0x68 */

/* Register indexes (offset / 4) a burst access may cover, per timer. RM0444 21.4.32 / 22.4.29 /
 * 24.4.25 / 25.4.24 register maps; the SVD (DFP 2.1.0) is inconsistent between SKUs (G071
 * TIM1_TISEL, G070 TIM15_AF1 / TISEL missing), the RM is authoritative. A burst write to a
 * reserved address is ignored and its read returns 0: a buffer prepared for another timer's
 * layout shifts silently (2026-11-14 external review EDR-002: TIM15 index 15 = no CCR3). DCR /
 * DMAR are refused separately. Tests/test_capability_cmsis.c checks this table against the CMSIS
 * channel / RCR / BDTR / SMCR capabilities. Pure; used only in checks (FAST: inside sizeof, not
 * evaluated). */
#define TIM_BURST_REGS_ADVANCED 0x07FFFFFFUL /* 0..26 */
#define TIM_BURST_REGS_GENERAL 0x051DEFFFUL  /* 0..11, 13..16, 18..20, 24, 26 */
#define TIM_BURST_REGS_TIM15 0x050E7F7FUL    /* 0..6, 8..14, 17..19, 24, 26 */
#define TIM_BURST_REGS_TIM16_17 0x050E3F7BUL /* 0, 1, 3..6, 8..13, 17..19, 24, 26 */
static inline uint32_t Tim_BurstRegs(const TIM_TypeDef *t)
{
    uint32_t regs = 0U;

    if (Tim_IsAdvanced(t) != 0U)
    {
        regs = TIM_BURST_REGS_ADVANCED;
    }
    else if (Tim_IsGeneral(t) != 0U)
    {
        regs = TIM_BURST_REGS_GENERAL;
    }
    else if SPL_G0_IS_TIM15 (t)
    {
        regs = TIM_BURST_REGS_TIM15;
    }
    else if ((t == TIM16) || (t == TIM17))
    {
        regs = TIM_BURST_REGS_TIM16_17;
    }
    else
    {
        regs = 0U;
    }
    return regs;
}

SPL_Status TIM_DmaBurstConfig(TIM_TypeDef *t, uint32_t base_index, uint32_t transfers)
{
    uint32_t last;

    /* timer without DCR; the burst would overwrite DCR / DMAR. */
    SPL_REQUIRE((Tim_DmaMask(t) != 0U) && (Tim_IsBasic(t) == 0U) && (transfers >= 1U) &&
                    (transfers <= 18U),
                SPL_ERR_PARAM);
    last = base_index + transfers - 1U;
    SPL_REQUIRE((base_index <= TIM_LAST_INDEX) && (last <= TIM_LAST_INDEX) &&
                    ((base_index > TIM_DMAR_INDEX) || (last < TIM_DCR_INDEX)),
                SPL_ERR_PARAM);
    /* (EDR-002): every index in the range must be a register present on this timer.
     * last <= 26 is checked above; the shift does not exceed 32 bits. */
    SPL_REQUIRE(((((1UL << transfers) - 1UL) << base_index) & ~Tim_BurstRegs(t)) == 0U,
                SPL_ERR_PARAM);
    /* A burst reconfigured while a DMA request is enabled would mix two layouts. */
    SPL_REQUIRE((t->DIER & Tim_DmaMask(t)) == 0U, SPL_ERR_STATE);
    t->DCR = (base_index << TIM_DCR_DBA_Pos) | ((transfers - 1U) << TIM_DCR_DBL_Pos);
    return SPL_OK;
}

SPL_Status TIM_GetDmaBurstAddress(TIM_TypeDef *t, uint32_t *address)
{
    SPL_REQUIRE((Tim_DmaMask(t) != 0U) && (Tim_IsBasic(t) == 0U), SPL_ERR_PARAM);
    SPL_REQUIRE(address != (uint32_t *)0, SPL_ERR_PARAM);
    *address = (uint32_t)(uintptr_t)&t->DMAR;
    return SPL_OK;
}
