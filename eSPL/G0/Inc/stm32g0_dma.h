/* SPDX-License-Identifier: MIT */
#ifndef STM32G0_DMA_H
#define STM32G0_DMA_H
#include <stdint.h>
#include "stm32g0xx.h"
#include "stm32g0_status.h"

#ifdef __cplusplus
extern "C"
{
#endif
/* DMA controller channel driver (RM0444 Rev 6 section 10).
 * Scope: DMA1 channels 1..7 and DMA2 channels 1..5 on G0B1. Ported from the STM32G4 SPL v0.13.3
 * (same DMA IP); the channel map is G0-specific (Private/stm32g0_dma_priv.h).
 * Usage: DMA_Init (channel disabled), optional DMA_Interrupts, DMA_Enable(ch, 1), then the
 * peripheral request; DMA_Flags / DMA_ClearFlags in the handler; DMA_DeInit to release.
 * Common rules: no function waits; no static state; no clock is enabled here.
 * Caller duties (all APIs):
 *  - Clock: RCC_PERIPH_DMA1 / RCC_PERIPH_DMA2 (DMA1 also clocks DMAMUX).
 *  - Request: DMAMUX_SetRequest selects the request line; the peripheral's own DMA request enable
 *    is set through that peripheral's driver.
 *  - Interrupts: NVIC enable and handler of the channel's vector; the handler clears the flags it
 *    handled (DMA_ClearFlags), the hardware does not.
 *  - Concurrency: serialize calls per channel (ISR included).
 * Contract: Docs/v0.3_DMA_CONTRACT.md. */
typedef struct
{
    uint32_t peripheral_address;
    uint32_t memory_address;
    uint32_t direction;       /* 0: peripheral->memory; 1: memory->peripheral; 2: memory->memory */
    uint16_t count;           /* transfer items, 1..65535 */
    uint8_t peripheral_width; /* 0=8bit, 1=16bit, 2=32bit */
    uint8_t memory_width;
    uint8_t peripheral_increment;
    uint8_t memory_increment;
    uint8_t circular;
    uint8_t priority; /* 0..3 */
} DMA_InitTypeDef;
/* Caller: addresses are not range checked: peripheral_address must be the data
 * register of the peripheral the DMAMUX request belongs to, and the memory buffer must
 * hold count items of memory_width. */
/* Level: PARAM[F] STATE[F] */
SPL_Status DMA_Init(DMA_Channel_TypeDef *ch, const DMA_InitTypeDef *cfg);
/* enable=1: STATE when CNDTR=0 or (v0.4, G4 v0.13.4) a stale TC/HT/TE flag of this channel is set
 * (clear it with DMA_ClearFlags). enable=0 clears CCR.EN only; DMA_DeInit resets the channel. */
/* Caller: enable=1: keep the buffer valid until TC (circular: until the channel is
 * disabled), then enable the peripheral request. enable=0 stops a running transfer:
 * DMA_Remaining tells how many items were not moved. */
/* Level: PARAM[F] STATE[F] */
SPL_Status DMA_Enable(DMA_Channel_TypeDef *ch, uint8_t enable);
/* EN=0, then CCR (IRQ enables included), CNDTR, CPAR, CMAR = 0 and the channel's flags cleared
 * (IFCR, GIF too). The DMAMUX request line is not touched. BUSY when EN still reads 1 after the
 * EN=0 write: nothing else is written then (call again). */
/* Caller: stop the peripheral DMA request first; on BUSY nothing is reset, call again.
 * Release the buffer only after OK. */
/* Level: PARAM[F] BUSY[r] */
SPL_Status DMA_DeInit(DMA_Channel_TypeDef *ch);
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] STATE[F] */
SPL_Status DMA_SetCount(DMA_Channel_TypeDef *ch, uint16_t count);
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status DMA_Remaining(DMA_Channel_TypeDef *ch, uint16_t *remaining);
/* DMA flag mask uses 4 bits per channel: GIF=1 TCIF=2 HTIF=4 TEIF=8. */
#define DMA_FLAG_GIF 0x1U
/* TCIF | HTIF | TEIF in a channel's ISR nibble (GIF is their OR). */
#define DMA_STALE_FLAGS 0xEU
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status DMA_Flags(DMA_Channel_TypeDef *ch, uint8_t *flags);
/* ERRATA ES0548 2.4.1: GIF (bit0) with CCR.EN = 1 ->
 * SPL_ERR_STATE, nothing written; on an active channel clear TC/HT/TE (0xE) only, GIF after
 * the channel is off. */
/* Caller: clear the handled TC / HT / TE flags in the channel ISR (the hardware does
 * not clear them); GIF only after the channel is off. */
/* Level: PARAM[F] STATE[F] */
SPL_Status DMA_ClearFlags(DMA_Channel_TypeDef *ch, uint8_t flags);
/* Caller: clear stale TC / HT / TE flags (DMA_ClearFlags) before enabling them. */
/* Level: PARAM[F] STATE[F] */
SPL_Status DMA_Interrupts(DMA_Channel_TypeDef *ch, uint8_t mask); /* bit0 TC, bit1 HT, bit2 TE */

#ifdef __cplusplus
}
#endif
#endif
