/* SPDX-License-Identifier: MIT */
/* GPIO: the LED follows the user button (polled input, push-pull output).
 * Board: NUCLEO-G0B1RE, button B1 PC13, LED LD4 PA5 (ex_board.h). Console prints each change.
 * Shows: port clock before GPIO_Init (driver caller duty), GPIO_StructInit defaults, GPIO_Read
 * with a pin mask, GPIO_Write. A press is debounced by requiring the same level for 20 ms. */
#include "ex_board.h"

#define DEBOUNCE_MS 20U

int main(void)
{
    uint8_t stable = 0U;
    uint8_t last = 0U;
    uint32_t since = 0U;

    Ex_Init(); /* LED output PA5 and button input PC13 are configured there (GPIO_Init) */
    Ex_Print("\r\nex_gpio: press B1, LD4 follows\r\n");

    for (;;)
    {
        uint8_t now = Ex_ButtonPressed(); /* GPIO_Read(GPIOC, 1 << 13, &value) */

        if (now != last)
        {
            last = now;
            since = Ex_Millis();
        }
        else if ((now != stable) && (Ex_Expired(since, DEBOUNCE_MS) != 0U))
        {
            stable = now;
            Ex_Led(stable); /* GPIO_Write(GPIOA, 1 << 5, level) */
            Ex_Print((stable != 0U) ? "pressed\r\n" : "released\r\n");
        }
        else
        {
            /* no change */
        }
    }
}
