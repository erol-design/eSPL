/* NVIC / SysTick wrappers for Cortex-M0+ (PM0223, CMSIS Core(M) 5.6 core_cm0plus.h). */
/* Kutuphane birimi: spl_check.h karisma korumasi referansini bu birim de uretir; uretmeyen tek
 * birim stm32g0_check.c (SPL_CHECK_LEVEL_DEFINER). BULGU-02, CHECK_LEVEL.md 3.2. */
#define SPL_LIBRARY_BUILD 1
#include <stddef.h>
#include "stm32g0_nvic.h"

/* Denetim seviyesi (A4 dalga 2, Docs/CHECK_LEVEL.md 5.4 NVIC tablosu): SPL_REQUIRE = normal sinif
 * (yalniz FULL), SPL_REQUIRE_SAFE = SAFE sinif (FULL ve MIX). Butun makrolar ilk MMIO yazimindan
 * once (METHOD 1.3). */

#if SPL_CHECK_LEVEL <= SPL_CHECK_MIX
/* SAFE denetim yardimcilari (irq & 31 baska kesmeye dokunur); FAST'ta derlenmez. Saf. */
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
#define NVIC_DEVICE_IRQ_OK(irq) (1) /* kapali seviyede degerlendirilmez */
#define NVIC_PRIORITY_IRQ_OK(irq) (1)
#endif

SPL_Status NVIC_SetIrqPriority(IRQn_Type irq, uint32_t priority)
{
    SPL_REQUIRE_SAFE(NVIC_PRIORITY_IRQ_OK(irq) && (priority <= NVIC_PRIORITY_MAX), SPL_ERR_PARAM);
    NVIC_SetPriority(irq, priority);
    return SPL_OK;
}

SPL_Status NVIC_GetIrqPriority(IRQn_Type irq, uint32_t *priority)
{
    SPL_REQUIRE_SAFE(NVIC_PRIORITY_IRQ_OK(irq), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(priority != NULL, SPL_ERR_PARAM);
    *priority = NVIC_GetPriority(irq);
    return SPL_OK;
}

SPL_Status NVIC_Enable(IRQn_Type irq)
{
    SPL_REQUIRE_SAFE(NVIC_DEVICE_IRQ_OK(irq), SPL_ERR_PARAM); /* SAFE: irq & 31 baska kesme */
    NVIC_EnableIRQ(irq);
    return SPL_OK;
}

SPL_Status NVIC_Disable(IRQn_Type irq)
{
    SPL_REQUIRE_SAFE(NVIC_DEVICE_IRQ_OK(irq), SPL_ERR_PARAM); /* SAFE: irq & 31 baska kesme */
    NVIC_DisableIRQ(irq);
    return SPL_OK;
}

SPL_Status NVIC_IsEnabled(IRQn_Type irq, uint8_t *enabled)
{
    SPL_REQUIRE_SAFE(NVIC_DEVICE_IRQ_OK(irq), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(enabled != NULL, SPL_ERR_PARAM);
    *enabled = (NVIC_GetEnableIRQ(irq) != 0U) ? 1U : 0U;
    return SPL_OK;
}

SPL_Status NVIC_SetPending(IRQn_Type irq)
{
    SPL_REQUIRE_SAFE(NVIC_DEVICE_IRQ_OK(irq), SPL_ERR_PARAM); /* SAFE: irq & 31 baska kesme */
    NVIC_SetPendingIRQ(irq);
    return SPL_OK;
}

SPL_Status NVIC_ClearPending(IRQn_Type irq)
{
    SPL_REQUIRE_SAFE(NVIC_DEVICE_IRQ_OK(irq), SPL_ERR_PARAM); /* SAFE: irq & 31 baska kesme */
    NVIC_ClearPendingIRQ(irq);
    return SPL_OK;
}

SPL_Status NVIC_GetPending(IRQn_Type irq, uint8_t *pending)
{
    SPL_REQUIRE_SAFE(NVIC_DEVICE_IRQ_OK(irq), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(pending != NULL, SPL_ERR_PARAM);
    *pending = (NVIC_GetPendingIRQ(irq) != 0U) ? 1U : 0U;
    return SPL_OK;
}

SPL_Status NVIC_SysTickConfig(uint32_t reload, NVIC_SysTickClockTypeDef clock, uint8_t interrupt)
{
    uint32_t ctrl = SysTick_CTRL_ENABLE_Msk;

    SPL_REQUIRE((reload != 0U) && (interrupt <= 1U), SPL_ERR_PARAM); /* normal */
    /* SAFE: 24 bit disi reload / gecersiz saat sessizce baska periyot ya da kaynak. */
    SPL_REQUIRE_SAFE(
        (reload <= SysTick_LOAD_RELOAD_Msk) &&
            ((clock == NVIC_SYSTICK_CLK_HCLK_DIV8) || (clock == NVIC_SYSTICK_CLK_HCLK)),
        SPL_ERR_PARAM);
    /* SAFE (K-A4-3): calisan SysTick'i yeniden kurmak baska modulun zaman tabanini sessizce
     * degistirir. */
    SPL_REQUIRE_SAFE((SysTick->CTRL & SysTick_CTRL_ENABLE_Msk) == 0U, SPL_ERR_STATE);
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
