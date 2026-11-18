/* SPDX-License-Identifier: MIT */
/* CRC: compute CRC-32 (IEEE 802.3, as used by zip and Ethernet) of the ASCII string 123456789
 * with the CRC unit and compare it with the standard check value 0xCBF43926.
 * CRC-32: polynomial 0x04C11DB7, init 0xFFFFFFFF, input and output bit-reversed, final XOR
 * 0xFFFFFFFF. The unit has no XOR-out stage: the final XOR is done in software (driver caller
 * duty, CRC_GetResult). */
#include "ex_board.h"

#define CRC32_CHECK 0xCBF43926UL

int main(void)
{
    static const char text[] = "123456789";
    CRC_ConfigTypeDef cfg;
    uint32_t crc = 0U;
    uint32_t i;

    Ex_Init();
    Ex_Print("\r\nex_crc: CRC-32 of 123456789\r\n");

    EX_CHECK(RCC_EnablePeriphClock(RCC_PERIPH_CRC));
    cfg.polysize = CRC_POLYSIZE_32;
    cfg.polynomial = 0x04C11DB7UL;
    cfg.init = 0xFFFFFFFFUL;
    cfg.rev_in = CRC_REV_IN_BYTE; /* fed byte by byte, each byte bit-reversed */
    cfg.rev_out = 1U;
    EX_CHECK(CRC_Config(&cfg)); /* also starts a new computation from init */

    for (i = 0U; text[i] != '\0'; i++)
    {
        EX_CHECK(CRC_Feed8((uint8_t)text[i]));
    }
    EX_CHECK(CRC_GetResult(&crc));
    crc ^= 0xFFFFFFFFUL;

    Ex_Print("result ");
    Ex_PrintHex(crc);
    Ex_Print((crc == CRC32_CHECK) ? " = check value, OK\r\n" : " != 0xCBF43926, FAILED\r\n");

    for (;;)
    {
        Ex_LedToggle(); /* slow blink = OK, fast blink = failed */
        Ex_DelayMs((crc == CRC32_CHECK) ? 1000U : 100U);
    }
}
