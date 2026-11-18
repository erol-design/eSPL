/* SPDX-License-Identifier: MIT */
/* SPI: loop-back test of SPI1 as master. Wire MOSI to MISO (PA7 to PA6 = Arduino D11 to D12 on
 * NUCLEO-G0B1RE); every byte sent must come back. SCK is on PA1 (Arduino A1) so that PA5 stays
 * the LED. Without the wire every byte reads 0x00 or 0xFF and the test reports FAILED.
 * SPI1: 8-bit frames, mode 0, MSB first, PCLK 16 MHz / 16 = 1 MHz, software NSS.
 * Shows: full duplex needs one read per written frame (else OVR), the Try* calls return
 * SPL_WOULD_BLOCK instead of waiting, SPI_Init with SPE = 0 and SPI_Enable afterwards. */
#include "ex_board.h"

static void SpiPins(void)
{
    GPIO_InitTypeDef pin;

    EX_CHECK(RCC_GpioClockEnable(GPIOA));
    GPIO_StructInit(&pin);
    pin.pins = (1UL << 1U) | (1UL << 6U) | (1UL << 7U); /* PA1 SCK, PA6 MISO, PA7 MOSI */
    pin.mode = (uint32_t)GPIO_MODE_AF;
    pin.output_type = (uint32_t)GPIO_OUTPUT_PUSH_PULL;
    pin.speed = (uint32_t)GPIO_SPEED_HIGH;
    pin.alternate = 0U; /* AF0 = SPI1 */
    EX_CHECK(GPIO_Init(GPIOA, &pin));
}

int main(void)
{
    SPI_ConfigTypeDef cfg = {0};
    uint32_t errors = 0U;
    uint32_t i;

    Ex_Init();
    Ex_Print("\r\nex_spi: SPI1 loop-back, wire PA7 (MOSI) to PA6 (MISO)\r\n");
    SpiPins();

    EX_CHECK(RCC_EnablePeriphClock(RCC_PERIPH_SPI1));
    cfg.mode = SPI_MODE_MASTER;
    cfg.data_bits = 8U;
    cfg.cpol = 0U;
    cfg.cpha = 0U;
    cfg.lsb_first = 0U;
    cfg.prescaler = 3U; /* PCLK / (2 << 3) = PCLK / 16 */
    cfg.nss = SPI_NSS_SOFT;
    cfg.frame_format = SPI_FRAME_MOTOROLA;
    cfg.direction = SPI_DIR_FULL_DUPLEX;
    cfg.crc_enable = 0U;
    EX_CHECK(SPI_Init(SPI1, &cfg));
    EX_CHECK(SPI_Enable(SPI1, 1U));

    for (i = 0U; i < 256U; i++)
    {
        uint8_t rx = 0U;

        EX_WAIT(SPI_TryWrite8(SPI1, (uint8_t)i), 5U);
        EX_WAIT(SPI_TryRead8(SPI1, &rx), 5U); /* the frame clocked in while i was sent */
        if (rx != (uint8_t)i)
        {
            if (errors < 4U)
            {
                Ex_Print("  sent ");
                Ex_PrintHex(i);
                Ex_Print(" got ");
                Ex_PrintHex(rx);
                Ex_Print("\r\n");
            }
            errors++;
        }
    }
    /* Master disable: BUSY until the last frame has left (ES0548 2.14.1). */
    EX_WAIT(SPI_Enable(SPI1, 0U), 5U);

    Ex_Print((errors == 0U) ? "loop-back OK, 256 bytes\r\n" : "loop-back FAILED, errors: ");
    if (errors != 0U)
    {
        Ex_PrintU32(errors);
        Ex_Print("\r\n");
    }

    for (;;)
    {
        Ex_LedToggle(); /* slow blink = OK, fast blink = failed */
        Ex_DelayMs((errors == 0U) ? 1000U : 100U);
    }
}
