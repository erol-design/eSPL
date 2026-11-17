/* SPDX-License-Identifier: MIT */
#ifndef STM32G4_DMAMUX_H
#define STM32G4_DMAMUX_H
#include <stdint.h>
#include "stm32g4xx.h"
#include "stm32g4_status.h"

/* DMA request multiplexer (DMAMUX1, RM0440 Rev 9 section 13). Stateless, no waits.
 * Scope: request routing (DMAMUX_CxCR = request id) and the synchronization overrun flags.
 * Synchronization mode and the request generator are not implemented. Works on 6-channel
 * (12 mux lines) and 8-channel (16 mux lines) DMA SKUs; the DMA2 address hole of the
 * 6-channel SKUs is handled by the driver.
 * Usage: RCC DMAMUX1 + DMA clocks on -> DMA channel disabled -> DMAMUX_SetRequest -> DMA
 *   channel setup and enable (stm32g4_dma.h).
 * Caller duties (all APIs): the DMAMUX1 clock (RCC_AHB1ENR.DMAMUX1EN) must be enabled by the
 *   caller; the driver cannot read it.
 * ERRATA ES0431 2.4.1-2.4.4 / ES0430 2.4.1-2.4.4 / ES0523 2.4.1-2.4.4: not reachable:
 *   CxCR is written as the request id only (SE = 0, SPOL = 00, no sync mode) and the request
 *   generator is not implemented. A future sync API must set SE and SPOL in one write and
 *   keep SPOL = 00 while SE = 0 (2.4.4).
 * Contract: Docs/v0.11_LEGACY_CONTRACT.md (sections 4, 5a). */

#ifdef __cplusplus
extern "C" {
#endif
/* Routes request_id to the DMAMUX line of dma_channel (full CxCR write, sync fields 0).
 * PARAM: request_id > 127, unknown DMA channel. STATE: DMA channel enabled (CCR.EN = 1).
 * Caller: request_id must be a request that exists on this SKU (RM0440 Table 93); only the
 * 7-bit range is checked. The DMA clock must be on for the CCR.EN check to be meaningful. */
/* Level: PARAM[F,r] STATE[F] */
SPL_Status DMAMUX_SetRequest(DMA_Channel_TypeDef *dma_channel, uint8_t request_id);
/* Clears the synchronization overrun flags in mask (CFR, W1C, single write, then a CSR
 * read-back). PARAM: bit beyond the mux lines of this SKU. Mask 0 is allowed.
 * Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status DMAMUX_ClearOverrun(uint16_t mask);

#ifdef __cplusplus
}
#endif
#endif
