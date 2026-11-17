/* SPDX-License-Identifier: MIT */
/* DMA controller channel driver, RM0440 section 12. Channel count per controller (6 or 8)
 * comes from CMSIS through Private/stm32g4_dma_priv.h, shared with DMAMUX.
 * Public contract: stm32g4_dma.h. */
#include "stm32g4_dma.h"
#include "stm32g4_dma_priv.h"

/* TCIF | HTIF | TEIF in a channel's ISR nibble (GIF is their OR). */
#define DMA_STALE_FLAGS 0xEU

/*
 * DMA register operations are intentionally non-blocking.
 * Channel address decoding is shared with DMAMUX; no clock is enabled here.
 */
static uint8_t Dma_ChannelInfo(const DMA_Channel_TypeDef *channel, DMA_TypeDef **controller,
                               uint32_t *local_index)
{
    uint32_t global_index;

    return STM32G4_DmaChannelLocation(channel, controller, local_index, &global_index);
}

SPL_Status DMA_Init(DMA_Channel_TypeDef *ch, const DMA_InitTypeDef *cfg)
{
    DMA_TypeDef *controller;
    uint32_t index;
    uint32_t ccr;

    /* Structural channel decoding also computes the controller and index. */
    if (Dma_ChannelInfo(ch, &controller, &index) == 0U)
    {
        return SPL_ERR_PARAM;
    }
    SPL_REQUIRE(cfg != (const DMA_InitTypeDef *)0, SPL_ERR_PARAM);
    /* Null config could read unrelated memory or fault. */
    SPL_REQUIRE(cfg->count != 0U, SPL_ERR_PARAM);
    /* A zero length does not initiate a transfer. */
    SPL_REQUIRE(cfg->peripheral_width <= 2U, SPL_ERR_PARAM);
    SPL_REQUIRE(cfg->memory_width <= 2U, SPL_ERR_PARAM);
    /* Width 3 is reserved; shifting it can alter adjacent CCR fields. */
    SPL_REQUIRE(cfg->direction <= 2U, SPL_ERR_PARAM);
    SPL_REQUIRE(cfg->priority <= 3U, SPL_ERR_PARAM);
    /* Unsupported direction / priority can select a different operation. */
    SPL_REQUIRE(cfg->peripheral_increment <= 1U, SPL_ERR_PARAM);
    SPL_REQUIRE(cfg->memory_increment <= 1U, SPL_ERR_PARAM);
    SPL_REQUIRE(cfg->circular <= 1U, SPL_ERR_PARAM);
    /* Nonzero boolean values act like enabled. */
    SPL_REQUIRE(cfg->peripheral_address != 0U, SPL_ERR_PARAM);
    SPL_REQUIRE(cfg->memory_address != 0U, SPL_ERR_PARAM);
    /* DMA to address zero may read/write unintended memory. */
    SPL_REQUIRE((cfg->peripheral_address & ((1U << cfg->peripheral_width) - 1U)) == 0U,
                SPL_ERR_PARAM);
    SPL_REQUIRE((cfg->memory_address & ((1U << cfg->memory_width) - 1U)) == 0U, SPL_ERR_PARAM);
    /* Unaligned DMA addresses can silently target different bytes. */
    SPL_REQUIRE((cfg->direction != 2U) || (cfg->circular == 0U), SPL_ERR_PARAM);
    /* Memory-to-memory + circular is not a supported DMA configuration. */
    SPL_REQUIRE((ch->CCR & DMA_CCR_EN) == 0U, SPL_ERR_STATE);
    /* Active channel configuration changes may corrupt an in-flight transfer. */

    ccr = ((uint32_t)cfg->peripheral_width << DMA_CCR_PSIZE_Pos) |
          ((uint32_t)cfg->memory_width << DMA_CCR_MSIZE_Pos) |
          ((uint32_t)cfg->priority << DMA_CCR_PL_Pos);

    if (cfg->direction == 1U)
    {
        ccr |= DMA_CCR_DIR;
    }
    if (cfg->direction == 2U)
    {
        ccr |= DMA_CCR_MEM2MEM;
    }
    if (cfg->peripheral_increment != 0U)
    {
        ccr |= DMA_CCR_PINC;
    }
    if (cfg->memory_increment != 0U)
    {
        ccr |= DMA_CCR_MINC;
    }
    if (cfg->circular != 0U)
    {
        ccr |= DMA_CCR_CIRC;
    }

    ch->CCR = ccr;
    ch->CNDTR = cfg->count;
    ch->CPAR = cfg->peripheral_address;
    ch->CMAR = cfg->memory_address;

    /* IFCR is W1C. Retire completion/error flags from the old transfer.
     * Configuring a disabled channel starts a new transfer epoch. */
    controller->IFCR = (uint32_t)0xFU << (index * 4U);

    return SPL_OK;
}

SPL_Status DMA_Enable(DMA_Channel_TypeDef *ch, uint8_t enable)
{
    DMA_TypeDef *controller;
    uint32_t index;

    if (Dma_ChannelInfo(ch, &controller, &index) == 0U)
    {
        return SPL_ERR_PARAM;
    }
    SPL_REQUIRE(enable <= 1U, SPL_ERR_PARAM);
    /* Nonzero values request enable. */
    SPL_REQUIRE((enable == 0U) || (ch->CNDTR != 0U), SPL_ERR_STATE);
    /* A zero count cannot progress. */
    SPL_REQUIRE((enable == 0U) || (((controller->ISR >> (index * 4U)) & DMA_STALE_FLAGS) == 0U),
                SPL_ERR_STATE);
    /* Old completion/error flags can trigger a false interrupt on restart. */

    if (enable != 0U)
    {
        ch->CCR |= DMA_CCR_EN;
    }
    else
    {
        ch->CCR &= ~DMA_CCR_EN;
    }
    return SPL_OK;
}

SPL_Status DMA_DeInit(DMA_Channel_TypeDef *ch)
{
    DMA_TypeDef *controller;
    uint32_t index;

    if (Dma_ChannelInfo(ch, &controller, &index) == 0U)
    {
        return SPL_ERR_PARAM;
    }
    /* RM0440 12.6.3: CCR fields other than EN are writable only with EN=0. */
    ch->CCR &= ~DMA_CCR_EN;
    /* Read back: reprogram only once the channel is disabled (CCL-2 DMA-04). EN still 1 ->
     * BUSY (the channel ends the running beat by itself; METHOD 1.14, same code as eSPL G0),
     * nothing else written. */
    if ((ch->CCR & DMA_CCR_EN) != 0U)
    {
        return SPL_BUSY;
    }
    ch->CCR = 0U;
    ch->CNDTR = 0U;
    ch->CPAR = 0U;
    ch->CMAR = 0U;
    /* IFCR is W1C; GIF is safe now that EN=0 (ERRATA ES0431 2.3.1 / ES0430 2.3.1 /
     * ES0523 2.3.1). */
    controller->IFCR = (uint32_t)0xFU << (index * 4U);
    return SPL_OK;
}

SPL_Status DMA_SetCount(DMA_Channel_TypeDef *ch, uint16_t count)
{
    DMA_TypeDef *controller;
    uint32_t index;

    /* Structural channel decoding must remain present in every level. */
    if (Dma_ChannelInfo(ch, &controller, &index) == 0U)
    {
        return SPL_ERR_PARAM;
    }
    SPL_REQUIRE(count != 0U, SPL_ERR_PARAM); /* Zero count cannot transfer */
    (void)controller;
    (void)index;

    SPL_REQUIRE((ch->CCR & DMA_CCR_EN) == 0U, SPL_ERR_STATE);
    /* CNDTR write while EN=1 is ignored or disturbs active DMA. */

    ch->CNDTR = count;
    return SPL_OK;
}

SPL_Status DMA_Remaining(DMA_Channel_TypeDef *ch, uint16_t *remaining)
{
    DMA_TypeDef *controller;
    uint32_t index;

    SPL_REQUIRE(remaining != (uint16_t *)0, SPL_ERR_PARAM);
    /* Invalid output pointer may corrupt memory or fault. */
    if (Dma_ChannelInfo(ch, &controller, &index) == 0U)
    {
        return SPL_ERR_PARAM;
    }
    (void)controller;
    (void)index;

    *remaining = (uint16_t)ch->CNDTR;
    return SPL_OK;
}

SPL_Status DMA_Flags(DMA_Channel_TypeDef *ch, uint8_t *flags)
{
    DMA_TypeDef *controller;
    uint32_t index;

    SPL_REQUIRE(flags != (uint8_t *)0, SPL_ERR_PARAM);
    /* Invalid output pointer may corrupt memory or fault. */
    if (Dma_ChannelInfo(ch, &controller, &index) == 0U)
    {
        return SPL_ERR_PARAM;
    }

    /* Each DMA channel occupies four consecutive status bits in ISR. */
    *flags = (uint8_t)((controller->ISR >> (index * 4U)) & 0xFU);
    return SPL_OK;
}

SPL_Status DMA_ClearFlags(DMA_Channel_TypeDef *ch, uint8_t flags)
{
    DMA_TypeDef *controller;
    uint32_t index;

    if (Dma_ChannelInfo(ch, &controller, &index) == 0U)
    {
        return SPL_ERR_PARAM;
    }
    SPL_REQUIRE(flags <= 15U, SPL_ERR_PARAM);
    /* An oversized W1C mask can clear an adjacent channel's flags. */

    /* ERRATA ES0431 2.3.1 / ES0430 2.3.1 / ES0523 2.3.1: CGIFx in the cycle of a transfer error
     * defeats the automatic channel disable and loses TEIFx: refuse GIF while the channel is
     * enabled. */
    SPL_REQUIRE(((flags & DMA_FLAG_GIF) == 0U) || ((ch->CCR & DMA_CCR_EN) == 0U), SPL_ERR_STATE);
    /* Errata prohibits GIF clear while the DMA channel is enabled. */

    /* IFCR is write-one-to-clear: do not use read-modify-write. */
    controller->IFCR = ((uint32_t)flags) << (index * 4U);
    /* Read back: the clear reaches the peripheral before return (METHOD 1.7, issue #23). */
    (void)controller->ISR;
    return SPL_OK;
}

SPL_Status DMA_Interrupts(DMA_Channel_TypeDef *ch, uint8_t mask)
{
    DMA_TypeDef *controller;
    uint32_t index;
    uint32_t ccr;
    uint32_t irq_bits;
    const uint32_t irq_mask = DMA_CCR_TCIE | DMA_CCR_HTIE | DMA_CCR_TEIE;

    if (Dma_ChannelInfo(ch, &controller, &index) == 0U)
    {
        return SPL_ERR_PARAM;
    }
    SPL_REQUIRE(mask <= 7U, SPL_ERR_PARAM);
    /* Excess interrupt mask bits are discarded by irq_mask. */
    (void)controller;
    (void)index;

    ccr = ch->CCR;
    SPL_REQUIRE((ccr & DMA_CCR_EN) == 0U, SPL_ERR_STATE);
    /* Configuration writes to an enabled DMA channel are not permitted. */

    /* API: TC=bit0, HT=bit1, TE=bit2. CCR starts at TCIE=bit1. */
    irq_bits = ((uint32_t)mask << DMA_CCR_TCIE_Pos) & irq_mask;
    ch->CCR = (ccr & ~irq_mask) | irq_bits;

    return SPL_OK;
}
