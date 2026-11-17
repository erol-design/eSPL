/* SPDX-License-Identifier: MIT */
#ifndef STM32G0_EXTI_H
#define STM32G0_EXTI_H
#include <stdint.h>
#include "stm32g0xx.h"
#include "stm32g0_status.h"

/* Extended interrupt and event controller (RM0444 Rev 6 section 13, Table 65;
 * Docs/v0.4_EXTI_CONTRACT.md). Written for G0 (the G4 EXTI model does not apply):
 *  - GPIO routing (EXTICR) is inside EXTI, not SYSCFG; the EXTI has no bus clock enable;
 *  - configurable lines have separate rising (RPR) and falling (FPR) pending registers;
 *  - lines 0..15 GPIO, 16 PVD, 17 COMP1, 18 COMP2, 20 COMP3 and 34 VDDIO2 are configurable, the
 *    others (19, 21..33, 35, 36) are direct (no trigger, no software trigger, no pending bit here).
 * HW-001: the CMSIS layout of bank 2 (RTSR2..FPR2) disagrees with RM0444 and the SVD: line 34
 * (the only configurable bank-2 line) -> SPL_ERR_UNSUPPORTED in every API until G6 settles it.
 * v0.8 SKU profiles: a line 0..36 that the selected SKU does not have (IMR1/IMR2 bit not defined
 * by its CMSIS header; e.g. bank 2 on G03x/G05x, only line 36 on G0B0) -> SPL_ERR_UNSUPPORTED;
 * the configurable set is the SKU's RTSR1 bits (G031: 0..16).
 * line > 36 -> SPL_ERR_PARAM. Rejected calls write nothing. Configuration APIs use
 * read-modify-write: serialise them. NVIC is not touched.
 * Reset state note: IMR1 resets to 0xFFF8 0000 (lines 19..31 unmasked, COMP3 line 20 included;
 * RM0444 13.5.12).
 * Caller duties (all APIs): serialise the configuration calls (read-modify-write); NVIC enable,
 * priority and the handler are the caller's (the handler calls EXTI_ClearPending); lines 0..15
 * need the pin configured as input (GPIO_Init). */

#ifdef __cplusplus
extern "C"
{
#endif

#define EXTI_LINE_COUNT 37U

#define EXTI_MODE_INTERRUPT 0x1U
#define EXTI_MODE_EVENT 0x2U

typedef enum
{
    EXTI_TRIGGER_NONE = 0,
    EXTI_TRIGGER_RISING = 1,
    EXTI_TRIGGER_FALLING = 2,
    EXTI_TRIGGER_BOTH = 3
} EXTI_TriggerTypeDef;

/* EXTI_EXTICRx: route pin 'line' (0..15) of port GPIOA..GPIOF to EXTI line 'line'. */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F,r] */
SPL_Status EXTI_SetGpioSource(GPIO_TypeDef *port, uint8_t line);
/* Configurable lines: RTSR/FTSR from trigger, then IMR/EMR from mode (non-zero EXTI_MODE_* mask).
 * Direct lines: trigger must be EXTI_TRIGGER_NONE (else PARAM); only IMR/EMR. Pending untouched. */
/* Caller: select the source first (EXTI_SetGpioSource, PVD, COMP) and clear a stale pending bit
 * (EXTI_ClearPending) before unmasking. */
/* Level: PARAM[F] UNSUPPORTED[r] */
SPL_Status EXTI_LineConfig(uint8_t line, EXTI_TriggerTypeDef trigger, uint32_t mode);
/* Clears IMR/EMR (and RTSR/FTSR on configurable lines). Pending bits untouched. */
/* Caller: clear the pending bits afterwards when an edge may have been latched. */
/* Level: PARAM[F] UNSUPPORTED[r] */
SPL_Status EXTI_LineDisable(uint8_t line);
/* Configurable lines only: writes only this line's SWIER bit (a rising-edge event, RPR set). */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] UNSUPPORTED[r] */
SPL_Status EXTI_SoftwareTrigger(uint8_t line);
/* Configurable lines only. *pending = 1 when the rising or the falling pending bit is set
 * (G4-compatible signature). */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] UNSUPPORTED[r] */
SPL_Status EXTI_GetPending(uint8_t line, uint8_t *pending);
/* G0 only: rising (RPR) and falling (FPR) pending bits separately. */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] UNSUPPORTED[r] */
SPL_Status EXTI_GetPendingEdges(uint8_t line, uint8_t *rising, uint8_t *falling);
/* Configurable lines only. RPR and FPR are write-1-to-clear: this line's bit is written to both,
 * nothing else. ISR-safe. */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] UNSUPPORTED[r] */
SPL_Status EXTI_ClearPending(uint8_t line);

#ifdef __cplusplus
}
#endif
#endif /* STM32G0_EXTI_H */
