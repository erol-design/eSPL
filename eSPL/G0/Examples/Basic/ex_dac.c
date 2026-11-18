/* SPDX-License-Identifier: MIT */
/* DAC: DAC1 channel 1 on PA4 (Arduino A2 on NUCLEO-G0B1RE) outputs a slow staircase: 0, 25, 50,
 * 75 and 100 % of VDDA, one step per second. Measure PA4 with a multimeter, or wire it to PA0 and
 * read it with ex_adc on a second board.
 * Shows: DAC configured with the channel off, buffered external output, tWAKEUP wait after
 * DAC_Enable (G0 has no ready flag: caller duty), software-loaded 12-bit right-aligned data. */
#include "ex_board.h"

#define DAC_CH DAC_CHANNEL_1

int main(void)
{
    GPIO_InitTypeDef pin;
    DAC_ChannelConfigTypeDef cfg = {0};
    uint32_t step = 0U;

    Ex_Init();
    Ex_Print("\r\nex_dac: staircase on PA4\r\n");

    EX_CHECK(RCC_GpioClockEnable(GPIOA));
    GPIO_StructInit(&pin);
    pin.pins = 1UL << 4U;
    pin.mode = (uint32_t)GPIO_MODE_ANALOG;
    EX_CHECK(GPIO_Init(GPIOA, &pin));

    EX_CHECK(RCC_EnablePeriphClock(RCC_PERIPH_DAC1));
    cfg.trigger_enable = 0U; /* the output follows each data write */
    cfg.trigger_sel = 0U;
    cfg.mode = DAC_MODE_EXT_BUFFERED;
    cfg.dma_enable = 0U;
    cfg.dma_underrun_irq = 0U;
    EX_CHECK(DAC_ChannelConfig(DAC1, DAC_CH, &cfg));
    EX_CHECK(DAC_Enable(DAC1, DAC_CH));
    Ex_DelayMs(1U); /* tWAKEUP is a few microseconds (datasheet) */

    for (;;)
    {
        uint32_t code = (step * 4095U) / 4U;

        EX_CHECK(DAC_WriteData(DAC1, DAC_CH, DAC_ALIGN_12R, code));
        Ex_Print("DAC code ");
        Ex_PrintU32(code);
        Ex_Print(" (");
        Ex_PrintU32(step * 25U);
        Ex_Print(" % of VDDA)\r\n");
        step = (step + 1U) % 5U;
        Ex_DelayMs(1000U);
    }
}
