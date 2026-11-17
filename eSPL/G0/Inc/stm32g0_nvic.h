#ifndef STM32G0_NVIC_H
#define STM32G0_NVIC_H
#include <stdint.h>
#include "stm32g0xx.h"
#include "stm32g0_status.h"

/* NVIC and SysTick wrappers for Cortex-M0+ (ARMv6-M, PM0223; Docs/v0.2_NVIC_CONTRACT.md).
 * Range-checked calls of the CMSIS functions; no state.
 * G4 difference: ARMv6-M has no priority grouping (AIRCR.PRIGROUP), no BASEPRI and 2 priority bits:
 * one priority value 0..3 per interrupt (0 = most urgent). NVIC_SetGrouping / NVIC_GetGrouping do
 * not exist: G4 code that calls them fails to compile instead of silently doing nothing.
 *
 * Accepted numbers: device IRQs 0..NVIC_IRQ_MAX for every API; for the priority APIs also SVCall,
 * PendSV and SysTick. NMI, HardFault and any other number -> SPL_ERR_PARAM.
 * Context: Enable/Disable/SetPending/ClearPending use the write-1 registers and are safe from any
 * context. SetIrqPriority is a read-modify-write of a shared IPR word (four IRQs per word on
 * ARMv6-M): serialise priority changes (start-up code).
 * Caller duties (all APIs): a handler for every enabled IRQ (the startup default loops); the
 * handler clears the peripheral source flag; serialise priority changes (see above). */

#ifdef __cplusplus
extern "C"
{
#endif

/* Highest device IRQ of the selected SKU (v0.8; G0B1 30 = CEC, G0C1/G041/G061/G081 31 = AES_RNG,
 * G031 29, G030 28). Kept in step with Private/stm32g0_sku_masks.h by run_v08.sh. */
#if defined(STM32G030xx) || defined(STM32G050xx)
#define NVIC_IRQ_MAX 28
#elif defined(STM32G031xx) || defined(STM32G051xx) || defined(STM32G070xx) || defined(STM32G0B0xx)
#define NVIC_IRQ_MAX 29
#elif defined(STM32G071xx) || defined(STM32G0B1xx)
#define NVIC_IRQ_MAX 30
#else
#define NVIC_IRQ_MAX 31
#endif
#define NVIC_PRIORITY_MAX ((1UL << __NVIC_PRIO_BITS) - 1UL)

/* SysTick_CTRL.CLKSOURCE: 0 = HCLK / 8, 1 = HCLK (RM0444 5.2). */
typedef enum
{
    NVIC_SYSTICK_CLK_HCLK_DIV8 = 0,
    NVIC_SYSTICK_CLK_HCLK = 1
} NVIC_SysTickClockTypeDef;

/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status NVIC_SetIrqPriority(IRQn_Type irq, uint32_t priority);
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status NVIC_GetIrqPriority(IRQn_Type irq, uint32_t *priority);
/* Caller: set the priority and clear stale peripheral flags and the NVIC pending bit
 * (NVIC_ClearPending) before enabling. */
/* Level: PARAM[F] */
SPL_Status NVIC_Enable(IRQn_Type irq);
/* ICER write followed by DSB/ISB (CMSIS): the IRQ cannot be taken after the return. */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status NVIC_Disable(IRQn_Type irq);
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status NVIC_IsEnabled(IRQn_Type irq, uint8_t *enabled);
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status NVIC_SetPending(IRQn_Type irq);
/* Caller: clear the peripheral source flag first, else a level request pends again. */
/* Level: PARAM[F] */
SPL_Status NVIC_ClearPending(IRQn_Type irq);
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status NVIC_GetPending(IRQn_Type irq, uint8_t *pending);
/* LOAD = reload (1..0x00FFFFFF), VAL = 0, CTRL = CLKSOURCE | TICKINT | ENABLE. SysTick already
 * enabled -> STATE (owner may be an RTOS: stop it first). Priority is not touched. */
/* Caller: compute reload from the current HCLK and the selected divider (recompute after clock
 * changes); with interrupt = 1 set the SysTick priority and provide SysTick_Handler. */
/* Level: PARAM[F] STATE[F] */
SPL_Status NVIC_SysTickConfig(uint32_t reload, NVIC_SysTickClockTypeDef clock, uint8_t interrupt);
/* Clears ENABLE and TICKINT; LOAD, VAL and CLKSOURCE unchanged. */
/* Caller: none beyond the file-header duties. */
/* Level: - */
SPL_Status NVIC_SysTickStop(void);

#ifdef __cplusplus
}
#endif
#endif /* STM32G0_NVIC_H */
