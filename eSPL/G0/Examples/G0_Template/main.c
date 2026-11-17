/* SPDX-License-Identifier: MIT */
/* eSPL STM32G0 template application: the starting point for a new project (copy the folder).
 *
 * What it does: SysTick 1 ms tick, then blinks one LED with the checked GPIO API. It shows the
 * three things every eSPL application needs:
 *  1. one include (stm32g0_spl.h) and one CMSIS device macro in the project (STM32G0B1xx here);
 *  2. the caller duties the driver does not do itself: the port clock (RCC_GpioClockEnable)
 *     before GPIO_Init (Inc/stm32g0_gpio.h file header);
 *  3. every SPL_Status is checked. A failure stops in App_Fatal with the status kept in
 *     g_fatal_status (watch it in the debugger): an error stays visible, it is never retried or
 *     hidden.
 *
 * Default board: NUCLEO-G0B1RE (STM32G0B1RETx), user LED LD4 on PA5. For another board change
 * LED_PORT / LED_PIN, the device macro and the Keil device; see README.md in this folder.
 * Clock: HSI16 after reset (SystemCoreClock = 16 MHz, CMSIS system file); no PLL set-up. */
#include "stm32g0_spl.h"

#define LED_PORT GPIOA
#define LED_PIN 5U
#define BLINK_HALF_PERIOD_MS 500U

static volatile uint32_t g_tick_ms;
/* Last failing status; SPL_OK while the application runs. */
volatile SPL_Status g_fatal_status = SPL_OK;

void SysTick_Handler(void);

void SysTick_Handler(void)
{
    g_tick_ms++;
}

/* Stops the application on an unexpected status: interrupts off, status kept for the debugger.
 * Replace the loop with the product's safe state (outputs off, reset reason logged). */
static void App_Fatal(SPL_Status status)
{
    g_fatal_status = status;
    __disable_irq();
    for (;;)
    {
    }
}

static void App_Check(SPL_Status status)
{
    if (status != SPL_OK)
    {
        App_Fatal(status);
    }
}

/* Busy wait on the SysTick counter; the unsigned difference stays correct across wrap-around. */
static void App_DelayMs(uint32_t ms)
{
    uint32_t start = g_tick_ms;

    while ((uint32_t)(g_tick_ms - start) < ms)
    {
    }
}

int main(void)
{
    GPIO_InitTypeDef led;
    uint8_t level = 0U;

    /* CMSIS: returns 1 when the reload value does not fit the 24-bit counter. */
    if (SysTick_Config(SystemCoreClock / 1000U) != 0U)
    {
        App_Fatal(SPL_ERR_PARAM);
    }

    App_Check(RCC_GpioClockEnable(LED_PORT));
    GPIO_StructInit(&led);
    led.pins = 1UL << LED_PIN;
    led.mode = (uint32_t)GPIO_MODE_OUTPUT;
    led.output_type = (uint32_t)GPIO_OUTPUT_PUSH_PULL;
    led.speed = (uint32_t)GPIO_SPEED_LOW;
    App_Check(GPIO_Init(LED_PORT, &led));

    for (;;)
    {
        level = (uint8_t)(level ^ 1U);
        App_Check(GPIO_Write(LED_PORT, (uint16_t)(1U << LED_PIN), level));
        App_DelayMs(BLINK_HALF_PERIOD_MS);
    }
}
