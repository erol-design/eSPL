#ifndef STM32G0_DMA_H
#define STM32G0_DMA_H
#include <stdint.h>
#include "stm32g0xx.h"
#include "stm32g0_status.h"

#ifdef __cplusplus
extern "C"
{
#endif
/* DMA controller channels (RM0444 Rev 6 section 10, Docs/v0.3_DMA_CONTRACT.md): DMA1 channels 1..7
 * and DMA2 channels 1..5 on G0B1. Ported from the STM32G4 SPL v0.13.3 (same DMA IP); the channel
 * map is G0-specific (Private/stm32g0_dma_priv.h). The caller enables the DMA clock
 * (RCC_PERIPH_DMA1 / RCC_PERIPH_DMA2) and selects the request with DMAMUX_SetRequest. */
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
/* Seviye: PARAM[S,n] STATE[S] */
SPL_Status DMA_Init(DMA_Channel_TypeDef *ch, const DMA_InitTypeDef *cfg);
/* enable=1: STATE when CNDTR=0 or (v0.4, G4 v0.13.4) a stale TC/HT/TE flag of this channel is set
 * (clear it with DMA_ClearFlags). enable=0 clears CCR.EN only; DMA_DeInit resets the channel. */
/* Seviye: PARAM[S,n] STATE[S,n] */
SPL_Status DMA_Enable(DMA_Channel_TypeDef *ch, uint8_t enable);
/* EN=0, then CCR (IRQ enables included), CNDTR, CPAR, CMAR = 0 and the channel's flags cleared
 * (IFCR, GIF too). The DMAMUX request line is not touched. */
/* Seviye: PARAM[S] */
SPL_Status DMA_DeInit(DMA_Channel_TypeDef *ch);
/* Seviye: PARAM[S,n] STATE[S] */
SPL_Status DMA_SetCount(DMA_Channel_TypeDef *ch, uint16_t count);
/* Seviye: PARAM[S] */
SPL_Status DMA_Remaining(DMA_Channel_TypeDef *ch, uint16_t *remaining);
/* DMA flag mask uses 4 bits per channel: GIF=1 TCIF=2 HTIF=4 TEIF=8. */
#define DMA_FLAG_GIF 0x1U
/* TCIF | HTIF | TEIF in a channel's ISR nibble (GIF is their OR). */
#define DMA_STALE_FLAGS 0xEU
/* Seviye: PARAM[S] */
SPL_Status DMA_Flags(DMA_Channel_TypeDef *ch, uint8_t *flags);
/* ERRATA ES0548 2.4.1: GIF (bit0) with CCR.EN = 1 ->
 * SPL_ERR_STATE, nothing written; on an active channel clear TC/HT/TE (0xE) only, GIF after
 * the channel is off. */
/* Seviye: PARAM[S] STATE[S] */
SPL_Status DMA_ClearFlags(DMA_Channel_TypeDef *ch, uint8_t flags);
/* Seviye: PARAM[S,n] STATE[S] */
SPL_Status DMA_Interrupts(DMA_Channel_TypeDef *ch, uint8_t mask); /* bit0 TC, bit1 HT, bit2 TE */

#ifdef __cplusplus
}
#endif
#endif
