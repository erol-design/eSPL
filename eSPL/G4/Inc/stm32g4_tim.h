/* SPDX-License-Identifier: MIT */
#ifndef STM32G4_TIM_H
#define STM32G4_TIM_H
#include "stm32g4xx.h"
#include "stm32g4_status.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C"
{
#endif
    /* General-purpose, advanced and basic timer driver (RM0440 Rev 9 sections 29..32):
     * TIM1/8/20 (advanced), TIM2/5 (32 bit), TIM3/4, TIM6/7 (basic), TIM15, TIM16, TIM17
     * (instances SKU-guarded). The v0.10 extension (counter mode, encoder, complementary
     * outputs, break / dead time, flags, TRGO) is compiled only with the full CMSIS header
     * (TIM_SPL_EXTENDED).
     * Common rules: non-blocking, no static state, no interrupt handlers installed; invalid
     * timer or argument -> SPL_ERR_PARAM and a rejected state -> SPL_ERR_STATE, both with no
     * register write (exception: TIM_BreakDeadTimeInit LOCK read-back). Read-only getters and
     * TIM_ClearFlags (single write) are ISR-safe. Issue #27 policy: ARR and CCRx are
     * independent, CCR > ARR is valid (PWM: OCxREF held, OC: no match in the period).
     * Usage: configure with CEN = 0 (channels with CCxE = 0): TIM_BaseInit, optional
     * TIM_CounterInit and channel init, TIM_GenerateUpdate, then TIM_Enable; on break timers
     * TIM_BreakDeadTimeInit before the first TIM_MainOutputEnable after reset.
     * Caller duties (all APIs): TIMx bus clock on (RCC); PSC / ARR / CCR / DTG computed from
     * the actual timer clock; GPIO AF for the CHx, CHxN, ETR and BKIN pins; IRQ handler, NVIC
     * priority and enable (the handler clears the flags it serves with TIM_ClearFlags /
     * TIM_ClearUpdate); serialise calls per instance with ISRs using the same timer (CR1, CR2,
     * DIER, CCER, CCMRx, SMCR, BDTR are read-modify-write).
     * Caller duty (CCL-2 ZMN-08): this driver has no error or shutdown path of its own; on
     * error and shutdown the caller puts the outputs at a safe level (TIM_MainOutputEnable 0
     * with OSSI, TIM_ChannelEnable 0, or the pin as GPIO at the safe level); stopping the
     * counter (TIM_Enable 0) alone keeps the last output level.
     * Contract: Docs/v0.10_TIM_CONTRACT.md. */
    typedef struct
    {
        uint32_t prescaler;           /* PSC register: 0..65535, division by PSC+1 */
        uint32_t period;              /* ARR register value */
        uint32_t auto_reload_preload; /* 0/1 */
    } TIM_BaseInitTypeDef;
    typedef enum
    {
        TIM_OC_FROZEN = 0,
        TIM_OC_ACTIVE_ON_MATCH = 1,
        TIM_OC_INACTIVE_ON_MATCH = 2,
        TIM_OC_TOGGLE = 3,
        TIM_OC_PWM1 = 6,
        TIM_OC_PWM2 = 7
    } TIM_OCMode;
    typedef struct
    {
        uint32_t mode; /* TIM_OCMode value; fixed 32-bit API storage */
        uint32_t compare;
        uint32_t inverted; /* CCxP: 0/1 */
        uint32_t preload;  /* OCxPE: 0/1 */
    } TIM_OCInitTypeDef;
    typedef struct
    {
        uint8_t falling_edge; /* CCxP */
        uint8_t filter;       /* ICxF: 0..15 */
        uint8_t prescaler;    /* ICxPSC: 0..3 (1,2,4,8 captures) */
    } TIM_ICInitTypeDef;
    /* PSC, ARR, ARPE. ARR/period zero is rejected; CEN = 1 -> STATE; no registers are written
     * on error. Issue #27 policy: the written CCRx are not checked; ARR and CCR are independent,
     * CCR > ARR is valid.
     * Caller: PSC (always preloaded) and ARR with ARPE = 1 take effect only at the next update
     * event and no UG is generated here: call TIM_GenerateUpdate before TIM_Enable. */
    /* Level: PARAM[F] STATE[r] */
    SPL_Status TIM_BaseInit(TIM_TypeDef *tim, const TIM_BaseInitTypeDef *cfg);
    /* CR1.CEN set/cleared. v0.13.3: one-pulse mode with a pulse running
     * (OPM=1, CEN=1) and enable=1 -> SPL_BUSY, no write (retry after the
     * pulse; hardware clears CEN at its end).
     * Caller: configure the timer and load PSC / ARR (TIM_GenerateUpdate) before enable = 1;
     * enable = 0 is not refused while a pulse or period runs and cuts it. */
    /* Level: PARAM[F] BUSY[r] */
    SPL_Status TIM_Enable(TIM_TypeDef *tim, uint8_t enable);
    /* EGR.UG: software update event.
     * Caller: UG reloads PSC / ARR / RCR / preloaded CCRx, restarts the counter and, with
     * URS = 0, sets UIF; clear UIF (TIM_ClearUpdate) afterwards if that event is unwanted. */
    /* Level: PARAM[F] */
    SPL_Status TIM_GenerateUpdate(TIM_TypeDef *tim);
    /* Sets/clears DIER.UIE only. enable=1 with UIF=1 (stale event) -> STATE, no write (CCL-2
     * KSM-12): clear it with TIM_ClearUpdate (also after TIM_GenerateUpdate), then enable.
     * Caller: the handler clears UIF (TIM_ClearUpdate). */
    /* Level: PARAM[F] STATE[r] */
    SPL_Status TIM_UpdateInterrupt(TIM_TypeDef *tim, uint8_t enable);
    /* *pending = SR.UIF. */
    /* Caller: none beyond the file-header duties. */
    /* Level: PARAM[F] */
    SPL_Status TIM_UpdatePending(TIM_TypeDef *tim, uint8_t *pending);
    /* SR = valid flags & ~UIF (rc_w0, reserved bits 0; v0.13.5). */
    /* Caller: none beyond the file-header duties. */
    /* Level: PARAM[F] */
    SPL_Status TIM_ClearUpdate(TIM_TypeDef *tim);
    /* OCxM, CCxP, OCxPE, CCRx of one channel. CEN = 1 or channel enabled (CCxE) -> STATE.
     * cfg->compare: register width only. Issue #27 policy: not checked against ARR; CCR > ARR
     * is valid (PWM: OCxREF held, OC: no match in the period).
     * Caller: with OCxPE = 1 the CCRx loads at the next update event (TIM_GenerateUpdate); then
     * TIM_ChannelEnable. TIM_OC_TOGGLE: see the ERRATA note at TIM_SetCompare. */
    /* Level: PARAM[F] STATE[r] */
    SPL_Status TIM_OutputCompareInit(TIM_TypeDef *tim, uint8_t channel,
                                     const TIM_OCInitTypeDef *cfg);
    /* CCxS = TIx input, ICxF, ICxPSC, CCxP of one channel. CEN = 1 or channel enabled -> STATE.
     * Caller: choose the filter for the real tDTS (CKD); then TIM_ChannelEnable. */
    /* Level: PARAM[F] STATE[r] */
    SPL_Status TIM_InputCaptureInit(TIM_TypeDef *tim, uint8_t channel,
                                    const TIM_ICInitTypeDef *cfg);
    /* CCER.CCxE set / cleared.
     * Caller: configure the channel (TIM_OutputCompareInit / TIM_InputCaptureInit) and its pin
     * AF first; on TIM1/8/15/16/17/20 an output is driven only with MOE = 1
     * (TIM_MainOutputEnable). */
    /* Level: PARAM[F] */
    SPL_Status TIM_ChannelEnable(TIM_TypeDef *tim, uint8_t channel, uint8_t enable);
    /* ERRATA ES0431 2.8.2 / ES0430 2.12.2 / ES0523 2.8.2: a CCR change giving matches on two
     * consecutive counter clocks (ARR->0, crest ARR-1->ARR, valley 1->0) loses the 2nd compare
     * event (toggle output, CCxIF); no workaround.
     * Issue #27 policy: register width only; CCR > ARR is valid.
     * Caller: do not rely on the second compare event of such a one-clock CCR step (ERRATA
     * above). ARR and CCR are separate, non-atomic writes: to change both, write them within
     * one update interval with OCxPE = ARPE = 1. */
    /* Level: PARAM[F] */
    SPL_Status TIM_SetCompare(TIM_TypeDef *tim, uint8_t channel, uint32_t compare);
    /* Caller duty (CCL-2 ZMN-06): returns one raw CCRx value; the difference of two captures
     * and counter overflows between them are the caller's (count update events, choose PSC/ARR
     * so the longest interval fits one period, or mark the measurement invalid). */
    /* Level: PARAM[F] HW[r] WOULD_BLOCK[r] */
    SPL_Status TIM_GetCapture(TIM_TypeDef *tim, uint8_t channel, uint32_t *capture);
    /* BDTR.MOE set/clear. TIM1/8/15/16/17/20 only (TIM8/TIM20 where the SKU
     * has them), else PARAM. enable=1 returns BUSY (no write) while BKDSRM or
     * BK2DSRM is still set (break re-arm in progress, RM0440 29.3.19); retry.
     * The write never copies BKDSRM/BK2DSRM back as 1. Any BDTR write fixes the write-once
     * LOCK field, so an earlier MOE write freezes LOCK at 0 and a later TIM_BreakDeadTimeInit
     * cannot set the wanted level (contract).
     * v0.15 (CCL-2 ZMN-23), enable=1: a set break flag of this timer (BIF; B2IF/SBIF on
     * TIM1/8/20) -> SPL_ERR_HW, no write; after the write MOE is read back and a MOE the
     * hardware did not latch (break input still active) -> SPL_ERR_HW (state on failure: BDTR
     * written with the MOE request, MOE = 0 in hardware, no other field changed). The flags are
     * never cleared here. enable=0 always clears MOE (flags not checked). AOE
     * (TIM_BreakDeadTimeInit auto_output_enable = 1): hardware sets MOE again at the next update
     * event after the break input is released, without this function; choose it only when an
     * automatic restart after a break is intended.
     * Caller: call TIM_BreakDeadTimeInit before the first TIM_MainOutputEnable after reset; on
     * ERR_HW remove the break cause, clear the flag with TIM_ClearFlags (it stays set while the
     * input is active), then retry. */
    /* Level: PARAM[F] HW[r] BUSY[r] */
    SPL_Status TIM_MainOutputEnable(TIM_TypeDef *tim, uint8_t enable);

/* v0.10 extension (counter mode, encoder, complementary outputs, break/dead time, flags,
 * TRGO). It needs the full CMSIS device header bit definitions. The legacy minimal host
 * mock (Tests/mock/stm32g4xx.h) predates it and lacks these macros, so the extension is
 * compiled out there. See Docs/v0.10_TIM_CONTRACT.md. */
#if defined(TIM_BDTR_DTG_Msk) && defined(TIM_SMCR_SMS_3) && defined(TIM_CR1_UIFREMAP)
#define TIM_SPL_EXTENDED 1

    /* Counter mode (CR1 DIR/CMS). Only TIM_COUNTER_UP on TIM6/7/15/16/17. */
    typedef enum
    {
        TIM_COUNTER_UP = 0,
        TIM_COUNTER_DOWN = 1,
        TIM_COUNTER_CENTER1 = 2, /* CMS=01: OC flags set while counting down */
        TIM_COUNTER_CENTER2 = 3, /* CMS=10: OC flags set while counting up */
        TIM_COUNTER_CENTER3 = 4  /* CMS=11: OC flags set both ways */
    } TIM_CounterMode;

    typedef struct
    {
        uint32_t mode;           /* TIM_CounterMode value */
        uint32_t clock_division; /* CKD: 0 = tDTS x1, 1 = x2, 2 = x4 (0 only on TIM6/7) */
        uint32_t one_pulse;      /* OPM: 0/1 */
        uint32_t uif_remap;      /* UIFREMAP: 0/1, copy UIF into CNT bit 31 */
    } TIM_CounterInitTypeDef;

    /* Quadrature encoder modes (SMCR SMS[3:0]); TIM1/2/3/4/5/8/20 only. */
    typedef enum
    {
        TIM_ENCODER_X2_TI1 = 1, /* SMS=0001: count on TI1FP1 edges */
        TIM_ENCODER_X2_TI2 = 2, /* SMS=0010: count on TI2FP2 edges */
        TIM_ENCODER_X4 = 3      /* SMS=0011: count on both edges */
    } TIM_EncoderMode;

    typedef struct
    {
        uint32_t mode;         /* TIM_EncoderMode value */
        uint32_t ti1_inverted; /* CC1P: 0/1 */
        uint32_t ti2_inverted; /* CC2P: 0/1 */
        uint32_t ti1_filter;   /* IC1F: 0..15 */
        uint32_t ti2_filter;   /* IC2F: 0..15 */
    } TIM_EncoderInitTypeDef;

    /* Complete BDTR image; TIM1/8/20/15/16/17 only. Written in one access because LOCK is
     * write-once after reset (RM0440 29.6.20 note). MOE, BKBID/BK2BID and BKDSRM/BK2DSRM are
     * written as 0. */
    typedef struct
    {
        uint32_t dead_time;          /* DTG[7:0] raw, 0..255 (see TIM_DeadTimeToDtg) */
        uint32_t lock_level;         /* LOCK: 0..3 */
        uint32_t off_state_run;      /* OSSR: 0/1 */
        uint32_t off_state_idle;     /* OSSI: 0/1 */
        uint32_t auto_output_enable; /* AOE: 0/1 */
        uint32_t break_enable;       /* BKE: 0/1 */
        uint32_t break_active_high;  /* BKP: 0/1 */
        uint32_t break_filter;       /* BKF: 0..15 */
        uint32_t break2_enable;      /* BK2E: 0/1, TIM1/8/20 only (else must be 0) */
        uint32_t break2_active_high; /* BK2P: 0/1, TIM1/8/20 only (else must be 0) */
        uint32_t break2_filter;      /* BK2F: 0..15, TIM1/8/20 only (else must be 0) */
    } TIM_BreakDeadTimeInitTypeDef;

    /* Master mode selection (CR2 MMS[3:0]) for TRGO. */
    typedef enum
    {
        TIM_TRGO_RESET = 0,
        TIM_TRGO_ENABLE = 1,
        TIM_TRGO_UPDATE = 2,
        TIM_TRGO_CC1_PULSE = 3,
        TIM_TRGO_OC1REF = 4,
        TIM_TRGO_OC2REF = 5,
        TIM_TRGO_OC3REF = 6,
        TIM_TRGO_OC4REF = 7,
        TIM_TRGO_ENCODER_CLOCK = 8
    } TIM_TrgoSource;

/* Status flags (SR, rc_w0). Availability depends on the timer, see the contract. */
#define TIM_FLAG_UPDATE TIM_SR_UIF
#define TIM_FLAG_CC1 TIM_SR_CC1IF
#define TIM_FLAG_CC2 TIM_SR_CC2IF
#define TIM_FLAG_CC3 TIM_SR_CC3IF
#define TIM_FLAG_CC4 TIM_SR_CC4IF
#define TIM_FLAG_COM TIM_SR_COMIF
#define TIM_FLAG_TRIGGER TIM_SR_TIF
#define TIM_FLAG_BREAK TIM_SR_BIF
#define TIM_FLAG_BREAK2 TIM_SR_B2IF
#define TIM_FLAG_CC1_OVERCAPTURE TIM_SR_CC1OF
#define TIM_FLAG_CC2_OVERCAPTURE TIM_SR_CC2OF
#define TIM_FLAG_CC3_OVERCAPTURE TIM_SR_CC3OF
#define TIM_FLAG_CC4_OVERCAPTURE TIM_SR_CC4OF
#define TIM_FLAG_SYSTEM_BREAK TIM_SR_SBIF
#define TIM_FLAG_CC5 TIM_SR_CC5IF
#define TIM_FLAG_CC6 TIM_SR_CC6IF
#define TIM_FLAG_INDEX TIM_SR_IDXF
#define TIM_FLAG_DIRECTION TIM_SR_DIRF
#define TIM_FLAG_INDEX_ERROR TIM_SR_IERRF
#define TIM_FLAG_TRANSITION_ERROR TIM_SR_TERRF

    /* CR1 DIR, CMS, CKD, OPM, UIFREMAP; CEN = 1 -> STATE. Non-UP mode on TIM6/7/15/16/17 or
     * CKD != 0 on TIM6/7 -> PARAM.
     * Caller: none beyond the file-header duties. */
    /* Level: PARAM[F] STATE[r] */
    SPL_Status TIM_CounterInit(TIM_TypeDef *tim, const TIM_CounterInitTypeDef *cfg);
    /* RCR (TIM1/8/20: 16 bit, TIM15/16/17: 8 bit); CEN = 1 -> STATE.
     * Caller: RCR is preloaded and becomes active at the next update event
     * (TIM_GenerateUpdate). */
    /* Level: PARAM[F] STATE[r] */
    SPL_Status TIM_SetRepetition(TIM_TypeDef *tim, uint32_t repetition);
    /* *count = CNT (bit 31 masked with UIFREMAP = 1 on 32-bit timers); *uif_copy optional (NULL
     * allowed), UIFCPY or 0. CR1 then CNT are read once each (one sample). */
    /* Caller: none beyond the file-header duties. */
    /* Level: PARAM[F] */
    SPL_Status TIM_GetCounter(TIM_TypeDef *tim, uint32_t *count, uint8_t *uif_copy);
    /* CNT = count (counter width; bit 31 must be 0 with UIFREMAP = 1). Allowed while running.
     * Caller: a write while running races the counting edge (e.g. encoder re-zero): stop the
     * timer if the exact value matters. */
    /* Level: PARAM[F] */
    SPL_Status TIM_SetCounter(TIM_TypeDef *tim, uint32_t count);
    /* Encoder mode on TI1 / TI2 (CCMR1, CCER, SMCR.SMS). CEN = 1, CC1E / CC2E set or SMCR.ECE
     * set -> STATE.
     * Caller: set ARR first (TIM_BaseInit; the counter runs 0..ARR); GPIO AF of CH1 / CH2. */
    /* Level: PARAM[F] STATE[r] */
    SPL_Status TIM_EncoderInit(TIM_TypeDef *tim, const TIM_EncoderInitTypeDef *cfg);
    /* *down = CR1.DIR (hardware-updated in encoder and center-aligned mode). */
    /* Caller: none beyond the file-header duties. */
    /* Level: PARAM[F] */
    SPL_Status TIM_GetDirection(TIM_TypeDef *tim, uint8_t *down);
    /* CCER.CCxNP = inverted. CCxNE set, or LOCK >= 2 with the channel an output -> STATE.
     * Caller: none beyond the file-header duties. */
    /* Level: PARAM[F] STATE[r] */
    SPL_Status TIM_ComplementaryOutputInit(TIM_TypeDef *tim, uint8_t channel, uint8_t inverted);
    /* v0.13.5: enable=1 with BDTR.DTG = 0 -> SPL_ERR_STATE (no dead time: shoot-through in
     * a bridge); call TIM_BreakDeadTimeInit first. enable=0 always allowed.
     * Caller: the dead time value must suit the power stage (only DTG != 0 is checked); the
     * output is driven only with MOE = 1 and follows OSSR / OSSI. */
    /* Level: PARAM[F] STATE[r] */
    SPL_Status TIM_ComplementaryEnable(TIM_TypeDef *tim, uint8_t channel, uint8_t enable);
    /* Pure helper, no register access: DTG code for dead_time_ns at dts_hz (timer kernel clock
     * / CKD), rounded up. More than 1008 ticks, dts_hz = 0 or dtg = NULL -> PARAM.
     * Caller: pass the real tDTS frequency (kernel clock and CKD). */
    /* Level: PARAM[F] */
    SPL_Status TIM_DeadTimeToDtg(uint32_t dts_hz, uint32_t dead_time_ns, uint8_t *dtg);
    /* ERRATA ES0431 2.8.5 / ES0430 2.12.5 / ES0523 2.8.5: BKBID/BK2BID always written 0
     * (bidirectional break fails with short pulses); keep it so if the API is extended.
     * Idle states OISx/OISxN (CR2) are not configured by this driver: they keep the reset value
     * 0. With MOE = 0 and off_state_idle (OSSI) = 1, OCx and OCxN go to level 0 after the dead
     * time; with OSSI = 0 the timer releases the pins (level set by the GPIO configuration).
     * v0.14: LOCK is read back after the write; a level other than cfg->lock_level (LOCK
     * frozen at 0 by an earlier BDTR write, e.g. TIM_MainOutputEnable) -> SPL_ERR_STATE. In
     * that case the other BDTR fields WERE written (LOCK 0 locks nothing): an exception to
     * "no write on error", kept visible instead of silently returning OK. Only a timer/system
     * reset restores the write-once LOCK. dead_time = 0 while any CCxNE is set -> STATE, no
     * write (CCL-2 ZMN-09: no dead time on an enabled complementary pair).
     * Caller: call it before any other BDTR write after reset (TIM_MainOutputEnable included);
     * configure OISx / OISxN outside this driver if a non-zero idle level is needed. */
    /* Level: PARAM[F] STATE[r] */
    SPL_Status TIM_BreakDeadTimeInit(TIM_TypeDef *tim, const TIM_BreakDeadTimeInitTypeDef *cfg);
    /* *level = BDTR.LOCK (break timers only). */
    /* Caller: none beyond the file-header duties. */
    /* Level: PARAM[F] */
    SPL_Status TIM_GetLockLevel(TIM_TypeDef *tim, uint8_t *level);
    /* *flags = SR masked with the flags this timer implements (TIM_FLAG_*). */
    /* Caller: none beyond the file-header duties. */
    /* Level: PARAM[F] */
    SPL_Status TIM_GetFlags(TIM_TypeDef *tim, uint32_t *flags);
    /* Single write SR = valid_mask & ~flags (rc_w0, no read-modify-write); flags non-zero subset
     * of the timer's flags.
     * Caller: BIF / B2IF / SBIF stay set while their break source is active: remove the cause
     * and re-read with TIM_GetFlags to confirm. */
    /* Level: PARAM[F] */
    SPL_Status TIM_ClearFlags(TIM_TypeDef *tim, uint32_t flags);
    /* CR2.MMS (TRGO source); CEN = 1 -> STATE; TIM_TRGO_ENCODER_CLOCK without an encoder SMS
     * -> STATE. TIM16/17 -> PARAM.
     * Caller: configure the slave peripheral (ADC, DAC, timer) that consumes TRGO. */
    /* Level: PARAM[F,r] STATE[r] */
    SPL_Status TIM_SetMasterTrigger(TIM_TypeDef *tim, uint32_t source);
#endif /* TIM_SPL_EXTENDED */

#ifdef __cplusplus
}
#endif
#endif
