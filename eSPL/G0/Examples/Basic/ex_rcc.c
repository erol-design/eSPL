/* SPDX-License-Identifier: MIT */
/* RCC: raise the system clock from HSI16 (16 MHz after reset) to 64 MHz with the PLL, then print
 * the clock tree and blink the LED at 1 Hz from the re-programmed SysTick.
 * PLL: HSI16 / M 1 x N 8 = VCO 128 MHz, / R 2 = 64 MHz (voltage range 1, the reset value).
 * Shows the order every frequency increase needs (RM0444 3.3.4, driver caller duties):
 *   1. flash wait states first (FLASH_CalcLatency / FLASH_SetLatency);
 *   2. PLL configured while off, started, then polled until ready (no driver waits);
 *   3. SYSCLK switch request, polled until the switch status reports the PLL;
 *   4. everything derived from the clock is the application's: SystemCoreClock, SysTick, the
 *      console baud rate. */
#include "ex_board.h"

#define TARGET_HZ 64000000UL

static void PrintClocks(void)
{
    RCC_ClocksTypeDef clk;

    EX_CHECK(RCC_GetClocks(0U, &clk)); /* hse_hz = 0: no HSE on this example */
    Ex_Print("SYSCLK ");
    Ex_PrintU32(clk.sysclk_hz);
    Ex_Print(" Hz, HCLK ");
    Ex_PrintU32(clk.hclk_hz);
    Ex_Print(" Hz, PCLK ");
    Ex_PrintU32(clk.pclk_hz);
    Ex_Print(" Hz\r\n");
}

int main(void)
{
    RCC_PllConfigTypeDef pll;
    RCC_SysclkTypeDef source = RCC_SYSCLK_HSISYS;
    uint32_t latency = 0U;
    uint8_t ready = 0U;
    uint32_t t0 = 0U;

    Ex_Init();
    Ex_Print("\r\nex_rcc: before\r\n");
    PrintClocks();

    /* The PLL limits depend on the voltage range in PWR_CR1.VOS: the PWR clock must be on so the
     * driver can read it. */
    EX_CHECK(RCC_EnablePeriphClock(RCC_PERIPH_PWR));

    EX_CHECK(FLASH_CalcLatency(TARGET_HZ, FLASH_RANGE1, &latency));
    EX_CHECK(FLASH_SetLatency(latency)); /* 2 wait states for 64 MHz */

    pll.source = (uint32_t)RCC_PLLSRC_HSI16;
    pll.m = 1U;
    pll.n = 8U;
    pll.p = 2U; /* P and Q outputs stay disabled; their values only need to be legal */
    pll.q = 2U;
    pll.r = 2U;
    pll.outputs = RCC_PLL_OUT_R;
    EX_CHECK(RCC_ConfigPll(&pll, 0U));
    EX_CHECK(RCC_EnablePll());
    EX_WAIT_FLAG(RCC_GetPllReady(&ready), ready, 10U);

    EX_CHECK(RCC_SetSysclkSource(RCC_SYSCLK_PLLR, 0U));
    t0 = Ex_Millis();
    do
    {
        EX_CHECK(RCC_GetSysclkSource(&source));
        if (Ex_Expired(t0, 10U) != 0U)
        {
            Ex_Fatal(SPL_ERR_TIMEOUT, (uint32_t)__LINE__);
        }
    } while (source != RCC_SYSCLK_PLLR);

    /* Clock-derived settings are not updated by the driver. */
    SystemCoreClockUpdate();
    if (SysTick_Config(SystemCoreClock / 1000U) != 0U)
    {
        Ex_Fatal(SPL_ERR_PARAM, (uint32_t)__LINE__);
    }
    Ex_ConsoleInit(); /* new PCLK -> new BRR */

    Ex_Print("ex_rcc: after\r\n");
    PrintClocks();

    for (;;)
    {
        Ex_LedToggle();
        Ex_DelayMs(500U);
    }
}
