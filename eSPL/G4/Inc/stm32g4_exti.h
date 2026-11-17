/* SPDX-License-Identifier: MIT */
#ifndef STM32G4_EXTI_H
#define STM32G4_EXTI_H
#include <stdint.h>
#include "stm32g4xx.h"
#include "stm32g4_status.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* Extended interrupt and event controller, lines 0..42 (RM0440 section 15), plus the GPIO
 * routing in SYSCFG_EXTICR. Line masks follow the CMSIS per-SKU bit macros (contract 1.1):
 * line > 42 -> SPL_ERR_PARAM, line not present on this SKU -> SPL_ERR_UNSUPPORTED, an operation
 * that needs a configurable line on a direct line -> SPL_ERR_PARAM. Rejected calls write nothing.
 * No function waits, the module keeps no static state, RCC and NVIC are not touched.
 * Usage: GPIO_Init (input), EXTI_SetGpioSource for lines 0..15, EXTI_ClearPending,
 * EXTI_LineConfig; in the handler EXTI_GetPending / EXTI_ClearPending.
 * Concurrency: configuration APIs (SetGpioSource, LineConfig, LineDisable) use read-modify-write
 * on registers shared by all lines; SoftwareTrigger, GetPending and ClearPending are single
 * register accesses and are ISR-safe.
 * Caller duties (all APIs): SYSCFG clock on (RCC APB2ENR SYSCFGEN) before EXTI_SetGpioSource;
 * pins of lines 0..15 configured as input (GPIO_Init); NVIC enable, priority and handler (the
 * handler clears the pending bit it serves); serialise the configuration calls against each
 * other and against ISRs that call them; clear all EXTI_PR1 bits before Stop-mode entry
 * (RM0440 6.3.6).
 * Contract: Docs/v0.10_EXTI_CONTRACT.md. */
#define EXTI_LINE_COUNT 43U

#define EXTI_MODE_INTERRUPT 0x1U
#define EXTI_MODE_EVENT 0x2U

    typedef enum
    {
        EXTI_TRIGGER_NONE = 0,
        EXTI_TRIGGER_RISING = 1,
        EXTI_TRIGGER_FALLING = 2,
        EXTI_TRIGGER_BOTH = 3
    } EXTI_TriggerTypeDef;

    /* SYSCFG_EXTICR: route GPIO port pin 'line' (0..15) to EXTI line 'line'.
     * GPIOG only for lines 0..10 (RM0440 10.2.3-10.2.6). Read-back mismatch
     * (SYSCFG clock off) -> SPL_ERR_STATE.
     * Caller: SYSCFG clock on (file header).
     * Caller duty (CCL-2 GPIO-05): one port per line; a later call for the same line from
     * another port silently replaces the earlier selection, so keep line numbers distinct. */
    /* Level: PARAM[F] STATE[r] */
    SPL_Status EXTI_SetGpioSource(GPIO_TypeDef *port, uint8_t line);
    /* Triggers (configurable lines only; direct lines need EXTI_TRIGGER_NONE), then
     * IMR/EMR from 'mode' (non-zero EXTI_MODE_* mask). Pending bit is not cleared.
     * Caller: select the source first (EXTI_SetGpioSource, PVD, COMP, ...) and clear a stale
     * pending bit (EXTI_ClearPending) before unmasking, else the IRQ fires at once.
     * Caller duty (CCL-2 GPIO-06): with EXTI_TRIGGER_BOTH the handler (caller code) must read
     * the pin level to tell rising from falling edge, unless its work is edge-independent. */
    /* Level: PARAM[F,r] UNSUPPORTED[r] */
    SPL_Status EXTI_LineConfig(uint8_t line, EXTI_TriggerTypeDef trigger, uint32_t mode);
    /* Clears IMR/EMR (and RTSR/FTSR on configurable lines). Pending bit untouched. On a direct
     * line this masks the wake-up interrupt that reset enables.
     * Caller: clear the pending bit afterwards when an edge may have been latched. */
    /* Level: PARAM[r] UNSUPPORTED[r] */
    SPL_Status EXTI_LineDisable(uint8_t line);
    /* Configurable lines only: writes only this line's SWIER bit; the pending bit is set only
     * while the line is unmasked (IMR), SWI clears when PR is cleared. ISR-safe.
     * Caller: none beyond the file-header duties. */
    /* Level: PARAM[r] UNSUPPORTED[r] */
    SPL_Status EXTI_SoftwareTrigger(uint8_t line);
    /* Configurable lines only. *pending = 1 when the PR bit is set. ISR-safe.
     * Caller: none beyond the file-header duties. */
    /* Level: PARAM[F,r] UNSUPPORTED[r] */
    SPL_Status EXTI_GetPending(uint8_t line, uint8_t *pending);
    /* Configurable lines only. PR is W1C: writes only this line's bit. ISR-safe.
     * Caller: none beyond the file-header duties. */
    /* Level: PARAM[r] UNSUPPORTED[r] */
    SPL_Status EXTI_ClearPending(uint8_t line);

#ifdef __cplusplus
}
#endif
#endif
