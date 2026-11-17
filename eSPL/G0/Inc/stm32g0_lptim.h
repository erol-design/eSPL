/* SPDX-License-Identifier: MIT */
#ifndef STM32G0_LPTIM_H
#define STM32G0_LPTIM_H
#include <stdint.h>
#include "stm32g0xx.h"
#include "stm32g0_status.h"
#include "stm32g0_sku.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* v0.8 SKU profile: this API exists only where the device has the peripheral (SPL_G0_HAS_LPTIM);
 * absent on the value line G030 / G050 / G070 / G0B0. */
#if SPL_G0_HAS_LPTIM

/* Low-power timers LPTIM1 and LPTIM2 (RM0444 Rev 6 section 26, Docs/v0.4_LPTIM_CONTRACT.md),
 * ported from the STM32G4 SPL v0.13.4. Any other pointer is SPL_ERR_PARAM. G0 differences:
 * encoder mode on LPTIM1 only (RM0444 Table 135: LPTIM2 + encoder -> SPL_ERR_UNSUPPORTED),
 * TRIGSEL[2:0] (sources 0..7), input remap in LPTIM_CFGR2 (not handled). No static state, no
 * waits. Caller duties (all APIs): kernel clock (RCC_SetKernelClock RCC_KERNEL_LPTIM1 / LPTIM2)
 * and bus clock (RCC_PERIPH_LPTIM1 / LPTIM2); GPIO AF for LPTIMx_IN1, IN2, ETR, OUT; LPTIMx IRQ
 * handler, NVIC priority and enable when interrupts are used (the handler clears the flags it
 * serves with LPTIM_ClearFlags); keep the LPTIM_SyncTypeDef of each instance alive and shared by
 * all writers of that instance.
 * Serialize all callers per instance.
 *
 * Usage, recommended sequence (RM0444 26.4.11 / 26.4.13 and ST's reference flow):
 *   LPTIM_Init (ENABLE=0) -> LPTIM_InterruptConfig (ENABLE=0) -> LPTIM_Enable
 *   -> LPTIM_SetAutoReload / LPTIM_SetCompare -> poll LPTIM_SyncStatus until
 *   OK (also covers the two-counter-clock enable latency) -> LPTIM_Start.
 * Bound the SyncStatus poll (caller timeout): ARROK/CMPOK come from the
 * kernel clock domain, and with CKSEL=1 Input1 is the kernel clock
 * (RM0444 26.4.12), so without input edges they never set. */

/* Field values for LPTIM_InitTypeDef (raw, unshifted). */
#define LPTIM_CLOCK_INTERNAL 0U /* CKSEL=0 */
#define LPTIM_CLOCK_EXTERNAL 1U /* CKSEL=1: clocked by Input1 */

#define LPTIM_CKPOL_RISING 0U /* Encoder sub-mode 1 when encoder=1. */
#define LPTIM_CKPOL_FALLING 1U
#define LPTIM_CKPOL_BOTH 2U /* Needs an internal clock >= 4x input. */

#define LPTIM_TRIGGER_SOFTWARE 0U /* TRIGEN=00 */
#define LPTIM_TRIGGER_RISING 1U
#define LPTIM_TRIGGER_FALLING 2U
#define LPTIM_TRIGGER_BOTH 3U

#define LPTIM_TRIGSEL_MAX 7U /* lptim_ext_trig0..7 (RM0444 26.4.3, 26.7.4). */

/* LPTIM_Start modes. */
#define LPTIM_START_CONTINUOUS 0U /* CNTSTRT */
#define LPTIM_START_SINGLE 1U     /* SNGSTRT */

/* Flags accepted by LPTIM_ClearFlags (ISR bit == ICR clear bit position).
 * ARROK/CMPOK are owned by the ARR/CMP write services and are excluded. */
#define LPTIM_FLAGS_CLEARABLE                                                                      \
    (LPTIM_ISR_CMPM | LPTIM_ISR_ARRM | LPTIM_ISR_EXTTRIG | LPTIM_ISR_UP | LPTIM_ISR_DOWN)
#define LPTIM_FLAGS_ALL (LPTIM_FLAGS_CLEARABLE | LPTIM_ISR_ARROK | LPTIM_ISR_CMPOK)

/* Interrupts accepted by LPTIM_InterruptConfig. ARROKIE/CMPOKIE are not
 * offered: their flags are owned by the write services (polling only). */
#define LPTIM_IT_ALLOWED                                                                           \
    (LPTIM_IER_CMPMIE | LPTIM_IER_ARRMIE | LPTIM_IER_EXTTRIGIE | LPTIM_IER_UPIE | LPTIM_IER_DOWNIE)

typedef struct
{
    uint32_t clock_source;   /* LPTIM_CLOCK_x (CKSEL). */
    uint32_t clock_polarity; /* LPTIM_CKPOL_x (CKPOL). */
    uint32_t clock_filter;   /* 0..3 (CKFLT): 1, 2, 4, 8 samples. */
    uint32_t trigger_filter; /* 0..3 (TRGFLT). */
    uint32_t prescaler;      /* 0..7 (PRESC): /1 .. /128. */
    uint32_t trigger_source; /* 0..7 (TRIGSEL), used when trigger_edge != 0. 5 needs COMP3
                              * (LPTIM1) / TAMP3 (LPTIM2), 6 / 7 need COMP1 / COMP2 (RM0444 Tables
                              * 138 / 139): absent source -> UNSUPPORTED. */
    uint32_t trigger_edge;   /* LPTIM_TRIGGER_x (TRIGEN). */
    uint32_t timeout;        /* 0/1 (TIMOUT). */
    uint32_t set_once;       /* 0/1 (WAVE): 1 = set-once waveform. */
    uint32_t polarity;       /* 0/1 (WAVPOL): 1 = inverted output. */
    uint32_t preload;        /* 0/1 (PRELOAD): ARR/CMP update at period end. */
    uint32_t count_external; /* 0/1 (COUNTMODE): count Input1 events. */
    uint32_t encoder;        /* 0/1 (ENC), LPTIM1 only. */
} LPTIM_InitTypeDef;

/* Caller-owned ARR/CMP write tracking (zero-initialise; zero again after
 * LPTIM_Disable). Non-zero = a write is not acknowledged yet. */
typedef struct
{
    uint32_t arr_pending;
    uint32_t cmp_pending;
} LPTIM_SyncTypeDef;

/* Writes the whole CFGR. STATE: ENABLE=1 (RM0444 26.7.4 caution).
 * PARAM: field out of range, CKPOL=3, CKSEL=1 with both edges, encoder
 * with CKSEL=1 or PRESC!=0, COUNTMODE=1 with CKSEL=0 and PRESC!=0. */
/* Caller: follow the recommended sequence above. */
/* Level: PARAM[F] STATE[F] UNSUPPORTED[F] */
SPL_Status LPTIM_Init(LPTIM_TypeDef *lptim, const LPTIM_InitTypeDef *init);
/* Writes IER = mask (subset of LPTIM_IT_ALLOWED, 0 disables all).
 * STATE: ENABLE=1 (RM0444 26.7.3 caution). */
/* Caller: clear stale flags with LPTIM_ClearFlags while IER = 0, before this call; the handler
 * clears the flags it serves (LPTIM_ClearFlags from the ISR). */
/* Level: PARAM[F] STATE[F] UNSUPPORTED[F] */
SPL_Status LPTIM_InterruptConfig(LPTIM_TypeDef *lptim, uint32_t mask);
/* CR = ENABLE (RSTARE kept, command bits never written back). */
/* Caller: poll LPTIM_SyncStatus after the ARR / CMP writes (covers the two-counter-clock enable
 * latency) before LPTIM_Start. */
/* Level: PARAM[F] */
SPL_Status LPTIM_Enable(LPTIM_TypeDef *lptim);
/* IER = 0: CR = 0. Cannot touch the caller's sync state: zero every
 * LPTIM_SyncTypeDef in use afterwards, else a write left unacknowledged
 * keeps pending=1 and SetAutoReload/SetCompare/SyncStatus return BUSY
 * forever after the next LPTIM_Enable.
 * ERRATA ES0548 2.9.1: with IER != 0 ENABLE is not cleared;
 * LPTIM1 is reset via RCC_APB1RSTR1.LPTIM1RST / LPTIM2RST (set, then cleared) and ALL
 * LPTIM registers (CFGR, IER, ARR, CMP, ...) return to reset values: run
 * LPTIM_Init / LPTIM_InterruptConfig again. Do not race other writers of
 * RCC_APB1RSTR1 (RMW). */
/* Caller: zero every LPTIM_SyncTypeDef afterwards; after the RCC reset path (IER != 0) run
 * LPTIM_Init and LPTIM_InterruptConfig again; do not race other RCC_APBRSTR1 writers. */
/* Level: PARAM[F] */
SPL_Status LPTIM_Disable(LPTIM_TypeDef *lptim);
/* Sets CNTSTRT or SNGSTRT. STATE: ENABLE=0 (hardware discards the write,
 * RM0444 26.4.8) or single mode with ENC=1 (encoder needs continuous).
 * Checks CR.ENABLE only: the timer is enabled two counter clocks after
 * ENABLE=1 (RM0444 26.4.13) and start bits written earlier are lost, so
 * OK does not prove a start then. Call it after the recommended
 * sequence or at least two counter clocks after LPTIM_Enable. */
/* SINGLE during a running one-shot cycle: OK, discarded by hardware (RM0444 26.4.8); start
 * the next cycle after ARRM (G4 v0.13.5 #17). */
/* Caller: call it after the recommended sequence; SINGLE during a running one-shot is
 * discarded: start the next cycle after ARRM. */
/* Level: PARAM[F,r] STATE[F] */
SPL_Status LPTIM_Start(LPTIM_TypeDef *lptim, uint32_t mode);
/* ARR write (1..0xFFFF; 0 can never be > CMP). STATE: ENABLE=0. BUSY:
 * previous ARR write not acknowledged (ARROK=0). Clears ARROK via
 * ICR.ARROKCF immediately before the write, then sets arr_pending.
 * ERRATA ES0548 2.9.2: STATE (no write) while IER != 0;
 * program ARR/CMP with all LPTIM interrupts disabled. */
/* Caller: disable all LPTIM interrupts first; with CMP: enlarge ARR first, shrink CMP first;
 * poll LPTIM_SyncStatus (bounded) before the next write. */
/* Level: PARAM[F] STATE[F] BUSY[r] */
SPL_Status LPTIM_SetAutoReload(LPTIM_TypeDef *lptim, LPTIM_SyncTypeDef *sync, uint32_t arr);
/* CMP write (0..0xFFFF), same rules with CMPOK / ICR.CMPOKCF. CMP >= ARR -> PARAM
 * (RM0444 26.4.10: CMP strictly below ARR for PWM / one-pulse / set-once; checked once the
 * STATE gates pass).
 * ERRATA ES0548 2.9.2: STATE while IER != 0 (as ARR).
 * G4 note ES0431 2.9.3 (events one kernel clock late, CMP = wanted - 1) is not listed in
 * ES0548 Rev 4 for G0B1: no compensation is applied or documented as required (G6 check). */
/* Caller: same as LPTIM_SetAutoReload (IER = 0, write order, SyncStatus poll). */
/* Level: PARAM[F] STATE[F] BUSY[r] */
SPL_Status LPTIM_SetCompare(LPTIM_TypeDef *lptim, LPTIM_SyncTypeDef *sync, uint32_t cmp);
/* Updates sync from ARROK/CMPOK (flags not cleared). OK when nothing is
 * pending, BUSY otherwise. */
/* Caller: bound the poll with a timeout (no kernel clock or Input1 edges: never OK). */
/* Level: PARAM[F] BUSY[r] */
SPL_Status LPTIM_SyncStatus(LPTIM_TypeDef *lptim, LPTIM_SyncTypeDef *sync);
/* RM0444 26.7.8 double read: up to three CNT reads; OK when two
 * consecutive reads match, BUSY otherwise (retry). STATE: RSTARE=1
 * (a read resets CNT, RM0444 26.4.14). */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] STATE[F] BUSY[r] */
SPL_Status LPTIM_ReadCounter(LPTIM_TypeDef *lptim, uint32_t *count);
/* *flags = ISR & LPTIM_FLAGS_ALL. No side effects. */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status LPTIM_GetFlags(LPTIM_TypeDef *lptim, uint32_t *flags);
/* ICR = flags (W1C), non-zero subset of LPTIM_FLAGS_CLEARABLE.
 * ERRATA ES0548 2.9.2: with IER != 0, STATE (no write) unless
 * every flag has its IER bit set, and call it from the LPTIM ISR only. */
/* Caller: with IER != 0 call it only from the LPTIM ISR (ES0548 2.9.2, not checked). */
/* Level: PARAM[F] STATE[F] UNSUPPORTED[F] */
SPL_Status LPTIM_ClearFlags(LPTIM_TypeDef *lptim, uint32_t flags);

#endif /* SPL_G0_HAS_LPTIM */

#ifdef __cplusplus
}
#endif
#endif
