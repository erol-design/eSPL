/* SPDX-License-Identifier: MIT */
/* NVIC / SysTick wrappers for Cortex-M0+ (PM0223, CMSIS Core(M) 5.6 core_cm0plus.h). Public
 * contract: stm32g0_nvic.h. */
/* Library unit: this unit also emits the spl_check.h mismatch-guard reference; the only unit
 * that does not is stm32g0_check.c (SPL_CHECK_LEVEL_DEFINER). BULGU-02, CHECK_LEVEL.md 3.2. */
#define SPL_LIBRARY_BUILD 1
#include <stddef.h>
#include "stm32g0_nvic.h"

/* Check level (A4 wave 2, Docs/CHECK_LEVEL.md 5.4 NVIC table): SPL_REQUIRE = checked at FULL only
 * (not compiled at FAST). All macros come before the first MMIO write (METHOD 1.3). */

#if SPL_CHECK_LEVEL == SPL_CHECK_FULL
/* Check helpers (irq & 31 touches another interrupt); not compiled in FAST. Pure. */
static uint8_t Nvic_DeviceIrq(IRQn_Type irq)
{
    return (((int32_t)irq >= 0) && ((int32_t)irq <= (int32_t)NVIC_IRQ_MAX)) ? 1U : 0U;
}

static uint8_t Nvic_PriorityIrq(IRQn_Type irq)
{
    return ((Nvic_DeviceIrq(irq) != 0U) || (irq == SVCall_IRQn) || (irq == PendSV_IRQn) ||
            (irq == SysTick_IRQn))
               ? 1U
               : 0U;
}
#define NVIC_DEVICE_IRQ_OK(irq) (Nvic_DeviceIrq(irq) != 0U)
#define NVIC_PRIORITY_IRQ_OK(irq) (Nvic_PriorityIrq(irq) != 0U)
#else
#define NVIC_DEVICE_IRQ_OK(irq) (1) /* not evaluated at a disabled level */
#define NVIC_PRIORITY_IRQ_OK(irq) (1)
#endif

SPL_Status NVIC_SetIrqPriority(IRQn_Type irq, uint32_t priority)
{
    SPL_REQUIRE(NVIC_PRIORITY_IRQ_OK(irq) && (priority <= NVIC_PRIORITY_MAX), SPL_ERR_PARAM);
    NVIC_SetPriority(irq, priority);
    return SPL_OK;
}

SPL_Status NVIC_GetIrqPriority(IRQn_Type irq, uint32_t *priority)
{
    SPL_REQUIRE(NVIC_PRIORITY_IRQ_OK(irq), SPL_ERR_PARAM);
    SPL_REQUIRE(priority != NULL, SPL_ERR_PARAM);
    *priority = NVIC_GetPriority(irq);
    return SPL_OK;
}

SPL_Status NVIC_Enable(IRQn_Type irq)
{
    SPL_REQUIRE(NVIC_DEVICE_IRQ_OK(irq), SPL_ERR_PARAM); /* irq & 31 = other IRQ */
    NVIC_EnableIRQ(irq);
    return SPL_OK;
}

SPL_Status NVIC_Disable(IRQn_Type irq)
{
    SPL_REQUIRE(NVIC_DEVICE_IRQ_OK(irq), SPL_ERR_PARAM); /* irq & 31 = other IRQ */
    NVIC_DisableIRQ(irq);
    return SPL_OK;
}

SPL_Status NVIC_IsEnabled(IRQn_Type irq, uint8_t *enabled)
{
    SPL_REQUIRE(NVIC_DEVICE_IRQ_OK(irq), SPL_ERR_PARAM);
    SPL_REQUIRE(enabled != NULL, SPL_ERR_PARAM);
    *enabled = (NVIC_GetEnableIRQ(irq) != 0U) ? 1U : 0U;
    return SPL_OK;
}

SPL_Status NVIC_SetPending(IRQn_Type irq)
{
    SPL_REQUIRE(NVIC_DEVICE_IRQ_OK(irq), SPL_ERR_PARAM); /* irq & 31 = other IRQ */
    NVIC_SetPendingIRQ(irq);
    return SPL_OK;
}

SPL_Status NVIC_ClearPending(IRQn_Type irq)
{
    SPL_REQUIRE(NVIC_DEVICE_IRQ_OK(irq), SPL_ERR_PARAM); /* irq & 31 = other IRQ */
    NVIC_ClearPendingIRQ(irq);
    return SPL_OK;
}

SPL_Status NVIC_GetPending(IRQn_Type irq, uint8_t *pending)
{
    SPL_REQUIRE(NVIC_DEVICE_IRQ_OK(irq), SPL_ERR_PARAM);
    SPL_REQUIRE(pending != NULL, SPL_ERR_PARAM);
    *pending = (NVIC_GetPendingIRQ(irq) != 0U) ? 1U : 0U;
    return SPL_OK;
}

SPL_Status NVIC_SysTickConfig(uint32_t reload, NVIC_SysTickClockTypeDef clock, uint8_t interrupt)
{
    uint32_t ctrl = SysTick_CTRL_ENABLE_Msk;

    SPL_REQUIRE((reload != 0U) && (interrupt <= 1U), SPL_ERR_PARAM);
    /* a reload outside 24 bits / an invalid clock silently gives another period or source. */
    SPL_REQUIRE((reload <= SysTick_LOAD_RELOAD_Msk) &&
                    ((clock == NVIC_SYSTICK_CLK_HCLK_DIV8) || (clock == NVIC_SYSTICK_CLK_HCLK)),
                SPL_ERR_PARAM);
    /* (K-A4-3): reconfiguring a running SysTick silently changes another module's time
     * base. */
    SPL_REQUIRE((SysTick->CTRL & SysTick_CTRL_ENABLE_Msk) == 0U, SPL_ERR_STATE);
    if (clock == NVIC_SYSTICK_CLK_HCLK)
    {
        ctrl |= SysTick_CTRL_CLKSOURCE_Msk;
    }
    if (interrupt != 0U)
    {
        ctrl |= SysTick_CTRL_TICKINT_Msk;
    }
    SysTick->LOAD = reload;
    SysTick->VAL = 0U;
    SysTick->CTRL = ctrl;
    return SPL_OK;
}

SPL_Status NVIC_SysTickStop(void)
{
    SysTick->CTRL = SysTick->CTRL & ~(SysTick_CTRL_ENABLE_Msk | SysTick_CTRL_TICKINT_Msk);
    return SPL_OK;
}
