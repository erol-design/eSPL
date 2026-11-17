/* SPDX-License-Identifier: MIT */
/* Internal DMA/DMAMUX device mapping for every STM32G0 SKU (v0.8). Not a public API.
 * RM0444 Rev 6 11.3.2 / Tables 49 and 54: DMAMUX1 channels 0..n-1 serve DMA1 channels 1..n
 * (n = 5 on G030/G031/G041, 7 elsewhere), the next 5 serve DMA2 channels 1..5 (group 0BX only;
 * contiguous, HWD-T2). G4 difference: no address hole in the DMAMUX channel map. */
#ifndef STM32G0_DMA_PRIV_H
#define STM32G0_DMA_PRIV_H

#include <stdint.h>
#include "stm32g0xx.h"
#include "stm32g0_sku.h"

#define STM32G0_DMA1_CHANNELS SPL_G0_DMA1_CHANNELS
#define STM32G0_DMA2_CHANNELS SPL_G0_DMA2_CHANNELS
#define STM32G0_DMA_CHANNEL_STRIDE 0x14U
#define STM32G0_DMAMUX_CHANNELS (STM32G0_DMA1_CHANNELS + STM32G0_DMA2_CHANNELS)
#define STM32G0_DMAMUX_VALID_MASK (((uint32_t)1U << STM32G0_DMAMUX_CHANNELS) - 1U)
/* Highest DMAMUX peripheral request input (RM0444 Table 55: 77 = USART6_TX). The numbering is
 * the same on every SKU; an input whose peripheral is absent never requests (not rejected).
 * But DMAREQ_ID is 7 bits only on G0B0 / G0B1 / G0C1 (CMSIS mask 0x7F); on the other nine SKUs
 * it is 6 bits (0x3F) and inputs 64..77 (LPUART2, SPI3, TIM4, USART5/6: 0BX peripherals) would be
 * masked into another real request (2026-11-14 capability audit). Limit = field width there. */
#if ((DMAMUX_CxCR_DMAREQ_ID_Msk >> DMAMUX_CxCR_DMAREQ_ID_Pos) >= 77U)
#define STM32G0_DMAMUX_REQUEST_MAX 77U
#else
#define STM32G0_DMAMUX_REQUEST_MAX (DMAMUX_CxCR_DMAREQ_ID_Msk >> DMAMUX_CxCR_DMAREQ_ID_Pos)
#endif

/* A3 (Docs/CHECK_LEVEL.md 4.2, 5.4): the producer of a value and its validator are separate. An
 * SPL_REQUIRE* condition must be PURE. The old STM32G0_DmaChannelLocation both validated and wrote
 * an output; its last user, DMAMUX, moved to the helpers below in A4 wave 2 (2026-11-13) and it
 * was removed. The helpers below write no output and do not touch MMIO. */

/* 1: channel is a DMA channel base of this SKU; 0: it is not. Pure (allowed by
 * Tools/pure_checks.txt). */
static inline uint32_t STM32G0_DmaIsChannel(const DMA_Channel_TypeDef *channel)
{
    const uintptr_t address = (uintptr_t)channel;
    uintptr_t base;
    uintptr_t span;

    if ((address >= (uintptr_t)DMA1_Channel1_BASE) &&
        (address <
         ((uintptr_t)DMA1_Channel1_BASE + (STM32G0_DMA1_CHANNELS * STM32G0_DMA_CHANNEL_STRIDE))))
    {
        base = (uintptr_t)DMA1_Channel1_BASE;
    }
#if defined(DMA2)
    else if ((address >= (uintptr_t)DMA2_Channel1_BASE) &&
             (address < ((uintptr_t)DMA2_Channel1_BASE +
                         (STM32G0_DMA2_CHANNELS * STM32G0_DMA_CHANNEL_STRIDE))))
    {
        base = (uintptr_t)DMA2_Channel1_BASE;
    }
#endif
    else
    {
        return 0U;
    }
    span = address - base;
    /* k = span / 0x14 without a division (span < 0x8C, exact at multiples of 0x14); if it is not a
     * base, k * 0x14 != span. */
    return ((((span * 0xCCCDU) >> 20U) * STM32G0_DMA_CHANNEL_STRIDE) == span) ? 1U : 0U;
}

/* Controller of the channel. Assumes a valid channel (validated separately, per level). */
static inline DMA_TypeDef *STM32G0_DmaController(const DMA_Channel_TypeDef *channel)
{
#if defined(DMA2)
    if ((uintptr_t)channel >= (uintptr_t)DMA2_Channel1_BASE)
    {
        return DMA2;
    }
#else
    (void)channel;
#endif
    return DMA1;
}

/* 0-based index of the channel in its controller (ISR / IFCR 4-bit group). Assumes a valid
 * channel. */
static inline uint32_t STM32G0_DmaLocalIndex(const DMA_Channel_TypeDef *channel)
{
    uintptr_t base = (uintptr_t)DMA1_Channel1_BASE;
#if defined(DMA2)
    if ((uintptr_t)channel >= (uintptr_t)DMA2_Channel1_BASE)
    {
        base = (uintptr_t)DMA2_Channel1_BASE;
    }
#endif
    return (uint32_t)((((uintptr_t)channel - base) * 0xCCCDU) >> 20U);
}

/* Global (= DMAMUX channel) index of the channel: DMA1 channels 0.., DMA2 channels after DMA1.
 * Assumes a valid channel (A4 wave 2, DMAMUX). */
static inline uint32_t STM32G0_DmaGlobalIndex(const DMA_Channel_TypeDef *channel)
{
#if defined(DMA2)
    if ((uintptr_t)channel >= (uintptr_t)DMA2_Channel1_BASE)
    {
        return STM32G0_DMA1_CHANNELS + STM32G0_DmaLocalIndex(channel);
    }
#endif
    return STM32G0_DmaLocalIndex(channel);
}

#endif /* STM32G0_DMA_PRIV_H */
