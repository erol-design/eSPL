/* SPDX-License-Identifier: MIT */
/* DAC driver for STM32G0B1 (RM0444 Rev 6 section 16, ES0548 Rev 4 section 2.7), ported from the
 * G4 SPL v0.13.5 DAC driver (Autometry_Mcu_Spl 13c21ce); public contract: Inc/stm32g0_dac.h. */
/* Library unit: this unit also emits the spl_check.h mismatch-guard reference; the only unit
 * that does not is stm32g0_check.c (SPL_CHECK_LEVEL_DEFINER). BULGU-02, CHECK_LEVEL.md 3.2. */
#define SPL_LIBRARY_BUILD 1
#include <stddef.h>
#include "stm32g0_dac.h"

#if SPL_G0_HAS_DAC

#define DAC_TSEL_MAX 15U
/* TSEL codes with a connected trigger (RM0444 Table 85): 0 = SWTRIG, trg1/2/3/5/6/8/11/12/13.
 * DAC is on G071 / G081 / G0B1 / G0C1 only, where every listed source exists. 2026-11-14
 * capability audit: unconnected codes were accepted (trigger never fires). */
#define DAC_TSEL_CONNECTED 0x396FU
#define DAC_DHR12L_MAX 0xFFF0U
#define DAC_DHR12L_LOW_BITS 0x000FU

/* Check level (A4 wave 5, Docs/CHECK_LEVEL.md 5.4 DAC table): SPL_REQUIRE = checked at FULL only
 * (not compiled at FAST). The invalid-alignment branch of DAC_WriteData is structural: at every
 * level. */

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
    uint32_t dmaudr;
    uint32_t swtrig;
} Dac_ChannelBits;

static inline uint8_t Dac_Valid(const DAC_TypeDef *dac)
{
    return (uint8_t)((dac == DAC1) ? 1U : 0U);
}

/* Validates instance and channel; on success *bits points to the channel masks. */
static SPL_Status Dac_Check(const DAC_TypeDef *dac, uint32_t channel, const Dac_ChannelBits **bits)
{
    static const Dac_ChannelBits dac_bits[2] = {
        {DAC_CR_EN1, DAC_CR_CEN1, DAC_CR_TEN1, DAC_CR_TSEL1_Msk, DAC_CR_TSEL1_Pos, DAC_CR_WAVE1_Msk,
         DAC_CR_MAMP1_Msk, DAC_CR_DMAEN1, DAC_CR_DMAUDRIE1, DAC_MCR_MODE1_Msk, DAC_MCR_MODE1_Pos,
         DAC_SR_DMAUDR1, DAC_SWTRIGR_SWTRIG1},
        {DAC_CR_EN2, DAC_CR_CEN2, DAC_CR_TEN2, DAC_CR_TSEL2_Msk, DAC_CR_TSEL2_Pos, DAC_CR_WAVE2_Msk,
         DAC_CR_MAMP2_Msk, DAC_CR_DMAEN2, DAC_CR_DMAUDRIE2, DAC_MCR_MODE2_Msk, DAC_MCR_MODE2_Pos,
         DAC_SR_DMAUDR2, DAC_SWTRIGR_SWTRIG2}};

    /* arbitrary address / read outside the channel table. */
    SPL_REQUIRE((Dac_Valid(dac) != 0U) &&
                    ((channel == DAC_CHANNEL_1) || (channel == DAC_CHANNEL_2)),
                SPL_ERR_PARAM);
    *bits = &dac_bits[channel - 1U];
    return SPL_OK;
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
    SPL_REQUIRE(config != NULL, SPL_ERR_PARAM);
    SPL_REQUIRE((config->trigger_enable <= 1U) && (config->dma_enable <= 1U) &&
                    (config->dma_underrun_irq <= 1U),
                SPL_ERR_PARAM);
    /* TSEL / MODE overflow; with the trigger off TSEL would be silently unused. */
    SPL_REQUIRE((config->trigger_sel <= DAC_TSEL_MAX) &&
                    (((DAC_TSEL_CONNECTED >> config->trigger_sel) & 1U) != 0U) &&
                    ((config->trigger_enable != 0U) || (config->trigger_sel == 0U)) &&
                    (config->mode <= DAC_MODE_INT_UNBUFFERED),
                SPL_ERR_PARAM);
    /* RM0444 16.7.16: MODEx writes are ignored while ENx = 1 or CENx = 1. */
    SPL_REQUIRE((dac->CR & (bits->en | bits->cen)) == 0U, SPL_ERR_STATE);

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
    mcr = dac->MCR & (uint32_t)~bits->mode_msk;
    mcr |= (uint32_t)config->mode << bits->mode_pos;

    /* ERRATA ES0548 2.7.1: one data register write access before MODEx is programmed
     * (EN = 0 here); the current DHR12Rx value is written back, so the output level is kept. */
    if (channel == DAC_CHANNEL_1)
    {
        dac->DHR12R1 = dac->DHR12R1 & DAC_DHR12R1_DACC1DHR_Msk;
    }
    else
    {
        dac->DHR12R2 = dac->DHR12R2 & DAC_DHR12R2_DACC2DHR_Msk;
    }
    dac->MCR = mcr;
    dac->CR = cr;
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
    SPL_REQUIRE((dac->CR & bits->cen) == 0U, SPL_ERR_STATE); /* in calibration mode */
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

SPL_Status DAC_WriteData(DAC_TypeDef *dac, uint32_t channel, uint32_t align, uint32_t value)
{
    const Dac_ChannelBits *bits = NULL;
    SPL_Status status = Dac_Check(dac, channel, &bits);
    volatile uint32_t *reg;

    if (status != SPL_OK)
    {
        return status;
    }
    /* (5.3): a value that does not fit the alignment gives a silently wrong output. */
    if (align == DAC_ALIGN_12R)
    {
        SPL_REQUIRE(value <= DAC_DHR12R1_DACC1DHR_Msk, SPL_ERR_PARAM);
        reg = (channel == DAC_CHANNEL_1) ? &dac->DHR12R1 : &dac->DHR12R2;
    }
    else if (align == DAC_ALIGN_12L)
    {
        SPL_REQUIRE((value <= DAC_DHR12L_MAX) && ((value & DAC_DHR12L_LOW_BITS) == 0U),
                    SPL_ERR_PARAM);
        reg = (channel == DAC_CHANNEL_1) ? &dac->DHR12L1 : &dac->DHR12L2;
    }
    else if (align == DAC_ALIGN_8R)
    {
        SPL_REQUIRE(value <= DAC_DHR8R1_DACC1DHR_Msk, SPL_ERR_PARAM);
        reg = (channel == DAC_CHANNEL_1) ? &dac->DHR8R1 : &dac->DHR8R2;
    }
    else
    {
        return SPL_ERR_PARAM; /* structural: no target register can be selected, at every level */
    }
    /* (5.3): a manual write races the DMA. */
    SPL_REQUIRE((dac->CR & bits->dmaen) == 0U, SPL_ERR_STATE);
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
    /* SWTRIG is silently ignored. */
    SPL_REQUIRE(((dac->CR & bits->ten) != 0U) && ((dac->CR & bits->tsel_msk) == 0U), SPL_ERR_STATE);
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
    SPL_REQUIRE(value != NULL, SPL_ERR_PARAM);
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
    SPL_REQUIRE(set != NULL, SPL_ERR_PARAM);
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
    /* DMAUDRx is rc_w1; the other SR bits are read-only (RM0444 16.7.14). */
    dac->SR = bits->dmaudr;
    /* Read back: the clear reaches the peripheral before return (METHOD 1.7, issue #23). */
    (void)dac->SR;
    return SPL_OK;
}

/* ---- v0.11: wave generation, sample and hold, dual channel, calibration (RM0444 16) ---- */

/* Channel field position in the dual-channel registers: channel 2 = channel 1 shifted by 16. */
static uint32_t Dac_Shift(uint32_t channel)
{
    return (channel == DAC_CHANNEL_2) ? 16U : 0U;
}

SPL_Status DAC_WaveConfig(DAC_TypeDef *dac, uint32_t channel, uint32_t wave, uint32_t amplitude)
{
    const Dac_ChannelBits *bits = NULL;
    SPL_Status status = Dac_Check(dac, channel, &bits);
    const uint32_t shift = Dac_Shift(channel);

    if (status != SPL_OK)
    {
        return status;
    }
    SPL_REQUIRE(wave <= DAC_WAVE_TRIANGLE, SPL_ERR_PARAM); /* reserved WAVE code */
    /* MAMP codes above 1011 give the same as 1011 (RM0444 16.7.1) */
    SPL_REQUIRE(amplitude <= 11U, SPL_ERR_PARAM);
    SPL_REQUIRE((dac->CR & (bits->en | bits->cen)) == 0U, SPL_ERR_STATE);
    /* RM0444 16.7.1: wave generation is only used with TENx = 1. */
    SPL_REQUIRE((wave == DAC_WAVE_NONE) || ((dac->CR & bits->ten) != 0U), SPL_ERR_STATE);
    dac->CR = (dac->CR & ~(bits->wave | bits->mamp)) | ((wave << DAC_CR_WAVE1_Pos) << shift) |
              ((amplitude << DAC_CR_MAMP1_Pos) << shift);
    return SPL_OK;
}

SPL_Status DAC_SampleHoldConfig(DAC_TypeDef *dac, uint32_t channel, uint32_t mode, uint32_t sample,
                                uint32_t hold, uint32_t refresh)
{
    const Dac_ChannelBits *bits = NULL;
    SPL_Status status = Dac_Check(dac, channel, &bits);
    const uint32_t shift = Dac_Shift(channel);

    if (status != SPL_OK)
    {
        return status;
    }
    /* SHSR would be truncated; SHHR / SHRR would spill into the channel 2 field. */
    SPL_REQUIRE((mode <= DAC_MODE_INT_UNBUFFERED) && (sample <= 1023U) && (hold <= 1023U) &&
                    (refresh <= 255U),
                SPL_ERR_PARAM);
    /* SHHR / SHRR / MODEx are writable only with ENx = 0 and CENx = 0 (RM0444 16.7.16 .. 16.7.20);
     * */
    SPL_REQUIRE((dac->CR & (bits->en | bits->cen)) == 0U, SPL_ERR_STATE);
    /* ERRATA ES0548 2.7.1: one data register write before MODEx is programmed. */
    if (channel == DAC_CHANNEL_1)
    {
        dac->DHR12R1 = dac->DHR12R1 & DAC_DHR12R1_DACC1DHR_Msk;
        dac->SHSR1 = sample;
    }
    else
    {
        dac->DHR12R2 = dac->DHR12R2 & DAC_DHR12R2_DACC2DHR_Msk;
        dac->SHSR2 = sample;
    }
    dac->SHHR = (dac->SHHR & ~(DAC_SHHR_THOLD1_Msk << shift)) | (hold << shift);
    dac->SHRR = (dac->SHRR & ~(DAC_SHRR_TREFRESH1_Msk << shift)) | (refresh << shift);
    /* MODEx = 1xx: sample and hold on top of the normal mode code (RM0444 16.7.16). */
    dac->MCR = (dac->MCR & ~bits->mode_msk) | ((DAC_MODE_SAMPLE_HOLD | mode) << bits->mode_pos);
    return SPL_OK;
}

SPL_Status DAC_WriteDual(DAC_TypeDef *dac, uint32_t align, uint32_t value1, uint32_t value2)
{
    SPL_REQUIRE(Dac_Valid(dac) != 0U, SPL_ERR_PARAM); /* arbitrary address */
    /* (5.3): a value that does not fit the alignment spills into channel 2; with an invalid
     * alignment nothing would be written and OK returned. */
    if (align == DAC_ALIGN_12R)
    {
        SPL_REQUIRE((value1 <= 0xFFFU) && (value2 <= 0xFFFU), SPL_ERR_PARAM);
        dac->DHR12RD = value1 | (value2 << 16U);
    }
    else if (align == DAC_ALIGN_12L)
    {
        SPL_REQUIRE((value1 <= DAC_DHR12L_MAX) && (value2 <= DAC_DHR12L_MAX) &&
                        (((value1 | value2) & DAC_DHR12L_LOW_BITS) == 0U),
                    SPL_ERR_PARAM);
        dac->DHR12LD = value1 | (value2 << 16U);
    }
    else if (align == DAC_ALIGN_8R)
    {
        SPL_REQUIRE((value1 <= 0xFFU) && (value2 <= 0xFFU), SPL_ERR_PARAM);
        dac->DHR8RD = value1 | (value2 << 8U); /* RM0444 16.7.11: DACC2DHR at bits 15:8 */
    }
    else
    {
        SPL_REQUIRE(0, SPL_ERR_PARAM);
    }
    return SPL_OK;
}

SPL_Status DAC_SoftwareTriggerDual(DAC_TypeDef *dac)
{
    SPL_REQUIRE(Dac_Valid(dac) != 0U, SPL_ERR_PARAM); /* arbitrary address */
    dac->SWTRIGR = DAC_SWTRIGR_SWTRIG1 | DAC_SWTRIGR_SWTRIG2;
    return SPL_OK;
}

SPL_Status DAC_SetOffsetTrim(DAC_TypeDef *dac, uint32_t channel, uint32_t trim)
{
    const Dac_ChannelBits *bits = NULL;
    SPL_Status status = Dac_Check(dac, channel, &bits);
    const uint32_t shift = Dac_Shift(channel);

    if (status != SPL_OK)
    {
        return status;
    }
    SPL_REQUIRE(trim <= 31U, SPL_ERR_PARAM); /* OTRIM overflow */
    /* trimming is done with the channel disabled (RM0444 16.4.12); */
    SPL_REQUIRE((dac->CR & bits->en) == 0U, SPL_ERR_STATE);
    dac->CCR = (dac->CCR & ~(DAC_CCR_OTRIM1_Msk << shift)) | (trim << shift);
    return SPL_OK;
}

SPL_Status DAC_CalibrationMode(DAC_TypeDef *dac, uint32_t channel, uint8_t enable)
{
    const Dac_ChannelBits *bits = NULL;
    SPL_Status status = Dac_Check(dac, channel, &bits);

    if (status != SPL_OK)
    {
        return status;
    }
    SPL_REQUIRE(enable <= 1U, SPL_ERR_PARAM);
    SPL_REQUIRE((dac->CR & bits->en) == 0U, SPL_ERR_STATE); /* CENx only with ENx = 0 */
    dac->CR = (dac->CR & ~bits->cen) | ((enable != 0U) ? bits->cen : 0U);
    return SPL_OK;
}

SPL_Status DAC_CalibrationFlag(DAC_TypeDef *dac, uint32_t channel, uint8_t *high)
{
    const Dac_ChannelBits *bits = NULL;
    SPL_Status status = Dac_Check(dac, channel, &bits);
    (void)bits;

    if (status != SPL_OK)
    {
        return status;
    }
    SPL_REQUIRE(high != NULL, SPL_ERR_PARAM);
    *high = ((dac->SR & (DAC_SR_CAL_FLAG1 << Dac_Shift(channel))) != 0U) ? 1U : 0U;
    return SPL_OK;
}

#else
/* ISO C forbids an empty translation unit. */
typedef int stm32g0_dac_absent;
#endif /* SPL_G0_HAS_DAC */
