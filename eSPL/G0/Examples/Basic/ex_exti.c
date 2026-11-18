/* SPDX-License-Identifier: MIT */
/* EXTI: the user button raises an interrupt on its falling edge; the handler toggles the LED and
 * counts presses. Board: NUCLEO-G0B1RE, B1 on PC13 -> EXTI line 13 -> EXTI4_15_IRQn.
 * Shows: G0 routes the pin inside EXTI (EXTI_SetGpioSource, not SYSCFG), a stale pending bit is
 * cleared before unmasking, the handler clears the pending bit itself (driver caller duty), NVIC
 * priority and enable through the checked wrappers. */
#include "ex_board.h"

#define BUTTON_LINE EX_BUTTON_PIN

static volatile uint32_t g_presses;

void EXTI4_15_IRQHandler(void);

void EXTI4_15_IRQHandler(void)
{
    uint8_t rising = 0U;
    uint8_t falling = 0U;

    /* G0 keeps separate rising / falling pending bits per line. */
    EX_CHECK(EXTI_GetPendingEdges(BUTTON_LINE, &rising, &falling));
    if (falling != 0U)
    {
        g_presses++;
        Ex_LedToggle();
    }
    EX_CHECK(EXTI_ClearPending(BUTTON_LINE));
}

int main(void)
{
    uint32_t shown = 0U;

    Ex_Init(); /* button pin PC13 is already an input */
    Ex_Print("\r\nex_exti: press B1 (interrupt on falling edge)\r\n");

    EX_CHECK(EXTI_SetGpioSource(EX_BUTTON_PORT, BUTTON_LINE));
    EX_CHECK(EXTI_ClearPending(BUTTON_LINE));
    EX_CHECK(EXTI_LineConfig(BUTTON_LINE, EXTI_TRIGGER_FALLING, EXTI_MODE_INTERRUPT));
    EX_CHECK(NVIC_SetIrqPriority(EXTI4_15_IRQn, 2U));
    EX_CHECK(NVIC_Enable(EXTI4_15_IRQn));

    for (;;)
    {
        uint32_t presses = g_presses;

        /* No debounce here on purpose: a bouncing contact shows as several counts. */
        if (presses != shown)
        {
            shown = presses;
            Ex_Print("presses: ");
            Ex_PrintU32(shown);
            Ex_Print("\r\n");
        }
        __WFI(); /* sleep until the next interrupt (SysTick or EXTI) */
    }
}
