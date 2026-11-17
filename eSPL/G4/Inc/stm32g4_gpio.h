#ifndef STM32G4_GPIO_H
#define STM32G4_GPIO_H
#include <stdint.h>
#include "stm32g4xx.h"
#include "stm32g4_status.h"

#ifdef __cplusplus
extern "C" {
#endif
typedef enum { GPIO_MODE_INPUT=0, GPIO_MODE_OUTPUT=1, GPIO_MODE_AF=2, GPIO_MODE_ANALOG=3 } GPIO_Mode;
typedef enum { GPIO_OUTPUT_PUSH_PULL=0, GPIO_OUTPUT_OPEN_DRAIN=1 } GPIO_OutputType;
typedef enum { GPIO_SPEED_LOW=0, GPIO_SPEED_MEDIUM=1, GPIO_SPEED_HIGH=2, GPIO_SPEED_VERY_HIGH=3 } GPIO_Speed;
typedef enum { GPIO_NO_PULL=0, GPIO_PULL_UP=1, GPIO_PULL_DOWN=2 } GPIO_Pull;
typedef struct {
    uint32_t pins;           /* nonzero mask, GPIO pins 0..15 */
    uint32_t mode;
    uint32_t output_type;
    uint32_t speed;
    uint32_t pull;
    uint32_t alternate;      /* 0..15; used only in AF mode */
    uint32_t initial_level; /* 0 or 1, applied via BSRR before output MODER */
} GPIO_InitTypeDef;
/* Caller duty (CCL-2 GPIO-03): GPIO_Init writes every field to the registers; call
 * GPIO_StructInit (or zero-initialize) and set every field before each GPIO_Init call. */
void GPIO_StructInit(GPIO_InitTypeDef *cfg);
/* Clock must be enabled by caller. Configuration is not concurrency-safe.
 * A selected pin locked through GPIOx_LCKR causes SPL_ERR_STATE with no writes.
 * ERRATA ES0431 2.2.1 / ES0430 2.2.1 / ES0523 2.2.1: reconfiguring PB4 (NJTRST) loses a
 * 4-wire JTAG debugger; PB4 is not protected: use SWD, or leave PB4 as NJTRST.
 * ERRATA ES0431 2.2.8 / ES0430 2.2.8 / ES0523 2.2.6: LQFP/UFQFPN: toggling PC13 (input or
 * output) disturbs the LSE crystal; no workaround: keep PC13 static while LSE runs. */
/* Level: PARAM[F] STATE[F] */
SPL_Status GPIO_Init(GPIO_TypeDef *port, const GPIO_InitTypeDef *cfg);
/* Atomic set/reset using BSRR; clock must be enabled. */
/* Level: PARAM[F] */
SPL_Status GPIO_Write(GPIO_TypeDef *port, uint16_t pins, uint8_t high);
/* Level: PARAM[F] */
SPL_Status GPIO_Read(GPIO_TypeDef *port, uint16_t pins, uint16_t *value);

#ifdef __cplusplus
}
#endif
#endif
