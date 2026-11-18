/* SPDX-License-Identifier: MIT */
/* Board helpers for the basic examples; see ex_board.h. */
#include "ex_board.h"

/* Per-character bound of the console wait: about 1 ms per character at 16 MHz, far more than
 * one frame at 115200 bit/s. Counted in loop passes, so it also works with interrupts off. */
#define EX_CONSOLE_SPIN 20000UL

volatile SPL_Status g_ex_fatal_status = SPL_OK;
volatile uint32_t g_ex_fatal_line;
static volatile uint32_t g_ex_tick_ms;
static uint8_t g_ex_console_ready;
static uint8_t g_ex_led;

void SysTick_Handler(void);

void SysTick_Handler(void)
{
    g_ex_tick_ms++;
}

uint32_t Ex_Millis(void)
{
    return g_ex_tick_ms;
}

uint8_t Ex_Expired(uint32_t start, uint32_t ms)
{
    return ((uint32_t)(g_ex_tick_ms - start) >= ms) ? 1U : 0U;
}

void Ex_DelayMs(uint32_t ms)
{
    uint32_t start = g_ex_tick_ms;

    while (Ex_Expired(start, ms) == 0U)
    {
    }
}

void Ex_Fatal(SPL_Status status, uint32_t line)
{
    g_ex_fatal_status = status;
    g_ex_fatal_line = line;
    if (g_ex_console_ready != 0U)
    {
        Ex_Print("\r\nFATAL status ");
        Ex_PrintU32((uint32_t)status);
        Ex_Print(" at line ");
        Ex_PrintU32(line);
        Ex_Print("\r\n");
    }
    __disable_irq();
    for (;;)
    {
    }
}

void Ex_CheckLine(SPL_Status status, uint32_t line)
{
    if (status != SPL_OK)
    {
        Ex_Fatal(status, line);
    }
}

void Ex_Led(uint8_t on)
{
    g_ex_led = (on != 0U) ? 1U : 0U;
    EX_CHECK(GPIO_Write(EX_LED_PORT, (uint16_t)(1U << EX_LED_PIN), g_ex_led));
}

void Ex_LedToggle(void)
{
    Ex_Led((uint8_t)(g_ex_led ^ 1U));
}

uint8_t Ex_ButtonPressed(void)
{
    uint16_t value = 0U;

    EX_CHECK(GPIO_Read(EX_BUTTON_PORT, (uint16_t)(1U << EX_BUTTON_PIN), &value));
    return (value == 0U) ? 1U : 0U; /* pressed = low */
}

void Ex_ConsoleInit(void)
{
    GPIO_InitTypeDef pins;
    USART_InitTypeDef uart;

    g_ex_console_ready = 0U;
    EX_CHECK(RCC_GpioClockEnable(GPIOA));
    EX_CHECK(RCC_EnablePeriphClock(RCC_PERIPH_USART2));
    GPIO_StructInit(&pins);
    pins.pins = (1UL << 2U) | (1UL << 3U); /* PA2 TX, PA3 RX */
    pins.mode = (uint32_t)GPIO_MODE_AF;
    pins.output_type = (uint32_t)GPIO_OUTPUT_PUSH_PULL;
    pins.speed = (uint32_t)GPIO_SPEED_LOW;
    pins.pull = (uint32_t)GPIO_PULL_UP; /* RX idles high also without the ST-LINK */
    pins.alternate = 1U;                /* AF1 = USART2 */
    EX_CHECK(GPIO_Init(GPIOA, &pins));

    /* USART_Init needs UE = 0 (a second call after a clock change). Disable returns BUSY until
     * the last frame has left the line (TC = 1). */
    EX_WAIT(USART_Disable(EX_CONSOLE), 10U);
    /* The driver computes BRR from kernel_hz: ask RCC for the real kernel clock (PCLK after reset,
     * it changes with the system clock). */
    EX_CHECK(RCC_GetUsartKernelClock(EX_CONSOLE, 0U, &uart.kernel_hz));
    uart.baudrate = EX_CONSOLE_BAUD;
    EX_CHECK(USART_Init(EX_CONSOLE, &uart));
    g_ex_console_ready = 1U;
}

void Ex_Init(void)
{
    GPIO_InitTypeDef pin;

    /* CMSIS: returns 1 when the reload value does not fit the 24-bit counter. */
    if (SysTick_Config(SystemCoreClock / 1000U) != 0U)
    {
        Ex_Fatal(SPL_ERR_PARAM, (uint32_t)__LINE__);
    }

    /* Caller duty of the GPIO driver: the port clock before GPIO_Init. */
    EX_CHECK(RCC_GpioClockEnable(EX_LED_PORT));
    GPIO_StructInit(&pin);
    pin.pins = 1UL << EX_LED_PIN;
    pin.mode = (uint32_t)GPIO_MODE_OUTPUT;
    pin.output_type = (uint32_t)GPIO_OUTPUT_PUSH_PULL;
    pin.speed = (uint32_t)GPIO_SPEED_LOW;
    pin.initial_level = 0U; /* written before MODER: no glitch on the pin */
    EX_CHECK(GPIO_Init(EX_LED_PORT, &pin));

    EX_CHECK(RCC_GpioClockEnable(EX_BUTTON_PORT));
    GPIO_StructInit(&pin);
    pin.pins = 1UL << EX_BUTTON_PIN;
    pin.mode = (uint32_t)GPIO_MODE_INPUT;
    pin.pull = (uint32_t)GPIO_NO_PULL; /* the board has the pull-up */
    EX_CHECK(GPIO_Init(EX_BUTTON_PORT, &pin));

    Ex_ConsoleInit();
}

static void Ex_PutChar(uint8_t c)
{
    uint32_t spin = 0U;
    SPL_Status status = USART_TryWrite(EX_CONSOLE, c);

    /* WOULD_BLOCK: TX register still full; it empties within one frame. */
    while ((status == SPL_WOULD_BLOCK) && (spin < EX_CONSOLE_SPIN))
    {
        spin++;
        status = USART_TryWrite(EX_CONSOLE, c);
    }
    if (status != SPL_OK)
    {
        g_ex_console_ready = 0U; /* do not print from Ex_Fatal through a broken console */
        Ex_Fatal((status == SPL_WOULD_BLOCK) ? SPL_ERR_TIMEOUT : status, (uint32_t)__LINE__);
    }
}

void Ex_Print(const char *text)
{
    while (*text != '\0')
    {
        Ex_PutChar((uint8_t)*text);
        text++;
    }
}

void Ex_PrintU32(uint32_t value)
{
    char digits[10];
    uint32_t n = 0U;

    do
    {
        digits[n] = (char)('0' + (char)(value % 10U));
        value /= 10U;
        n++;
    } while (value != 0U);
    while (n > 0U)
    {
        n--;
        Ex_PutChar((uint8_t)digits[n]);
    }
}

void Ex_PrintHex(uint32_t value)
{
    static const char hex[] = "0123456789ABCDEF";
    uint32_t shift = 32U;

    Ex_Print("0x");
    while (shift > 0U)
    {
        shift -= 4U;
        Ex_PutChar((uint8_t)hex[(value >> shift) & 0xFU]);
    }
}

uint8_t Ex_GetChar(uint8_t *byte)
{
    uint32_t errors = 0U;
    SPL_Status status = USART_TryRead(EX_CONSOLE, byte);

    if (status == SPL_OK)
    {
        return 1U;
    }
    if (status == SPL_ERR_HW)
    {
        /* Overrun / framing / noise: the frame is lost. Report it, then clear the flags (driver
         * rule: capture before clearing). */
        EX_CHECK(USART_ErrorFlags(EX_CONSOLE, &errors));
        EX_CHECK(USART_ClearErrorFlags(EX_CONSOLE, errors));
        Ex_Print("\r\n[console rx error ");
        Ex_PrintHex(errors);
        Ex_Print("]\r\n");
        return 0U;
    }
    if (status != SPL_WOULD_BLOCK)
    {
        Ex_Fatal(status, (uint32_t)__LINE__);
    }
    return 0U;
}
