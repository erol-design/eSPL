#define SPL_LIBRARY_BUILD 1
#include <stddef.h>
#include "stm32g4_nvic.h"

/* Contract: Docs/v0.12_NVIC_CONTRACT.md. Register access goes through the CMSIS
 * core functions only (field names differ between CMSIS 4/5/6: SHP/SHPR,
 * IP/IPR), except SysTick CTRL/LOAD/VAL whose names are stable. */

/* AIRCR.PRIGROUP p gives min(7 - p, __NVIC_PRIO_BITS) preemption bits (ARMv7-M,
 * CMSIS NVIC_EncodePriority). PRIGROUP = 7 - group for group <= PRIO_BITS. */
#define NVIC_PRIGROUP_TOP 7U

/* Compile-time check (CCL-2 YAP-03): every device IRQ 0..NVIC_IRQ_MAX indexes the CMSIS NVIC
 * enable words (ISER, same name in CMSIS 4/5/6: size taken with sizeof) and the priority byte
 * array, whose name differs between CMSIS versions (IP / IPR) and whose length is the ARMv7-M
 * architectural maximum of 240 external interrupts. */
#define NVIC_ARMV7M_EXT_IRQ_MAX 240U
#define NVIC_ISER_WORDS ((uint32_t)(sizeof(((NVIC_Type *)0)->ISER) / sizeof(uint32_t)))
typedef char Nvic_IrqRangeCheck[(((uint32_t)NVIC_IRQ_MAX < (32U * NVIC_ISER_WORDS)) &&
                                 ((uint32_t)NVIC_IRQ_MAX < NVIC_ARMV7M_EXT_IRQ_MAX))
                                    ? 1
                                    : -1];

static uint32_t Nvic_IsDeviceIrq(IRQn_Type irq)
{
    int32_t n = (int32_t)irq;
    return ((n >= 0) && (n <= (int32_t)NVIC_IRQ_MAX)) ? 1U : 0U;
}

/* Exceptions whose priority lives in SCB->SHPR (configurable). */
static uint32_t Nvic_IsPriorityIrq(IRQn_Type irq)
{
    uint32_t ok;

    switch (irq)
    {
    case MemoryManagement_IRQn:
    case BusFault_IRQn:
    case UsageFault_IRQn:
    case SVCall_IRQn:
    case DebugMonitor_IRQn:
    case PendSV_IRQn:
    case SysTick_IRQn:
        ok = 1U;
        break;
    default:
        ok = Nvic_IsDeviceIrq(irq);
        break;
    }
    return ok;
}

static uint32_t Nvic_PreemptBits(void)
{
    uint32_t prigroup = NVIC_GetPriorityGrouping();
    uint32_t bits = NVIC_PRIGROUP_TOP - prigroup;

    if (bits > (uint32_t)__NVIC_PRIO_BITS)
    {
        bits = (uint32_t)__NVIC_PRIO_BITS;
    }
    return bits;
}

SPL_Status NVIC_SetGrouping(NVIC_PriorityGroupTypeDef group)
{
    uint32_t g = (uint32_t)group;

    /* SAFE: invalid priority grouping underflows the AIRCR PRIGROUP encoding. */
    SPL_REQUIRE_SAFE(g <= (uint32_t)__NVIC_PRIO_BITS, SPL_ERR_PARAM);
    NVIC_SetPriorityGrouping(NVIC_PRIGROUP_TOP - g);
    return SPL_OK;
}

SPL_Status NVIC_GetGrouping(NVIC_PriorityGroupTypeDef *group)
{
    /* SAFE: an invalid output pointer may read/write an unrelated memory region. */
    SPL_REQUIRE_SAFE(group != NULL, SPL_ERR_PARAM);
    switch (Nvic_PreemptBits())
    {
    case 0U:
        *group = NVIC_PRIOGROUP_0;
        break;
    case 1U:
        *group = NVIC_PRIOGROUP_1;
        break;
    case 2U:
        *group = NVIC_PRIOGROUP_2;
        break;
    case 3U:
        *group = NVIC_PRIOGROUP_3;
        break;
    default:
        *group = NVIC_PRIOGROUP_4;
        break;
    }
    return SPL_OK;
}

SPL_Status NVIC_SetIrqPriority(IRQn_Type irq, uint32_t preempt, uint32_t sub)
{
    uint32_t preemptBits;
    uint32_t subBits;

    /* SAFE: invalid exception numbers can address unrelated NVIC/SCB registers. */
    SPL_REQUIRE_SAFE(Nvic_IsPriorityIrq(irq) != 0U, SPL_ERR_PARAM);
    preemptBits = Nvic_PreemptBits();
    subBits = (uint32_t)__NVIC_PRIO_BITS - preemptBits;
    /* SAFE: overflow silently truncates the encoded interrupt priority. */
    SPL_REQUIRE_SAFE(preempt < (1U << preemptBits), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(sub < (1U << subBits), SPL_ERR_PARAM);
    NVIC_SetPriority(irq, (preempt << subBits) | sub);
    return SPL_OK;
}

SPL_Status NVIC_GetIrqPriority(IRQn_Type irq, uint32_t *preempt, uint32_t *sub)
{
    uint32_t subBits;
    uint32_t priority;

    SPL_REQUIRE_SAFE(Nvic_IsPriorityIrq(irq) != 0U, SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(preempt != NULL, SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(sub != NULL, SPL_ERR_PARAM);
    subBits = (uint32_t)__NVIC_PRIO_BITS - Nvic_PreemptBits();
    priority = NVIC_GetPriority(irq);
    *preempt = priority >> subBits;
    *sub = priority & ((1U << subBits) - 1U);
    return SPL_OK;
}

SPL_Status NVIC_Enable(IRQn_Type irq)
{
    /* SAFE: IRQ outside the architectural vector table may corrupt SCS memory. */
    SPL_REQUIRE_SAFE(Nvic_IsDeviceIrq(irq) != 0U, SPL_ERR_PARAM);
    NVIC_EnableIRQ(irq);
    return SPL_OK;
}

SPL_Status NVIC_Disable(IRQn_Type irq)
{
    /* SAFE: IRQ outside the architectural vector table may corrupt SCS memory. */
    SPL_REQUIRE_SAFE(Nvic_IsDeviceIrq(irq) != 0U, SPL_ERR_PARAM);
    NVIC_DisableIRQ(irq);
    return SPL_OK;
}

SPL_Status NVIC_IsEnabled(IRQn_Type irq, uint8_t *enabled)
{
    SPL_REQUIRE_SAFE(Nvic_IsDeviceIrq(irq) != 0U, SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(enabled != NULL, SPL_ERR_PARAM);
    *enabled = (NVIC_GetEnableIRQ(irq) != 0U) ? 1U : 0U;
    return SPL_OK;
}

SPL_Status NVIC_SetPending(IRQn_Type irq)
{
    /* SAFE: IRQ outside the architectural vector table may corrupt SCS memory. */
    SPL_REQUIRE_SAFE(Nvic_IsDeviceIrq(irq) != 0U, SPL_ERR_PARAM);
    NVIC_SetPendingIRQ(irq);
    return SPL_OK;
}

SPL_Status NVIC_ClearPending(IRQn_Type irq)
{
    /* SAFE: IRQ outside the architectural vector table may corrupt SCS memory. */
    SPL_REQUIRE_SAFE(Nvic_IsDeviceIrq(irq) != 0U, SPL_ERR_PARAM);
    NVIC_ClearPendingIRQ(irq);
    return SPL_OK;
}

SPL_Status NVIC_GetPending(IRQn_Type irq, uint8_t *pending)
{
    SPL_REQUIRE_SAFE(Nvic_IsDeviceIrq(irq) != 0U, SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(pending != NULL, SPL_ERR_PARAM);
    *pending = (NVIC_GetPendingIRQ(irq) != 0U) ? 1U : 0U;
    return SPL_OK;
}

SPL_Status NVIC_SysTickConfig(uint32_t reload, NVIC_SysTickClockTypeDef clock, uint8_t interrupt)
{
    uint32_t ctrl = SysTick_CTRL_ENABLE_Msk;

    /* SAFE: invalid reload/interrupt can silently misconfigure system tick. */
    SPL_REQUIRE_SAFE(reload != 0U, SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(reload <= SysTick_LOAD_RELOAD_Msk, SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(interrupt <= 1U, SPL_ERR_PARAM);
    if (clock == NVIC_SYSTICK_CLK_HCLK)
    {
        ctrl |= SysTick_CTRL_CLKSOURCE_Msk;
    }
    else if (clock != NVIC_SYSTICK_CLK_HCLK_DIV8)
    {
        return SPL_ERR_PARAM;
    }
    else
    {
        /* CLKSOURCE = 0: HCLK/8. */
    }
    if (interrupt != 0U)
    {
        ctrl |= SysTick_CTRL_TICKINT_Msk;
    }
    if ((SysTick->CTRL & SysTick_CTRL_ENABLE_Msk) != 0U)
    {
        return SPL_ERR_STATE;
    }
    SysTick->LOAD = reload;
    SysTick->VAL = 0U;
    SysTick->CTRL = ctrl;
    return SPL_OK;
}

SPL_Status NVIC_SysTickStop(void)
{
    /* Write back CLKSOURCE only: never copy the read-only COUNTFLAG back. */
    SysTick->CTRL = SysTick->CTRL & SysTick_CTRL_CLKSOURCE_Msk;
    return SPL_OK;
}
