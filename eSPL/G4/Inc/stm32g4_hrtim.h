/* SPDX-License-Identifier: MIT */
#ifndef STM32G4_HRTIM_H
#define STM32G4_HRTIM_H
#include <stdint.h>
#include "stm32g4xx.h"
#include "stm32g4_status.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* High-resolution timer HRTIM1 driver (RM0440 Rev 9 section 28). Present only where CMSIS
 * defines HRTIM1 (G474/G484/G414); on every other SKU each API returns SPL_ERR_UNSUPPORTED
 * without register access.
 * Scope v0.14: DLL calibration, timer A..F time base (continuous / single-shot), compare units
 * 1..4, output set/reset crossbar, polarity, idle and fault levels, positive dead time, output
 * enable/disable, counter enable, software update/reset/force, flags. NOT handled: master timer
 * configuration, up-down counting, push-pull, interleaved / half mode, burst mode, external
 * events, fault inputs, capture, chopper, ADC triggers, DMA, delayed protection, greater-than
 * comparison (HRTIM_TimerInit clears HRTIM_TIMxCR2, so those modes stay off).
 * Common rules: no static state, no function waits, registers are written only after all
 * parameter and state checks passed (on any error nothing is written), no automatic recovery.
 * Issue #27: PER and CMP are independent (CMP > PER is valid, no event in the period); PER and
 * CMP are separate, non-atomic writes (contract, multi-register rules).
 * Usage (RM0440 28.3.24):
 *   HRTIM_DllCalibrationStart -> poll HRTIM_DllReady -> HRTIM_DllPeriodic(1, rate)
 *   -> HRTIM_TimerInit (prescaler first, RM 28.3.3) -> HRTIM_SetCompare -> HRTIM_OutputConfig
 *   -> HRTIM_DeadTimeConfig (optional) -> GPIO AF -> HRTIM_OutputEnable
 *   -> HRTIM_ForceOutput (with dead time: complementary start state, RM 28.3.24)
 *   -> HRTIM_CounterEnable.
 * Caller duties (all APIs): HRTIM1 APB2 clock on (RCC_PERIPH_HRTIM1) with fHRTIM inside the DLL
 * lock range (datasheet); GPIO alternate functions of the HRTIM pins; NVIC and handlers (the
 * handler clears the flags it serves); fault input configuration; serialize all callers (MCR,
 * OUTxR and CR2 are read-modify-write); the PER / CMP relation and the duty / event timing are
 * the caller's design (the driver checks only per-register limits); with PREEN = 1 change PER
 * and CMP together within one update window (or TxUDIS + HRTIM_SoftwareUpdate); on a STATE
 * error decide yourself (stop the counter, disable the outputs); a caller that sets HALF
 * directly owns the CMP1 relation.
 * Contract: Docs/v0.14_HRTIM_CONTRACT.md. */

/* Timer index (A..F). */
#define HRTIM_TIMER_A 0U
#define HRTIM_TIMER_B 1U
#define HRTIM_TIMER_C 2U
#define HRTIM_TIMER_D 3U
#define HRTIM_TIMER_E 4U
#define HRTIM_TIMER_F 5U
#define HRTIM_TIMER_COUNT 6U
/* Timer mask bit for HRTIM_CounterEnable / HRTIM_SoftwareUpdate / HRTIM_SoftwareReset. */
#define HRTIM_TIMER_BIT(t) (1UL << (t))
#define HRTIM_TIMER_ALL 0x3FUL

/* Output mask bit for HRTIM_OutputEnable / HRTIM_OutputDisable / HRTIM_OutputStatus:
 * timer t (0..5), output o (1..2) -> OENR/ODISR/ODSR bit 2*t + (o-1). */
#define HRTIM_OUTPUT_BIT(t, o) (1UL << ((2UL * (t)) + ((o) - 1UL)))
#define HRTIM_OUTPUT_ALL 0xFFFUL

/* Set / reset sources (HRTIM_SETx1R / RSTx1R layout, RM0440 28.5.32..35). Bit 0 (SST / SRT,
 * software trigger) is not a source: use HRTIM_ForceOutput. */
#define HRTIM_SRC_RESYNC (1UL << 1)
#define HRTIM_SRC_PER (1UL << 2)
#define HRTIM_SRC_CMP1 (1UL << 3)
#define HRTIM_SRC_CMP2 (1UL << 4)
#define HRTIM_SRC_CMP3 (1UL << 5)
#define HRTIM_SRC_CMP4 (1UL << 6)
#define HRTIM_SRC_MSTPER (1UL << 7)
#define HRTIM_SRC_MSTCMP1 (1UL << 8)
#define HRTIM_SRC_MSTCMP2 (1UL << 9)
#define HRTIM_SRC_MSTCMP3 (1UL << 10)
#define HRTIM_SRC_MSTCMP4 (1UL << 11)
/* Bits 12..20 TIMEVNT1..9, 21..30 EXTEVNT1..10, 31 UPDATE: valid sources, raw bit values. */
#define HRTIM_SRC_UPDATE (1UL << 31)

/* Timer flags (HRTIM_TIMxISR clearable flags, RM0440 28.5.13): CMP1..4, REP, UPD, CPT1..2,
 * SET1, RST1, SET2, RST2, RST, DLYPRT. */
#define HRTIM_TIMFLAG_CMP1 (1UL << 0)
#define HRTIM_TIMFLAG_CMP2 (1UL << 1)
#define HRTIM_TIMFLAG_CMP3 (1UL << 2)
#define HRTIM_TIMFLAG_CMP4 (1UL << 3)
#define HRTIM_TIMFLAG_REP (1UL << 4)
#define HRTIM_TIMFLAG_UPD (1UL << 6)
#define HRTIM_TIMFLAG_ALL 0x7FDFUL
/* Common flags clearable by HRTIM_ClearCommonFlags (HRTIM_ISR): FLT1..6, SYSFLT, BMPER.
 * DLLRDY is the DLL readiness evidence: it is cleared only by HRTIM_DllCalibrationStart. */
#define HRTIM_FLAG_FLT_ALL 0x7FUL
#define HRTIM_FLAG_DLLRDY (1UL << 16)
#define HRTIM_FLAG_BMPER (1UL << 17)

    typedef struct
    {
        uint32_t prescaler;     /* CKPSC 0..7: fHRCK = 32 x fHRTIM / 2^CKPSC */
        uint32_t period;        /* PER, within the CKPSC limits of RM0440 Table 232 */
        uint32_t repetition;    /* REP 0..255 */
        uint32_t continuous;    /* CONT 0/1: 0 = single-shot (started by a reset event) */
        uint32_t retrigger;     /* RETRIG 0/1 (single-shot only) */
        uint32_t preload;       /* PREEN 0/1: PER/CMP/REP written to preload registers */
        uint32_t update_on_rep; /* TxREPU 0/1: preload transfer when REP counts to 0 */
    } HRTIM_TimerInitTypeDef;

    typedef struct
    {
        uint32_t set_sources;   /* HRTIM_SRC_* mask -> SETxyR (bit 0 must be 0) */
        uint32_t reset_sources; /* HRTIM_SRC_* mask -> RSTxyR (bit 0 must be 0) */
        uint32_t polarity;      /* POLy 0 = active high, 1 = active low */
        uint32_t idle_level;    /* IDLESy 0 = inactive, 1 = active in IDLE state */
        uint32_t fault_state;   /* FAULTy 0 = no action, 1 = active, 2 = inactive, 3 = Hi-Z */
    } HRTIM_OutputTypeDef;

    typedef struct
    {
        uint32_t enable;    /* DTEN 0/1; 0 clears DTEN only (DTxR untouched) */
        uint32_t prescaler; /* DTPRSC 0..7: tDTG = 2^DTPRSC x tHRTIM / 8 */
        uint32_t rising;    /* DTRx 0..511 (positive dead time only) */
        uint32_t falling;   /* DTFx 0..511 (positive dead time only) */
    } HRTIM_DeadTimeTypeDef;

    /* --- DLL (RM0440 28.3.24, 28.5.75) --- */
    /* CALEN=1 -> STATE (CAL and CALEN must not be set together). Clears DLLRDY (ICR), then
     * DLLCR = CAL (CALRTE kept).
     * Caller: poll HRTIM_DllReady with a caller-defined bound (no timeout here); no timer may run
     * until DLLRDY = 1 (HRTIM_CounterEnable checks it). */
    /* Level: STATE[r] UNSUPPORTED[r] */
    SPL_Status HRTIM_DllCalibrationStart(void);
    /* *ready = ISR.DLLRDY. */
    /* Caller: none beyond the file-header duties. */
    /* Level: PARAM[F] UNSUPPORTED[r] */
    SPL_Status HRTIM_DllReady(uint8_t *ready);
    /* enable=1: rate 0..3 (CALRTE), DLLRDY=0 -> STATE; DLLCR = CALEN | rate. enable=0: DLLCR = 0
     * (rate ignored).
     * Caller: none beyond the file-header duties. */
    /* Level: PARAM[F] STATE[r] UNSUPPORTED[r] */
    SPL_Status HRTIM_DllPeriodic(uint8_t enable, uint32_t rate);

    /* --- timer A..F --- */
    /* Counter must be stopped (TxCEN=0, else STATE): the prescaler cannot change once the
     * timer runs (RM 28.3.3). Writes TIMxCR = CKPSC | CONT | RETRIG | PREEN | TxREPU (all other
     * bits 0), TIMxCR2 = 0, PERxR, REPxR. v0.14: a non-zero CMP1..4 already written that is
     * outside the Table 232 limits of the new prescaler -> STATE, no write (reprogram it with
     * HRTIM_SetCompare first, or write it 0). Issue #27: CMP > PER is valid, not checked.
     * Caller: with preload = 1 PER / REP are written to the preload registers and become active
     * at the next update event (e.g. HRTIM_SoftwareUpdate); TIMxCR2 = 0 drops any up-down /
     * GTCMP / DCDx setting made outside the driver. */
    /* Level: PARAM[F] STATE[r] UNSUPPORTED[r] */
    SPL_Status HRTIM_TimerInit(uint32_t timer, const HRTIM_TimerInitTypeDef *cfg);
    /* period within the Table 232 limits of the current CKPSC. Allowed while running (PREEN
     * decides when it takes effect). Issue #27 policy: the written CMP1..4 are not checked,
     * CMP > PER is valid (no event in the period). PER and CMP are separate, non-atomic writes.
     * Running with PREEN = 0 and period < current PERxR -> STATE, no write (a period below the
     * moving counter would let it run to 0xFFFF; no counter read, no race). Shrink with PREEN = 1
     * or while stopped; growing is allowed.
     * Caller: keep PER and CMP consistent for the duty you want; with PREEN = 0, or with an
     * update event between the PER and CMP writes, the active pair can briefly mix old and new
     * values (file header). */
    /* Level: PARAM[F] STATE[r] UNSUPPORTED[r] */
    SPL_Status HRTIM_SetPeriod(uint32_t timer, uint32_t period);
    /* unit 1..4. value within the Table 232 limits of the current CKPSC, or 0 for units 1 and 3
     * (skipped pulse, RM 28.3.4). Issue #27 policy: the relation to PER is not checked;
     * CMP > PER is valid (no match and no event in the period). Allowed while running.
     * Caller: same PER / CMP consistency duty as HRTIM_SetPeriod. */
    /* Level: PARAM[F] UNSUPPORTED[r] */
    SPL_Status HRTIM_SetCompare(uint32_t timer, uint32_t unit, uint32_t value);

    /* --- outputs --- */
    /* output 1..2. Timer stopped and this output not enabled (OENR), else STATE (POL, FAULT,
     * IDLES are fixed once the timer runs, RM 28.5.52). Writes SETxyR, RSTxyR and the POLy,
     * IDLESy, FAULTy fields of OUTxR (other OUTxR bits kept).
     * Caller: fault_state takes effect only with fault inputs configured outside this driver;
     * choose polarity / idle_level so that the idle level is the safe level of the power stage. */
    /* Level: PARAM[F] STATE[r] UNSUPPORTED[r] */
    SPL_Status HRTIM_OutputConfig(uint32_t timer, uint32_t output, const HRTIM_OutputTypeDef *cfg);
    /* Timer stopped, both outputs of the timer disabled and no DTxR lock bit set, else STATE.
     * enable=1: DTxR = rising | prescaler | falling (positive signs, locks 0), OUTxR.DTEN = 1.
     * enable=0: OUTxR.DTEN = 0. Negative (overlapping) dead time is not offered.
     * enable=1 with rising = 0 or falling = 0 -> PARAM (both bridge switches would conduct at the
     * same edge).
     * Caller: compute rising / falling from the real tHRTIM and the switch timing of the power
     * stage; enable = 0 removes the dead time (shoot-through risk on a complementary pair). */
    /* Level: PARAM[F] STATE[r] UNSUPPORTED[r] */
    SPL_Status HRTIM_DeadTimeConfig(uint32_t timer, const HRTIM_DeadTimeTypeDef *cfg);
    /* Software set (active=1, SETxyR.SST) or reset (active=0, RSTxyR.SRT) of one output; the
     * trigger bit self-clears. Works with the counter stopped (RM 28.3.24).
     * Caller: none beyond the file-header duties. */
    /* Level: PARAM[F] UNSUPPORTED[r] */
    SPL_Status HRTIM_ForceOutput(uint32_t timer, uint32_t output, uint8_t active);
    /* mask = HRTIM_OUTPUT_BIT(...) set, 1..0xFFF. OENR / ODISR = mask (writing 0 has no effect).
     * Caller: GPIO AF set and the output start state forced (HRTIM_ForceOutput) before enabling
     * the outputs; the pins are driven at once. */
    /* Level: PARAM[F] UNSUPPORTED[r] */
    SPL_Status HRTIM_OutputEnable(uint32_t mask);
    /* Disables the outputs in mask (ODISR); they go to their IDLE level.
     * Caller: none beyond the file-header duties. */
    /* Level: PARAM[F] UNSUPPORTED[r] */
    SPL_Status HRTIM_OutputDisable(uint32_t mask);
    /* *enabled = OENR (1 = output in RUN; cleared by hardware on IDLE / FAULT), *disable_status =
     * ODSR. An ODSR bit is significant only while its OENR bit is 0: 0 = IDLE, 1 = FAULT
     * (RM0440 28.5.63, Table 249); with the output enabled it may read 1 (seen on G474RE).
     * Caller: none beyond the file-header duties. */
    /* Level: PARAM[F] UNSUPPORTED[r] */
    SPL_Status HRTIM_OutputStatus(uint32_t *enabled, uint32_t *disable_status);

    /* --- counters, software events --- */
    /* timer_mask = HRTIM_TIMER_BIT(...) set, 1..0x3F. enable=1: DLLRDY=0 -> STATE (RM 28.3.24:
     * start only once the DLL is ready); MCR TxCEN set / cleared (other MCR bits kept).
     * Caller: configure the timer, outputs and dead time before enable = 1; enable = 0 stops
     * the counter but keeps the last output levels (disable the outputs for a safe state). */
    /* Level: PARAM[F] STATE[r] UNSUPPORTED[r] */
    SPL_Status HRTIM_CounterEnable(uint32_t timer_mask, uint8_t enable);
    /* CR2 TxSWU / TxRST (self-clearing triggers): one write, the swap bits SWPx kept, no other
     * trigger copied back. timer_mask 1..0x3F (master timer not handled).
     * Caller: none beyond the file-header duties. */
    /* Level: PARAM[F] UNSUPPORTED[r] */
    SPL_Status HRTIM_SoftwareUpdate(uint32_t timer_mask);
    /* CR2 TxRST: software counter reset of the timers in timer_mask (same write rules).
     * Caller: none beyond the file-header duties. */
    /* Level: PARAM[F] UNSUPPORTED[r] */
    SPL_Status HRTIM_SoftwareReset(uint32_t timer_mask);

    /* --- flags --- */
    /* *flags = TIMxISR (status bits 16..21 included). */
    /* Caller: none beyond the file-header duties. */
    /* Level: PARAM[F] UNSUPPORTED[r] */
    SPL_Status HRTIM_GetFlags(uint32_t timer, uint32_t *flags);
    /* flags non-zero subset of HRTIM_TIMFLAG_ALL -> TIMxICR = flags (W1C), ISR read back. */
    /* Caller: none beyond the file-header duties. */
    /* Level: PARAM[F] UNSUPPORTED[r] */
    SPL_Status HRTIM_ClearFlags(uint32_t timer, uint32_t flags);
    /* *flags = ISR. */
    /* Caller: none beyond the file-header duties. */
    /* Level: PARAM[F] UNSUPPORTED[r] */
    SPL_Status HRTIM_GetCommonFlags(uint32_t *flags);
    /* flags non-zero subset of FLT1..6, SYSFLT, BMPER -> ICR = flags (W1C). DLLRDY refused.
     * Caller: remove the fault cause before clearing a fault flag. */
    /* Level: PARAM[F] UNSUPPORTED[r] */
    SPL_Status HRTIM_ClearCommonFlags(uint32_t flags);

#ifdef __cplusplus
}
#endif
#endif /* STM32G4_HRTIM_H */
