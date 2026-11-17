/* SPDX-License-Identifier: MIT */
/* eSPL STM32G4 project template, NUCLEO-G431RB.
 *
 * - SYSCLK = HCLK = PCLK1 = PCLK2 = 170 MHz from the 24 MHz HSE crystal (PLL, Range 1 boost)
 * - SysTick 1 ms interrupt
 * - LD2 (PA5) blinks at 1 Hz
 * - LPUART1 PA2/PA3 = ST-LINK virtual COM port, 115200 8N1: prints a banner, echoes input
 *
 * Every eSPL call returns SPL_Status. Any unexpected status goes to error_trap(), where LD2
 * blinks fast. Errors stay visible: nothing is silently retried or "corrected".
 *
 * Board values (HSE, pins, alternate functions) are for NUCLEO-G431RB (UM2505). Check them first
 * on another board. */
#include "stm32g4_spl.h"

#define BOARD_HSE_HZ 24000000UL /* X3 crystal */
#define BOARD_SYSCLK_HZ 170000000UL
#define BOARD_PLL_M 6U  /* 24 MHz / 6 = 4 MHz VCO input */
#define BOARD_PLL_N 85U /* 4 MHz * 85 = 340 MHz VCO */
#define BOARD_PLL_R 2U  /* 340 MHz / 2 = 170 MHz SYSCLK */

#define LED_PORT GPIOA
#define LED_PIN 5U
#define VCP_PORT GPIOA
#define VCP_TX_PIN 2U
#define VCP_RX_PIN 3U
#define VCP_AF 12U /* AF12 = LPUART1 on PA2/PA3 */
#define VCP_BAUD 115200UL

#define TX_TIMEOUT_MS 10U

static volatile uint32_t s_ms;

void SysTick_Handler(void)
{
    s_ms++;
}

static uint32_t now_ms(void)
{
    return s_ms;
}

/* 1 ms SysTick at the given HCLK. Called again after every HCLK change. */
static void tick_start(uint32_t hclk_hz)
{
    (void)SysTick_Config(hclk_hz / 1000UL);
}

static void delay_ms(uint32_t ms)
{
    uint32_t start = now_ms();
    while ((now_ms() - start) <= ms) /* "<=": a partial first tick does not shorten the delay */
    {
        __NOP();
    }
}

/* Fatal error: LD2 blinks at about 5 Hz forever. Busy loop, the clock may be unknown here. */
static void error_trap(void)
{
    volatile uint32_t i;

    __disable_irq();
    for (;;)
    {
        LED_PORT->ODR ^= (1UL << LED_PIN);
        for (i = 0U; i < 400000UL; i++)
        {
        }
    }
}

static void check(SPL_Status status)
{
    if (status != SPL_OK)
    {
        error_trap();
    }
}

/* Polls the ready flag until it is 1 or timeout_ms has elapsed. */
static void wait_ready(SPL_Status (*get)(uint8_t *), uint32_t timeout_ms)
{
    uint32_t start = now_ms();
    uint8_t ready = 0U;

    do
    {
        check(get(&ready));
    } while ((ready == 0U) && ((now_ms() - start) <= timeout_ms));
    if (ready == 0U)
    {
        error_trap();
    }
}

static SPL_Status get_hse_ready(uint8_t *ready)
{
    return RCC_GetOscReady(RCC_OSC_HSE, ready);
}

static void wait_sysclk_pll(uint32_t timeout_ms)
{
    uint32_t start = now_ms();
    RCC_SysclkTypeDef source = RCC_SYSCLK_HSI16;

    do
    {
        check(RCC_GetSysclkSource(&source));
    } while ((source != RCC_SYSCLK_PLL) && ((now_ms() - start) <= timeout_ms));
    if (source != RCC_SYSCLK_PLL)
    {
        error_trap();
    }
}

/* Reset clock (HSI16) -> 170 MHz. Order per RM0440 6.1.5 and 7.2.7: AHB /2 before boost, Range 1
 * boost, flash wait states, HSE, PLL, switch, >= 1 us wait, AHB /1. */
static void clock_init_170mhz(void)
{
    RCC_PllConfigTypeDef pll;
    uint32_t ws = 0U;

    check(RCC_EnablePeriphClock(RCC_PERIPH_PWR));
    check(RCC_SetAhbPrescaler(2U));
    tick_start(RCC_HSI16_HZ / 2UL);

    check(PWR_StartVoltageScaling(PWR_RANGE1_BOOST));
    wait_ready(PWR_GetVoltageScalingStatus, 100U);

    check(FLASH_CalcLatency(BOARD_SYSCLK_HZ, FLASH_RANGE1_BOOST, &ws));
    check(FLASH_SetLatency(ws));
    check(FLASH_ConfigAccelerator(1U, 1U, 1U)); /* prefetch, instruction and data cache */

    check(RCC_SetHseBypass(0U)); /* crystal, not an external clock input */
    check(RCC_StartOsc(RCC_OSC_HSE));
    wait_ready(get_hse_ready, 200U);

    pll.source = RCC_PLLSRC_HSE;
    pll.m = BOARD_PLL_M;
    pll.n = BOARD_PLL_N;
    pll.p = 2U;
    pll.q = 2U;
    pll.r = BOARD_PLL_R;
    pll.p_enable = 0U;
    pll.q_enable = 0U;
    pll.r_enable = 1U;
    pll.reserved = 0U;
    check(RCC_ConfigPll(&pll, BOARD_HSE_HZ));
    check(RCC_EnablePll());
    wait_ready(RCC_GetPllReady, 10U);

    check(RCC_SetSysclkSource(RCC_SYSCLK_PLL));
    wait_sysclk_pll(10U);
    tick_start(BOARD_SYSCLK_HZ / 2UL);
    delay_ms(1U); /* >= 1 us at 85 MHz before HCLK = 170 MHz */

    check(RCC_SetAhbPrescaler(1U));
    check(RCC_SetApbPrescalers(1U, 1U));
    tick_start(BOARD_SYSCLK_HZ);
    SystemCoreClock = BOARD_SYSCLK_HZ;
}

static void led_init(void)
{
    GPIO_InitTypeDef pin;

    check(RCC_GpioClockEnable(LED_PORT));
    GPIO_StructInit(&pin);
    pin.pins = (uint16_t)(1U << LED_PIN);
    pin.mode = GPIO_MODE_OUTPUT;
    check(GPIO_Init(LED_PORT, &pin));
}

/* LPUART1 runs from HSI16; the baud rate does not depend on SYSCLK. */
static void vcp_init(void)
{
    GPIO_InitTypeDef pin;
    USART_InitExTypeDef cfg;
    uint32_t start;
    uint8_t tx_ready = 0U;
    uint8_t rx_ready = 0U;

    check(RCC_GpioClockEnable(VCP_PORT));
    GPIO_StructInit(&pin);
    pin.pins = (uint16_t)(1U << VCP_TX_PIN);
    pin.mode = GPIO_MODE_AF;
    pin.alternate = VCP_AF;
    check(GPIO_Init(VCP_PORT, &pin));
    pin.pins = (uint16_t)(1U << VCP_RX_PIN);
    pin.pull = GPIO_PULL_UP;
    check(GPIO_Init(VCP_PORT, &pin));

    check(RCC_SetKernelClock(RCC_KERNEL_LPUART1, RCC_KCLK_HSI16));
    check(RCC_EnablePeriphClock(RCC_PERIPH_LPUART1));

    cfg.kernel_hz = RCC_HSI16_HZ;
    cfg.baudrate = VCP_BAUD;
    cfg.word_length = USART_WORDLENGTH_8B;
    cfg.parity = USART_PARITY_NONE;
    cfg.stop_bits = USART_STOPBITS_1;
    cfg.oversampling = USART_OVERSAMPLING_16;
    cfg.prescaler = USART_PRESC_DIV1;
    cfg.direction = USART_DIR_TX | USART_DIR_RX;
    cfg.flow_control = USART_FLOW_NONE;
    cfg.options = 0U;
    check(USART_InitEx(LPUART1, &cfg));
    check(USART_Enable(LPUART1));

    /* Wait for the transmitter and receiver enable acknowledge (TEACK/REACK). */
    start = now_ms();
    do
    {
        check(USART_Ready(LPUART1, &tx_ready, &rx_ready));
    } while (((tx_ready == 0U) || (rx_ready == 0U)) && ((now_ms() - start) <= 10U));
    if ((tx_ready == 0U) || (rx_ready == 0U))
    {
        error_trap();
    }
}

/* Non-blocking write with bounded retry: SPL_WOULD_BLOCK = "transmitter busy, try again". */
static void vcp_putc(char c)
{
    uint32_t start = now_ms();
    SPL_Status st;

    do
    {
        st = USART_TryWrite(LPUART1, (uint8_t)c);
    } while ((st == SPL_WOULD_BLOCK) && ((now_ms() - start) <= TX_TIMEOUT_MS));
    check(st);
}

static void vcp_puts(const char *s)
{
    while (*s != '\0')
    {
        vcp_putc(*s);
        s++;
    }
}

static void vcp_put_u32(uint32_t value)
{
    char buf[11];
    uint32_t i = 0U;

    do
    {
        buf[i] = (char)('0' + (value % 10U));
        value /= 10U;
        i++;
    } while (value != 0U);
    while (i > 0U)
    {
        i--;
        vcp_putc(buf[i]);
    }
}

int main(void)
{
    RCC_ClocksTypeDef clocks;
    uint32_t last_toggle;
    uint8_t led_on = 0U;
    uint8_t rx;
    SPL_Status st;

    tick_start(RCC_HSI16_HZ); /* reset clock: HSI16 */
    led_init();
    clock_init_170mhz();
    vcp_init();

    check(RCC_GetClocks(BOARD_HSE_HZ, &clocks));
    vcp_puts("\r\neSPL G431 template\r\nSYSCLK = ");
    vcp_put_u32(clocks.sysclk_hz);
    vcp_puts(" Hz\r\nType something, it is echoed back.\r\n");

    last_toggle = now_ms();
    for (;;)
    {
        if ((now_ms() - last_toggle) >= 500U)
        {
            last_toggle += 500U;
            led_on ^= 1U;
            check(GPIO_Write(LED_PORT, (uint16_t)(1U << LED_PIN), led_on));
        }

        st = USART_TryRead(LPUART1, &rx);
        if (st == SPL_OK)
        {
            vcp_putc((char)rx);
        }
        else if (st != SPL_WOULD_BLOCK)
        {
            /* SPL_ERR_HW: framing, noise, parity or overrun. A real application reads
             * USART_ErrorFlags() and decides; the template stops to keep the error visible. */
            error_trap();
        }
        else
        {
            /* no data */
        }
    }
}
