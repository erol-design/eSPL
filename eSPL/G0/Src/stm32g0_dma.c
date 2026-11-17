/* SPDX-License-Identifier: MIT */
/* DMA channel driver implementation (RM0444 Rev 6 section 10); public contract in
 * stm32g0_dma.h. Ported from STM32G4 SPL v0.13.3 (Autometry_Mcu_Spl 130de7a); reviewed against
 * RM0444 Rev 6 section 10: same channel registers and ISR/IFCR layout, G0B1 channel map in
 * stm32g0_dma_priv.h. Channel address decoding is shared with DMAMUX.
 */
/* Library unit: this unit also emits the spl_check.h mismatch-guard reference; the only unit
 * that does not is stm32g0_check.c (SPL_CHECK_LEVEL_DEFINER). BULGU-02, CHECK_LEVEL.md 3.2. */
#define SPL_LIBRARY_BUILD 1
#include "stm32g0_dma.h"
#include "stm32g0_dma_priv.h"

/*
 * Check level (A3, Docs/CHECK_LEVEL.md 5.4 DMA table): SPL_REQUIRE = checked at FULL only (not
 * compiled at FAST). The channel location (controller, index) is found at every level by a pure
 * address computation, independent of validation. Ordering: all macros come before the first MMIO
 * write (METHOD 1.3).
 */

/* Channel valid: an invalid channel pointer writes to another peripheral's register and,
 * through the IFCR shift, clears the flags of a neighbouring channel (silent corruption). */
#if SPL_CHECK_LEVEL == SPL_CHECK_FULL
/* Single copy so that it is not inlined into each of the 8 APIs (FULL size, CHECK_LEVEL.md 7.2).
 * Used only in validation; not compiled in FAST (no unused function). Pure. */
SPL_NOINLINE static uint32_t Dma_IsChannel(const DMA_Channel_TypeDef *ch)
{
    return STM32G0_DmaIsChannel(ch);
}
#define DMA_CHANNEL_OK(ch) (Dma_IsChannel(ch) != 0U)
#else
#define DMA_CHANNEL_OK(ch) (STM32G0_DmaIsChannel(ch) != 0U) /* not evaluated, checks are off */
#endif

/* Location (controller + index) in one shared copy: inlined into every API, the base address
 * constants would be reloaded in each function (FULL +172 B, CHECK_LEVEL.md 7.2). Does not
 * validate; writes an output, so it is not used in an SPL_REQUIRE* condition (PURE CHECK). */
SPL_NOINLINE static DMA_TypeDef *Dma_Locate(const DMA_Channel_TypeDef *ch, uint32_t *index)
{
    *index = STM32G0_DmaLocalIndex(ch);
    return STM32G0_DmaController(ch);
}

/* The channel's 4 bits in ISR (GIF, TCIF, HTIF, TEIF). Pure: the ISR read has no side effect, no
 * output (allowed in Tools/pure_checks.txt); shared by the stale-flag check of DMA_Enable and by
 * DMA_Flags.
 */
SPL_NOINLINE static uint32_t Dma_ChannelIsr(const DMA_Channel_TypeDef *ch)
{
    uint32_t index;
    const DMA_TypeDef *controller = Dma_Locate(ch, &index);

    return (controller->ISR >> (index * 4U)) & 0xFU;
}

SPL_Status DMA_Init(DMA_Channel_TypeDef *ch, const DMA_InitTypeDef *cfg)
{
    DMA_TypeDef *controller;
    uint32_t index;
    uint32_t ccr;

    /* Validate everything before the first peripheral register write. */
    SPL_REQUIRE(DMA_CHANNEL_OK(ch), SPL_ERR_PARAM);
    SPL_REQUIRE(cfg != (const DMA_InitTypeDef *)0, SPL_ERR_PARAM);
    /* count = 0: the channel never transfers, the error is visible at once. */
    SPL_REQUIRE(cfg->count != 0U, SPL_ERR_PARAM);
    /* Width > 2: reserved encoding (11) in PSIZE / MSIZE or overflow into a neighbouring field
     *. */
    SPL_REQUIRE((cfg->peripheral_width <= 2U) && (cfg->memory_width <= 2U), SPL_ERR_PARAM);
    /* direction > 2: the direction silently becomes peripheral to memory. */
    SPL_REQUIRE(cfg->direction <= 2U, SPL_ERR_PARAM);
    /* priority > 3: the bit overflowing PL sets MEM2MEM, the channel copies memory to memory
     *. */
    SPL_REQUIRE(cfg->priority <= 3U, SPL_ERR_PARAM);
    /* 0/1 flags: a value above 1 acts as 1, no other bit is written. */
    SPL_REQUIRE((cfg->peripheral_increment <= 1U) && (cfg->memory_increment <= 1U) &&
                    (cfg->circular <= 1U),
                SPL_ERR_PARAM);
    /* Address 0: the DMA silently reads or writes the vector table region. */
    SPL_REQUIRE((cfg->peripheral_address != 0U) && (cfg->memory_address != 0U), SPL_ERR_PARAM);
    /* Transfer widths are encoded as 0=8, 1=16, 2=32 bits. Unaligned address: the DMA ignores the
     * low bits, the transfer goes to another address. The width check comes before this
     * one (shift). */
    SPL_REQUIRE(((cfg->peripheral_address & ((1U << cfg->peripheral_width) - 1U)) == 0U) &&
                    ((cfg->memory_address & ((1U << cfg->memory_width) - 1U)) == 0U),
                SPL_ERR_PARAM);
    /* RM0444 10.3: MEM2MEM and circular mode cannot be used together. */
    SPL_REQUIRE((cfg->direction != 2U) || (cfg->circular == 0U), SPL_ERR_PARAM);
    /* Reconfiguring an active channel would corrupt an in-flight transfer. */
    SPL_REQUIRE((ch->CCR & DMA_CCR_EN) == 0U, SPL_ERR_STATE);

    controller = Dma_Locate(ch, &index);
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
    SPL_REQUIRE(DMA_CHANNEL_OK(ch), SPL_ERR_PARAM);
    /* enable > 1 acts as enable. */
    SPL_REQUIRE(enable <= 1U, SPL_ERR_PARAM);
    /* A channel enabled with CNDTR = 0 never transfers; no TC comes, the error is visible at once
     *. */
    SPL_REQUIRE((enable == 0U) || (ch->CNDTR != 0U), SPL_ERR_STATE);
    /* v0.4 (G4 v0.13.4 #9, DMA-08a): a stale TC/HT/TE flag of the previous transfer would raise
     * an enabled interrupt at once: the caller clears it with DMA_ClearFlags first (spurious
     * interrupt). The ISR read has no side effect. */
    SPL_REQUIRE((enable == 0U) || ((Dma_ChannelIsr(ch) & DMA_STALE_FLAGS) == 0U), SPL_ERR_STATE);

    if (enable != 0U)
    {
        /* Normal memory writes (buffer) complete before the channel starts: if LTO inlines the
         * code that fills the buffer into this function, the compiler cannot move the writes
         * after EN. */
        /* Barrier: buffer writes complete before the channel starts (METHOD 1.7, issue #24). */
        __DMB();
        ch->CCR |= DMA_CCR_EN;
    }
    else
    {
        ch->CCR &= (uint32_t)~DMA_CCR_EN;
    }
    return SPL_OK;
}

SPL_Status DMA_DeInit(DMA_Channel_TypeDef *ch)
{
    DMA_TypeDef *controller;
    uint32_t index;

    SPL_REQUIRE(DMA_CHANNEL_OK(ch), SPL_ERR_PARAM);
    controller = Dma_Locate(ch, &index);
    /* RM0444 10.6.3: CCR fields other than EN are writable only with EN = 0. */
    ch->CCR &= (uint32_t)~DMA_CCR_EN;
    /* CCL-2 DMA-04: the other fields are rewritten only after EN = 0 is read back. A channel that
     * still reads EN = 1 is reported, not reconfigured (no wait loop: non-blocking API). */
    if ((ch->CCR & DMA_CCR_EN) != 0U)
    {
        return SPL_BUSY;
    }
    ch->CCR = 0U;
    ch->CNDTR = 0U;
    ch->CPAR = 0U;
    ch->CMAR = 0U;
    /* IFCR is W1C; GIF is safe now that EN = 0 (ERRATA ES0548 2.4.1). */
    controller->IFCR = (uint32_t)0xFU << (index * 4U);
    return SPL_OK;
}

SPL_Status DMA_SetCount(DMA_Channel_TypeDef *ch, uint16_t count)
{
    SPL_REQUIRE(DMA_CHANNEL_OK(ch), SPL_ERR_PARAM);
    SPL_REQUIRE(count != 0U, SPL_ERR_PARAM);
    /* CNDTR is read-only while EN = 1: the write is silently ignored. */
    SPL_REQUIRE((ch->CCR & DMA_CCR_EN) == 0U, SPL_ERR_STATE);

    ch->CNDTR = count;
    return SPL_OK;
}

SPL_Status DMA_Remaining(DMA_Channel_TypeDef *ch, uint16_t *remaining)
{
    SPL_REQUIRE(DMA_CHANNEL_OK(ch), SPL_ERR_PARAM);
    SPL_REQUIRE(remaining != (uint16_t *)0, SPL_ERR_PARAM);

    *remaining = (uint16_t)ch->CNDTR;
    return SPL_OK;
}

SPL_Status DMA_Flags(DMA_Channel_TypeDef *ch, uint8_t *flags)
{
    SPL_REQUIRE(DMA_CHANNEL_OK(ch), SPL_ERR_PARAM);
    SPL_REQUIRE(flags != (uint8_t *)0, SPL_ERR_PARAM);

    /* Each DMA channel occupies four consecutive status bits in ISR. */
    *flags = (uint8_t)Dma_ChannelIsr(ch);
    return SPL_OK;
}

SPL_Status DMA_ClearFlags(DMA_Channel_TypeDef *ch, uint8_t flags)
{
    DMA_TypeDef *controller;
    uint32_t index;

    SPL_REQUIRE(DMA_CHANNEL_OK(ch), SPL_ERR_PARAM);
    /* flags > 15: the extra bits clear the flags of the neighbouring channel. */
    SPL_REQUIRE(flags <= 15U, SPL_ERR_PARAM);
    /* ERRATA ES0548 2.4.1: CGIFx in the cycle of a transfer error
     * defeats the automatic channel disable and loses TEIFx: refuse GIF while the channel is
     * enabled. */
    SPL_REQUIRE(((flags & DMA_FLAG_GIF) == 0U) || ((ch->CCR & DMA_CCR_EN) == 0U), SPL_ERR_STATE);

    controller = Dma_Locate(ch, &index);
    /* IFCR is write-one-to-clear: do not use read-modify-write. */
    controller->IFCR = ((uint32_t)flags) << (index * 4U);
    /* Read back: the clear reaches the peripheral before return (METHOD 1.7, issue #23). */
    (void)controller->ISR;
    return SPL_OK;
}

SPL_Status DMA_Interrupts(DMA_Channel_TypeDef *ch, uint8_t mask)
{
    uint32_t ccr;
    uint32_t irq_bits;
    const uint32_t irq_mask = DMA_CCR_TCIE | DMA_CCR_HTIE | DMA_CCR_TEIE;

    SPL_REQUIRE(DMA_CHANNEL_OK(ch), SPL_ERR_PARAM);
    /* mask > 7: the extra bits are dropped by irq_mask, no other bit is written. */
    SPL_REQUIRE(mask <= 7U, SPL_ERR_PARAM);
    /* CCR is read once (previous behaviour in FULL): CCR fields are not writable while EN = 1,
     * the write is silently ignored. A read is not a write; it does not break the
     * METHOD 1.3 order. */
    ccr = ch->CCR;
    SPL_REQUIRE((ccr & DMA_CCR_EN) == 0U, SPL_ERR_STATE);

    /* API: TC=bit0, HT=bit1, TE=bit2. CCR starts at TCIE=bit1. */
    irq_bits = ((uint32_t)mask << DMA_CCR_TCIE_Pos) & irq_mask;
    ch->CCR = (ccr & ~irq_mask) | irq_bits;

    return SPL_OK;
}
