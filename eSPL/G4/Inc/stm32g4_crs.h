#ifndef STM32G4_CRS_H
#define STM32G4_CRS_H
#include <stdint.h>
#include "stm32g4xx.h"
#include "stm32g4_status.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* Clock recovery system (CRS, RM0440 Rev 9 section 8): HSI48 trimming
 * against a SYNC signal (USB SOF, LSE or the CRS_SYNC pin). Single instance,
 * no static state, no waits.
 * Caller duties (RCC / GPIO modules, not done here):
 *  - APB1 clock: RCC_EnablePeriphClock(RCC_PERIPH_CRS) (RCC_APB1ENR1.CRSEN).
 *  - HSI48 started and ready (RCC_StartOsc(RCC_OSC_HSI48)).
 *  - SYNC source running: USB SOF (USB enabled and enumerated), LSE on, or
 *    the CRS_SYNC alternate-function pin configured.
 * CR writes never copy SWSYNC back (self-clearing command, RM0440 8.7.1).
 * Serialize callers (task vs ISR): CR updates are read-modify-write.
 * Contract: Docs/v0.13_CRS_CONTRACT.md. */

/* SYNCSRC values (RM0440 Table 54 + CFGR reset value 0x2022 BB7F). */
#define CRS_SYNCSRC_GPIO 0U    /* crs_sync_in_0: CRS_SYNC pin */
#define CRS_SYNCSRC_LSE 1U     /* crs_sync_in_1: LSE */
#define CRS_SYNCSRC_USB_SOF 2U /* crs_sync_in_2: USB SOF (reset value) */

/* Reset-value configuration: 48 MHz target, 1 kHz SOF (RM0440 8.4.6). */
#define CRS_RELOAD_DEFAULT 47999U
#define CRS_FELIM_DEFAULT 34U
#define CRS_TRIM_DEFAULT 64U
#define CRS_TRIM_MAX 127U

/* CR interrupt-enable bits accepted by CRS_InterruptConfig. */
#define CRS_IT_ALL (CRS_CR_SYNCOKIE | CRS_CR_SYNCWARNIE | CRS_CR_ERRIE | CRS_CR_ESYNCIE)
/* ICR clear bits accepted by CRS_ClearFlags. ERRC clears SYNCERR, SYNCMISS,
 * TRIMOVF and with them ERRF (RM0440 8.7.4). */
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

    /* RELOAD = (target_hz / sync_hz) - 1 (RM0440 8.4.6, integer division).
     * sync_hz is the SYNC frequency AFTER the SYNCDIV divider. PARAM: NULL,
     * sync_hz = 0, target_hz < sync_hz, result > 0xFFFF. No register access. */
    /* Seviye: PARAM[S,r] */
    SPL_Status CRS_CalcReload(uint32_t target_hz, uint32_t sync_hz, uint32_t *reload);
    /* FELIM = (target_hz / sync_hz) * STEP[%] / 100 % / 2, rounded up
     * (RM0440 8.4.6). step_centi_pct = HSI48 trim step in 0.01 % units from
     * the datasheet (14 = 0.14 %, the step the reset value assumes). PARAM:
     * NULL, sync_hz = 0, target_hz < sync_hz, step 0 or > 10000, result > 255.
     * No register access. */
    /* Seviye: PARAM[S,r] */
    SPL_Status CRS_CalcErrorLimit(uint32_t target_hz, uint32_t sync_hz, uint32_t step_centi_pct,
                                  uint32_t *error_limit);
    /* Writes the whole CFGR (reserved bits 0) and CR.TRIM / CR.AUTOTRIMEN;
     * CR interrupt enables kept. STATE: CEN=1 (CFGR write-protected, RM0440
     * 8.7.2). UNSUPPORTED: USB SOF source on an SKU without USB. PARAM: NULL,
     * field out of range, reload <= 128 * error_limit (RM0440 8.4.6). */
    /* Seviye: PARAM[S] STATE[r] UNSUPPORTED[r] */
    SPL_Status CRS_Config(const CRS_InitTypeDef *init);
    /* CEN=1 / CEN=0 (frequency error counter). Other CR bits kept. */
    /* Seviye: - */
    SPL_Status CRS_Enable(void);
    /* Seviye: - */
    SPL_Status CRS_Disable(void);
    /* Sets SWSYNC once (software SYNC event). STATE: CEN=0. BUSY: the
     * previous SWSYNC has not self-cleared yet. */
    /* Seviye: STATE[r] BUSY[r] */
    SPL_Status CRS_GenerateSwSync(void);
    /* enable=1: AUTOTRIMEN=1 (TRIM read-only, hardware-controlled);
     * enable=0: manual trimming. Allowed with CEN=0 or 1. */
    /* Seviye: PARAM[S] */
    SPL_Status CRS_SetAutoTrim(uint8_t enable);
    /* CR.TRIM = trim (0..127). STATE: AUTOTRIMEN=1 (TRIM read-only, RM0440
     * 8.7.1). */
    /* Seviye: PARAM[S] STATE[r] */
    SPL_Status CRS_SetTrim(uint32_t trim);
    /* Seviye: PARAM[S] */
    SPL_Status CRS_GetTrim(uint32_t *trim);
    /* Sets (enable=1) or clears (enable=0) the CR bits in mask (non-zero
     * subset of CRS_IT_ALL). One CRS interrupt line (CRS_IRQn). */
    /* Seviye: PARAM[S] */
    SPL_Status CRS_InterruptConfig(uint32_t mask, uint8_t enable);
    /* Writes ICR = flags (non-zero subset of CRS_CLEAR_ALL). W1C: one plain
     * write, no read-modify-write. */
    /* Seviye: PARAM[S] */
    SPL_Status CRS_ClearFlags(uint32_t flags);
    /* One ISR read (+ CR for trim). OK, or ERR_HW when a CRS_ERRORS_ALL bit
     * is set (status filled in both cases; clear with CRS_ICR_ERRC). */
    /* Seviye: PARAM[S] HW[r] */
    SPL_Status CRS_GetStatus(CRS_StatusTypeDef *status);
    /* Poll for a SYNC result. OK: SYNCOKF=1 and no error; ERR_HW: error bit
     * set; WOULD_BLOCK: CEN=1 and no SYNCOKF yet (SYNCWARNF alone = still
     * converging); STATE: CEN=0 (counter stopped, no SYNC will be evaluated).
     * Flags are sticky: clear SYNCOKC/SYNCWARNC first for a fresh result.
     * *status filled on every return except PARAM. Bound the poll: a missing
     * external SYNC is not visible in any register. */
    /* Seviye: PARAM[S] STATE[r] HW[r] WOULD_BLOCK[r] */
    SPL_Status CRS_TrySyncOk(CRS_StatusTypeDef *status);

#ifdef __cplusplus
}
#endif
#endif
