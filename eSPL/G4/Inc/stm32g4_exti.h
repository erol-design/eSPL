#ifndef STM32G4_EXTI_H
#define STM32G4_EXTI_H
#include <stdint.h>
#include "stm32g4xx.h"
#include "stm32g4_status.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* EXTI lines 0..42 (RM0440 section 15). Line masks follow the CMSIS per-SKU bit
 * macros; see Docs/v0.10_EXTI_CONTRACT.md. line > 42 -> SPL_ERR_PARAM, line not
 * present on this SKU -> SPL_ERR_UNSUPPORTED. Rejected calls write nothing.
 * Caller duties: SYSCFG clock (RCC APB2ENR SYSCFGEN) and NVIC are not touched.
 * Configuration APIs use read-modify-write: serialise them. */
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
     * Caller duty (CCL-2 GPIO-05): one port per line; a later call for the same line from
     * another port silently replaces the earlier selection, so keep line numbers distinct. */
    /* Level: PARAM[F] STATE[r] */
    SPL_Status EXTI_SetGpioSource(GPIO_TypeDef *port, uint8_t line);
    /* Triggers (configurable lines only; direct lines need EXTI_TRIGGER_NONE), then
     * IMR/EMR from 'mode' (non-zero EXTI_MODE_* mask). Pending bit is not cleared.
     * Caller duty (CCL-2 GPIO-06): with EXTI_TRIGGER_BOTH the handler (caller code) must read
     * the pin level to tell rising from falling edge, unless its work is edge-independent. */
    /* Level: PARAM[F,r] UNSUPPORTED[r] */
    SPL_Status EXTI_LineConfig(uint8_t line, EXTI_TriggerTypeDef trigger, uint32_t mode);
    /* Clears IMR/EMR (and RTSR/FTSR on configurable lines). Pending bit untouched. */
    /* Level: PARAM[r] UNSUPPORTED[r] */
    SPL_Status EXTI_LineDisable(uint8_t line);
    /* Configurable lines only: writes only this line's SWIER bit. */
    /* Level: PARAM[r] UNSUPPORTED[r] */
    SPL_Status EXTI_SoftwareTrigger(uint8_t line);
    /* Configurable lines only. *pending = 1 when the PR bit is set. */
    /* Level: PARAM[F,r] UNSUPPORTED[r] */
    SPL_Status EXTI_GetPending(uint8_t line, uint8_t *pending);
    /* Configurable lines only. PR is W1C: writes only this line's bit. ISR-safe. */
    /* Level: PARAM[r] UNSUPPORTED[r] */
    SPL_Status EXTI_ClearPending(uint8_t line);

#ifdef __cplusplus
}
#endif
#endif
