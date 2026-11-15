/* Bare-metal example: LD2 (PA5) on NUCLEO-G431RB blinks at 1 Hz (500 ms on, 500 ms off).
 * Clock: reset state, HSI16 = 16 MHz (SystemInit leaves the clock tree alone).
 * Delay: SysTick polled on COUNTFLAG, 1 ms per reload (no interrupt). */
#include "stm32g4_rcc.h"
#include "stm32g4_gpio.h"

#define BLINK_CORE_HZ 16000000UL /* HSI16 after reset */
#define BLINK_HALF_PERIOD_MS 500U

static void delay_ms(uint32_t ms)
{
    SysTick->LOAD = (BLINK_CORE_HZ / 1000UL) - 1UL;
    SysTick->VAL = 0UL;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk;
    while (ms > 0U)
    {
        if ((SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk) != 0UL) /* read clears COUNTFLAG */
        {
            ms--;
        }
    }
    SysTick->CTRL = 0UL;
}

int main(void)
{
    GPIO_InitTypeDef led;

    if (RCC_GpioClockEnable(GPIOA) != SPL_OK) { for (;;) {} }
    GPIO_StructInit(&led);
    led.pins = (uint16_t)(1U << 5U); /* NUCLEO-G431RB LD2 = PA5 (UM2505) */
    led.mode = GPIO_MODE_OUTPUT;
    if (GPIO_Init(GPIOA, &led) != SPL_OK) { for (;;) {} }
    for (;;) {
        (void)GPIO_Write(GPIOA, led.pins, 1U);
        delay_ms(BLINK_HALF_PERIOD_MS);
        (void)GPIO_Write(GPIOA, led.pins, 0U);
        delay_ms(BLINK_HALF_PERIOD_MS);
    }
}
