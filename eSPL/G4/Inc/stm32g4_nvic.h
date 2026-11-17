/* SPDX-License-Identifier: MIT */
#ifndef STM32G4_NVIC_H
#define STM32G4_NVIC_H
#include <stdint.h>
#include "stm32g4xx.h"
#include "stm32g4_status.h"

/* NVIC and SysTick wrappers for Cortex-M4 (ARMv7-M, PM0214).
 * Scope: validated wrappers over the CMSIS core functions (NVIC_SetPriorityGrouping,
 * NVIC_SetPriority, NVIC_EnableIRQ, ...); the CMSIS names themselves are not redefined, these
 * wrappers add range checks and SPL_Status results.
 * Accepted interrupt numbers:
 *  - device IRQs 0..NVIC_IRQ_MAX (FMAC_IRQn on every supported SKU) for every API; numbers inside
 *    that range that are reserved on a SKU are RAZ/WI in hardware and are accepted (no per-SKU
 *    hole table);
 *  - priority APIs only: the configurable system exceptions MemoryManagement, BusFault,
 *    UsageFault, SVCall, DebugMonitor, PendSV, SysTick (SCB->SHPR).
 *  NMI/HardFault (fixed priority) and any other negative number -> SPL_ERR_PARAM.
 * Usage: NVIC_SetGrouping once at start-up, NVIC_SetIrqPriority, then NVIC_Enable; SysTick:
 * NVIC_SysTickConfig / NVIC_SysTickStop.
 * Common rules: no function waits; no hidden state. Read-modify-write is limited to SCB->AIRCR
 * (inside CMSIS NVIC_SetPriorityGrouping) and SysTick->CTRL. Enable/Disable/SetPending/
 * ClearPending use the W1S/W1C registers and are safe from any context.
 * Caller duties (all APIs):
 *  - Handlers: a handler for every enabled IRQ (the startup default loops); the handler clears
 *    the peripheral source flag.
 *  - Concurrency: call NVIC_SetGrouping and the SysTick APIs from one context (start-up code).
 *  - Core errata (ISR / startup code is the caller's; no code here):
 * ERRATA ES0431 2.1.1 / ES0430 2.1.1 / ES0523 2.1.1: hand-written asm must not load SP
 *   directly (LDR SP,...): load a GPR, then MOV SP. Library C code and CMSIS startup comply.
 * ERRATA ES0431 2.1.2 / ES0430 2.1.2 / ES0523 2.1.2: FPU on + lazy stacking: every ISR needs
 *   more than 2 instructions besides the return, or clear FPCCR.LSPEN (bit 30).
 * ERRATA ES0431 2.1.3 / ES0430 2.1.3 / ES0523 2.1.3: set ACTLR.DISDEFWBUF (no MPU), or a DSB
 *   before each ISR return, else a pending IRQ may be vectored twice.
 * Contract: Docs/v0.12_NVIC_CONTRACT.md. */

#ifdef __cplusplus
extern "C"
{
#endif

#define NVIC_IRQ_MAX FMAC_IRQn

    /* Number of preemption-priority bits (the rest of the __NVIC_PRIO_BITS = 4
     * implemented bits are subpriority). AIRCR.PRIGROUP = 7 - group. */
    typedef enum
    {
        NVIC_PRIOGROUP_0 = 0, /* 0 preemption bits, 4 subpriority bits */
        NVIC_PRIOGROUP_1 = 1, /* 1 preemption bit,  3 subpriority bits */
        NVIC_PRIOGROUP_2 = 2, /* 2 preemption bits, 2 subpriority bits */
        NVIC_PRIOGROUP_3 = 3, /* 3 preemption bits, 1 subpriority bit */
        NVIC_PRIOGROUP_4 = 4  /* 4 preemption bits, 0 subpriority bits */
    } NVIC_PriorityGroupTypeDef;

    /* SysTick_CTRL.CLKSOURCE: 0 = external reference = HCLK/8 on STM32G4
     * (RM0440 7.2 "The RCC feeds the Cortex System Timer (SysTick) external
     * clock with the AHB clock (HCLK) divided by 8"), 1 = processor clock HCLK. */
    typedef enum
    {
        NVIC_SYSTICK_CLK_HCLK_DIV8 = 0,
        NVIC_SYSTICK_CLK_HCLK = 1
    } NVIC_SysTickClockTypeDef;

    /* Writes AIRCR.PRIGROUP = 7 - group with VECTKEY 0x5FA (CMSIS
     * NVIC_SetPriorityGrouping). group > NVIC_PRIOGROUP_4 -> PARAM, no write.
     * Changing the grouping re-interprets every priority already programmed. */
    /* Caller: call once at start-up before any priority is set (existing priorities are
     * re-interpreted); AIRCR read-modify-write: one context only. */
    /* Level: PARAM[F] */
    SPL_Status NVIC_SetGrouping(NVIC_PriorityGroupTypeDef group);
    /* Reads AIRCR.PRIGROUP and reports the effective number of preemption bits
     * (PRIGROUP 0..3 all give NVIC_PRIOGROUP_4 with 4 priority bits). */
    /* Caller: none beyond the file-header duties. */
    /* Level: PARAM[F] */
    SPL_Status NVIC_GetGrouping(NVIC_PriorityGroupTypeDef *group);

    /* Encodes (preempt, sub) for the CURRENT grouping and writes IPR/SHPR via
     * CMSIS NVIC_SetPriority. preempt >= 2^preemptBits or sub >= 2^subBits ->
     * PARAM, no write. Lower value = higher urgency. */
    /* Caller: set the grouping first (the encoding uses the current grouping). */
    /* Level: PARAM[F] */
    SPL_Status NVIC_SetIrqPriority(IRQn_Type irq, uint32_t preempt, uint32_t sub);
    /* Decodes the programmed priority with the CURRENT grouping. */
    /* Caller: none beyond the file-header duties. */
    /* Level: PARAM[F] */
    SPL_Status NVIC_GetIrqPriority(IRQn_Type irq, uint32_t *preempt, uint32_t *sub);

    /* Device IRQs only (0..NVIC_IRQ_MAX). ISER/ICER/ISPR/ICPR single-bit writes. */
    /* Caller: set the priority and clear stale peripheral flags and the NVIC pending bit
     * (NVIC_ClearPending) before enabling. */
    /* Level: PARAM[F] */
    SPL_Status NVIC_Enable(IRQn_Type irq);
    /* ICER write followed by DSB/ISB (CMSIS NVIC_DisableIRQ): after return the
     * IRQ can no longer be taken. A pending bit stays pending. */
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

    /* Programs and starts SysTick: LOAD = reload, VAL = 0 (clears the counter
     * and COUNTFLAG), CTRL = CLKSOURCE | TICKINT(interrupt) | ENABLE. Period =
     * reload + 1 clock cycles. reload 1..0x00FFFFFF, interrupt 0/1.
     * SysTick already enabled -> STATE (call NVIC_SysTickStop first: the timer
     * may be owned by an RTOS). Does not wait for anything and does not touch
     * the SysTick exception priority (use NVIC_SetIrqPriority(SysTick_IRQn)).
     * Note: reading CTRL clears COUNTFLAG (PM0214 4.5.1). */
    /* Caller: compute reload from the current HCLK and the selected divider (recompute after clock
     * changes); with interrupt = 1 set the SysTick priority and provide SysTick_Handler. */
    /* Level: PARAM[F,r] STATE[r] */
    SPL_Status NVIC_SysTickConfig(uint32_t reload, NVIC_SysTickClockTypeDef clock,
                                  uint8_t interrupt);
    /* Clears CTRL.ENABLE and CTRL.TICKINT, keeps CLKSOURCE. LOAD/VAL unchanged.
     * A SysTick exception already pending stays pending (ICSR.PENDSTSET). */
    /* Caller: stop only a SysTick this code owns (an RTOS may use it); clear a pending SysTick
     * exception (ICSR.PENDSTCLR) if it must not run. */
    /* Level: - */
    SPL_Status NVIC_SysTickStop(void);

#ifdef __cplusplus
}
#endif
#endif
