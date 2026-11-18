/* SPDX-License-Identifier: MIT */
/* USART: echo every received character back on the ST-LINK virtual COM port (USART2, PA2 / PA3,
 * 115200 8N1; open a terminal on the board's COM port). Enter prints a line count.
 * Shows: the console init in ex_board.c (bus clock, pins in AF1, kernel clock from
 * RCC_GetUsartKernelClock, USART_Init), and the non-blocking transfer API: USART_TryRead /
 * USART_TryWrite return SPL_WOULD_BLOCK instead of waiting, ERR_HW reports PE / FE / NE / ORE
 * (capture with USART_ErrorFlags, then clear). */
#include "ex_board.h"

static void EchoByte(uint8_t byte)
{
    /* The driver never waits: retry while the transmit register is still full. */
    EX_WAIT(USART_TryWrite(EX_CONSOLE, byte), 5U);
}

int main(void)
{
    uint32_t lines = 0U;
    uint8_t byte = 0U;

    Ex_Init();
    Ex_Print("\r\nex_usart: type something, it is echoed\r\n> ");

    for (;;)
    {
        SPL_Status status = USART_TryRead(EX_CONSOLE, &byte);

        if (status == SPL_OK)
        {
            if (byte == (uint8_t)'\r')
            {
                lines++;
                Ex_Print("\r\nline ");
                Ex_PrintU32(lines);
                Ex_Print("\r\n> ");
            }
            else
            {
                EchoByte(byte);
            }
        }
        else if (status == SPL_ERR_HW)
        {
            uint32_t errors = 0U;

            EX_CHECK(USART_ErrorFlags(EX_CONSOLE, &errors));
            EX_CHECK(USART_ClearErrorFlags(EX_CONSOLE, errors));
            Ex_Print("\r\n[rx error flags ");
            Ex_PrintHex(errors); /* bit0 PE, bit1 FE, bit2 NE, bit3 ORE */
            Ex_Print("]\r\n> ");
        }
        else if (status != SPL_WOULD_BLOCK)
        {
            Ex_Fatal(status, (uint32_t)__LINE__);
        }
        else
        {
            /* nothing received yet */
        }
    }
}
