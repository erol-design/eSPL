/* SPDX-License-Identifier: MIT */
#ifndef STM32G0_CRS_H
#define STM32G0_CRS_H
#include <stdint.h>
#include "stm32g0xx.h"
#include "stm32g0_status.h"
#include "stm32g0_sku.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* v0.8 SKU profile: this API exists only where the device has the peripheral (SPL_G0_HAS_CRS);
 * absent on every SKU except G0B1 / G0C1. */
#if SPL_G0_HAS_CRS

/* Clock recovery system (CRS, RM0444 Rev 6 section 6): HSI48 trimming
 * against a SYNC signal (USB SOF, LSE or the CRS_SYNC pin).
 * Usage: CRS_CalcReload / CRS_CalcErrorLimit, CRS_Config, CRS_ClearFlags, CRS_Enable, then poll
 * CRS_TrySyncOk (or use the interrupt).
 * Single instance, no static state, no waits. Output parameters are written only on OK unless
 * the function says otherwise (CRS_GetStatus, CRS_TrySyncOk).
 * Caller duties (all APIs; RCC / GPIO modules, not done here):
 *  - APB1 clock: RCC_EnablePeriphClock(RCC_PERIPH_CRS) (RCC_APBENR1.CRSEN).
 *  - HSI48 started and ready (RCC_StartOsc(RCC_OSC_HSI48)).
 *  - SYNC source running: USB SOF (USB enabled and enumerated), LSE on, or
 *    the CRS_SYNC alternate-function pin configured.
 *  - Interrupts: RCC_CRS_IRQn (shared with RCC) in the NVIC.
 * CR writes never copy SWSYNC back (self-clearing command, RM0444 6.7.1).
 * Serialize callers (task vs ISR): CR updates are read-modify-write.
 * Contract: Docs/v0.3_CRS_CONTRACT.md. */

/* SYNCSRC values (RM0444 Table 40 + CFGR reset value 0x2022 BB7F). */
#define CRS_SYNCSRC_GPIO 0U    /* crs_sync_in_0: CRS_SYNC pin */
#define CRS_SYNCSRC_LSE 1U     /* crs_sync_in_1: LSE */
#define CRS_SYNCSRC_USB_SOF 2U /* crs_sync_in_2: USB SOF (reset value) */

/* Reset-value configuration: 48 MHz target, 1 kHz SOF (RM0444 6.4.6). */
#define CRS_RELOAD_DEFAULT 47999U
#define CRS_FELIM_DEFAULT 34U
#define CRS_TRIM_DEFAULT 64U
#define CRS_TRIM_MAX 127U

/* CR interrupt-enable bits accepted by CRS_InterruptConfig. */
#define CRS_IT_ALL (CRS_CR_SYNCOKIE | CRS_CR_SYNCWARNIE | CRS_CR_ERRIE | CRS_CR_ESYNCIE)
/* ICR clear bits accepted by CRS_ClearFlags. ERRC clears SYNCERR, SYNCMISS,
 * TRIMOVF and with them ERRF (RM0444 6.7.4). */
#define CRS_CLEAR_ALL (CRS_ICR_SYNCOKC | CRS_ICR_SYNCWARNC | CRS_ICR_ERRC | CRS_ICR_ESYNCC)
/* ISR error bits: CRS_GetStatus / CRS_TrySyncOk return ERR_HW if any is set. */
#define CRS_ERRORS_ALL (CRS_ISR_ERRF | CRS_ISR_SYNCERR | CRS_ISR_SYNCMISS | CRS_ISR_TRIMOVF)
/* ISR flag bits copied into CRS_StatusTypeDef.flags. */
#define CRS_FLAGS_ALL (CRS_ISR_SYNCOKF | CRS_ISR_SYNCWARNF | CRS_ISR_ESYNCF | CRS_ERRORS_ALL)

typedef struct
{
    uint32_t sync_source;   /* CRS_SYNCSRC_x (SYNCSRC). */
    uint32_t sync_divider;  /* 0..7 (SYNCDIV): /1, /2, .. /128. */
    uint32_t sync_polarity; /* 0: rising edge, 1: falling edge (SYNCPOL). */
    uint32_t reload;        /* 0..0xFFFF (RELOAD), > 128 * error_limit. */
    uint32_t error_limit;   /* 1..255 (FELIM). */
    uint32_t trim;          /* 0..127 initial TRIM. */
    uint32_t auto_trim;     /* 0/1 (AUTOTRIMEN). */
} CRS_InitTypeDef;

typedef struct
{
    uint32_t flags; /* CRS_ISR & CRS_FLAGS_ALL. */
    uint32_t fecap; /* FECAP: counter value at the last SYNC. */
    uint32_t fedir; /* FEDIR: 1 = HSI48 below target, 0 = above. */
    uint32_t trim;  /* CR.TRIM at the time of the call. */
} CRS_StatusTypeDef;

/* RELOAD = (target_hz / sync_hz) - 1 (RM0444 6.4.6, integer division).
 * sync_hz is the SYNC frequency AFTER the SYNCDIV divider. PARAM: NULL,
 * sync_hz = 0, target_hz < sync_hz, result > 0xFFFF. No register access. */
/* Caller: sync_hz is the SYNC frequency after SYNCDIV. */
/* Level: PARAM[F,r] */
SPL_Status CRS_CalcReload(uint32_t target_hz, uint32_t sync_hz, uint32_t *reload);
/* FELIM = (target_hz / sync_hz) * STEP[%] / 100 % / 2, rounded up
 * (RM0444 6.4.6). step_centi_pct = HSI48 trim step in 0.01 % units from
 * the datasheet (14 = 0.14 %, the step the reset value assumes). PARAM:
 * NULL, sync_hz = 0, target_hz < sync_hz, step 0 or > 10000, result > 255.
 * No register access. */
/* Caller: step_centi_pct from the device datasheet (HSI48 trim step). */
/* Level: PARAM[F,r] */
SPL_Status CRS_CalcErrorLimit(uint32_t target_hz, uint32_t sync_hz, uint32_t step_centi_pct,
                              uint32_t *error_limit);
/* Writes the whole CFGR (reserved bits 0) and CR.TRIM / CR.AUTOTRIMEN;
 * CR interrupt enables kept. STATE: CEN=1 (CFGR write-protected, RM0444
 * 6.7.2). UNSUPPORTED: USB SOF source on an SKU without USB. PARAM: NULL,
 * field out of range, reload <= 128 * error_limit (RM0444 6.4.6). */
/* Caller: reload / error_limit for the actual SYNC frequency (CRS_CalcReload,
 * CRS_CalcErrorLimit); then CRS_Enable. */
/* Level: PARAM[F] STATE[F] UNSUPPORTED[r] */
SPL_Status CRS_Config(const CRS_InitTypeDef *init);
/* CEN=1 / CEN=0 (frequency error counter). Other CR bits kept. */
/* Caller: clear stale flags (CRS_ClearFlags) before relying on a SYNC result. */
/* Level: - */
SPL_Status CRS_Enable(void);
/* Caller: none beyond the file-header duties. */
/* Level: - */
SPL_Status CRS_Disable(void);
/* Sets SWSYNC once (software SYNC event). STATE: CEN=0. BUSY: the
 * previous SWSYNC has not self-cleared yet. */
/* Caller: none beyond the file-header duties. */
/* Level: STATE[F] BUSY[r] */
SPL_Status CRS_GenerateSwSync(void);
/* enable=1: AUTOTRIMEN=1 (TRIM read-only, hardware-controlled);
 * enable=0: manual trimming. Allowed with CEN=0 or 1. */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status CRS_SetAutoTrim(uint8_t enable);
/* CR.TRIM = trim (0..127). STATE: AUTOTRIMEN=1 (TRIM read-only, RM0444
 * 6.7.1). */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] STATE[F] */
SPL_Status CRS_SetTrim(uint32_t trim);
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status CRS_GetTrim(uint32_t *trim);
/* Sets (enable=1) or clears (enable=0) the CR bits in mask (non-zero
 * subset of CRS_IT_ALL). One CRS interrupt line (RCC_CRS_IRQn, shared with RCC). */
/* Caller: clear stale flags first; RCC_CRS_IRQn handler (shared with RCC) clears the
 * flags with CRS_ClearFlags; NVIC enable. */
/* Level: PARAM[F] */
SPL_Status CRS_InterruptConfig(uint32_t mask, uint8_t enable);
/* Writes ICR = flags (non-zero subset of CRS_CLEAR_ALL). W1C: one plain
 * write, no read-modify-write. */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status CRS_ClearFlags(uint32_t flags);
/* One ISR read (+ CR for trim). OK, or ERR_HW when a CRS_ERRORS_ALL bit
 * is set (status filled in both cases; clear with CRS_ICR_ERRC). */
/* Caller: on SPL_ERR_HW handle the cause, then clear it with CRS_ClearFlags(CRS_ICR_ERRC). */
/* Level: PARAM[F] HW[r] */
SPL_Status CRS_GetStatus(CRS_StatusTypeDef *status);
/* Poll for a SYNC result. OK: SYNCOKF=1 and no error; ERR_HW: error bit
 * set; WOULD_BLOCK: CEN=1 and no SYNCOKF yet (SYNCWARNF alone = still
 * converging); STATE: CEN=0 (counter stopped, no SYNC will be evaluated).
 * Flags are sticky: clear SYNCOKC/SYNCWARNC first for a fresh result.
 * *status filled on every return except PARAM. Bound the poll: a missing
 * external SYNC is not visible in any register. */
/* Caller: clear SYNCOKC / SYNCWARNC first; bound the poll. */
/* Level: PARAM[F] STATE[F] HW[r] WOULD_BLOCK[r] */
SPL_Status CRS_TrySyncOk(CRS_StatusTypeDef *status);

#endif /* SPL_G0_HAS_CRS */

#ifdef __cplusplus
}
#endif
#endif
