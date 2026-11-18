/* SPDX-License-Identifier: MIT */
/* TIM update interrupt: TIM6 (basic timer) raises an update interrupt every 250 ms; the handler
 * toggles the LED and the main loop prints a seconds counter.
 * Timer clock 16 MHz: PSC 15999 -> 1 kHz counter, ARR 249 -> 4 Hz update rate.
 * Shows: TIM_GenerateUpdate sets UIF as a side effect, so the stale flag is cleared before the
 * interrupt is enabled (driver caller duty); the handler clears UIF itself; TIM6 shares its
 * vector with DAC and LPTIM1 on G0B1 (TIM6_DAC_LPTIM1_IRQn). */
#include "ex_board.h"

static volatile uint32_t g_quarters;

void TIM6_DAC_LPTIM1_IRQHandler(void);

void TIM6_DAC_LPTIM1_IRQHandler(void)
{
    uint8_t pending = 0U;

    EX_CHECK(TIM_UpdatePending(TIM6, &pending));
    if (pending != 0U)
    {
        EX_CHECK(TIM_ClearUpdate(TIM6));
        g_quarters++;
        Ex_LedToggle();
    }
}

int main(void)
{
    TIM_BaseInitTypeDef base;
    uint32_t shown = 0U;

    Ex_Init();
    Ex_Print("\r\nex_tim_irq: TIM6 update interrupt at 4 Hz\r\n");

    EX_CHECK(RCC_EnablePeriphClock(RCC_PERIPH_TIM6));
    base.prescaler = 15999U;
    base.period = 249U;
    base.auto_reload_preload = 0U;
    EX_CHECK(TIM_BaseInit(TIM6, &base));
    EX_CHECK(TIM_GenerateUpdate(TIM6)); /* loads PSC / ARR, also sets UIF */
    EX_CHECK(TIM_ClearUpdate(TIM6));
    EX_CHECK(TIM_UpdateInterrupt(TIM6, 1U));
    EX_CHECK(NVIC_SetIrqPriority(TIM6_DAC_LPTIM1_IRQn, 1U));
    EX_CHECK(NVIC_Enable(TIM6_DAC_LPTIM1_IRQn));
    EX_CHECK(TIM_Enable(TIM6, 1U));

    for (;;)
    {
        uint32_t seconds = g_quarters / 4U;

        if (seconds != shown)
        {
            shown = seconds;
            Ex_Print("t = ");
            Ex_PrintU32(shown);
            Ex_Print(" s\r\n");
        }
        __WFI();
    }
}
