/* SPDX-License-Identifier: MIT */
/* Board helpers shared by the basic examples (Examples/Basic): 1 ms tick, LED, user button, text
 * console on the ST-LINK virtual COM port, and the status-check habit of eSPL applications.
 *
 * Default board: NUCLEO-G0B1RE (STM32G0B1RETx)
 *   LED LD4      PA5 (high = on)
 *   Button B1    PC13 (pressed = low, external pull-up on the board)
 *   Console      USART2, PA2 TX / PA3 RX, AF1, 115200 8N1 (ST-LINK virtual COM port)
 * For another board change the EX_* pin macros below.
 *
 * Every SPL call in the examples goes through EX_CHECK or EX_WAIT: a status other than SPL_OK
 * prints the status and the source line on the console and stops in Ex_Fatal (the status stays in
 * g_ex_fatal_status and g_ex_fatal_line for the debugger). An error stays visible; it is never
 * retried silently or hidden. */
#ifndef EX_BOARD_H
#define EX_BOARD_H

#include <stdint.h>
#include "stm32g0_spl.h"

#define EX_LED_PORT GPIOA
#define EX_LED_PIN 5U
#define EX_BUTTON_PORT GPIOC
#define EX_BUTTON_PIN 13U
#define EX_CONSOLE USART2
#define EX_CONSOLE_BAUD 115200UL

extern volatile SPL_Status g_ex_fatal_status;
extern volatile uint32_t g_ex_fatal_line;

/* SysTick 1 ms tick at SystemCoreClock, LED off, button input, console. Call first in main. */
void Ex_Init(void);
/* Console (re)init from the current USART2 kernel clock; call again after a clock change. */
void Ex_ConsoleInit(void);

/* Stops the application: prints the status and line, interrupts off, endless loop. Replace with
 * the product's safe state in a real application. */
void Ex_Fatal(SPL_Status status, uint32_t line);
void Ex_CheckLine(SPL_Status status, uint32_t line);
#define EX_CHECK(call) Ex_CheckLine((call), (uint32_t)__LINE__)

/* eSPL never waits inside a driver: BUSY / WOULD_BLOCK mean "try again later". EX_WAIT repeats
 * the call while it returns one of them and stops with SPL_ERR_TIMEOUT when timeout_ms runs out;
 * any other status than SPL_OK is fatal. The bound matters: a missing crystal or a stuck bus
 * would otherwise hang the loop forever. */
#define EX_WAIT(call, timeout_ms)                                                                  \
    do                                                                                             \
    {                                                                                              \
        uint32_t ex_t0_ = Ex_Millis();                                                             \
        SPL_Status ex_s_ = (call);                                                                 \
        while ((ex_s_ == SPL_BUSY) || (ex_s_ == SPL_WOULD_BLOCK))                                  \
        {                                                                                          \
            if (Ex_Expired(ex_t0_, (timeout_ms)) != 0U)                                            \
            {                                                                                      \
                ex_s_ = SPL_ERR_TIMEOUT;                                                           \
            }                                                                                      \
            else                                                                                   \
            {                                                                                      \
                ex_s_ = (call);                                                                    \
            }                                                                                      \
        }                                                                                          \
        Ex_CheckLine(ex_s_, (uint32_t)__LINE__);                                                   \
    } while (0)

/* Polls a query API (one that writes a uint8_t flag) until the flag is 1, bounded by
 * timeout_ms. Example: EX_WAIT_FLAG(RCC_GetPllReady(&ready), ready, 10U). */
#define EX_WAIT_FLAG(call, flag, timeout_ms)                                                       \
    do                                                                                             \
    {                                                                                              \
        uint32_t ex_t0_ = Ex_Millis();                                                             \
        (flag) = 0U;                                                                               \
        Ex_CheckLine((call), (uint32_t)__LINE__);                                                  \
        while ((flag) == 0U)                                                                       \
        {                                                                                          \
            if (Ex_Expired(ex_t0_, (timeout_ms)) != 0U)                                            \
            {                                                                                      \
                Ex_Fatal(SPL_ERR_TIMEOUT, (uint32_t)__LINE__);                                     \
            }                                                                                      \
            Ex_CheckLine((call), (uint32_t)__LINE__);                                              \
        }                                                                                          \
    } while (0)

uint32_t Ex_Millis(void);
/* 1 when ms milliseconds have passed since start (wrap-around safe). */
uint8_t Ex_Expired(uint32_t start, uint32_t ms);
void Ex_DelayMs(uint32_t ms);

void Ex_Led(uint8_t on);
void Ex_LedToggle(void);
uint8_t Ex_ButtonPressed(void);

/* Blocking console output (bounded per character). */
void Ex_Print(const char *text);
void Ex_PrintU32(uint32_t value);
void Ex_PrintHex(uint32_t value);
/* Non-blocking console input: 1 and *byte when a character was received, else 0. */
uint8_t Ex_GetChar(uint8_t *byte);

#endif /* EX_BOARD_H */
