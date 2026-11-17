#ifndef STM32G4_DMAMUX_H
#define STM32G4_DMAMUX_H
#include <stdint.h>
#include "stm32g4xx.h"
#include "stm32g4_status.h"

#ifdef __cplusplus
extern "C" {
#endif
/* DMA channel must be disabled while changing its request mux. G431 only.
 * ERRATA ES0431 2.4.1-2.4.4 / ES0430 2.4.1-2.4.4 / ES0523 2.4.1-2.4.4: not reachable:
 * CxCR is written as the request id only (SE = 0, SPOL = 00, no sync mode) and the request
 * generator is not implemented. A future sync API must set SE and SPOL in one write and
 * keep SPOL = 00 while SE = 0 (2.4.4). */
/* Level: PARAM[F,r] STATE[F] */
SPL_Status DMAMUX_SetRequest(DMA_Channel_TypeDef *dma_channel, uint8_t request_id);
/* Level: PARAM[F] */
SPL_Status DMAMUX_ClearOverrun(uint16_t mask);

#ifdef __cplusplus
}
#endif
#endif
