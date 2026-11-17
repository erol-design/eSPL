/* SPDX-License-Identifier: MIT */
#ifndef STM32G4_DMA_H
#define STM32G4_DMA_H
#include <stdint.h>
#include "stm32g4xx.h"
#include "stm32g4_status.h"

#ifdef __cplusplus
extern "C"
{
#endif
    /* DMA controller channel driver (RM0440 section 12).
     * Scope: DMA1 / DMA2 channels as present in CMSIS (6 per controller, 8 on SKUs that define
     * DMA1_Channel8); channel map in Private/stm32g4_dma_priv.h, shared with DMAMUX.
     * Usage: DMA_Init (channel disabled), optional DMA_Interrupts, DMA_Enable(ch, 1), then the
     * peripheral request; DMA_Flags / DMA_ClearFlags in the handler; DMA_DeInit to release.
     * Common rules: no function waits; no static state; no clock is enabled here.
     * Caller duties (all APIs):
     *  - Clock: RCC clocks of DMA1 / DMA2 and DMAMUX1.
     *  - Request: DMAMUX_SetRequest selects the request line; the peripheral's own DMA request
     *    enable is set through that peripheral's driver.
     *  - Concurrency: no other code writes a channel's CCR/CNDTR/CPAR/CMAR concurrently with
     *    these APIs; serialize calls per channel (ISR included).
     *  - Caller duty (CCL-2 DMA-06a, DMA-06b, DMA-07): the library has no DMA IRQ handler and
     *    sets no NVIC priority. The caller handles TE (TE interrupt, or DMA_Flags at transfer
     *    end), clears every enabled event flag in its handler (DMA_ClearFlags), and gives the DMA
     *    IRQ a priority no more urgent than the served peripheral's IRQ unless the DMA handler
     *    leaves that peripheral's state and registers alone.
     * Contract: Docs/ESPL_A3N_DMA_COMPLETE.md. */
    typedef struct
    {
        uint32_t peripheral_address;
        uint32_t memory_address;
        uint32_t direction; /* 0: peripheral->memory; 1: memory->peripheral; 2: memory->memory */
        uint16_t count;     /* transfer items, 1..65535 */
        uint8_t peripheral_width; /* 0=8bit, 1=16bit, 2=32bit */
        uint8_t memory_width;
        uint8_t peripheral_increment;
        uint8_t memory_increment;
        uint8_t circular;
        uint8_t priority; /* 0..3 */
    } DMA_InitTypeDef;
    /* Caller duty (CCL-2 DMA-01, KSM-29): memory_address/peripheral_address are the caller's;
     * the library checks only non-zero and alignment. Each buffer lies in memory the DMA can
     * access (RM0440 bus matrix), stays valid (static, or alive until the transfer ends) and is
     * neither written nor read by the caller until TC (DMA_Flags or interrupt) or DMA_DeInit.
     * Caller duty (CCL-2 DMA-10): widths match the peripheral data register and the buffer
     * element type; peripheral_increment 0 for a peripheral register, memory_increment 1 for a
     * buffer and 0 for a single fixed item, both 1 memory->memory. Only codes and alignment are
     * checked.
     * Caller duty (CCL-2 DMA-09): the EN=0 checks of DMA_Init, DMA_SetCount and DMA_Interrupts
     * (STATE) are checks and are removed at SPL_CHECK_LEVEL FAST; at that level call them
     * only with the channel disabled (DMA_DeInit OK, or EN read back 0 after DMA_Enable(ch, 0)). */
    /* Level: PARAM[F,r] STATE[F] */
    SPL_Status DMA_Init(DMA_Channel_TypeDef *ch, const DMA_InitTypeDef *cfg);
    /* enable=1: STATE when CNDTR=0 or (v0.13.4) a stale TC/HT/TE flag of this channel is set
     * (clear it with DMA_ClearFlags). enable=0 clears CCR.EN only; DMA_DeInit resets the channel.
     */
    /* Caller: enable=1: keep the buffer valid until TC (circular: until the channel is
     * disabled), then enable the peripheral request. enable=0 stops a running transfer:
     * DMA_Remaining tells how many items were not moved. */
    /* Level: PARAM[F,r] STATE[F] */
    SPL_Status DMA_Enable(DMA_Channel_TypeDef *ch, uint8_t enable);
    /* v0.13.4: EN=0, then CCR (IRQ enables included), CNDTR, CPAR, CMAR = 0 and the channel's
     * flags cleared (IFCR, GIF too). DMAMUX request line is not touched. EN read back after the
     * disable: still 1 -> BUSY (call again), no further write. */
    /* Caller: stop the peripheral DMA request first; on BUSY nothing is reset, call again.
     * Release the buffer only after OK. */
    /* Level: PARAM[r] BUSY[r] */
    SPL_Status DMA_DeInit(DMA_Channel_TypeDef *ch);
    /* Caller: channel disabled at SPL_CHECK_LEVEL FAST (CCL-2 DMA-09, see DMA_Init). */
    /* Level: PARAM[F,r] STATE[F] */
    SPL_Status DMA_SetCount(DMA_Channel_TypeDef *ch, uint16_t count);
    /* Caller duty (CCL-2 DMA-13): a circular buffer is read from the half the DMA is not
     * writing (HT/TC), from a copy, or only up to the position derived from DMA_Remaining. */
    /* Level: PARAM[F,r] */
    SPL_Status DMA_Remaining(DMA_Channel_TypeDef *ch, uint16_t *remaining);
/* DMA flag mask uses 4 bits per channel: GIF=1 TCIF=2 HTIF=4 TEIF=8. */
#define DMA_FLAG_GIF 0x1U
    /* Caller: none beyond the file-header duties. */
    /* Level: PARAM[F,r] */
    SPL_Status DMA_Flags(DMA_Channel_TypeDef *ch, uint8_t *flags);
    /* ERRATA ES0431 2.3.1 / ES0430 2.3.1 / ES0523 2.3.1: GIF (bit0) with CCR.EN = 1 ->
     * SPL_ERR_STATE, nothing written; on an active channel clear TC/HT/TE (0xE) only, GIF after
     * the channel is off. */
    /* Caller: clear the handled TC / HT / TE flags in the channel ISR (the hardware does
     * not clear them); GIF only after the channel is off. */
    /* Level: PARAM[F,r] STATE[F] */
    SPL_Status DMA_ClearFlags(DMA_Channel_TypeDef *ch, uint8_t flags);
    /* Caller: clear stale TC / HT / TE flags (DMA_ClearFlags) before enabling them; NVIC enable
     * of the channel vector (file header); channel disabled at FAST (CCL-2 DMA-09). */
    /* Level: PARAM[F,r] STATE[F] */
    SPL_Status DMA_Interrupts(DMA_Channel_TypeDef *ch,
                              uint8_t mask); /* bit0 TC, bit1 HT, bit2 TE */

#ifdef __cplusplus
}
#endif
#endif
