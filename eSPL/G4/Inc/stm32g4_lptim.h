#ifndef STM32G4_LPTIM_H
#define STM32G4_LPTIM_H
#include <stdint.h>
#include "stm32g4xx.h"
#include "stm32g4_status.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* Low-power timer LPTIM1 (RM0440 Rev 9 section 33). Only LPTIM1 exists on the
 * supported SKUs; any other pointer is SPL_ERR_PARAM. No static state, no
 * waits. Caller duties: kernel clock RCC_CCIPR.LPTIM1SEL and bus clock
 * RCC_APB1ENR1.LPTIM1EN (RCC module / board layer); GPIO AF for LPTIM1_IN1,
 * IN2, ETR, OUT; LPTIM_OR input remap (not handled in v0.11).
 * Serialize all callers per instance. Contract: Docs/v0.11_LPTIM_CONTRACT.md.
 *
 * Recommended sequence (as RM0440 33.4.10/33.4.12 and ST's reference flow):
 *   LPTIM_Init (ENABLE=0) -> LPTIM_InterruptConfig (ENABLE=0) -> LPTIM_Enable
 *   -> LPTIM_SetAutoReload / LPTIM_SetCompare -> poll LPTIM_SyncStatus until
 *   OK (also covers the two-counter-clock enable latency) -> LPTIM_Start.
 * Bound the SyncStatus poll (caller timeout): ARROK/CMPOK come from the
 * kernel clock domain, and with CKSEL=1 Input1 is the kernel clock
 * (RM0440 33.4.11), so without input edges they never set. */

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

#define LPTIM_TRIGSEL_MAX 12U /* lptim_ext_trig0..12 (RM0440 Table 327). */

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
        uint32_t trigger_source; /* 0..12 (TRIGSEL), used when trigger_edge != 0. */
        uint32_t trigger_edge;   /* LPTIM_TRIGGER_x (TRIGEN). */
        uint32_t timeout;        /* 0/1 (TIMOUT). */
        uint32_t set_once;       /* 0/1 (WAVE): 1 = set-once waveform. */
        uint32_t polarity;       /* 0/1 (WAVPOL): 1 = inverted output. */
        uint32_t preload;        /* 0/1 (PRELOAD): ARR/CMP update at period end. */
        uint32_t count_external; /* 0/1 (COUNTMODE): count Input1 events. */
        uint32_t encoder;        /* 0/1 (ENC). */
    } LPTIM_InitTypeDef;

    /* Caller-owned ARR/CMP write tracking (zero-initialise; zero again after
     * LPTIM_Disable). Non-zero = a write is not acknowledged yet. */
    typedef struct
    {
        uint32_t arr_pending;
        uint32_t cmp_pending;
    } LPTIM_SyncTypeDef;

    /* Writes the whole CFGR. STATE: ENABLE=1 (RM0440 33.7.4 caution).
     * PARAM: field out of range, CKPOL=3, CKSEL=1 with both edges, encoder
     * with CKSEL=1 or PRESC!=0, COUNTMODE=1 with CKSEL=0 and PRESC!=0. */
    /* Seviye: PARAM[S] STATE[r] */
    SPL_Status LPTIM_Init(LPTIM_TypeDef *lptim, const LPTIM_InitTypeDef *init);
    /* Writes IER = mask (subset of LPTIM_IT_ALLOWED, 0 disables all).
     * STATE: ENABLE=1 (RM0440 33.7.3 caution). */
    /* Seviye: PARAM[S] STATE[r] */
    SPL_Status LPTIM_InterruptConfig(LPTIM_TypeDef *lptim, uint32_t mask);
    /* CR = ENABLE (RSTARE kept, command bits never written back). */
    /* Seviye: PARAM[S] */
    SPL_Status LPTIM_Enable(LPTIM_TypeDef *lptim);
    /* IER = 0: CR = 0. Cannot touch the caller's sync state: zero every
     * LPTIM_SyncTypeDef in use afterwards, else a write left unacknowledged
     * keeps pending=1 and SetAutoReload/SetCompare/SyncStatus return BUSY
     * forever after the next LPTIM_Enable.
     * ERRATA ES0431 2.9.1 / ES0430 2.13.1 / ES0523 2.9.1: with IER != 0 ENABLE is not cleared;
     * LPTIM1 is reset via RCC_APB1RSTR1.LPTIM1RST (set, then cleared) and ALL
     * LPTIM registers (CFGR, IER, ARR, CMP, ...) return to reset values: run
     * LPTIM_Init / LPTIM_InterruptConfig again. Do not race other writers of
     * RCC_APB1RSTR1 (RMW). */
    /* Seviye: PARAM[S] */
    SPL_Status LPTIM_Disable(LPTIM_TypeDef *lptim);
    /* Sets CNTSTRT or SNGSTRT. STATE: ENABLE=0 (hardware discards the write,
     * RM0440 33.4.6) or single mode with ENC=1 (encoder needs continuous).
     * Checks CR.ENABLE only: the timer is enabled two counter clocks after
     * ENABLE=1 (RM0440 33.4.12) and start bits written earlier are lost, so
     * OK does not prove a start then. Call it after the recommended
     * sequence or at least two counter clocks after LPTIM_Enable. */
    /* SINGLE during a running one-shot cycle: OK, discarded by hardware (RM0440 33.4.7);
     * start the next cycle after ARRM (contract LP-5, v0.13.5). */
    /* Seviye: PARAM[S,r] STATE[r] */
    SPL_Status LPTIM_Start(LPTIM_TypeDef *lptim, uint32_t mode);
    /* ARR write (1..0xFFFF; 0 can never be > CMP). STATE: ENABLE=0. BUSY:
     * previous ARR write not acknowledged (ARROK=0). Clears ARROK via
     * ICR.ARROKCF immediately before the write, then sets arr_pending.
     * ERRATA ES0431 2.9.2 / ES0430 2.13.2 / ES0523 2.9.2: STATE (no write) while IER != 0;
     * program ARR/CMP with all LPTIM interrupts disabled. */
    /* Seviye: PARAM[S] STATE[r] BUSY[r] */
    SPL_Status LPTIM_SetAutoReload(LPTIM_TypeDef *lptim, LPTIM_SyncTypeDef *sync, uint32_t arr);
    /* CMP write (0..0xFFFF), same rules with CMPOK / ICR.CMPOKCF. The caller
     * keeps CMP < ARR for PWM / one-pulse / set-once (RM0440 33.4.9).
     * ERRATA ES0431 2.9.2 / ES0430 2.13.2 / ES0523 2.9.2: STATE while IER != 0 (as ARR).
     * ERRATA ES0431 2.9.3 / ES0430 2.13.3 / ES0523 2.9.3: CMPM, ARRM, PWM output and IRQs come
     * one kernel clock late; program CMP = wanted - 1; 0 % / 100 % duty cannot
     * be produced. */
    /* Seviye: PARAM[S] STATE[r] BUSY[r] */
    SPL_Status LPTIM_SetCompare(LPTIM_TypeDef *lptim, LPTIM_SyncTypeDef *sync, uint32_t cmp);
    /* Updates sync from ARROK/CMPOK (flags not cleared). OK when nothing is
     * pending, BUSY otherwise. */
    /* Seviye: PARAM[S] BUSY[r] */
    SPL_Status LPTIM_SyncStatus(LPTIM_TypeDef *lptim, LPTIM_SyncTypeDef *sync);
    /* RM0440 33.7.8 double read: up to three CNT reads; OK when two
     * consecutive reads match, BUSY otherwise (retry). STATE: RSTARE=1
     * (a read resets CNT, RM0440 33.4.13). */
    /* Seviye: PARAM[S] STATE[r] BUSY[r] */
    SPL_Status LPTIM_ReadCounter(LPTIM_TypeDef *lptim, uint32_t *count);
    /* *flags = ISR & LPTIM_FLAGS_ALL. No side effects. */
    /* Seviye: PARAM[S] */
    SPL_Status LPTIM_GetFlags(LPTIM_TypeDef *lptim, uint32_t *flags);
    /* ICR = flags (W1C), non-zero subset of LPTIM_FLAGS_CLEARABLE.
     * ERRATA ES0431 2.9.2 / ES0430 2.13.2 / ES0523 2.9.2: with IER != 0, STATE (no write) unless
     * every flag has its IER bit set, and call it from the LPTIM ISR only. */
    /* Seviye: PARAM[S] STATE[r] */
    SPL_Status LPTIM_ClearFlags(LPTIM_TypeDef *lptim, uint32_t flags);

#ifdef __cplusplus
}
#endif
#endif
