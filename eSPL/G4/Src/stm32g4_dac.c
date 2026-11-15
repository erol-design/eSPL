#define SPL_LIBRARY_BUILD 1
#include <stddef.h>
#include "stm32g4_dac.h"

/* RM0440 22.4.5 / 22.7.16: HFSEL thresholds on the AHB clock. */
#define DAC_HFSEL_80MHZ 80000000UL
#define DAC_HFSEL_160MHZ 160000000UL

#define DAC_TSEL_MAX 15U
#define DAC_DHR12L_MAX 0xFFF0U
#define DAC_DHR12L_LOW_BITS 0x000FU

/* Per-channel bit masks, all taken from CMSIS. */
typedef struct
{
    uint32_t en;
    uint32_t cen;
    uint32_t ten;
    uint32_t tsel_msk;
    uint32_t tsel_pos;
    uint32_t wave;
    uint32_t mamp;
    uint32_t dmaen;
    uint32_t dmaudrie;
    uint32_t mode_msk;
    uint32_t mode_pos;
    uint32_t dmadouble;
    uint32_t sinformat;
    uint32_t rdy;
    uint32_t dmaudr;
    uint32_t swtrig;
} Dac_ChannelBits;

static uint8_t Dac_Valid(const DAC_TypeDef *dac)
{
    uint8_t valid = ((dac == DAC1) || (dac == DAC3)) ? 1U : 0U;
#if defined(DAC2)
    valid = ((valid != 0U) || (dac == DAC2)) ? 1U : 0U;
#endif
#if defined(DAC4)
    valid = ((valid != 0U) || (dac == DAC4)) ? 1U : 0U;
#endif
    return valid;
}

/* DAC3/DAC4: no GPIO connection, no output buffer (RM0440 22.3 table 183). */
static uint8_t Dac_InternalOnly(const DAC_TypeDef *dac)
{
    uint8_t internal = (dac == DAC3) ? 1U : 0U;
#if defined(DAC4)
    internal = ((internal != 0U) || (dac == DAC4)) ? 1U : 0U;
#endif
    return internal;
}

/* Validates instance and channel; on success *bits points to the channel masks. */
static SPL_Status Dac_Check(const DAC_TypeDef *dac, uint32_t channel, const Dac_ChannelBits **bits)
{
    static const Dac_ChannelBits dac_bits[2] = {
        {DAC_CR_EN1, DAC_CR_CEN1, DAC_CR_TEN1, DAC_CR_TSEL1_Msk, DAC_CR_TSEL1_Pos, DAC_CR_WAVE1_Msk,
         DAC_CR_MAMP1_Msk, DAC_CR_DMAEN1, DAC_CR_DMAUDRIE1, DAC_MCR_MODE1_Msk, DAC_MCR_MODE1_Pos,
         DAC_MCR_DMADOUBLE1, DAC_MCR_SINFORMAT1, DAC_SR_DAC1RDY, DAC_SR_DMAUDR1,
         DAC_SWTRIGR_SWTRIG1},
        {DAC_CR_EN2, DAC_CR_CEN2, DAC_CR_TEN2, DAC_CR_TSEL2_Msk, DAC_CR_TSEL2_Pos, DAC_CR_WAVE2_Msk,
         DAC_CR_MAMP2_Msk, DAC_CR_DMAEN2, DAC_CR_DMAUDRIE2, DAC_MCR_MODE2_Msk, DAC_MCR_MODE2_Pos,
         DAC_MCR_DMADOUBLE2, DAC_MCR_SINFORMAT2, DAC_SR_DAC2RDY, DAC_SR_DMAUDR2,
         DAC_SWTRIGR_SWTRIG2}};

    /* SAFE: reject invalid parameters before peripheral register access. */
    SPL_REQUIRE_SAFE(!((Dac_Valid(dac) == 0U) || ((channel != DAC_CHANNEL_1) && (channel != DAC_CHANNEL_2))), SPL_ERR_PARAM);
#if defined(DAC2)
    /* DAC2 is single-channel (RM0440 22.3). */
    if ((dac == DAC2) && (channel == DAC_CHANNEL_2))
    {
        return SPL_ERR_UNSUPPORTED;
    }
#endif
    *bits = &dac_bits[channel - 1U];
    return SPL_OK;
}

/* Writing data or a trigger is not allowed while ENx = 1 and DACxRDY = 0 (RM0440 22.4.3). */
static uint8_t Dac_NotReady(const DAC_TypeDef *dac, const Dac_ChannelBits *bits)
{
    return (((dac->CR & bits->en) != 0U) && ((dac->SR & bits->rdy) == 0U)) ? 1U : 0U;
}

SPL_Status DAC_ChannelConfig(DAC_TypeDef *dac, uint32_t channel,
                             const DAC_ChannelConfigTypeDef *config)
{
    const Dac_ChannelBits *bits = NULL;
    SPL_Status status = Dac_Check(dac, channel, &bits);
    uint32_t cr;
    uint32_t mcr;

    if (status != SPL_OK)
    {
        return status;
    }
    /* SAFE: reject invalid parameters before peripheral register access. */
    SPL_REQUIRE_SAFE(!((config == NULL) || (config->trigger_enable > 1U) || (config->trigger_sel > DAC_TSEL_MAX) ||
        ((config->trigger_enable == 0U) && (config->trigger_sel != 0U)) ||
        (config->mode > DAC_MODE_INT_UNBUFFERED) || (config->dma_enable > 1U) ||
        (config->dma_underrun_irq > 1U)), SPL_ERR_PARAM);
    /* SAFE: reject invalid parameters before peripheral register access. */
    SPL_REQUIRE_SAFE(!((Dac_InternalOnly(dac) != 0U) && (config->mode != DAC_MODE_INT_UNBUFFERED)), SPL_ERR_PARAM);
    /* RM0440 22.7.16: MODEx writes are ignored while ENx = 1 or CENx = 1. */
    if ((dac->CR & (bits->en | bits->cen)) != 0U)
    {
        return SPL_ERR_STATE;
    }

    cr = dac->CR & (uint32_t)~(bits->ten | bits->tsel_msk | bits->wave | bits->mamp | bits->dmaen |
                               bits->dmaudrie);
    if (config->trigger_enable != 0U)
    {
        cr |= bits->ten | ((uint32_t)config->trigger_sel << bits->tsel_pos);
    }
    if (config->dma_enable != 0U)
    {
        cr |= bits->dmaen;
    }
    if (config->dma_underrun_irq != 0U)
    {
        cr |= bits->dmaudrie;
    }
    mcr = dac->MCR & (uint32_t)~(bits->mode_msk | bits->dmadouble | bits->sinformat);
    mcr |= (uint32_t)config->mode << bits->mode_pos;

    dac->MCR = mcr;
    dac->CR = cr;
    return SPL_OK;
}

SPL_Status DAC_SetHighFrequencyMode(DAC_TypeDef *dac, uint32_t hclk_hz)
{
    uint32_t hfsel = 0U;

    /* SAFE: reject invalid parameters before peripheral register access. */
    SPL_REQUIRE_SAFE(!((Dac_Valid(dac) == 0U) || (hclk_hz == 0U)), SPL_ERR_PARAM);
    /* Conservative: change MCR only with both channels disabled and not calibrating. */
    if ((dac->CR & (DAC_CR_EN1 | DAC_CR_CEN1 | DAC_CR_EN2 | DAC_CR_CEN2)) != 0U)
    {
        return SPL_ERR_STATE;
    }
    if (hclk_hz > DAC_HFSEL_160MHZ)
    {
        hfsel = DAC_MCR_HFSEL_1;
    }
    else if (hclk_hz > DAC_HFSEL_80MHZ)
    {
        hfsel = DAC_MCR_HFSEL_0;
    }
    else
    {
        hfsel = 0U;
    }
    dac->MCR = (dac->MCR & ~DAC_MCR_HFSEL_Msk) | hfsel;
    return SPL_OK;
}

SPL_Status DAC_Enable(DAC_TypeDef *dac, uint32_t channel)
{
    const Dac_ChannelBits *bits = NULL;
    SPL_Status status = Dac_Check(dac, channel, &bits);

    if (status != SPL_OK)
    {
        return status;
    }
    if ((dac->CR & bits->cen) != 0U)
    {
        return SPL_ERR_STATE;
    }
    dac->CR = dac->CR | bits->en;
    return SPL_OK;
}

SPL_Status DAC_Disable(DAC_TypeDef *dac, uint32_t channel)
{
    const Dac_ChannelBits *bits = NULL;
    SPL_Status status = Dac_Check(dac, channel, &bits);

    if (status != SPL_OK)
    {
        return status;
    }
    dac->CR = dac->CR & (uint32_t)~bits->en;
    return SPL_OK;
}

SPL_Status DAC_Ready(DAC_TypeDef *dac, uint32_t channel, uint8_t *ready)
{
    const Dac_ChannelBits *bits = NULL;
    SPL_Status status = Dac_Check(dac, channel, &bits);

    if (status != SPL_OK)
    {
        return status;
    }
    /* SAFE: reject invalid parameters before peripheral register access. */
    SPL_REQUIRE_SAFE(!(ready == NULL), SPL_ERR_PARAM);
    if ((dac->CR & bits->en) == 0U)
    {
        return SPL_ERR_STATE;
    }
    *ready = ((dac->SR & bits->rdy) != 0U) ? 1U : 0U;
    return SPL_OK;
}

SPL_Status DAC_WriteData(DAC_TypeDef *dac, uint32_t channel, uint32_t align, uint32_t value)
{
    const Dac_ChannelBits *bits = NULL;
    SPL_Status status = Dac_Check(dac, channel, &bits);
    volatile uint32_t *reg;

    if (status != SPL_OK)
    {
        return status;
    }
    if (align == DAC_ALIGN_12R)
    {
        /* SAFE: reject invalid parameters before peripheral register access. */
        SPL_REQUIRE_SAFE(!(value > DAC_DHR12R1_DACC1DHR_Msk), SPL_ERR_PARAM);
        reg = (channel == DAC_CHANNEL_1) ? &dac->DHR12R1 : &dac->DHR12R2;
    }
    else if (align == DAC_ALIGN_12L)
    {
        /* SAFE: reject invalid parameters before peripheral register access. */
        SPL_REQUIRE_SAFE(!((value > DAC_DHR12L_MAX) || ((value & DAC_DHR12L_LOW_BITS) != 0U)), SPL_ERR_PARAM);
        reg = (channel == DAC_CHANNEL_1) ? &dac->DHR12L1 : &dac->DHR12L2;
    }
    else if (align == DAC_ALIGN_8R)
    {
        /* SAFE: reject invalid parameters before peripheral register access. */
        SPL_REQUIRE_SAFE(!(value > DAC_DHR8R1_DACC1DHR_Msk), SPL_ERR_PARAM);
        reg = (channel == DAC_CHANNEL_1) ? &dac->DHR8R1 : &dac->DHR8R2;
    }
    else
    {
        return SPL_ERR_PARAM;
    }
    if ((dac->CR & bits->dmaen) != 0U)
    {
        return SPL_ERR_STATE;
    }
    if (Dac_NotReady(dac, bits) != 0U)
    {
        return SPL_WOULD_BLOCK;
    }
    *reg = value;
    return SPL_OK;
}

SPL_Status DAC_SoftwareTrigger(DAC_TypeDef *dac, uint32_t channel)
{
    const Dac_ChannelBits *bits = NULL;
    SPL_Status status = Dac_Check(dac, channel, &bits);

    if (status != SPL_OK)
    {
        return status;
    }
    if (((dac->CR & bits->ten) == 0U) || ((dac->CR & bits->tsel_msk) != 0U))
    {
        return SPL_ERR_STATE;
    }
    if (Dac_NotReady(dac, bits) != 0U)
    {
        return SPL_WOULD_BLOCK;
    }
    /* SWTRIGx is set by software and cleared by hardware: write only this bit. */
    dac->SWTRIGR = bits->swtrig;
    return SPL_OK;
}

SPL_Status DAC_ReadOutput(DAC_TypeDef *dac, uint32_t channel, uint32_t *value)
{
    const Dac_ChannelBits *bits = NULL;
    SPL_Status status = Dac_Check(dac, channel, &bits);

    if (status != SPL_OK)
    {
        return status;
    }
    /* SAFE: reject invalid parameters before peripheral register access. */
    SPL_REQUIRE_SAFE(!(value == NULL), SPL_ERR_PARAM);
    if (channel == DAC_CHANNEL_1)
    {
        *value = dac->DOR1 & DAC_DOR1_DACC1DOR_Msk;
    }
    else
    {
        *value = dac->DOR2 & DAC_DOR2_DACC2DOR_Msk;
    }
    return SPL_OK;
}

SPL_Status DAC_UnderrunFlag(DAC_TypeDef *dac, uint32_t channel, uint8_t *set)
{
    const Dac_ChannelBits *bits = NULL;
    SPL_Status status = Dac_Check(dac, channel, &bits);

    if (status != SPL_OK)
    {
        return status;
    }
    /* SAFE: reject invalid parameters before peripheral register access. */
    SPL_REQUIRE_SAFE(!(set == NULL), SPL_ERR_PARAM);
    *set = ((dac->SR & bits->dmaudr) != 0U) ? 1U : 0U;
    return SPL_OK;
}

SPL_Status DAC_ClearUnderrun(DAC_TypeDef *dac, uint32_t channel)
{
    const Dac_ChannelBits *bits = NULL;
    SPL_Status status = Dac_Check(dac, channel, &bits);

    if (status != SPL_OK)
    {
        return status;
    }
    /* DMAUDRx is rc_w1; the other SR bits are read-only (RM0440 22.7.14). */
    dac->SR = bits->dmaudr;
    /* Read back: the clear reaches the peripheral before return (METHOD 1.7, issue #23). */
    (void)dac->SR;
    return SPL_OK;
}
