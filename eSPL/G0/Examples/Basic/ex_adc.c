/* SPDX-License-Identifier: MIT */
/* ADC: measure PA0 (ADC_IN0, Arduino A0 on NUCLEO-G0B1RE) and the internal reference VREFINT
 * (channel 13) twice a second; VREFINT and its factory calibration give the real VDDA, which
 * converts the PA0 reading into millivolts.
 * ADC clock: synchronous PCLK / 2 = 8 MHz (no RCC kernel clock selection needed). Sampling time
 * 39.5 cycles = 4.9 us, enough for VREFINT (datasheet minimum 4 us).
 * Shows the G0 ADC start-up order (driver caller duties): regulator on, wait tADCVREG_STUP,
 * calibration with ADEN = 0, configuration with ADEN = 0, channel selection with the CCRDY
 * handshake, enable and poll ADRDY, then start + non-blocking reads. */
#include "ex_board.h"

/* VREFINT factory calibration, measured at VDDA = 3.0 V (STM32G0B1 datasheet, "Internal voltage
 * reference calibration values"; not provided by the CMSIS header). */
#define VREFINT_CAL (*(const volatile uint16_t *)0x1FFF75AAUL)
#define VREFINT_CAL_MV 3000UL

int main(void)
{
    GPIO_InitTypeDef pin;
    ADC_RegularConfigTypeDef reg = {0};
    ADC_ChannelConfigTypeDef ch = {0};
    uint8_t flag = 0U;

    Ex_Init();
    Ex_Print("\r\nex_adc: PA0 and VDDA\r\n");

    EX_CHECK(RCC_GpioClockEnable(GPIOA));
    GPIO_StructInit(&pin);
    pin.pins = 1UL << 0U;
    pin.mode = (uint32_t)GPIO_MODE_ANALOG; /* pull must stay GPIO_NO_PULL in analog mode */
    EX_CHECK(GPIO_Init(GPIOA, &pin));

    EX_CHECK(RCC_EnablePeriphClock(RCC_PERIPH_ADC));
    EX_CHECK(ADC_CommonClockConfig(ADC1, ADC_CLOCK_SYNC_DIV2, 0U));
    EX_CHECK(ADC_PowerRegulator(ADC1));
    Ex_DelayMs(1U); /* tADCVREG_STUP is 20 us max (datasheet) */
    EX_CHECK(ADC_StartCalibration(ADC1));
    EX_WAIT_FLAG(ADC_CalibrationDone(ADC1, &flag), flag, 10U);

    EX_CHECK(ADC_InternalChannelsConfig(ADC1, ADC_INTERNAL_VREFINT));
    reg.resolution = ADC_RES_12BIT;
    reg.align = ADC_ALIGN_RIGHT;
    reg.continuous = 0U; /* one ADC_Start converts the sequence once */
    reg.ext_edge = ADC_EDGE_SOFTWARE;
    EX_CHECK(ADC_RegularConfig(ADC1, &reg));
    ch.mode = ADC_SEQ_FORWARD; /* bit mask, lowest channel first: IN0, then VREFINT */
    ch.mask = (1UL << 0U) | (1UL << 13U);
    /* BUSY / WOULD_BLOCK until the previous channel configuration is applied (CCRDY). */
    EX_WAIT(ADC_ChannelConfig(ADC1, &ch), 10U);
    EX_WAIT_FLAG(ADC_ChannelConfigReady(ADC1, &flag), flag, 10U);
    EX_CHECK(ADC_SamplingConfig(ADC1, 5U, 5U, 0U)); /* code 5 = 39.5 cycles */

    EX_CHECK(ADC_Enable(ADC1));
    EX_WAIT_FLAG(ADC_Ready(ADC1, &flag), flag, 10U);

    for (;;)
    {
        uint16_t pa0 = 0U;
        uint16_t vref = 0U;
        uint32_t vdda_mv;

        EX_CHECK(ADC_Start(ADC1));
        EX_WAIT(ADC_TryRead(ADC1, &pa0), 5U);  /* rank 1: IN0 */
        EX_WAIT(ADC_TryRead(ADC1, &vref), 5U); /* rank 2: VREFINT */
        if (vref == 0U)
        {
            Ex_Fatal(SPL_ERR_HW, (uint32_t)__LINE__); /* VREFINT reads 0: no division by 0 */
        }
        vdda_mv = (VREFINT_CAL_MV * (uint32_t)VREFINT_CAL) / vref;

        Ex_Print("PA0 raw ");
        Ex_PrintU32(pa0);
        Ex_Print(" = ");
        Ex_PrintU32(((uint32_t)pa0 * vdda_mv) / 4095U);
        Ex_Print(" mV, VDDA ");
        Ex_PrintU32(vdda_mv);
        Ex_Print(" mV\r\n");
        Ex_DelayMs(500U);
    }
}
