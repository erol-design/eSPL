#define SPL_LIBRARY_BUILD 1
#include "stm32g4_adc.h"

/* ADC_CR command bits are "rs": writing 0 has no effect, writing 1 issues the command.
 * Every CR write clears all of them except the one command being issued. */
#define ADC_CR_COMMANDS                                                                            \
    (ADC_CR_ADCAL | ADC_CR_ADEN | ADC_CR_ADDIS | ADC_CR_ADSTART | ADC_CR_ADSTP | ADC_CR_JADSTART | \
     ADC_CR_JADSTP)
#define ADC_CR_PENDING (ADC_CR_ADDIS | ADC_CR_ADSTP | ADC_CR_JADSTP)
#define ADC_CHANNEL_MAX 18U
#define ADC_SEQ_MAX 16U
#define ADC_ISR_ALL (ADC_ISR_ADRDY | ADC_FLAGS_CLEARABLE)

/* ADC1/ADC2 everywhere; ADC3..5 where the device header defines them. */
static uint8_t Adc_Valid(const ADC_TypeDef *adc)
{
    uint8_t valid = ((adc == ADC1) || (adc == ADC2)) ? 1U : 0U;
#if defined(ADC3)
    if (adc == ADC3)
    {
        valid = 1U;
    }
#endif
#if defined(ADC4)
    if (adc == ADC4)
    {
        valid = 1U;
    }
#endif
#if defined(ADC5)
    if (adc == ADC5)
    {
        valid = 1U;
    }
#endif
    return valid;
}

/* ADC fully disabled: ADCAL, JADSTART, JADSTP, ADSTART, ADSTP, ADDIS and ADEN all 0.
 * Pending self-clearing commands: BUSY; otherwise STATE. */
static SPL_Status Adc_Idle(const ADC_TypeDef *adc)
{
    uint32_t cr = adc->CR;

    if ((cr & ADC_CR_PENDING) != 0U)
    {
        return SPL_BUSY;
    }
    if ((cr & ADC_CR_COMMANDS) != 0U)
    {
        return SPL_ERR_STATE;
    }
    return SPL_OK;
}

/* No conversion of the groups in start_bits (ADSTART and/or JADSTART). BUSY when every
 * running group already has its stop command pending, STATE otherwise. */
static SPL_Status Adc_NoConversion(const ADC_TypeDef *adc, uint32_t start_bits)
{
    uint32_t cr = adc->CR;
    uint32_t running = cr & start_bits;
    uint32_t stopping = 0U;

    if (running == 0U)
    {
        return SPL_OK;
    }
    if ((cr & ADC_CR_ADSTP) != 0U)
    {
        stopping |= ADC_CR_ADSTART;
    }
    if ((cr & ADC_CR_JADSTP) != 0U)
    {
        stopping |= ADC_CR_JADSTART;
    }
    if ((running & ~stopping) == 0U)
    {
        return SPL_BUSY;
    }
    return SPL_ERR_STATE;
}

/* DIFSEL bits of the fixed internal channels (VREFINT, VTS, VBAT/3; RM0440 21.4.11). The
 * DIFSEL note (21.7.21) requires them to keep the reset value (single-ended). */
static uint32_t Adc_InternalChannels(const ADC_TypeDef *adc)
{
    uint32_t mask = 0U;

    if (adc == ADC1)
    {
        mask = ADC_DIFSEL_DIFSEL_16 | ADC_DIFSEL_DIFSEL_17 | ADC_DIFSEL_DIFSEL_18;
    }
#if defined(ADC3)
    if (adc == ADC3)
    {
        mask = ADC_DIFSEL_DIFSEL_17 | ADC_DIFSEL_DIFSEL_18;
    }
#endif
#if defined(ADC4)
    if (adc == ADC4)
    {
        mask = ADC_DIFSEL_DIFSEL_18;
    }
#endif
#if defined(ADC5)
    if (adc == ADC5)
    {
        mask = ADC_DIFSEL_DIFSEL_4 | ADC_DIFSEL_DIFSEL_17 | ADC_DIFSEL_DIFSEL_18;
    }
#endif
    return mask;
}

/* Common block of the pair (RM0440 21.8: ADC12 and ADC345). */
static ADC_Common_TypeDef *Adc_Common(const ADC_TypeDef *adc)
{
#if defined(ADC345_COMMON)
    if ((adc != ADC1) && (adc != ADC2))
    {
        return ADC345_COMMON;
    }
#else
    (void)adc;
#endif
    return ADC12_COMMON;
}

/* Every ADC of the pair Idle. STATE has priority over BUSY (retry cannot help). */
static SPL_Status Adc_PairIdle(const ADC_TypeDef *adc)
{
    const ADC_TypeDef *pair[3];
    uint32_t count = 0U;
    uint32_t i;
    SPL_Status result = SPL_OK;

    if ((adc == ADC1) || (adc == ADC2))
    {
        pair[0] = ADC1;
        pair[1] = ADC2;
        count = 2U;
    }
#if defined(ADC3)
    else
    {
        pair[count] = ADC3;
        count++;
#if defined(ADC4)
        pair[count] = ADC4;
        count++;
#endif
#if defined(ADC5)
        pair[count] = ADC5;
        count++;
#endif
    }
#endif

    for (i = 0U; i < count; ++i)
    {
        SPL_Status status = Adc_Idle(pair[i]);
        if (status == SPL_ERR_STATE)
        {
            return SPL_ERR_STATE;
        }
        if (status == SPL_BUSY)
        {
            result = SPL_BUSY;
        }
    }
    return result;
}

/* SMPR1 holds channels 0..9, SMPR2 channels 10..18; 3-bit fields (RM0440 21.7.6/7). */
static void Adc_WriteSmp(ADC_TypeDef *adc, uint8_t channel, uint8_t sample_code)
{
    uint32_t shift;
    uint32_t mask;

    if (channel < 10U)
    {
        shift = (uint32_t)channel * ADC_SMPR1_SMP1_Pos;
        mask = ADC_SMPR1_SMP0_Msk;
        mask <<= shift;
        adc->SMPR1 = (adc->SMPR1 & ~mask) | ((uint32_t)sample_code << shift);
    }
    else
    {
        shift = ((uint32_t)channel - 10U) * ADC_SMPR2_SMP11_Pos;
        mask = ADC_SMPR2_SMP10_Msk;
        mask <<= shift;
        adc->SMPR2 = (adc->SMPR2 & ~mask) | ((uint32_t)sample_code << shift);
    }
}

/* Calibration start; caldif is 0 or ADC_CR_ADCALDIF. */
static SPL_Status Adc_StartCal(ADC_TypeDef *adc, uint32_t caldif)
{
    uint32_t base;

    if ((adc->CR & (ADC_CR_ADEN | ADC_CR_ADCAL | ADC_CR_DEEPPWD)) != 0U)
    {
        return SPL_ERR_STATE;
    }
    if ((adc->CR & ADC_CR_ADVREGEN) == 0U)
    {
        return SPL_ERR_STATE;
    }

    /* RM0440 21.4.8: select ADCALDIF first, then set ADCAL. Command bits stay zero. */
    base = adc->CR & (uint32_t)~(ADC_CR_ADCALDIF | ADC_CR_COMMANDS);
    if ((adc->CR & ADC_CR_ADCALDIF) != caldif)
    {
        adc->CR = base | caldif;
    }
    adc->CR = base | caldif | ADC_CR_ADCAL;

    /* Hardware clears ADCAL when calibration finishes. */
    return SPL_OK;
}

SPL_Status ADC_PowerRegulator(ADC_TypeDef *adc)
{
    SPL_Status status;

    /* SAFE: reject invalid parameters before peripheral register access. */
    SPL_REQUIRE_SAFE(!(Adc_Valid(adc) == 0U), SPL_ERR_PARAM);
    /* RM0440 ADC_CR: DEEPPWD/ADVREGEN may be written only when ADCAL,
     * JADSTART, JADSTP, ADSTART, ADSTP, ADDIS and ADEN are all zero.
     * Pending stop/disable commands clear by themselves: report BUSY. */
    status = Adc_Idle(adc);
    if (status != SPL_OK)
    {
        return status;
    }

    /* RM0440 21.4.6: first exit Deep-power-down, then enable the regulator.
     * Read/set (RS) command bits are known zero here and stay zero. */
    if ((adc->CR & ADC_CR_DEEPPWD) != 0U)
    {
        adc->CR = adc->CR & (uint32_t)~ADC_CR_DEEPPWD;
    }
    adc->CR = adc->CR | ADC_CR_ADVREGEN;

    /* Caller must wait the reference-manual regulator startup time. */
    return SPL_OK;
}

SPL_Status ADC_StartCalibration(ADC_TypeDef *adc)
{
    /* SAFE: reject invalid parameters before peripheral register access. */
    SPL_REQUIRE_SAFE(!(Adc_Valid(adc) == 0U), SPL_ERR_PARAM);
    return Adc_StartCal(adc, 0U);
}

SPL_Status ADC_StartCalibrationMode(ADC_TypeDef *adc, uint32_t mode)
{
    /* SAFE: reject invalid parameters before peripheral register access. */
    SPL_REQUIRE_SAFE(!((Adc_Valid(adc) == 0U) || (mode > ADC_CAL_DIFF)), SPL_ERR_PARAM);
    return Adc_StartCal(adc, (mode == ADC_CAL_DIFF) ? ADC_CR_ADCALDIF : 0U);
}

SPL_Status ADC_CalibrationDone(ADC_TypeDef *adc, uint8_t *done)
{
    /* SAFE: reject invalid parameters before peripheral register access. */
    SPL_REQUIRE_SAFE(!((Adc_Valid(adc) == 0U) || (done == (uint8_t *)0)), SPL_ERR_PARAM);

    *done = (uint8_t)(((adc->CR & ADC_CR_ADCAL) == 0U) ? 1U : 0U);
    return SPL_OK;
}

/* RM0440 21.4.29: the oversampled sum is shifted, then truncated to 16 bits
 * without saturation. 1 when resolution res (ADC_RES_* code: 12 - 2 * code
 * bits) with ratio code (2^(ratio + 1) samples) and shift fits the 16-bit DR. */
static uint32_t Adc_OvsFits(uint32_t res, uint32_t ratio, uint32_t shift)
{
    return (((12U - (2U * res)) + (ratio + 1U)) <= (16U + shift)) ? 1U : 0U;
}

/* Regular or injected oversampling on: CFGR2 OVSR/OVSS must fit resolution res. */
static uint32_t Adc_OvsFitsCfgr2(const ADC_TypeDef *adc, uint32_t res)
{
    const uint32_t cfgr2 = adc->CFGR2;

    if ((cfgr2 & (ADC_CFGR2_ROVSE | ADC_CFGR2_JOVSE)) == 0U)
    {
        return 1U;
    }
    return Adc_OvsFits(res, (cfgr2 & ADC_CFGR2_OVSR) >> ADC_CFGR2_OVSR_Pos,
                       (cfgr2 & ADC_CFGR2_OVSS) >> ADC_CFGR2_OVSS_Pos);
}

SPL_Status ADC_RegularSingleInit(ADC_TypeDef *adc, uint8_t channel, uint8_t sample_code)
{
    const uint32_t cfgr_clear = ADC_CFGR_RES | ADC_CFGR_ALIGN | ADC_CFGR_CONT | ADC_CFGR_EXTEN |
                                ADC_CFGR_DMAEN | ADC_CFGR_DMACFG;

    /* SAFE: reject invalid parameters before peripheral register access. */
    SPL_REQUIRE_SAFE(!((Adc_Valid(adc) == 0U) || (channel > ADC_CHANNEL_MAX) || (sample_code > 7U)), SPL_ERR_PARAM);
    if ((adc->CR & (ADC_CR_ADEN | ADC_CR_ADSTART | ADC_CR_JADSTART)) != 0U)
    {
        return SPL_ERR_STATE;
    }
    /* Selects 12 bit: an enabled oversampling configured for a lower
     * resolution would overflow the 16-bit DR (reconfigure it first). */
    if (Adc_OvsFitsCfgr2(adc, ADC_RES_12BIT) == 0U)
    {
        return SPL_ERR_STATE;
    }

    /* Single regular rank, 12-bit, right aligned, software trigger. */
    adc->CFGR &= ~cfgr_clear;
    adc->SQR1 = (uint32_t)channel << ADC_SQR1_SQ1_Pos;
    Adc_WriteSmp(adc, channel, sample_code);

    return SPL_OK;
}

SPL_Status ADC_Enable(ADC_TypeDef *adc)
{
    /* SAFE: reject invalid parameters before peripheral register access. */
    SPL_REQUIRE_SAFE(!(Adc_Valid(adc) == 0U), SPL_ERR_PARAM);
    /* RM0440 21.7.3 ADEN note: ADEN may be set only with ADVREGEN = 1 (and the regulator
     * start-up time elapsed). With ADVREGEN = 0 ADRDY would never set. */
    if (((adc->CR & (ADC_CR_DEEPPWD | ADC_CR_ADCAL)) != 0U) || ((adc->CR & ADC_CR_ADVREGEN) == 0U))
    {
        return SPL_ERR_STATE;
    }

    /* A disable or stop command in progress takes precedence over ADEN.
     * ADEN can still read as one while ADDIS is active. Do not report
     * success during an asynchronous transition to the disabled state. */
    if ((adc->CR & ADC_CR_PENDING) != 0U)
    {
        return SPL_BUSY;
    }

    /* ADEN is a hardware command. Never reissue it for an enabled ADC:
     * clearing ADRDY on a second call would lose the ready indication. */
    if ((adc->CR & ADC_CR_ADEN) != 0U)
    {
        return SPL_OK;
    }

    /* Conversions must not be running when enabling the ADC. */
    if ((adc->CR & (ADC_CR_ADSTART | ADC_CR_JADSTART)) != 0U)
    {
        return SPL_ERR_STATE;
    }

    /* ADRDY is write-one-to-clear. Clear only before enabling. */
    adc->ISR = ADC_ISR_ADRDY;
    /* Caller must enforce at least four ADC kernel cycles after
     * calibration completes and the regulator startup interval. */
    adc->CR = (adc->CR & (uint32_t)~ADC_CR_COMMANDS) | ADC_CR_ADEN;

    return SPL_OK;
}

SPL_Status ADC_Ready(ADC_TypeDef *adc, uint8_t *ready)
{
    /* SAFE: reject invalid parameters before peripheral register access. */
    SPL_REQUIRE_SAFE(!((Adc_Valid(adc) == 0U) || (ready == (uint8_t *)0)), SPL_ERR_PARAM);

    *ready = (uint8_t)(((adc->ISR & ADC_ISR_ADRDY) != 0U) ? 1U : 0U);
    return SPL_OK;
}

SPL_Status ADC_Start(ADC_TypeDef *adc)
{
    /* SAFE: reject invalid parameters before peripheral register access. */
    SPL_REQUIRE_SAFE(!(Adc_Valid(adc) == 0U), SPL_ERR_PARAM);
    /* Pending stop/disable commands take precedence over readiness. */
    if ((adc->CR & ADC_CR_PENDING) != 0U)
    {
        return SPL_BUSY;
    }
    if (((adc->CR & ADC_CR_ADEN) == 0U) || ((adc->ISR & ADC_ISR_ADRDY) == 0U))
    {
        return SPL_ERR_STATE;
    }
    if ((adc->CR & ADC_CR_ADSTART) != 0U)
    {
        /* RM0440 21.7.3 ADSTART: hardware clears it by itself only in single mode with
         * software trigger (at EOS). With CONT = 1 or EXTEN != 0 it stays set until ADSTP:
         * retrying cannot help, the caller must ADC_StopRegular first. */
        if ((adc->CFGR & (ADC_CFGR_CONT | ADC_CFGR_EXTEN)) != 0U)
        {
            return SPL_ERR_STATE;
        }
        return SPL_BUSY;
    }
    if ((adc->ISR & ADC_ISR_OVR) != 0U)
    {
        /* Preserve error evidence until explicitly cleared by caller. */
        return SPL_ERR_HW;
    }

    /* Discard completion flags from the previous regular conversion. */
    adc->ISR = ADC_ISR_EOC | ADC_ISR_EOS;

    adc->CR = (adc->CR & (uint32_t)~ADC_CR_COMMANDS) | ADC_CR_ADSTART;
    return SPL_OK;
}

SPL_Status ADC_TryRead(ADC_TypeDef *adc, uint16_t *value)
{
    /* SAFE: reject invalid parameters before peripheral register access. */
    SPL_REQUIRE_SAFE(!((Adc_Valid(adc) == 0U) || (value == (uint16_t *)0)), SPL_ERR_PARAM);
    if ((adc->ISR & ADC_ISR_OVR) != 0U)
    {
        return SPL_ERR_HW;
    }
    if ((adc->ISR & ADC_ISR_EOC) == 0U)
    {
        /* Retained EOC data may be read after disable; an empty disabled
         * converter cannot produce a new sample through polling alone. */
        if ((adc->CR & ADC_CR_ADEN) == 0U)
        {
            return SPL_ERR_STATE;
        }
        /* Enabled but no regular conversion running (never started, sample already read,
         * or stopped): no sample can come. EOS clears ADSTART right after the last EOC, so
         * EOC is read again after seeing ADSTART = 0 to close that race. */
        if ((adc->CR & ADC_CR_ADSTART) == 0U)
        {
            if ((adc->ISR & ADC_ISR_EOC) == 0U)
            {
                return SPL_ERR_STATE;
            }
        }
        else
        {
            return SPL_WOULD_BLOCK;
        }
    }

    *value = (uint16_t)(adc->DR & 0xFFFFU);
    return SPL_OK;
}

SPL_Status ADC_EnableDma(ADC_TypeDef *adc, uint8_t circular)
{
    const uint32_t dma_bits = ADC_CFGR_DMAEN | ADC_CFGR_DMACFG;

    /* SAFE: reject invalid parameters before peripheral register access. */
    SPL_REQUIRE_SAFE(!((Adc_Valid(adc) == 0U) || (circular > 1U)), SPL_ERR_PARAM);
    /* RM0440 21.7.4: DMAEN/DMACFG writable only with ADSTART = 0 and JADSTART = 0. */
    if ((adc->CR & (ADC_CR_ADSTART | ADC_CR_JADSTART)) != 0U)
    {
        return SPL_ERR_STATE;
    }

    adc->CFGR =
        (adc->CFGR & ~dma_bits) | ADC_CFGR_DMAEN | ((circular != 0U) ? ADC_CFGR_DMACFG : 0UL);

    return SPL_OK;
}

SPL_Status ADC_DisableDma(ADC_TypeDef *adc)
{
    const uint32_t dma_bits = ADC_CFGR_DMAEN | ADC_CFGR_DMACFG;

    /* SAFE: reject invalid parameters before peripheral register access. */
    SPL_REQUIRE_SAFE(!(Adc_Valid(adc) == 0U), SPL_ERR_PARAM);
    if ((adc->CR & (ADC_CR_ADSTART | ADC_CR_JADSTART)) != 0U)
    {
        return SPL_ERR_STATE;
    }

    adc->CFGR &= ~dma_bits;
    return SPL_OK;
}

SPL_Status ADC_ClearErrors(ADC_TypeDef *adc)
{
    /* SAFE: reject invalid parameters before peripheral register access. */
    SPL_REQUIRE_SAFE(!(Adc_Valid(adc) == 0U), SPL_ERR_PARAM);

    /* ISR is W1C. Do not use a read-modify-write on error flags. */
    adc->ISR = ADC_ISR_OVR;
    /* Read back: the clear reaches the peripheral before return (METHOD 1.7, issue #23). */
    (void)adc->ISR;
    return SPL_OK;
}

SPL_Status ADC_CommonClockConfig(ADC_TypeDef *adc, uint32_t ckmode, uint32_t presc)
{
    ADC_Common_TypeDef *common;
    SPL_Status status;

    /* SAFE: reject invalid parameters before peripheral register access. */
    SPL_REQUIRE_SAFE(!((Adc_Valid(adc) == 0U) || (ckmode > ADC_CLOCK_SYNC_DIV4) || (presc > ADC_PRESC_MAX)), SPL_ERR_PARAM);
    /* PRESC is applied only in asynchronous mode (RM0440 21.8.2). */
    /* SAFE: reject invalid parameters before peripheral register access. */
    SPL_REQUIRE_SAFE(!((ckmode != ADC_CLOCK_ASYNC) && (presc != 0U)), SPL_ERR_PARAM);
    /* CKMODE/PRESC writable only when every ADC of the pair is disabled. */
    status = Adc_PairIdle(adc);
    if (status != SPL_OK)
    {
        return status;
    }

    common = Adc_Common(adc);
    common->CCR = (common->CCR & (uint32_t)~(ADC_CCR_CKMODE | ADC_CCR_PRESC)) |
                  (ckmode << ADC_CCR_CKMODE_Pos) | (presc << ADC_CCR_PRESC_Pos);
    return SPL_OK;
}

SPL_Status ADC_InternalChannelsConfig(ADC_TypeDef *adc, uint32_t mask)
{
    const uint32_t valid_bits = ADC_CCR_VREFEN | ADC_CCR_VSENSESEL | ADC_CCR_VBATSEL;
    ADC_Common_TypeDef *common;

    /* SAFE: reject invalid parameters before peripheral register access. */
    SPL_REQUIRE_SAFE(!((Adc_Valid(adc) == 0U) || ((mask & ~valid_bits) != 0U)), SPL_ERR_PARAM);

    /* RM0440 21.8.2 gives no write condition for these bits. */
    common = Adc_Common(adc);
    common->CCR = (common->CCR & ~valid_bits) | mask;
    return SPL_OK;
}

SPL_Status ADC_RegularSequenceConfig(ADC_TypeDef *adc, const uint8_t *channels, uint8_t length)
{
    static const uint8_t k_pos[ADC_SEQ_MAX] = {
        ADC_SQR1_SQ1_Pos,  ADC_SQR1_SQ2_Pos,  ADC_SQR1_SQ3_Pos,  ADC_SQR1_SQ4_Pos,
        ADC_SQR2_SQ5_Pos,  ADC_SQR2_SQ6_Pos,  ADC_SQR2_SQ7_Pos,  ADC_SQR2_SQ8_Pos,
        ADC_SQR2_SQ9_Pos,  ADC_SQR3_SQ10_Pos, ADC_SQR3_SQ11_Pos, ADC_SQR3_SQ12_Pos,
        ADC_SQR3_SQ13_Pos, ADC_SQR3_SQ14_Pos, ADC_SQR4_SQ15_Pos, ADC_SQR4_SQ16_Pos};
    /* Register index per rank: SQR1 ranks 1..4, SQR2 5..9, SQR3 10..14, SQR4 15..16. */
    static const uint8_t k_reg[ADC_SEQ_MAX] = {0U, 0U, 0U, 0U, 1U, 1U, 1U, 1U,
                                               1U, 2U, 2U, 2U, 2U, 2U, 3U, 3U};
    uint32_t sqr[4];
    uint32_t i;
    SPL_Status status;

    /* SAFE: reject invalid parameters before peripheral register access. */
    SPL_REQUIRE_SAFE(!((Adc_Valid(adc) == 0U) || (channels == (const uint8_t *)0) || (length == 0U) ||
        (length > ADC_SEQ_MAX)), SPL_ERR_PARAM);
    for (i = 0U; i < length; ++i)
    {
        /* SAFE: reject invalid parameters before peripheral register access. */
        SPL_REQUIRE_SAFE(!(channels[i] > ADC_CHANNEL_MAX), SPL_ERR_PARAM);
    }
    status = Adc_NoConversion(adc, ADC_CR_ADSTART);
    if (status != SPL_OK)
    {
        return status;
    }

    sqr[0] = ((uint32_t)length - 1U) << ADC_SQR1_L_Pos;
    sqr[1] = 0U;
    sqr[2] = 0U;
    sqr[3] = 0U;
    for (i = 0U; i < length; ++i)
    {
        sqr[k_reg[i]] |= (uint32_t)channels[i] << k_pos[i];
    }
    adc->SQR1 = sqr[0];
    adc->SQR2 = sqr[1];
    adc->SQR3 = sqr[2];
    adc->SQR4 = sqr[3];
    return SPL_OK;
}

SPL_Status ADC_SetSamplingTime(ADC_TypeDef *adc, uint8_t channel, uint8_t sample_code)
{
    SPL_Status status;

    /* SAFE: reject invalid parameters before peripheral register access. */
    SPL_REQUIRE_SAFE(!((Adc_Valid(adc) == 0U) || (channel > ADC_CHANNEL_MAX) || (sample_code > 7U)), SPL_ERR_PARAM);
    status = Adc_NoConversion(adc, ADC_CR_ADSTART | ADC_CR_JADSTART);
    if (status != SPL_OK)
    {
        return status;
    }

    Adc_WriteSmp(adc, channel, sample_code);
    return SPL_OK;
}

SPL_Status ADC_RegularConfig(ADC_TypeDef *adc, const ADC_RegularConfigTypeDef *config)
{
    const uint32_t fields = ADC_CFGR_RES | ADC_CFGR_ALIGN | ADC_CFGR_CONT | ADC_CFGR_DISCEN |
                            ADC_CFGR_DISCNUM | ADC_CFGR_EXTSEL | ADC_CFGR_EXTEN | ADC_CFGR_OVRMOD;
    uint32_t value;
    SPL_Status status;

    /* SAFE: reject invalid parameters before peripheral register access. */
    SPL_REQUIRE_SAFE(!((Adc_Valid(adc) == 0U) || (config == (const ADC_RegularConfigTypeDef *)0)), SPL_ERR_PARAM);
    /* SAFE: reject invalid parameters before peripheral register access. */
    SPL_REQUIRE_SAFE(!((config->resolution > ADC_RES_6BIT) || (config->align > ADC_ALIGN_LEFT) ||
        (config->continuous > 1U) || (config->disc_count > 8U) || (config->ext_sel > 31U) ||
        (config->ext_edge > ADC_EDGE_BOTH) || (config->overrun_overwrite > 1U)), SPL_ERR_PARAM);
    /* RM0440 21.7.4: CONT = 1 together with DISCEN = 1 is forbidden. */
    /* SAFE: reject invalid parameters before peripheral register access. */
    SPL_REQUIRE_SAFE(!((config->continuous != 0U) && (config->disc_count != 0U)), SPL_ERR_PARAM);
    /* Library rule: a trigger source without an edge would be silently ignored. */
    /* SAFE: reject invalid parameters before peripheral register access. */
    SPL_REQUIRE_SAFE(!((config->ext_edge == ADC_EDGE_SOFTWARE) && (config->ext_sel != 0U)), SPL_ERR_PARAM);
    /* RES and ALIGN also require JADSTART = 0. */
    status = Adc_NoConversion(adc, ADC_CR_ADSTART | ADC_CR_JADSTART);
    if (status != SPL_OK)
    {
        return status;
    }
    /* RM0440 21.4.29: an enabled oversampling must still fit the 16-bit DR at
     * the new resolution (reconfigure it with ADC_OversamplingConfig first). */
    if (Adc_OvsFitsCfgr2(adc, (uint32_t)config->resolution) == 0U)
    {
        return SPL_ERR_STATE;
    }
    /* RM0440 21.4.28 / Table 173: with RES < 12 bit the low 2/4/6 bits of LT1/HT1 must be 0.
     * An enabled AWD1 programmed for a finer resolution would silently compare wrongly:
     * reprogram it with ADC_Awd1Config (or disable it) first. */
    if ((adc->CFGR & ADC_CFGR_AWD1EN) != 0U)
    {
        uint32_t lsb_mask = (1U << (2U * (uint32_t)config->resolution)) - 1U;
        uint32_t tr1 = adc->TR1;
        uint32_t thresholds =
            ((tr1 & ADC_TR1_LT1) >> ADC_TR1_LT1_Pos) | ((tr1 & ADC_TR1_HT1) >> ADC_TR1_HT1_Pos);
        if ((thresholds & lsb_mask) != 0U)
        {
            return SPL_ERR_STATE;
        }
    }

    value = ((uint32_t)config->resolution << ADC_CFGR_RES_Pos) |
            ((uint32_t)config->align << ADC_CFGR_ALIGN_Pos) |
            ((uint32_t)config->continuous << ADC_CFGR_CONT_Pos) |
            ((uint32_t)config->ext_sel << ADC_CFGR_EXTSEL_Pos) |
            ((uint32_t)config->ext_edge << ADC_CFGR_EXTEN_Pos) |
            ((uint32_t)config->overrun_overwrite << ADC_CFGR_OVRMOD_Pos);
    if (config->disc_count != 0U)
    {
        value |= ADC_CFGR_DISCEN | (((uint32_t)config->disc_count - 1U) << ADC_CFGR_DISCNUM_Pos);
    }
    adc->CFGR = (adc->CFGR & ~fields) | value;
    return SPL_OK;
}

SPL_Status ADC_SetDifferentialChannels(ADC_TypeDef *adc, uint32_t mask)
{
    /* DIFSEL[0] is read-only (RM0440 21.7.21 register diagram; VINP[0] tied to VSSA). */
    const uint32_t writable = ADC_DIFSEL_DIFSEL & ~1UL;
    SPL_Status status;

    /* SAFE: reject invalid parameters before peripheral register access. */
    SPL_REQUIRE_SAFE(!((Adc_Valid(adc) == 0U) || ((mask & ~writable) != 0U) ||
        ((mask & Adc_InternalChannels(adc)) != 0U)), SPL_ERR_PARAM);
    status = Adc_Idle(adc);
    if (status != SPL_OK)
    {
        return status;
    }

    adc->DIFSEL = mask;
    return SPL_OK;
}

SPL_Status ADC_GetCalibrationFactors(ADC_TypeDef *adc, uint8_t *single, uint8_t *diff)
{
    uint32_t calfact;

    /* SAFE: reject invalid parameters before peripheral register access. */
    SPL_REQUIRE_SAFE(!((Adc_Valid(adc) == 0U) || (single == (uint8_t *)0) || (diff == (uint8_t *)0)), SPL_ERR_PARAM);
    if ((adc->CR & ADC_CR_ADCAL) != 0U)
    {
        return SPL_BUSY;
    }

    calfact = adc->CALFACT;
    *single = (uint8_t)((calfact & ADC_CALFACT_CALFACT_S) >> ADC_CALFACT_CALFACT_S_Pos);
    *diff = (uint8_t)((calfact & ADC_CALFACT_CALFACT_D) >> ADC_CALFACT_CALFACT_D_Pos);
    return SPL_OK;
}

SPL_Status ADC_SetCalibrationFactors(ADC_TypeDef *adc, uint8_t single, uint8_t diff)
{
    const uint32_t max = ADC_CALFACT_CALFACT_S_Msk >> ADC_CALFACT_CALFACT_S_Pos;
    SPL_Status status;

    /* SAFE: reject invalid parameters before peripheral register access. */
    SPL_REQUIRE_SAFE(!((Adc_Valid(adc) == 0U) || (single > max) || (diff > max)), SPL_ERR_PARAM);
    /* RM0440 21.7.22: ADEN = 1, ADSTART = 0 and JADSTART = 0. */
    if (((adc->CR & ADC_CR_ADEN) == 0U) || ((adc->CR & ADC_CR_ADDIS) != 0U))
    {
        return SPL_ERR_STATE;
    }
    status = Adc_NoConversion(adc, ADC_CR_ADSTART | ADC_CR_JADSTART);
    if (status != SPL_OK)
    {
        return status;
    }

    adc->CALFACT = ((uint32_t)diff << ADC_CALFACT_CALFACT_D_Pos) |
                   ((uint32_t)single << ADC_CALFACT_CALFACT_S_Pos);
    return SPL_OK;
}

SPL_Status ADC_OversamplingConfig(ADC_TypeDef *adc, const ADC_OversamplingTypeDef *config)
{
    const uint32_t fields = ADC_CFGR2_ROVSE | ADC_CFGR2_OVSR | ADC_CFGR2_OVSS | ADC_CFGR2_TROVS;
    SPL_Status status;

    /* SAFE: reject invalid parameters before peripheral register access. */
    SPL_REQUIRE_SAFE(!((Adc_Valid(adc) == 0U) || (config == (const ADC_OversamplingTypeDef *)0)), SPL_ERR_PARAM);
    /* SAFE: reject invalid parameters before peripheral register access. */
    SPL_REQUIRE_SAFE(!((config->enable > 1U) || (config->ratio > 7U) || (config->shift > 8U) ||
        (config->triggered > 1U)), SPL_ERR_PARAM);
    /* RM0440 21.4.29: result wider than 16 bits after the shift is truncated
     * without saturation. Checked when regular (this call) or injected
     * (CFGR2.JOVSE, same OVSR/OVSS) oversampling is on. */
    /* SAFE: reject invalid parameters before peripheral register access. */
    SPL_REQUIRE_SAFE(!(((config->enable != 0U) || ((adc->CFGR2 & ADC_CFGR2_JOVSE) != 0U)) &&
        (Adc_OvsFits((adc->CFGR & ADC_CFGR_RES) >> ADC_CFGR_RES_Pos, config->ratio,
                     config->shift) == 0U)), SPL_ERR_PARAM);
    /* ROVSE requires ADSTART = 0 and JADSTART = 0 (RM0440 21.7.5). */
    status = Adc_NoConversion(adc, ADC_CR_ADSTART | ADC_CR_JADSTART);
    if (status != SPL_OK)
    {
        return status;
    }

    adc->CFGR2 = (adc->CFGR2 & ~fields) | ((uint32_t)config->enable << ADC_CFGR2_ROVSE_Pos) |
                 ((uint32_t)config->ratio << ADC_CFGR2_OVSR_Pos) |
                 ((uint32_t)config->shift << ADC_CFGR2_OVSS_Pos) |
                 ((uint32_t)config->triggered << ADC_CFGR2_TROVS_Pos);
    return SPL_OK;
}

SPL_Status ADC_Awd1Config(ADC_TypeDef *adc, const ADC_Awd1ConfigTypeDef *config)
{
    const uint32_t fields = ADC_CFGR_AWD1EN | ADC_CFGR_AWD1SGL | ADC_CFGR_AWD1CH;
    uint32_t res;
    uint32_t lsb_mask;
    SPL_Status status;

    /* SAFE: reject invalid parameters before peripheral register access. */
    SPL_REQUIRE_SAFE(!((Adc_Valid(adc) == 0U) || (config == (const ADC_Awd1ConfigTypeDef *)0)), SPL_ERR_PARAM);
    /* SAFE: reject invalid parameters before peripheral register access. */
    SPL_REQUIRE_SAFE(!((config->enable > 1U) || (config->single > 1U) || (config->channel > ADC_CHANNEL_MAX) ||
        (config->filter > 7U) || (config->low > 0xFFFU) || (config->high > 0xFFFU) ||
        (config->low > config->high)), SPL_ERR_PARAM);
    /* RM0440 Table 173: thresholds are compared with left-aligned 12-bit data; for 10/8/6-bit
     * resolution the low 2/4/6 threshold bits must be zero. */
    res = (adc->CFGR & ADC_CFGR_RES) >> ADC_CFGR_RES_Pos;
    lsb_mask = (1U << (2U * res)) - 1U;
    /* SAFE: reject invalid parameters before peripheral register access. */
    SPL_REQUIRE_SAFE(!((((uint32_t)config->low | (uint32_t)config->high) & lsb_mask) != 0U), SPL_ERR_PARAM);
    status = Adc_NoConversion(adc, ADC_CR_ADSTART | ADC_CR_JADSTART);
    if (status != SPL_OK)
    {
        return status;
    }

    adc->TR1 = ((uint32_t)config->high << ADC_TR1_HT1_Pos) |
               ((uint32_t)config->filter << ADC_TR1_AWDFILT_Pos) |
               ((uint32_t)config->low << ADC_TR1_LT1_Pos);
    adc->CFGR = (adc->CFGR & ~fields) | ((uint32_t)config->enable << ADC_CFGR_AWD1EN_Pos) |
                ((uint32_t)config->single << ADC_CFGR_AWD1SGL_Pos) |
                ((uint32_t)config->channel << ADC_CFGR_AWD1CH_Pos);
    return SPL_OK;
}

SPL_Status ADC_GetFlags(ADC_TypeDef *adc, uint32_t *flags)
{
    /* SAFE: reject invalid parameters before peripheral register access. */
    SPL_REQUIRE_SAFE(!((Adc_Valid(adc) == 0U) || (flags == (uint32_t *)0)), SPL_ERR_PARAM);

    *flags = adc->ISR & ADC_ISR_ALL;
    return SPL_OK;
}

SPL_Status ADC_ClearFlags(ADC_TypeDef *adc, uint32_t flags)
{
    /* SAFE: reject invalid parameters before peripheral register access. */
    SPL_REQUIRE_SAFE(!((Adc_Valid(adc) == 0U) || (flags == 0U) || ((flags & ~ADC_FLAGS_CLEARABLE) != 0U)), SPL_ERR_PARAM);

    /* W1C: write only the selected flags, never read-modify-write. */
    adc->ISR = flags;
    /* Read back: the clear reaches the peripheral before return (METHOD 1.7, issue #23). */
    (void)adc->ISR;
    return SPL_OK;
}

SPL_Status ADC_StopRegular(ADC_TypeDef *adc)
{
    uint32_t cr;

    /* SAFE: reject invalid parameters before peripheral register access. */
    SPL_REQUIRE_SAFE(!(Adc_Valid(adc) == 0U), SPL_ERR_PARAM);
    cr = adc->CR;
    /* Already stopping, or nothing to stop (RM: ADSTP only with ADSTART = 1). */
    if (((cr & ADC_CR_ADSTP) != 0U) || ((cr & ADC_CR_ADSTART) == 0U))
    {
        return SPL_OK;
    }
    /* RM0440 21.7.3: ADSTP only with ADDIS = 0. */
    if ((cr & ADC_CR_ADDIS) != 0U)
    {
        return SPL_ERR_STATE;
    }

    adc->CR = (cr & (uint32_t)~ADC_CR_COMMANDS) | ADC_CR_ADSTP;
    return SPL_OK;
}

SPL_Status ADC_RegularStopped(ADC_TypeDef *adc, uint8_t *stopped)
{
    /* SAFE: reject invalid parameters before peripheral register access. */
    SPL_REQUIRE_SAFE(!((Adc_Valid(adc) == 0U) || (stopped == (uint8_t *)0)), SPL_ERR_PARAM);

    *stopped = (uint8_t)(((adc->CR & (ADC_CR_ADSTART | ADC_CR_ADSTP)) == 0U) ? 1U : 0U);
    return SPL_OK;
}

SPL_Status ADC_Disable(ADC_TypeDef *adc)
{
    uint32_t cr;

    /* SAFE: reject invalid parameters before peripheral register access. */
    SPL_REQUIRE_SAFE(!(Adc_Valid(adc) == 0U), SPL_ERR_PARAM);
    cr = adc->CR;
    if ((cr & ADC_CR_ADDIS) != 0U)
    {
        return SPL_OK;
    }
    if ((cr & (ADC_CR_ADSTP | ADC_CR_JADSTP)) != 0U)
    {
        return SPL_BUSY;
    }
    /* RM0440 21.4.9: stop conversions before ADDIS. */
    if ((cr & (ADC_CR_ADSTART | ADC_CR_JADSTART)) != 0U)
    {
        return SPL_ERR_STATE;
    }
    if ((cr & ADC_CR_ADCAL) != 0U)
    {
        return SPL_BUSY;
    }
    if ((cr & ADC_CR_ADEN) == 0U)
    {
        return SPL_OK;
    }

    adc->CR = (cr & (uint32_t)~ADC_CR_COMMANDS) | ADC_CR_ADDIS;
    return SPL_OK;
}

SPL_Status ADC_Disabled(ADC_TypeDef *adc, uint8_t *disabled)
{
    /* SAFE: reject invalid parameters before peripheral register access. */
    SPL_REQUIRE_SAFE(!((Adc_Valid(adc) == 0U) || (disabled == (uint8_t *)0)), SPL_ERR_PARAM);

    *disabled = (uint8_t)(((adc->CR & (ADC_CR_ADEN | ADC_CR_ADDIS)) == 0U) ? 1U : 0U);
    return SPL_OK;
}
