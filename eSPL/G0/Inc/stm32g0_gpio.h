#ifndef STM32G0_GPIO_H
#define STM32G0_GPIO_H
#include <stdint.h>
#include "stm32g0xx.h"
#include "stm32g0_status.h"

/* GPIO driver (RM0444 Rev 6 section 7, Docs/v0.2_GPIO_CONTRACT.md). Ports GPIOA..GPIOF.
 * Ported from the STM32G4 SPL (same GPIO IP); difference: analog mode with any pull is Reserved
 * on G0 (RM0444 Table 44) -> SPL_ERR_PARAM.
 * The port clock (RCC_IOPENR) must be enabled by the caller (RCC_GpioClockEnable).
 * ERRATA ES0548 2.2.6: toggling PC13 (input or output) disturbs the LSE crystal; no workaround:
 *   keep PC13 static while LSE runs.
 * ERRATA ES0548 2.3.1 (silicon A only): after wake-up from Standby, pins with the wake-up
 *   capability enabled cannot be reconfigured: disable that capability first (PWR). */

#ifdef __cplusplus
extern "C"
{
#endif

typedef enum
{
    GPIO_MODE_INPUT = 0,
    GPIO_MODE_OUTPUT = 1,
    GPIO_MODE_AF = 2,
    GPIO_MODE_ANALOG = 3
} GPIO_Mode;
typedef enum
{
    GPIO_OUTPUT_PUSH_PULL = 0,
    GPIO_OUTPUT_OPEN_DRAIN = 1
} GPIO_OutputType;
typedef enum
{
    GPIO_SPEED_VERY_LOW = 0,
    GPIO_SPEED_LOW = 1,
    GPIO_SPEED_HIGH = 2,
    GPIO_SPEED_VERY_HIGH = 3
} GPIO_Speed;
typedef enum
{
    GPIO_NO_PULL = 0,
    GPIO_PULL_UP = 1,
    GPIO_PULL_DOWN = 2
} GPIO_Pull;

typedef struct
{
    uint32_t pins;          /* nonzero mask, pins 0..15 */
    uint32_t mode;          /* GPIO_Mode */
    uint32_t output_type;   /* GPIO_OutputType (output and AF modes) */
    uint32_t speed;         /* GPIO_Speed */
    uint32_t pull;          /* GPIO_Pull; must be GPIO_NO_PULL in analog mode */
    uint32_t alternate;     /* 0..7 (AFSEL is 4 bits but G0 defines AF0..AF7); AF mode only */
    uint32_t initial_level; /* 0 or 1, applied via BSRR before MODER in output mode */
} GPIO_InitTypeDef;

/* Fills the reset-like default: input, push-pull, very low speed, no pull, AF0, level 0. */
void GPIO_StructInit(GPIO_InitTypeDef *cfg);
/* Configures the selected pins. Order per pin: OTYPER, OSPEEDR, PUPDR, AFR (AF mode), BSRR
 * (output mode), MODER last. A selected pin locked by GPIOx_LCKR (LCKK = 1) -> SPL_ERR_STATE,
 * nothing written. Not concurrency-safe for the same port (read-modify-write). */
/* Seviye: PARAM[S,n] STATE[S] */
SPL_Status GPIO_Init(GPIO_TypeDef *port, const GPIO_InitTypeDef *cfg);
/* Atomic set (high = 1) or reset (high = 0) through BSRR. Safe from any context. */
/* Seviye: PARAM[S,n] */
SPL_Status GPIO_Write(GPIO_TypeDef *port, uint16_t pins, uint8_t high);
/* value = IDR & pins. */
/* Seviye: PARAM[S,n] */
SPL_Status GPIO_Read(GPIO_TypeDef *port, uint16_t pins, uint16_t *value);

#ifdef __cplusplus
}
#endif
#endif /* STM32G0_GPIO_H */
