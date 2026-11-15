/* ADC driver for STM32G0B1 (RM0444 Rev 6 section 15, ES0548 Rev 4 section 2.6). Written for G0;
 * the G4 v0.13.5 ADC driver gave the API names, the state rules for the shared commands
 * (ADEN/ADSTART/ADSTP/ADDIS) and the oversampling overflow rule (#19). */
/* Kutuphane birimi: spl_check.h karisma korumasi referansini bu birim de uretir; uretmeyen tek
 * birim stm32g0_check.c (SPL_CHECK_LEVEL_DEFINER). BULGU-02, CHECK_LEVEL.md 3.2. */
#define SPL_LIBRARY_BUILD 1
#include "stm32g0_adc.h"

/* ADC_CR command bits are "rs": writing 0 has no effect, writing 1 issues the command. Every CR
 * write keeps ADVREGEN and issues exactly one command. */
#define ADC_CR_PENDING (ADC_CR_ADDIS | ADC_CR_ADSTP | ADC_CR_ADCAL)
#define ADC_INTERNAL_ALL (ADC_CCR_VREFEN | ADC_CCR_TSEN | ADC_CCR_VBATEN)
#define ADC_SEQ_BITS (ADC_CFGR1_CHSELRMOD | ADC_CFGR1_SCANDIR)
#define ADC_AWD1_BITS (ADC_CFGR1_AWD1CH | ADC_CFGR1_AWD1EN | ADC_CFGR1_AWD1SGL)
#define ADC_SQ_END 0xFU
#define ADC_THRESHOLD_MAX 0xFFFU

/* Denetim seviyesi (A4 dalga 5, Docs/CHECK_LEVEL.md 5.4 ADC tablosu): SPL_REQUIRE = normal sinif
 * (yalniz FULL), SPL_REQUIRE_SAFE = SAFE sinif (FULL ve MIX). Bekleyen komut / CCRDY (BUSY), OVR
 * ve WOULD_BLOCK result: her seviyede. Yalniz denetimde kullanilan yardimcilar static inline (saf,
 * Tools/pure_checks.txt). */

static inline uint8_t Adc_Valid(const ADC_TypeDef *adc)
{
    return (uint8_t)((adc == ADC1) ? 1U : 0U);
}

/* Command write: keeps ADVREGEN, never re-issues a command bit read as 1. */
static void Adc_Command(ADC_TypeDef *adc, uint32_t command)
{
    adc->CR = (adc->CR & ADC_CR_ADVREGEN) | command;
}

/* ADC fully disabled (CFGR1, CFGR2, CCR.PRESC, AWDxCR, ADVREGEN rules): pending command BUSY,
 * ADEN or ADSTART set ERR_STATE. */
static SPL_Status Adc_Off(const ADC_TypeDef *adc)
{
    const uint32_t cr = adc->CR;

    if ((cr & ADC_CR_PENDING) != 0U)
    {
        return SPL_BUSY;
    }
    /* SAFE: ADEN / ADSTART = 1 iken CFGR1 / CFGR2 / AWD yazimi yok sayilir (ES0548 2.6.2 / 2.6.4).
     */
    SPL_REQUIRE_SAFE((cr & (ADC_CR_ADEN | ADC_CR_ADSTART)) == 0U, SPL_ERR_STATE);
    return SPL_OK;
}

/* No conversion running (SMPR, CHSELR, CCR internal-channel rules). */
static SPL_Status Adc_NoConversion(const ADC_TypeDef *adc)
{
    const uint32_t cr = adc->CR;

    if ((cr & (ADC_CR_ADSTP | ADC_CR_ADCAL)) != 0U)
    {
        return SPL_BUSY;
    }
    SPL_REQUIRE_SAFE((cr & ADC_CR_ADSTART) == 0U, SPL_ERR_STATE); /* SAFE: yazim yok sayilir */
    return SPL_OK;
}

/* Data width in bits for RES code 0..3: 12, 10, 8, 6. */
static inline uint32_t Adc_ResBits(uint32_t res)
{
    return 12U - (2U * res);
}

/* RM0444 Table 77: threshold LSBs that must be 0 for the resolution (no oversampling). */
static inline uint32_t Adc_ThresholdLowMask(uint32_t res)
{
    return (1U << (12U - Adc_ResBits(res))) - 1U;
}

/* 1 when the oversampled result fits 16 bits (RM0444 15.8, G4 v0.13.5 #19). */
static inline uint32_t Adc_OvsFits(uint32_t res, uint32_t ratio, uint32_t shift)
{
    return ((Adc_ResBits(res) + ratio + 1U) <= (16U + shift)) ? 1U : 0U;
}

/* 1 when thresholds (LT | HT image) are valid for the resolution and oversampling state. */
static inline uint32_t Adc_ThresholdsFit(uint32_t thresholds, uint32_t res, uint32_t ovse)
{
    if (ovse != 0U)
    {
        return 1U; /* RM0444 15.8.2: RES ignored, comparison on DR[15:4] */
    }
    return ((thresholds & Adc_ThresholdLowMask(res)) == 0U) ? 1U : 0U;
}

/* Number of channels in the sequence and the first converted one. */
static inline uint32_t Adc_SequenceFirst(uint32_t cfgr1, uint32_t chselr, uint32_t *first)
{
    uint32_t count = 0U;
    uint32_t k;

    *first = 0xFFU;
    if ((cfgr1 & ADC_CFGR1_CHSELRMOD) != 0U)
    {
        for (k = 0U; k < 8U; ++k)
        {
            const uint32_t sq = (chselr >> (4U * k)) & 0xFU;
            if (sq == ADC_SQ_END)
            {
                break;
            }
            if (count == 0U)
            {
                *first = sq;
            }
            ++count;
        }
        return count;
    }
    for (k = 0U; k <= ADC_CHANNEL_MAX; ++k)
    {
        const uint32_t ch = ((cfgr1 & ADC_CFGR1_SCANDIR) != 0U) ? (ADC_CHANNEL_MAX - k) : k;
        if (((chselr >> ch) & 1U) != 0U)
        {
            if (count == 0U)
            {
                *first = ch;
            }
            ++count;
        }
    }
    return count;
}

/* ERRATA ES0548 2.6.3: AWD1 misses the out-of-window value of a guarded channel that is not the
 * first of a multi-channel sequence in single mode (CONT = 0). 1 = this configuration. */
static inline uint32_t Adc_Awd1Hazard(uint32_t cfgr1, uint32_t chselr)
{
    uint32_t first;
    uint32_t count;

    if (((cfgr1 & (ADC_CFGR1_AWD1EN | ADC_CFGR1_AWD1SGL)) !=
         (ADC_CFGR1_AWD1EN | ADC_CFGR1_AWD1SGL)) ||
        ((cfgr1 & ADC_CFGR1_CONT) != 0U))
    {
        return 0U;
    }
    count = Adc_SequenceFirst(cfgr1, chselr, &first);
    return ((count > 1U) && (((cfgr1 & ADC_CFGR1_AWD1CH) >> ADC_CFGR1_AWD1CH_Pos) != first)) ? 1U
                                                                                             : 0U;
}

/* 1 when an enabled watchdog's thresholds break the low-bit rule for res. */
static inline uint32_t Adc_AwdThresholdConflict(const ADC_TypeDef *adc, uint32_t cfgr1,
                                                uint32_t res)
{
    const uint32_t ovse = adc->CFGR2 & ADC_CFGR2_OVSE;
    uint32_t conflict = 0U;

    if (((cfgr1 & ADC_CFGR1_AWD1EN) != 0U) && (Adc_ThresholdsFit(adc->AWD1TR, res, ovse) == 0U))
    {
        conflict = 1U;
    }
    if ((adc->AWD2CR != 0U) && (Adc_ThresholdsFit(adc->AWD2TR, res, ovse) == 0U))
    {
        conflict = 1U;
    }
    if ((adc->AWD3CR != 0U) && (Adc_ThresholdsFit(adc->AWD3TR, res, ovse) == 0U))
    {
        conflict = 1U;
    }
    return conflict;
}

/* 1 when an earlier channel configuration is not applied yet. CCRDY is cleared only by this
 * driver right before a channel write; CCRDY = 0 with the reset sequencer state (CHSELR = 0,
 * forward mask mode) is the never-configured state, not a pending one. */
static uint32_t Adc_ChannelConfigPending(const ADC_TypeDef *adc)
{
    if ((adc->ISR & ADC_ISR_CCRDY) != 0U)
    {
        return 0U;
    }
    return ((adc->CHSELR != 0U) || ((adc->CFGR1 & ADC_SEQ_BITS) != 0U)) ? 1U : 0U;
}

SPL_Status ADC_PowerRegulator(ADC_TypeDef *adc)
{
    SPL_Status st;

    SPL_REQUIRE_SAFE(Adc_Valid(adc) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    st = Adc_Off(adc);
    if ((st != SPL_OK) || ((adc->CR & ADC_CR_ADVREGEN) != 0U))
    {
        return st;
    }
    adc->CR = ADC_CR_ADVREGEN;
    return SPL_OK;
}

SPL_Status ADC_PowerRegulatorDisable(ADC_TypeDef *adc)
{
    SPL_Status st;

    SPL_REQUIRE_SAFE(Adc_Valid(adc) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    st = Adc_Off(adc);
    if ((st != SPL_OK) || ((adc->CR & ADC_CR_ADVREGEN) == 0U))
    {
        return st;
    }
    adc->CR = 0U; /* RM0444 15.3.2 disable sequence: ADEN = 0, then ADVREGEN = 0 */
    return SPL_OK;
}

SPL_Status ADC_StartCalibration(ADC_TypeDef *adc)
{
    SPL_Status st;

    SPL_REQUIRE_SAFE(Adc_Valid(adc) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    st = Adc_Off(adc);
    if (st != SPL_OK)
    {
        return st;
    }
    /* RM0444 15.3.3: ADVREGEN = 1, AUTOFF = 0, DMAEN = 0 (SAFE: kalibrasyon bozuk). */
    SPL_REQUIRE_SAFE(((adc->CR & ADC_CR_ADVREGEN) != 0U) &&
                         ((adc->CFGR1 & (ADC_CFGR1_AUTOFF | ADC_CFGR1_DMAEN)) == 0U),
                     SPL_ERR_STATE);
    adc->ISR = ADC_ISR_EOCAL;
    Adc_Command(adc, ADC_CR_ADCAL);
    return SPL_OK;
}

SPL_Status ADC_CalibrationDone(ADC_TypeDef *adc, uint8_t *done)
{
    SPL_REQUIRE_SAFE(Adc_Valid(adc) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE_SAFE(done != (uint8_t *)0, SPL_ERR_PARAM);
    *done = (uint8_t)(((adc->CR & ADC_CR_ADCAL) == 0U) ? 1U : 0U);
    return SPL_OK;
}

SPL_Status ADC_GetCalibrationFactor(ADC_TypeDef *adc, uint8_t *factor)
{
    SPL_REQUIRE_SAFE(Adc_Valid(adc) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE_SAFE(factor != (uint8_t *)0, SPL_ERR_PARAM);
    if ((adc->CR & ADC_CR_ADCAL) != 0U)
    {
        return SPL_BUSY;
    }
    *factor = (uint8_t)(adc->CALFACT & ADC_CALFACT_CALFACT_Msk);
    return SPL_OK;
}

SPL_Status ADC_SetCalibrationFactor(ADC_TypeDef *adc, uint8_t factor)
{
    uint32_t cr;

    SPL_REQUIRE_SAFE(Adc_Valid(adc) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE_SAFE(factor <= 0x7FU, SPL_ERR_PARAM);      /* SAFE: CALFACT tasmasi */
    cr = adc->CR;
    if ((cr & ADC_CR_PENDING) != 0U)
    {
        return SPL_BUSY;
    }
    /* RM0444 15.12.15: ADEN = 1, no calibration, no conversion (SAFE: yazim yok sayilir). */
    SPL_REQUIRE_SAFE(((cr & ADC_CR_ADEN) != 0U) && ((cr & ADC_CR_ADSTART) == 0U), SPL_ERR_STATE);
    adc->CALFACT = factor;
    return SPL_OK;
}

SPL_Status ADC_CommonClockConfig(ADC_TypeDef *adc, uint32_t ckmode, uint32_t presc)
{
    SPL_Status st;

    SPL_REQUIRE_SAFE(Adc_Valid(adc) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    /* SAFE: CKMODE / PRESC tasmasi; senkron saatte PRESC sessizce kullanilmaz. */
    SPL_REQUIRE_SAFE((ckmode <= ADC_CLOCK_SYNC_DIV1) && (presc <= ADC_PRESC_MAX) &&
                         ((ckmode == ADC_CLOCK_ASYNC) || (presc == 0U)),
                     SPL_ERR_PARAM);
    st = Adc_Off(adc);
    if (st != SPL_OK)
    {
        return st;
    }
    /* ERRATA ES0548 2.6.4 / RM0444 15.12.5: CFGR2 only with ADEN = 0 (checked above). */
    adc->CFGR2 = (adc->CFGR2 & ~ADC_CFGR2_CKMODE) | (ckmode << ADC_CFGR2_CKMODE_Pos);
    ADC1_COMMON->CCR = (ADC1_COMMON->CCR & ~ADC_CCR_PRESC) | (presc << ADC_CCR_PRESC_Pos);
    return SPL_OK;
}

SPL_Status ADC_InternalChannelsConfig(ADC_TypeDef *adc, uint32_t mask)
{
    SPL_Status st;

    SPL_REQUIRE_SAFE(Adc_Valid(adc) != 0U, SPL_ERR_PARAM);             /* SAFE: rastgele adres */
    SPL_REQUIRE_SAFE((mask & ~ADC_INTERNAL_ALL) == 0U, SPL_ERR_PARAM); /* SAFE: CCR'ye OR */
    st = Adc_NoConversion(adc);
    if (st != SPL_OK)
    {
        return st;
    }
    ADC1_COMMON->CCR = (ADC1_COMMON->CCR & ~ADC_INTERNAL_ALL) | mask;
    return SPL_OK;
}

SPL_Status ADC_RegularConfig(ADC_TypeDef *adc, const ADC_RegularConfigTypeDef *config)
{
    uint32_t cfgr1;
    uint32_t cfgr2;
    SPL_Status st;

    SPL_REQUIRE_SAFE(Adc_Valid(adc) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE_SAFE(config != (const ADC_RegularConfigTypeDef *)0, SPL_ERR_PARAM);
    /* SAFE: kaydirilan CFGR1 alanlari, RM yasak birlesimler. */
    SPL_REQUIRE_SAFE((config->resolution <= ADC_RES_6BIT) && (config->align <= ADC_ALIGN_LEFT) &&
                         (config->continuous <= 1U) && (config->discontinuous <= 1U) &&
                         (config->ext_sel <= 7U) && (config->ext_edge <= ADC_EDGE_BOTH) &&
                         (config->overrun_overwrite <= 1U) &&
                         ((config->continuous == 0U) || (config->discontinuous == 0U)) &&
                         ((config->ext_edge != ADC_EDGE_SOFTWARE) || (config->ext_sel == 0U)),
                     SPL_ERR_PARAM);
    st = Adc_Off(adc);
    if (st != SPL_OK)
    {
        return st;
    }
    cfgr1 = adc->CFGR1 & (ADC_SEQ_BITS | ADC_AWD1_BITS | ADC_CFGR1_DMAEN | ADC_CFGR1_DMACFG);
    cfgr1 |= ((uint32_t)config->resolution << ADC_CFGR1_RES_Pos) |
             ((uint32_t)config->align << ADC_CFGR1_ALIGN_Pos) |
             ((uint32_t)config->continuous << ADC_CFGR1_CONT_Pos) |
             ((uint32_t)config->discontinuous << ADC_CFGR1_DISCEN_Pos) |
             ((uint32_t)config->ext_sel << ADC_CFGR1_EXTSEL_Pos) |
             ((uint32_t)config->ext_edge << ADC_CFGR1_EXTEN_Pos) |
             ((uint32_t)config->overrun_overwrite << ADC_CFGR1_OVRMOD_Pos);
    SPL_REQUIRE_SAFE(Adc_Awd1Hazard(cfgr1, adc->CHSELR) == 0U, SPL_ERR_PARAM); /* ES0548 2.6.3 */
    cfgr2 = adc->CFGR2;
    (void)cfgr2;
    /* SAFE: asiri ornekleme sonucu 16 biti asar; bekci esikleri cozunurluge uymaz. */
    SPL_REQUIRE_SAFE(
        ((cfgr2 & ADC_CFGR2_OVSE) == 0U) ||
            (Adc_OvsFits(config->resolution, (cfgr2 & ADC_CFGR2_OVSR) >> ADC_CFGR2_OVSR_Pos,
                         (cfgr2 & ADC_CFGR2_OVSS) >> ADC_CFGR2_OVSS_Pos) != 0U),
        SPL_ERR_STATE);
    SPL_REQUIRE_SAFE(Adc_AwdThresholdConflict(adc, cfgr1, config->resolution) == 0U, SPL_ERR_STATE);
    /* ERRATA ES0548 2.6.2: CFGR1 only with ADEN = 0 (checked above), never auto disable. */
    adc->CFGR1 = cfgr1;
    return SPL_OK;
}

/* A4 (CHECK_LEVEL.md 4.2): deger ureten dogrulayici ikiye ayrildi. Maske kipinde maske 0 /
 * tanimli kanallar disi normal sinif (CHSELR = 0 ADC_Start'ta gorunur; ayrilmis bitler). */
static inline uint32_t Adc_ChannelMaskValid(const ADC_ChannelConfigTypeDef *config)
{
    return (((config->mode != ADC_SEQ_FORWARD) && (config->mode != ADC_SEQ_BACKWARD)) ||
            ((config->mask != 0U) && ((config->mask & ~ADC_CHANNEL_MASK) == 0U)))
               ? 1U
               : 0U;
}

/* SAFE: kip, sirali uzunluk ve sirali kanal araligi (kanal komsu dortluye tasar). */
static inline uint32_t Adc_ChannelSeqValid(const ADC_ChannelConfigTypeDef *config)
{
    uint32_t k;

    if ((config->mode == ADC_SEQ_FORWARD) || (config->mode == ADC_SEQ_BACKWARD))
    {
        return 1U;
    }
    if ((config->mode != ADC_SEQ_ORDERED) || (config->length == 0U) || (config->length > 8U))
    {
        return 0U;
    }
    for (k = 0U; k < config->length; ++k)
    {
        if (config->channels[k] > ADC_ORDERED_CHANNEL_MAX)
        {
            return 0U;
        }
    }
    return 1U;
}

/* Builds CHSELR and the CFGR1 sequencer bits from a validated configuration. */
static void Adc_ChannelImage(const ADC_ChannelConfigTypeDef *config, uint32_t *chselr,
                             uint32_t *seq)
{
    uint32_t k;

    if ((config->mode == ADC_SEQ_FORWARD) || (config->mode == ADC_SEQ_BACKWARD))
    {
        *chselr = config->mask;
        *seq = (config->mode == ADC_SEQ_BACKWARD) ? ADC_CFGR1_SCANDIR : 0U;
        return;
    }
    *chselr = 0U;
    for (k = 0U; k < 8U; ++k)
    {
        uint32_t sq = ADC_SQ_END;
        if (k < config->length)
        {
            sq = config->channels[k];
        }
        *chselr |= sq << (4U * k);
    }
    *seq = ADC_CFGR1_CHSELRMOD;
}

SPL_Status ADC_ChannelConfig(ADC_TypeDef *adc, const ADC_ChannelConfigTypeDef *config)
{
    uint32_t chselr = 0U;
    uint32_t seq = 0U;
    uint32_t cfgr1;
    SPL_Status st;

    SPL_REQUIRE_SAFE(Adc_Valid(adc) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE_SAFE(config != (const ADC_ChannelConfigTypeDef *)0, SPL_ERR_PARAM);
    SPL_REQUIRE(Adc_ChannelMaskValid(config) != 0U, SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(Adc_ChannelSeqValid(config) != 0U, SPL_ERR_PARAM);
    Adc_ChannelImage(config, &chselr, &seq);
    st = Adc_NoConversion(adc);
    if (st != SPL_OK)
    {
        return st;
    }
    if ((adc->CR & ADC_CR_ADDIS) != 0U)
    {
        return SPL_BUSY;
    }
    cfgr1 = (adc->CFGR1 & ~ADC_SEQ_BITS) | seq;
    SPL_REQUIRE_SAFE(Adc_Awd1Hazard(cfgr1, chselr) == 0U, SPL_ERR_PARAM); /* ES0548 2.6.3 */
    /* RM0444 15.3.8: a write before CCRDY rose for the previous one is ignored by hardware. */
    if (Adc_ChannelConfigPending(adc) != 0U)
    {
        return SPL_BUSY;
    }
    if ((adc->CFGR1 & ADC_SEQ_BITS) != seq)
    {
        /* CFGR1 only with ADEN = 0 (ERRATA ES0548 2.6.2); SAFE. */
        SPL_REQUIRE_SAFE((adc->CR & ADC_CR_ADEN) == 0U, SPL_ERR_STATE);
        adc->ISR = ADC_ISR_CCRDY;
        adc->CFGR1 = cfgr1;
        return SPL_WOULD_BLOCK; /* call again after CCRDY to write CHSELR */
    }
    adc->ISR = ADC_ISR_CCRDY;
    adc->CHSELR = chselr;
    return SPL_OK;
}

SPL_Status ADC_ChannelConfigReady(ADC_TypeDef *adc, uint8_t *ready)
{
    SPL_REQUIRE_SAFE(Adc_Valid(adc) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE_SAFE(ready != (uint8_t *)0, SPL_ERR_PARAM);
    *ready = (uint8_t)(((adc->ISR & ADC_ISR_CCRDY) != 0U) ? 1U : 0U);
    return SPL_OK;
}

SPL_Status ADC_RegularSingleInit(ADC_TypeDef *adc, uint8_t channel, uint8_t sample_code)
{
    uint32_t cfgr2;
    SPL_Status st;

    SPL_REQUIRE_SAFE(Adc_Valid(adc) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE_SAFE((channel <= ADC_CHANNEL_MAX) && (sample_code <= 7U), SPL_ERR_PARAM);
    st = Adc_Off(adc);
    if (st != SPL_OK)
    {
        return st;
    }
    /* sequencer mode change needs the ADC_ChannelConfig handshake (SAFE: CHSELR yok sayilir) */
    SPL_REQUIRE_SAFE((adc->CFGR1 & ADC_SEQ_BITS) == 0U, SPL_ERR_STATE);
    cfgr2 = adc->CFGR2;
    (void)cfgr2;
    /* SAFE: sonuc 16 biti asar. */
    SPL_REQUIRE_SAFE(((cfgr2 & ADC_CFGR2_OVSE) == 0U) ||
                         (Adc_OvsFits(ADC_RES_12BIT, (cfgr2 & ADC_CFGR2_OVSR) >> ADC_CFGR2_OVSR_Pos,
                                      (cfgr2 & ADC_CFGR2_OVSS) >> ADC_CFGR2_OVSS_Pos) != 0U),
                     SPL_ERR_STATE);
    if (Adc_ChannelConfigPending(adc) != 0U)
    {
        return SPL_BUSY;
    }
    adc->CFGR1 = adc->CFGR1 & ADC_AWD1_BITS;
    adc->SMPR = (uint32_t)sample_code << ADC_SMPR_SMP1_Pos;
    adc->ISR = ADC_ISR_CCRDY;
    adc->CHSELR = (uint32_t)1U << channel;
    return SPL_OK;
}

SPL_Status ADC_SamplingConfig(ADC_TypeDef *adc, uint8_t smp1, uint8_t smp2, uint32_t smp2_channels)
{
    SPL_Status st;

    SPL_REQUIRE_SAFE(Adc_Valid(adc) != 0U, SPL_ERR_PARAM);         /* SAFE: rastgele adres */
    SPL_REQUIRE_SAFE((smp1 <= 7U) && (smp2 <= 7U), SPL_ERR_PARAM); /* SAFE: SMP tasmasi */
    /* SAFE (EDR-003): ayrilmis bite yazim. */
    SPL_REQUIRE_SAFE((smp2_channels & ~ADC_CHANNEL_MASK) == 0U, SPL_ERR_PARAM);
    st = Adc_NoConversion(adc);
    if (st != SPL_OK)
    {
        return st;
    }
    adc->SMPR = ((uint32_t)smp1 << ADC_SMPR_SMP1_Pos) | ((uint32_t)smp2 << ADC_SMPR_SMP2_Pos) |
                (smp2_channels << ADC_SMPR_SMPSEL_Pos);
    return SPL_OK;
}

SPL_Status ADC_Enable(ADC_TypeDef *adc)
{
    uint32_t cr;

    SPL_REQUIRE_SAFE(Adc_Valid(adc) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    cr = adc->CR;
    if ((cr & (ADC_CR_ADDIS | ADC_CR_ADSTP)) != 0U)
    {
        return SPL_BUSY;
    }
    /* RM0444 15.12.3 ADEN note: ADVREGEN = 1, no calibration (SAFE: ADEN yok sayilir). */
    SPL_REQUIRE_SAFE(((cr & ADC_CR_ADVREGEN) != 0U) && ((cr & ADC_CR_ADCAL) == 0U), SPL_ERR_STATE);
    if ((cr & ADC_CR_ADEN) != 0U)
    {
        return SPL_OK; /* never re-issue ADEN: clearing ADRDY would lose the ready state */
    }
    /* v0.11: with AUTOFF = 1 the power-on is automatic and ADRDY is never set (RM0444 15.6.2,
     * 15.3.4 note): ADEN only. */
    if ((adc->CFGR1 & ADC_CFGR1_AUTOFF) == 0U)
    {
        adc->ISR = ADC_ISR_ADRDY;
    }
    Adc_Command(adc, ADC_CR_ADEN);
    return SPL_OK;
}

SPL_Status ADC_Ready(ADC_TypeDef *adc, uint8_t *ready)
{
    SPL_REQUIRE_SAFE(Adc_Valid(adc) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE_SAFE(ready != (uint8_t *)0, SPL_ERR_PARAM);
    /* v0.11: AUTOFF = 1 -> ready as soon as ADEN = 1 (ADRDY is never set, RM0444 15.6.2). */
    *ready =
        (uint8_t)((((adc->CR & ADC_CR_ADEN) != 0U) &&
                   (((adc->ISR & ADC_ISR_ADRDY) != 0U) || ((adc->CFGR1 & ADC_CFGR1_AUTOFF) != 0U)))
                      ? 1U
                      : 0U);
    return SPL_OK;
}

SPL_Status ADC_Start(ADC_TypeDef *adc)
{
    uint32_t cr;

    SPL_REQUIRE_SAFE(Adc_Valid(adc) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    cr = adc->CR;
    if ((cr & (ADC_CR_ADDIS | ADC_CR_ADSTP)) != 0U)
    {
        return SPL_BUSY;
    }
    /* SAFE: ADSTART sessizce yok sayilir. */
    SPL_REQUIRE_SAFE(((cr & ADC_CR_ADEN) != 0U) && (((adc->ISR & ADC_ISR_ADRDY) != 0U) ||
                                                    ((adc->CFGR1 & ADC_CFGR1_AUTOFF) != 0U)),
                     SPL_ERR_STATE);
    if ((cr & ADC_CR_ADSTART) != 0U)
    {
        /* RM0444 15.12.3: ADSTART clears by itself only in single mode with software trigger.
         * SAFE: BUSY surer, bekleme dongusu takilir (K-A4-4 ilkesi). */
        SPL_REQUIRE_SAFE((adc->CFGR1 & (ADC_CFGR1_CONT | ADC_CFGR1_EXTEN)) == 0U, SPL_ERR_STATE);
        return SPL_BUSY;
    }
    if ((adc->ISR & ADC_ISR_OVR) != 0U)
    {
        return SPL_ERR_HW; /* keep the evidence until ADC_ClearErrors */
    }
    /* no channel selected (SAFE: EOC hic gelmez, K-A4-4 ilkesi) */
    SPL_REQUIRE_SAFE(((adc->CFGR1 & ADC_CFGR1_CHSELRMOD) != 0U) || (adc->CHSELR != 0U),
                     SPL_ERR_STATE);
    if ((adc->ISR & ADC_ISR_CCRDY) == 0U)
    {
        return SPL_BUSY; /* RM0444 15.3.8: ADSTART would be ignored before CCRDY */
    }
    adc->ISR = ADC_ISR_EOC | ADC_ISR_EOS;
    Adc_Command(adc, ADC_CR_ADSTART);
    return SPL_OK;
}

SPL_Status ADC_TryRead(ADC_TypeDef *adc, uint16_t *value)
{
    SPL_REQUIRE_SAFE(Adc_Valid(adc) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE_SAFE(value != (uint16_t *)0, SPL_ERR_PARAM);
    if ((adc->ISR & ADC_ISR_OVR) != 0U)
    {
        return SPL_ERR_HW;
    }
    if ((adc->ISR & ADC_ISR_EOC) == 0U)
    {
        /* SAFE (K-A4-4 ilkesi): ADC kapali ya da donusum bitti, WOULD_BLOCK surerdi. */
        SPL_REQUIRE_SAFE((adc->CR & ADC_CR_ADEN) != 0U, SPL_ERR_STATE);
        /* EOS clears ADSTART right after the last EOC: read EOC again after ADSTART = 0. */
        if ((adc->CR & ADC_CR_ADSTART) == 0U)
        {
            /* SAFE (K-A4-4): donusum yok ve EOC yok: WOULD_BLOCK sessiz takilma olurdu, DR cop.
             * ADEN = 0 iken ADSTART da 0 oldugu icin ustteki ADEN denetimini de kapsar. */
            SPL_REQUIRE_SAFE((adc->ISR & ADC_ISR_EOC) != 0U, SPL_ERR_STATE);
        }
        else
        {
            return SPL_WOULD_BLOCK;
        }
    }
    *value = (uint16_t)(adc->DR & 0xFFFFU);
    return SPL_OK;
}

SPL_Status ADC_ClearErrors(ADC_TypeDef *adc)
{
    SPL_REQUIRE_SAFE(Adc_Valid(adc) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    adc->ISR = ADC_ISR_OVR;
    /* Read back: the clear reaches the peripheral before return (METHOD 1.7, issue #23). */
    (void)adc->ISR;
    return SPL_OK;
}

SPL_Status ADC_EnableDma(ADC_TypeDef *adc, uint8_t circular)
{
    SPL_Status st;

    SPL_REQUIRE_SAFE(Adc_Valid(adc) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE_SAFE(circular <= 1U, SPL_ERR_PARAM);       /* SAFE: DMACFG SCANDIR'a tasar */
    st = Adc_Off(adc);
    if (st != SPL_OK)
    {
        return st;
    }
    adc->CFGR1 = (adc->CFGR1 & ~ADC_CFGR1_DMACFG) | ADC_CFGR1_DMAEN |
                 ((uint32_t)circular << ADC_CFGR1_DMACFG_Pos);
    return SPL_OK;
}

SPL_Status ADC_DisableDma(ADC_TypeDef *adc)
{
    SPL_Status st;

    SPL_REQUIRE_SAFE(Adc_Valid(adc) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    st = Adc_Off(adc);
    if (st != SPL_OK)
    {
        return st;
    }
    adc->CFGR1 = adc->CFGR1 & ~(ADC_CFGR1_DMAEN | ADC_CFGR1_DMACFG);
    return SPL_OK;
}

SPL_Status ADC_OversamplingConfig(ADC_TypeDef *adc, const ADC_OversamplingTypeDef *config)
{
    uint32_t res;
    SPL_Status st;

    SPL_REQUIRE_SAFE(Adc_Valid(adc) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE_SAFE(config != (const ADC_OversamplingTypeDef *)0, SPL_ERR_PARAM);
    /* SAFE: kaydirilan CFGR2 alanlari. */
    SPL_REQUIRE_SAFE((config->enable <= 1U) && (config->ratio <= 7U) && (config->shift <= 8U) &&
                         (config->triggered <= 1U),
                     SPL_ERR_PARAM);
    res = (adc->CFGR1 & ADC_CFGR1_RES) >> ADC_CFGR1_RES_Pos;
    (void)res;
    /* SAFE: sonuc 16 biti asar. */
    SPL_REQUIRE_SAFE((config->enable == 0U) ||
                         (Adc_OvsFits(res, config->ratio, config->shift) != 0U),
                     SPL_ERR_PARAM);
    st = Adc_Off(adc);
    if (st != SPL_OK)
    {
        return st;
    }
    adc->CFGR2 = (adc->CFGR2 & (ADC_CFGR2_CKMODE | ADC_CFGR2_LFTRIG)) |
                 ((uint32_t)config->enable << ADC_CFGR2_OVSE_Pos) |
                 ((uint32_t)config->ratio << ADC_CFGR2_OVSR_Pos) |
                 ((uint32_t)config->shift << ADC_CFGR2_OVSS_Pos) |
                 ((uint32_t)config->triggered << ADC_CFGR2_TOVS_Pos);
    return SPL_OK;
}

/* Threshold parameter check shared by the watchdog APIs (enable = watchdog will be on). */
static inline uint32_t Adc_ThresholdsValid(const ADC_TypeDef *adc, uint32_t low, uint32_t high,
                                           uint32_t enable)
{
    const uint32_t res = (adc->CFGR1 & ADC_CFGR1_RES) >> ADC_CFGR1_RES_Pos;

    if ((low > ADC_THRESHOLD_MAX) || (high > ADC_THRESHOLD_MAX) || (low > high))
    {
        return 0U;
    }
    if ((enable != 0U) && (Adc_ThresholdsFit(low | high, res, adc->CFGR2 & ADC_CFGR2_OVSE) == 0U))
    {
        return 0U;
    }
    return 1U;
}

SPL_Status ADC_Awd1Config(ADC_TypeDef *adc, const ADC_Awd1ConfigTypeDef *config)
{
    uint32_t cfgr1;
    SPL_Status st;

    SPL_REQUIRE_SAFE(Adc_Valid(adc) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE_SAFE(config != (const ADC_Awd1ConfigTypeDef *)0, SPL_ERR_PARAM);
    /* SAFE: kaydirilan AWD1 alanlari, esik araligi / RM0444 Table 77. */
    SPL_REQUIRE_SAFE(
        (config->enable <= 1U) && (config->single <= 1U) && (config->channel <= ADC_CHANNEL_MAX) &&
            (Adc_ThresholdsValid(adc, config->low, config->high, config->enable) != 0U),
        SPL_ERR_PARAM);
    cfgr1 = (adc->CFGR1 & ~ADC_AWD1_BITS) | ((uint32_t)config->channel << ADC_CFGR1_AWD1CH_Pos) |
            ((uint32_t)config->enable << ADC_CFGR1_AWD1EN_Pos) |
            ((uint32_t)config->single << ADC_CFGR1_AWD1SGL_Pos);
    SPL_REQUIRE_SAFE(Adc_Awd1Hazard(cfgr1, adc->CHSELR) == 0U, SPL_ERR_PARAM); /* ES0548 2.6.3 */
    st = Adc_Off(adc);
    if (st != SPL_OK)
    {
        return st;
    }
    adc->AWD1TR = ((uint32_t)config->high << ADC_AWD1TR_HT1_Pos) | config->low;
    adc->CFGR1 = cfgr1;
    return SPL_OK;
}

SPL_Status ADC_AwdConfig(ADC_TypeDef *adc, uint8_t index, uint16_t low, uint16_t high,
                         uint32_t channels)
{
    const uint32_t tr = ((uint32_t)high << ADC_AWD2TR_HT2_Pos) | low;
    SPL_Status st;

    SPL_REQUIRE_SAFE(Adc_Valid(adc) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    /* SAFE (EDR-003): ayrilmis bite yazim. */
    SPL_REQUIRE_SAFE((channels & ~ADC_CHANNEL_MASK) == 0U, SPL_ERR_PARAM);
    /* SAFE: AWD3 sessizce yazilir; esik araligi / RM0444 Table 77. */
    SPL_REQUIRE_SAFE(((index == 2U) || (index == 3U)) &&
                         (Adc_ThresholdsValid(adc, low, high, (channels != 0U) ? 1U : 0U) != 0U),
                     SPL_ERR_PARAM);
    st = Adc_Off(adc);
    if (st != SPL_OK)
    {
        return st;
    }
    if (index == 2U)
    {
        adc->AWD2TR = tr;
        adc->AWD2CR = channels;
    }
    else
    {
        adc->AWD3TR = tr;
        adc->AWD3CR = channels;
    }
    return SPL_OK;
}

SPL_Status ADC_GetFlags(ADC_TypeDef *adc, uint32_t *flags)
{
    SPL_REQUIRE_SAFE(Adc_Valid(adc) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE_SAFE(flags != (uint32_t *)0, SPL_ERR_PARAM);
    *flags = adc->ISR;
    return SPL_OK;
}

SPL_Status ADC_ClearFlags(ADC_TypeDef *adc, uint32_t flags)
{
    SPL_REQUIRE_SAFE(Adc_Valid(adc) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE(flags != 0U, SPL_ERR_PARAM);               /* normal: etkisiz */
    /* SAFE (EDR-003): ayrilmis bite yazim. */
    SPL_REQUIRE_SAFE((flags & ~ADC_FLAGS_CLEARABLE) == 0U, SPL_ERR_PARAM);
    adc->ISR = flags; /* W1C: only the selected flags, no read-modify-write */
    /* Read back: the clear reaches the peripheral before return (METHOD 1.7, issue #23). */
    (void)adc->ISR;
    return SPL_OK;
}

SPL_Status ADC_StopRegular(ADC_TypeDef *adc)
{
    uint32_t cr;

    SPL_REQUIRE_SAFE(Adc_Valid(adc) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    cr = adc->CR;
    if (((cr & ADC_CR_ADSTP) != 0U) || ((cr & ADC_CR_ADSTART) == 0U))
    {
        return SPL_OK;
    }
    SPL_REQUIRE_SAFE((cr & ADC_CR_ADDIS) == 0U, SPL_ERR_STATE); /* RM0444 15.12.3; SAFE */
    Adc_Command(adc, ADC_CR_ADSTP);
    return SPL_OK;
}

SPL_Status ADC_RegularStopped(ADC_TypeDef *adc, uint8_t *stopped)
{
    SPL_REQUIRE_SAFE(Adc_Valid(adc) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE_SAFE(stopped != (uint8_t *)0, SPL_ERR_PARAM);
    *stopped = (uint8_t)(((adc->CR & (ADC_CR_ADSTART | ADC_CR_ADSTP)) == 0U) ? 1U : 0U);
    return SPL_OK;
}

SPL_Status ADC_Disable(ADC_TypeDef *adc)
{
    uint32_t cr;

    SPL_REQUIRE_SAFE(Adc_Valid(adc) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    cr = adc->CR;
    if ((cr & ADC_CR_ADDIS) != 0U)
    {
        return SPL_OK;
    }
    if ((cr & (ADC_CR_ADSTP | ADC_CR_ADCAL)) != 0U)
    {
        return SPL_BUSY;
    }
    /* RM0444 15.3.4: stop conversions before ADDIS (SAFE). */
    SPL_REQUIRE_SAFE((cr & ADC_CR_ADSTART) == 0U, SPL_ERR_STATE);
    if ((cr & ADC_CR_ADEN) == 0U)
    {
        return SPL_OK;
    }
    Adc_Command(adc, ADC_CR_ADDIS);
    return SPL_OK;
}

SPL_Status ADC_Disabled(ADC_TypeDef *adc, uint8_t *disabled)
{
    SPL_REQUIRE_SAFE(Adc_Valid(adc) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE_SAFE(disabled != (uint8_t *)0, SPL_ERR_PARAM);
    *disabled = (uint8_t)(((adc->CR & (ADC_CR_ADEN | ADC_CR_ADDIS)) == 0U) ? 1U : 0U);
    return SPL_OK;
}

/* ---- v0.11: low-power modes (Docs/v0.11_TIME_ANALOG_SYSTEM.md, RM0444 15.6) ---- */
SPL_Status ADC_LowPowerConfig(ADC_TypeDef *adc, uint8_t auto_off, uint8_t wait, uint8_t lftrig)
{
    SPL_REQUIRE_SAFE(Adc_Valid(adc) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE((auto_off <= 1U) && (wait <= 1U) && (lftrig <= 1U), SPL_ERR_PARAM);
    /* CFGR1 / CFGR2 only with ADEN = 0 (RM0444 15.12.4 / 15.12.5, ERRATA ES0548 2.6.2 / 2.6.4);
     * SAFE. */
    SPL_REQUIRE_SAFE((adc->CR & ADC_CR_ADEN) == 0U, SPL_ERR_STATE);
    adc->CFGR1 = (adc->CFGR1 & ~(ADC_CFGR1_AUTOFF | ADC_CFGR1_WAIT)) |
                 ((auto_off != 0U) ? ADC_CFGR1_AUTOFF : 0U) | ((wait != 0U) ? ADC_CFGR1_WAIT : 0U);
    adc->CFGR2 = (adc->CFGR2 & ~ADC_CFGR2_LFTRIG) | ((lftrig != 0U) ? ADC_CFGR2_LFTRIG : 0U);
    return SPL_OK;
}
