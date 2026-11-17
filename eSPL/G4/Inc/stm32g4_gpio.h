/* SPDX-License-Identifier: MIT */
#ifndef STM32G4_GPIO_H
#define STM32G4_GPIO_H
#include <stdint.h>
#include "stm32g4xx.h"
#include "stm32g4_status.h"

/* GPIO driver (RM0440 Rev 9 section 9). Ports GPIOA..GPIOF, plus GPIOG where CMSIS defines it.
 * Stateless, no waits; every API returns SPL_Status.
 * Usage: RCC port clock on -> GPIO_StructInit -> set every field -> GPIO_Init -> GPIO_Write /
 *   GPIO_Read.
 * Common rules: analog mode with pull-up is Reserved (RM0440 Table 58) -> SPL_ERR_PARAM; analog
 *   with pull-down is valid. Pins are configured one by one, MODER last.
 * Caller duties (all APIs): the port clock (RCC_AHB2ENR.GPIOxEN) must be enabled by the caller;
 *   the driver cannot read it. The errata notes on GPIO_Init are caller rules.
 * Contract: Docs/v0.11_LEGACY_CONTRACT.md (sections 5, 5a). */

#ifdef __cplusplus
extern "C" {
#endif
typedef enum { GPIO_MODE_INPUT=0, GPIO_MODE_OUTPUT=1, GPIO_MODE_AF=2,
    GPIO_MODE_ANALOG=3 } GPIO_Mode;
typedef enum { GPIO_OUTPUT_PUSH_PULL=0, GPIO_OUTPUT_OPEN_DRAIN=1 } GPIO_OutputType;
typedef enum { GPIO_SPEED_LOW=0, GPIO_SPEED_MEDIUM=1, GPIO_SPEED_HIGH=2,
    GPIO_SPEED_VERY_HIGH=3 } GPIO_Speed;
typedef enum { GPIO_NO_PULL=0, GPIO_PULL_UP=1, GPIO_PULL_DOWN=2 } GPIO_Pull;
typedef struct {
    uint32_t pins;           /* nonzero mask, GPIO pins 0..15 */
    uint32_t mode;           /* GPIO_Mode */
    uint32_t output_type;    /* GPIO_OutputType (output and AF modes) */
    uint32_t speed;          /* GPIO_Speed */
    uint32_t pull;           /* GPIO_Pull; GPIO_PULL_UP is rejected in analog mode */
    uint32_t alternate;      /* 0..15; used only in AF mode */
    uint32_t initial_level; /* 0 or 1, applied via BSRR before output MODER */
} GPIO_InitTypeDef;
/* Fills the reset-like default: input, push-pull, low speed, no pull, AF0, level 0.
 * Caller duty (CCL-2 GPIO-03): GPIO_Init writes every field to the registers; call
 * GPIO_StructInit (or zero-initialize) and set every field before each GPIO_Init call. */
void GPIO_StructInit(GPIO_InitTypeDef *cfg);
/* Configures the selected pins. Order per pin: OTYPER, OSPEEDR, PUPDR, AFR (AF mode), BSRR
 * (output mode), MODER last. A selected pin locked through GPIOx_LCKR (LCKK = 1) causes
 * SPL_ERR_STATE with no writes. Not concurrency-safe for the same port (read-modify-write).
 * Caller: serialise calls per port (read-modify-write); alternate comes from the datasheet AF
 * table (not validated per pin); open drain for shared lines such as I2C.
 * ERRATA ES0431 2.2.1 / ES0430 2.2.1 / ES0523 2.2.1: reconfiguring PB4 (NJTRST) loses a
 * 4-wire JTAG debugger; PB4 is not protected: use SWD, or leave PB4 as NJTRST.
 * ERRATA ES0431 2.2.8 / ES0430 2.2.8 / ES0523 2.2.6: LQFP/UFQFPN: toggling PC13 (input or
 * output) disturbs the LSE crystal; no workaround: keep PC13 static while LSE runs. */
/* Level: PARAM[F] STATE[F] */
SPL_Status GPIO_Init(GPIO_TypeDef *port, const GPIO_InitTypeDef *cfg);
/* Atomic set (high = 1) or reset (high = 0) through BSRR. Safe from any context.
 * Caller: the pins must be outputs (GPIO_Init) for the level to reach the pin. */
/* Level: PARAM[F] */
SPL_Status GPIO_Write(GPIO_TypeDef *port, uint16_t pins, uint8_t high);
/* value = IDR & pins.
 * Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status GPIO_Read(GPIO_TypeDef *port, uint16_t pins, uint16_t *value);

#ifdef __cplusplus
}
#endif
#endif
