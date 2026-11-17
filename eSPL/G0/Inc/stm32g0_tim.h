/* SPDX-License-Identifier: MIT */
#ifndef STM32G0_TIM_H
#define STM32G0_TIM_H
#include "stm32g0xx.h"
#include "stm32g0_status.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C"
{
#endif
/* TIM driver (RM0444 Rev 6 sections 21..25, Docs/v0.4_TIM_CONTRACT.md), ported from the
 * STM32G4 SPL v0.13.4. G0B1 instances: TIM1 (advanced), TIM2 (32 bit), TIM3, TIM4, TIM6,
 * TIM7 (basic), TIM14 (one channel; no master/slave, repetition, break or DMA), TIM15,
 * TIM16, TIM17.
 * ERRATA ES0487 2.6.6 (TIM16 / TIM17 clocked by SYSCLK, not TIMPCLK; G030 / G031 / G041 / G050 /
 * G051 / G061 / G070 / G071 / G081, ES0486 2.6.5, ES0488 2.7.6, ES0544 2.7.5, ES0545 2.8.5,
 * ES0546 2.9.5, ES0468 2.6.5, ES0418 2.8.6, ES0412 2.9.6): their prescaler and periods follow
 * SYSCLK and ignore the AHB / APB prescalers; they work only while SYSCLK / PCLK <= 4. Caller:
 * compute TIM16 / TIM17 timing from SYSCLK and keep HPRE x PPRE <= 4 when they run
 * (RCC_SetAhbPrescaler / RCC_SetApbPrescaler). Peripheral clocks, GPIO AF mapping and IRQ priority
 * are board responsibilities. This module is non-blocking, keeps no static state and does not
 * install interrupt handlers. On SPL_ERR_PARAM no register is written.
 * Usage: configure with CEN = 0 (channels with CCxE = 0): TIM_BaseInit, optional TIM_CounterInit
 * and channel init, TIM_GenerateUpdate, then TIM_Enable.
 * Caller duties (all APIs): TIMx bus clock on (RCC); TIM1 / TIM15 kernel clock source if selected
 * (TIM1SEL / TIM15SEL); PSC / ARR / CCR / DTG computed from the actual timer clock; GPIO AF for
 * the CHx, CHxN, ETR and BKIN pins; IRQ handler, NVIC priority and enable (the handler clears the
 * flags it serves with TIM_ClearFlags / TIM_ClearUpdate); serialise calls per instance with ISRs
 * using the same timer (CR1, CR2, DIER, CCER, CCMRx, SMCR are read-modify-write). */
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
/* ARR/period zero is rejected; no registers are written on parameter error.
 * Issue #27 policy: the written CCRx are not checked; ARR and CCR are independent, CCR > ARR is
 * valid. */
/* Caller: PSC (always preloaded) and ARR with ARPE = 1 take effect only at the next update
 * event and no UG is generated here: call TIM_GenerateUpdate before TIM_Enable. */
/* Level: PARAM[F] STATE[F] */
SPL_Status TIM_BaseInit(TIM_TypeDef *tim, const TIM_BaseInitTypeDef *cfg);
/* CR1.CEN set/cleared. v0.13.3: one-pulse mode with a pulse running
 * (OPM=1, CEN=1) and enable=1 -> SPL_BUSY, no write (retry after the
 * pulse; hardware clears CEN at its end). */
/* Caller: configure the timer and load PSC / ARR (TIM_GenerateUpdate) before enable = 1; enable
 * = 0 is not refused while a pulse or period runs and cuts it. */
/* Level: PARAM[F] BUSY[r] */
SPL_Status TIM_Enable(TIM_TypeDef *tim, uint8_t enable);
/* Caller: UG reloads PSC / ARR / RCR / preloaded CCRx, restarts the counter and, with UIE = 1,
 * raises the update interrupt; clear UIF afterwards if that IRQ is unwanted. */
/* Level: PARAM[F] */
SPL_Status TIM_GenerateUpdate(TIM_TypeDef *tim);
/* Caller: clear a stale UIF (TIM_ClearUpdate) before enable = 1, else the IRQ fires at once;
 * the handler clears UIF. */
/* Level: PARAM[F] */
SPL_Status TIM_UpdateInterrupt(TIM_TypeDef *tim, uint8_t enable);
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status TIM_UpdatePending(TIM_TypeDef *tim, uint8_t *pending);
/* SR = valid flags & ~UIF (rc_w0, reserved bits 0; G4 v0.13.5). */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status TIM_ClearUpdate(TIM_TypeDef *tim);
/* cfg->compare: register width only, else PARAM, nothing written. Issue #27 policy: not checked
 * against ARR; CCR > ARR is valid (PWM: OCxREF constant, OC: no match within the period). */
/* Caller: then TIM_ChannelEnable (TIM1 / 15 / 16 / 17: also TIM_MainOutputEnable); the CCR /
 * ARR pair (duty, event timing) is the caller's design. */
/* Level: PARAM[F] STATE[F] */
SPL_Status TIM_OutputCompareInit(TIM_TypeDef *tim, uint8_t channel, const TIM_OCInitTypeDef *cfg);
/* Caller: then TIM_ChannelEnable and read with TIM_GetCapture. */
/* Level: PARAM[F] STATE[F] */
SPL_Status TIM_InputCaptureInit(TIM_TypeDef *tim, uint8_t channel, const TIM_ICInitTypeDef *cfg);
/* Caller: configure the channel first (TIM_OutputCompareInit / TIM_InputCaptureInit); not
 * checked here. TIM1 / 15 / 16 / 17 outputs also need TIM_MainOutputEnable. */
/* Level: PARAM[F] */
SPL_Status TIM_ChannelEnable(TIM_TypeDef *tim, uint8_t channel, uint8_t enable);
/* ERRATA ES0548 2.8.2: a CCR change giving matches on two
 * consecutive counter clocks (ARR->0, crest ARR-1->ARR, valley 1->0) loses the 2nd compare
 * event (toggle output, CCxIF); no workaround.
 * Issue #27 policy: width only; CCR > ARR is valid. ARR and CCR are separate writes, not atomic:
 * to change both together write them in the same update interval with OCxPE = ARPE = 1. */
/* Caller: when CCR and ARR both change, write them in one update interval with OCxPE = ARPE =
 * 1; avoid CCR changes that give matches on two consecutive counter clocks (ES0548 2.8.2). */
/* Level: PARAM[F] */
SPL_Status TIM_SetCompare(TIM_TypeDef *tim, uint8_t channel, uint32_t compare);
/* Caller: after SPL_ERR_HW clear the overcapture flag (TIM_ClearFlags
 * TIM_FLAG_CCx_OVERCAPTURE), it is never cleared here; one reader per channel (reading CCRx
 * clears CCxIF). */
/* Level: PARAM[F] HW[r] WOULD_BLOCK[r] */
SPL_Status TIM_GetCapture(TIM_TypeDef *tim, uint8_t channel, uint32_t *capture);
/* BDTR.MOE set/clear. TIM1/15/16/17 only, else PARAM. enable=1 returns BUSY (no write) while BKDSRM
 * or BK2DSRM is still set (break re-arm in progress, RM0444 21.3.16); retry. The write never copies
 * BKDSRM/BK2DSRM back as 1.
 * CALLER ORDER (LOCK write-once, RM0444 21.4.20): this function writes BDTR, and the first BDTR
 * write after reset freezes LOCK. Called before TIM_BreakDeadTimeInit it freezes LOCK at 0; the
 * later TIM_BreakDeadTimeInit then returns SPL_ERR_STATE (LOCK read-back). Order:
 * TIM_BreakDeadTimeInit, then TIM_MainOutputEnable. Only a reset clears LOCK. Idle level: CR2.OISx
 * / OISxN are never written by the driver (reset value 0). With MOE = 0 (RM0444 Table 120): OSSI =
 * 0 -> outputs not driven by the timer (Hi-Z; the pin level is set by the GPIO configuration, e.g.
 * a pull resistor); OSSI = 1 -> outputs enabled at the inactive level (OCx = CCxP, OCxN = CCxNP),
 * then after a break and a dead time OCx = OISx = 0, OCxN = OISxN = 0.
 * v0.15 (CCL-2 ZMN-23), enable=1: a set break flag of this timer (BIF; B2IF/SBIF where the timer
 * has them) -> SPL_ERR_HW, no write; after the write MOE is read back and a MOE the hardware did
 * not latch (break input still active) -> SPL_ERR_HW (state on failure: BDTR written with the MOE
 * request, MOE = 0 in hardware, no other field changed). Caller: remove the break cause, clear the
 * flag with TIM_ClearFlags (it stays set while the input is active), retry. The flags are never
 * cleared here. enable=0 always clears MOE (flags not checked). AOE (TIM_BreakDeadTimeInit
 * auto_output_enable = 1): hardware sets MOE again at the next update event after the break input
 * is released, without this function; choose it only when an automatic restart is intended. */
/* Caller: call TIM_BreakDeadTimeInit first (LOCK write-once, only a reset clears it); after
 * SPL_ERR_HW remove the break cause, clear the flag with TIM_ClearFlags, retry. */
/* Level: PARAM[F] HW[r] BUSY[r] */
SPL_Status TIM_MainOutputEnable(TIM_TypeDef *tim, uint8_t enable);

/* Extension (from G4 v0.10: counter mode, encoder, complementary outputs, break/dead time,
 * flags, TRGO). See Docs/v0.4_TIM_CONTRACT.md. Always available on G0; TIM_SPL_EXTENDED stays
 * defined for source compatibility with G4 code that tests it. */
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

/* Complete BDTR image; TIM1/15/16/17 only. Written in one access because LOCK is
 * write-once after reset (RM0444 21.4.20 note). MOE, BKBID/BK2BID and BKDSRM/BK2DSRM are
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
    uint32_t break2_enable;      /* BK2E: 0/1, TIM1 only (else must be 0) */
    uint32_t break2_active_high; /* BK2P: 0/1, TIM1 only (else must be 0) */
    uint32_t break2_filter;      /* BK2F: 0..15, TIM1 only (else must be 0) */
} TIM_BreakDeadTimeInitTypeDef;

/* Master mode selection (CR2 MMS) for TRGO. G0: MMS[2:0] only, TIM_TRGO_ENCODER_CLOCK (G4
 * MMS = 1000) is kept for source compatibility and rejected with SPL_ERR_PARAM. */
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
/* G0: no encoder index / direction / error flags (G4 TIM_FLAG_INDEX, _DIRECTION, _INDEX_ERROR,
 * _TRANSITION_ERROR are not provided). */

/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] STATE[F] */
SPL_Status TIM_CounterInit(TIM_TypeDef *tim, const TIM_CounterInitTypeDef *cfg);
/* Caller: RCR is preloaded: it takes effect at the next update event (TIM_GenerateUpdate). */
/* Level: PARAM[F] STATE[F] */
SPL_Status TIM_SetRepetition(TIM_TypeDef *tim, uint32_t repetition);
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status TIM_GetCounter(TIM_TypeDef *tim, uint32_t *count, uint8_t *uif_copy);
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status TIM_SetCounter(TIM_TypeDef *tim, uint32_t count);
/* Caller: set the count range with TIM_BaseInit (ARR) and start counting with TIM_Enable. */
/* Level: PARAM[F] STATE[F] */
SPL_Status TIM_EncoderInit(TIM_TypeDef *tim, const TIM_EncoderInitTypeDef *cfg);
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status TIM_GetDirection(TIM_TypeDef *tim, uint8_t *down);
/* Complementary (CHxN) outputs: TIM1 channel 1..3 (no CH4N on G0: RM0444 21.4.11, CC4NE is
 * reserved), TIM15 / TIM16 / TIM17 channel 1; other timers have none. Other channels: SPL_ERR_PARAM
 * (not checked at SPL_CHECK_FAST, the caller must keep the channel valid). */
/* Caller: then TIM_BreakDeadTimeInit (DTG != 0) and TIM_ComplementaryEnable. */
/* Level: PARAM[F] STATE[F] */
SPL_Status TIM_ComplementaryOutputInit(TIM_TypeDef *tim, uint8_t channel, uint8_t inverted);
/* G4 v0.13.5: enable=1 with BDTR.DTG = 0 -> SPL_ERR_STATE (no dead time: shoot-through in a
 * bridge); call TIM_BreakDeadTimeInit first. enable=0 always allowed. */
/* Caller: choose a dead time long enough for the power stage (only DTG != 0 is checked);
 * outputs also need TIM_MainOutputEnable. */
/* Level: PARAM[F] STATE[F] */
SPL_Status TIM_ComplementaryEnable(TIM_TypeDef *tim, uint8_t channel, uint8_t enable);
/* Caller: dts_hz is the tDTS clock (timer clock divided by CKD); it is not compared with the
 * RCC setting. */
/* Level: PARAM[F,r] */
SPL_Status TIM_DeadTimeToDtg(uint32_t dts_hz, uint32_t dead_time_ns, uint8_t *dtg);
/* ERRATA ES0548 2.8.4: BKBID/BK2BID always written 0
 * (bidirectional break fails with short pulses); keep it so if the API is extended.
 * Must be the first BDTR write after reset (see TIM_MainOutputEnable): MOE = 1 or LOCK != 0 ->
 * SPL_ERR_STATE before the write; after the write LOCK is read back and a value other than
 * cfg->lock_level (LOCK already frozen by an earlier BDTR write) -> SPL_ERR_STATE (the other
 * fields are written; check with TIM_GetLockLevel). OISx/OISxN: not written, see above. */
/* Caller: make it the first BDTR write after reset; check the result: SPL_ERR_STATE after the
 * write means LOCK is frozen at another level until reset (TIM_GetLockLevel). */
/* Level: PARAM[F] STATE[F,r] */
SPL_Status TIM_BreakDeadTimeInit(TIM_TypeDef *tim, const TIM_BreakDeadTimeInitTypeDef *cfg);
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status TIM_GetLockLevel(TIM_TypeDef *tim, uint8_t *level);
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status TIM_GetFlags(TIM_TypeDef *tim, uint32_t *flags);
/* Caller: a break flag stays set while the break input is active: remove the cause first. */
/* Level: PARAM[F] */
SPL_Status TIM_ClearFlags(TIM_TypeDef *tim, uint32_t flags);
/* Caller: configure the slave timer (TIM_SlaveConfig) with the ITRx that maps to this timer
 * (RM0444 Tables 119 / 125 / 130). */
/* Level: PARAM[F,r] STATE[F] */
SPL_Status TIM_SetMasterTrigger(TIM_TypeDef *tim, uint32_t source);

/* ---- v0.11: slave modes, output compare clear, DMA requests and burst
 * (Docs/v0.11_TIME_ANALOG_SYSTEM.md). Issue #27 rule kept: no new path checks a compare value
 * against ARR. ---- */

/* SMS[3:0] codes (RM0444 21.4.3); 1..3 are the encoder modes of TIM_EncoderInit. */
typedef enum
{
    TIM_SLAVE_DISABLED = 0,
    TIM_SLAVE_RESET = 4,
    TIM_SLAVE_GATED = 5,
    TIM_SLAVE_TRIGGER = 6,
    TIM_SLAVE_EXT_CLOCK1 = 7,
    TIM_SLAVE_RESET_TRIGGER = 8
} TIM_SlaveModeTypeDef;

/* TS[4:0] codes; the ITRx meaning per timer is RM0444 Tables 119 / 125 / 130. */
typedef enum
{
    TIM_TRIGGER_ITR0 = 0,
    TIM_TRIGGER_ITR1 = 1,
    TIM_TRIGGER_ITR2 = 2,
    TIM_TRIGGER_ITR3 = 3,
    TIM_TRIGGER_TI1F_ED = 4,
    TIM_TRIGGER_TI1FP1 = 5,
    TIM_TRIGGER_TI2FP2 = 6,
    TIM_TRIGGER_ETRF = 7 /* not on TIM15 */
} TIM_TriggerTypeDef;

typedef struct
{
    uint32_t mode;    /* TIM_SlaveModeTypeDef */
    uint32_t trigger; /* TIM_TriggerTypeDef */
    uint8_t msm;      /* MSM: delay TRGI for master / slave synchronization */
} TIM_SlaveTypeDef;

/* Slave mode controller of TIM1, TIM2..4 and TIM15 (other timers -> PARAM). PARAM: encoder codes
 * 1..3 (use TIM_EncoderInit), codes above 8, trigger above ETRF (TIM15: above TI2FP2), msm > 1,
 * gated mode with TI1F_ED (RM0444 21.4.3 note). Sequence: SMS = 000, then TS / MSM, then SMS
 * (TS must not change while used).
 * ERRATA ES0548 2.8.1: combined reset + trigger with MSM = 1 while CR1.OPM = 1 -> STATE (a trigger
 * at CNT = ARR is missed, no workaround; keep MSM = 0 unless cycle-level synchronization is
 * required). Caller note: setting OPM after this call is not checked.
 * ERRATA ES0548 2.8.3: reset or combined reset + trigger mode while an OCxCE is set -> STATE.
 * ERRATA ES0487 2.6.5 / ES0488 2.7.5 (G031 / G041: TIM1) and ES0418 2.8.5 / ES0412 2.9.5 (G071 /
 * G081: TIM1 and TIM15): a slave may miss the master's trigger when the master clock is faster
 * than the slave clock (no workaround). Caller: on these SKUs clock master and slave equally
 * (TIM1 at TIMPCLK, not the PLLQ x 2 TIM1SEL source) when the slave uses TIM1 / TIM15 TRGO. */
/* Caller: do not set OPM after this call when MSM = 1 (ES0548 2.8.1, not checked); on G031 /
 * G041 / G071 / G081 clock master and slave equally (see above). */
/* Level: PARAM[F] STATE[F] */
SPL_Status TIM_SlaveConfig(TIM_TypeDef *t, const TIM_SlaveTypeDef *cfg);

/* OCREF_CLR source for TIM1, TIM2..4: ETRF (OCCS = 1) or a comparator through OR1.OCREF_CLR
 * (OCCS = 0): 0 = COMP1, 1 = COMP2, 2 = COMP3 (G0B1 / G0C1 only). Other timers or codes -> PARAM.
 * A comparator source on a SKU without COMP (G031, G041, value line) -> UNSUPPORTED.
 * The ETR input itself (prescaler, filter, polarity) is configured by the caller.
 * ERRATA ES0545 2.8.6 / ES0546 2.9.6 (G051 / G061): TIM2_CHx outputs unstable with OCREF_CLR on
 * TIM2_ETRF -> TIM2 + ETRF UNSUPPORTED (also TIM_OcClearEnable on TIM2 with OCCS = 1); use a COMP.
 */
#define TIM_OCCLR_COMP1 0U
#define TIM_OCCLR_COMP2 1U
#define TIM_OCCLR_COMP3 2U
#define TIM_OCCLR_ETRF 0x10U
/* Caller: configure the ETR input (prescaler, filter, polarity) or the selected COMP. */
/* Level: PARAM[F] UNSUPPORTED[F] */
SPL_Status TIM_OcClearSource(TIM_TypeDef *t, uint32_t source);
/* OCxCE of an output channel (CCxS = 00, else STATE) of TIM1, TIM2..4.
 * ERRATA ES0548 2.8.3: enable while SMS is reset or combined reset + trigger -> STATE. */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] STATE[F] UNSUPPORTED[F] */
SPL_Status TIM_OcClearEnable(TIM_TypeDef *t, uint8_t ch, uint8_t enable);

/* DIER DMA request enables (image of the DE bits valid for the timer: UDE, CCxDE, COMDE, TDE;
 * TIM14 has none -> PARAM). Bits outside the timer's set -> PARAM; other DIER bits kept. */
/* Caller: configure the DMA channel and its DMAMUX request; keep the buffer valid while a
 * request is enabled. */
/* Level: PARAM[F] */
SPL_Status TIM_DmaRequestConfig(TIM_TypeDef *t, uint32_t requests);
/* DMA burst (DCR): base_index = register index from CR1 (DBA, offset / 4), transfers 1..18 (DBL).
 * Timers with DCR: TIM1..4, TIM15..17 (else PARAM). A burst range covering DCR (18) or DMAR (19),
 * or beyond TISEL (26) -> PARAM. A DMA request enabled in DIER -> STATE. The DMA channel uses
 * TIM_GetDmaBurstAddress as peripheral address. */
/* Caller: program the DMA channel with TIM_GetDmaBurstAddress as peripheral address and a
 * buffer laid out for this timer's register map, then enable the request
 * (TIM_DmaRequestConfig). */
/* Level: PARAM[F] STATE[F] */
SPL_Status TIM_DmaBurstConfig(TIM_TypeDef *t, uint32_t base_index, uint32_t transfers);
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status TIM_GetDmaBurstAddress(TIM_TypeDef *t, uint32_t *address);

#ifdef __cplusplus
}
#endif
#endif
