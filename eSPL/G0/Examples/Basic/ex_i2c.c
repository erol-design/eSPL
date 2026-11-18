/* SPDX-License-Identifier: MIT */
/* I2C: bus scan. I2C1 as controller on PB8 (SCL) / PB9 (SDA) = Arduino D15 / D14 on
 * NUCLEO-G0B1RE, 100 kHz. Each 7-bit address 0x08..0x77 gets a one-byte read; an acknowledged
 * address is a device and is printed. Connect a sensor board (with its pull-ups) to D14 / D15.
 * The internal pull-ups are enabled only so that an empty bus idles high; real buses need
 * external pull-ups (2.2..4.7 kOhm).
 * Clock: I2C1 kernel clock = PCLK = 16 MHz (reset selection), TIMINGR 0x30420F13 for 100 kHz
 * (RM0444 Table 173). PCLK / I2CCLK = 1 keeps clear of ES0548 2.11.4.
 * Shows: AUTOEND transfers (the hardware sends STOP), a NACK reported as SPL_ERR_HW, error flags
 * captured before they are cleared, STOPF acknowledged by the application, bounded BUSY retries
 * (a stuck bus would otherwise hang the scan). */
#include "ex_board.h"

#define I2C_TIMINGR_100K_16MHZ 0x30420F13UL

static void I2cPins(void)
{
    GPIO_InitTypeDef pin;

    EX_CHECK(RCC_GpioClockEnable(GPIOB));
    GPIO_StructInit(&pin);
    pin.pins = (1UL << 8U) | (1UL << 9U);
    pin.mode = (uint32_t)GPIO_MODE_AF;
    pin.output_type = (uint32_t)GPIO_OUTPUT_OPEN_DRAIN; /* I2C is open drain */
    pin.speed = (uint32_t)GPIO_SPEED_LOW;
    pin.pull = (uint32_t)GPIO_PULL_UP;
    pin.alternate = 6U; /* AF6 = I2C1 */
    EX_CHECK(GPIO_Init(GPIOB, &pin));
}

/* 1 when a device acknowledged address7. */
static uint8_t Probe(uint8_t address7)
{
    uint8_t present = 0U;
    uint8_t byte = 0U;
    uint8_t stop = 0U;
    uint32_t t0;
    SPL_Status status;

    /* BUSY while the bus is busy or the START is still pending. */
    EX_WAIT(I2C_Start(I2C1, address7, 1U, 1U), 5U);

    t0 = Ex_Millis();
    status = I2C_TryRead(I2C1, &byte);
    while (status == SPL_WOULD_BLOCK)
    {
        if (Ex_Expired(t0, 5U) != 0U)
        {
            Ex_Fatal(SPL_ERR_TIMEOUT, (uint32_t)__LINE__);
        }
        status = I2C_TryRead(I2C1, &byte);
    }
    if (status == SPL_OK)
    {
        present = 1U;
    }
    else if (status == SPL_ERR_HW)
    {
        uint32_t errors = 0U;

        EX_CHECK(I2C_GetErrors(I2C1, &errors));
        if (errors != I2C_ISR_NACKF)
        {
            Ex_Print("bus error flags ");
            Ex_PrintHex(errors); /* arbitration lost, bus error, ...: shown, not hidden */
            Ex_Print("\r\n");
        }
        EX_CHECK(I2C_ClearErrors(I2C1)); /* address not acknowledged: nobody there */
    }
    else
    {
        Ex_Fatal(status, (uint32_t)__LINE__);
    }

    /* AUTOEND: the hardware sends STOP after the byte or after the NACK. */
    EX_WAIT_FLAG(I2C_StopDetected(I2C1, &stop), stop, 5U);
    EX_CHECK(I2C_ClearStop(I2C1));
    return present;
}

int main(void)
{
    uint32_t found = 0U;
    uint8_t address;

    Ex_Init();
    Ex_Print("\r\nex_i2c: scanning I2C1 (PB8 SCL, PB9 SDA) at 100 kHz\r\n");
    I2cPins();

    EX_CHECK(RCC_EnablePeriphClock(RCC_PERIPH_I2C1));
    EX_CHECK(RCC_SetKernelClock(RCC_KERNEL_I2C1, RCC_KSRC_PCLK)); /* also the reset value */
    EX_CHECK(I2C_MasterInit(I2C1, I2C_TIMINGR_100K_16MHZ));

    for (address = 0x08U; address <= 0x77U; address++)
    {
        if (Probe(address) != 0U)
        {
            Ex_Print("  device at ");
            Ex_PrintHex(address);
            Ex_Print("\r\n");
            found++;
        }
    }
    Ex_Print("scan done, devices: ");
    Ex_PrintU32(found);
    Ex_Print("\r\n");

    for (;;)
    {
        Ex_LedToggle();
        Ex_DelayMs(500U);
    }
}
