/* SPDX-License-Identifier: MIT */
/* IWDG: independent watchdog with a nominal 1 s timeout. The main loop refreshes it every
 * 200 ms; hold the user button (B1) and the refresh stops, so the watchdog resets the MCU about
 * one second later. After each start the reset cause is printed from the RCC reset flags.
 * IWDG clock: LSI, nominal 32 kHz (29.5..34 kHz in the datasheet: the real timeout varies).
 * Prescaler code 3 = /32 -> 1 kHz, reload 1000 -> 1 s nominal.
 * Shows: reset flags read and cleared at start-up (otherwise they add up across resets), the
 * IWDG start order of RM0444 28.3.2 (start, then prescaler / reload, then poll the update
 * status: no driver waits), and that a started IWDG can only be stopped by a reset. */
#include "ex_board.h"

#define IWDG_PRESCALER 3U /* /32 */
#define IWDG_RELOAD 1000U

static void PrintResetCause(void)
{
    uint32_t flags = 0U;

    EX_CHECK(RCC_GetResetFlags(&flags));
    Ex_Print("reset cause:");
    if ((flags & RCC_CSR_IWDGRSTF) != 0U)
    {
        Ex_Print(" IWDG");
    }
    if ((flags & RCC_CSR_PINRSTF) != 0U)
    {
        Ex_Print(" PIN");
    }
    if ((flags & RCC_CSR_PWRRSTF) != 0U)
    {
        Ex_Print(" POWER");
    }
    if ((flags & RCC_CSR_SFTRSTF) != 0U)
    {
        Ex_Print(" SOFTWARE");
    }
    Ex_Print("\r\n");
    EX_CHECK(RCC_ClearResetFlags());
}

int main(void)
{
    uint32_t timeout_us = 0U;

    Ex_Init();
    Ex_Print("\r\nex_iwdg: hold B1 to stop refreshing the watchdog\r\n");
    PrintResetCause();

    EX_CHECK(IWDG_CalcTimeoutUs(IWDG_PRESCALER, IWDG_RELOAD, &timeout_us));
    Ex_Print("nominal timeout ");
    Ex_PrintU32(timeout_us / 1000U);
    Ex_Print(" ms\r\n");

    EX_CHECK(IWDG_Start()); /* irreversible until the next reset; starts LSI if needed */
    EX_WAIT(IWDG_SetPrescalerReload(IWDG_PRESCALER, IWDG_RELOAD), 10U);
    /* The new values reach the LSI domain within a few LSI cycles (BUSY until then). */
    EX_WAIT(IWDG_UpdateStatus(), 10U);
    EX_CHECK(IWDG_Refresh());

    for (;;)
    {
        if (Ex_ButtonPressed() == 0U)
        {
            EX_CHECK(IWDG_Refresh());
            Ex_LedToggle();
        }
        else
        {
            Ex_Print("button held: no refresh\r\n");
            Ex_Led(1U);
        }
        Ex_DelayMs(200U);
    }
}
