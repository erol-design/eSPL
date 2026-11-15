#ifndef STM32G4_ADC_H
#define STM32G4_ADC_H
#include <stdint.h>
#include "stm32g4xx.h"
#include "stm32g4_status.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* ADC1/ADC2 on every SKU, ADC3/ADC4/ADC5 where CMSIS defines them (RM0440 chapter 21).
 * Contract: Docs/v0.11_ADC_CONTRACT.md. RCC clocks, analog GPIO mode and all start-up delays
 * (regulator, 4 ADC clocks after calibration, sensor start-up) are the caller's duty.
 * Not reentrant per ADC instance (common-register APIs: per ADC pair); serialize calls. */
/* Errata (ES0431 2.5.x = ES0430 2.7.x, Rev 10; ES0523 2.6.x, Rev 7): contract section "Errata".
 * Rev-dependent items: REV_ID via DBGMCU_GetIdCode (0x2001 Rev Z, 0x2002 Rev Y, 0x2003 Rev X;
 * G491/G4A1: 0x1000 Rev A, 0x1001 Rev Z). G411/G414: no errata sheet available, assume the
 * items may apply. */

/* ADCx_CCR.CKMODE (RM0440 21.8.2). */
#define ADC_CLOCK_ASYNC 0U     /* adc_ker_ck from RCC, divided by PRESC */
#define ADC_CLOCK_SYNC_DIV1 1U /* adc_hclk/1: AHB prescaler 1 and 50 % duty SYSCLK only */
#define ADC_CLOCK_SYNC_DIV2 2U /* adc_hclk/2 */
#define ADC_CLOCK_SYNC_DIV4 3U /* adc_hclk/4 */
#define ADC_PRESC_MAX 11U      /* PRESC code 0..11 = /1,/2,/4,/6,/8,/10,/12,/16,/32,/64,/128,/256 */

/* Internal channels (ADCx_CCR). Readers (RM0440 21.4.11): VREFINT = ADC1/3/4/5 ch 18,
 * VTS = ADC1 ch 16 and ADC5 ch 4, VBAT/3 = ADC1/3/5 ch 17. ADC2 reads none of them. */
#define ADC_INTERNAL_VREFINT ADC_CCR_VREFEN
#define ADC_INTERNAL_VTS ADC_CCR_VSENSESEL
#define ADC_INTERNAL_VBAT ADC_CCR_VBATSEL

/* ADC_CFGR.RES / ALIGN / EXTEN codes. */
#define ADC_RES_12BIT 0U
#define ADC_RES_10BIT 1U
#define ADC_RES_8BIT 2U
#define ADC_RES_6BIT 3U
#define ADC_ALIGN_RIGHT 0U
#define ADC_ALIGN_LEFT 1U
#define ADC_EDGE_SOFTWARE 0U /* hardware trigger disabled, ADSTART starts */
#define ADC_EDGE_RISING 1U
#define ADC_EDGE_FALLING 2U
#define ADC_EDGE_BOTH 3U

/* Calibration input mode (ADC_CR.ADCALDIF). */
#define ADC_CAL_SINGLE 0U
#define ADC_CAL_DIFF 1U

/* ADC_ISR flags that ADC_ClearFlags accepts (W1C). ADRDY is owned by ADC_Enable/ADC_Start. */
#define ADC_FLAGS_CLEARABLE                                                                        \
    (ADC_ISR_EOSMP | ADC_ISR_EOC | ADC_ISR_EOS | ADC_ISR_OVR | ADC_ISR_JEOC | ADC_ISR_JEOS |       \
     ADC_ISR_AWD1 | ADC_ISR_AWD2 | ADC_ISR_AWD3 | ADC_ISR_JQOVF)

    typedef struct
    {
        uint8_t resolution;        /* ADC_RES_* */
        uint8_t align;             /* ADC_ALIGN_* */
        uint8_t continuous;        /* CONT 0/1; 1 not allowed with disc_count != 0 */
        uint8_t disc_count;        /* 0 = discontinuous off, 1..8 ranks per trigger */
        uint8_t ext_sel;           /* EXTSEL event 0..31; must be 0 with ADC_EDGE_SOFTWARE */
        uint8_t ext_edge;          /* ADC_EDGE_* */
        uint8_t overrun_overwrite; /* OVRMOD: 0 keep old DR, 1 overwrite */
    } ADC_RegularConfigTypeDef;

    typedef struct
    {
        uint8_t enable;    /* ROVSE 0/1 */
        uint8_t ratio;     /* OVSR code 0..7 = 2x..256x */
        uint8_t shift;     /* OVSS 0..8 bits */
        uint8_t triggered; /* TROVS 0/1: one trigger per oversampled conversion */
    } ADC_OversamplingTypeDef;

    typedef struct
    {
        uint16_t low;    /* LT1 0..0xFFF, low bits 0 for RES < 12 bit (RM Table 173) */
        uint16_t high;   /* HT1 0..0xFFF, >= low */
        uint8_t enable;  /* AWD1EN on regular channels 0/1 */
        uint8_t single;  /* AWD1SGL: 0 all channels, 1 channel only */
        uint8_t channel; /* AWD1CH 0..18 (used with single = 1) */
        uint8_t filter;  /* TR1.AWDFILT 0..7: n+1 consecutive detections */
    } ADC_Awd1ConfigTypeDef;

    /* Clears DEEPPWD, then sets ADVREGEN (two writes, RM0440 21.4.6).
     * Pending ADDIS/ADSTP/JADSTP: BUSY; ADEN/ADCAL/ADSTART/JADSTART: ERR_STATE;
     * both without register writes. Caller waits the regulator startup time. */
    /* Seviye: PARAM[S] STATE[r] BUSY[r] */
    SPL_Status ADC_PowerRegulator(ADC_TypeDef *adc);
    /* Single-ended calibration (same as ADC_StartCalibrationMode(adc, ADC_CAL_SINGLE)). */
    /* Seviye: PARAM[S] STATE[r] */
    SPL_Status ADC_StartCalibration(ADC_TypeDef *adc);
    /* Seviye: PARAM[S] */
    SPL_Status ADC_CalibrationDone(ADC_TypeDef *adc, uint8_t *done);
    /* Legacy one-rank setup: RES/ALIGN/CONT/EXTEN/DMAEN/DMACFG cleared; requires ADEN=0. */
    /* Seviye: PARAM[S] STATE[r] */
    SPL_Status ADC_RegularSingleInit(ADC_TypeDef *adc, uint8_t channel, uint8_t sample_code);
    /* v0.11.1: ADVREGEN=0 (or DEEPPWD/ADCAL set): ERR_STATE. After ADCAL clears, caller must
     * observe >=4 ADC clock cycles and the regulator start-up time before ADEN (not
     * detectable by software). Poll ADC_Ready with a caller timeout: ADRDY never sets if
     * those rules were broken or the ADC clock is missing. */
    /* Seviye: PARAM[S] STATE[r] BUSY[r] */
    SPL_Status ADC_Enable(ADC_TypeDef *adc);
    /* Seviye: PARAM[S] */
    SPL_Status ADC_Ready(ADC_TypeDef *adc, uint8_t *ready);
    /* Pending ADDIS/ADSTP/JADSTP: BUSY, no register writes. ADSTART=1: BUSY in single mode
     * with software trigger (clears at EOS); v0.11.1: ERR_STATE with CONT=1 or EXTEN!=0
     * (never clears by itself; ADC_StopRegular first). */
    /* ERRATA ES0431 2.5.9 / ES0430 2.7.9 / ES0523 2.6.7: a conversion > 1 ms after calibration or
     * the previous conversion may be wrong (all revisions); convert twice, drop the first. */
    /* Seviye: PARAM[S] STATE[r] HW[r] BUSY[r] */
    SPL_Status ADC_Start(ADC_TypeDef *adc);
    /* OVR: ERR_HW. EOC=0: disabled -> ERR_STATE; enabled and ADSTART=1 -> WOULD_BLOCK;
     * v0.11.1: enabled and ADSTART=0 (nothing running, sample already read, stopped) ->
     * ERR_STATE. Retained EOC data can be read after disable. Error leaves output unchanged. */
    /* Seviye: PARAM[S] STATE[r] HW[r] WOULD_BLOCK[r] */
    SPL_Status ADC_TryRead(ADC_TypeDef *adc, uint16_t *value);
    /* Clears OVR with W1C semantics. Read/capture error diagnosis before clearing. */
    /* Seviye: PARAM[S] */
    SPL_Status ADC_ClearErrors(ADC_TypeDef *adc);
    /* ADSTART=1 or JADSTART=1: ERR_STATE (RM0440 21.7.4 DMAEN/DMACFG). */
    /* ERRATA ES0431 2.5.10 / ES0430 2.7.10 / ES0523 2.6.8 (Rev Y; G491/G4A1 Rev A): after a
     * one-shot DMA end the first conversion reads channel 0 (result 0); discard it or ADC_Disable +
     * ADC_Enable. */
    /* Seviye: PARAM[S] STATE[r] */
    SPL_Status ADC_EnableDma(ADC_TypeDef *adc, uint8_t circular);
    /* Seviye: PARAM[S] STATE[r] */
    SPL_Status ADC_DisableDma(ADC_TypeDef *adc);

    /* Common block of the pair containing adc. All ADCs of the pair must be disabled
     * (STATE; pending ADDIS/ADSTP/JADSTP: BUSY). presc only with ADC_CLOCK_ASYNC, else 0. */
    /* ERRATA ES0431 2.5.11 / ES0430 2.7.11 / ES0523 2.6.9: overlapping conversions of two ADCs may
     * lose accuracy except ASYNC with presc 0 (/1) or SYNC_DIVx (trigger prescaler a multiple of
     * the divider); G47x: keep ADC12 and ADC345 blocks consistent (see contract). */
    /* Seviye: PARAM[S] STATE[r] BUSY[r] */
    SPL_Status ADC_CommonClockConfig(ADC_TypeDef *adc, uint32_t ckmode, uint32_t presc);
    /* Sets exactly VREFEN/VSENSESEL/VBATSEL of the pair's CCR from mask (ADC_INTERNAL_*). */
    /* Seviye: PARAM[S] */
    SPL_Status ADC_InternalChannelsConfig(ADC_TypeDef *adc, uint32_t mask);

    /* length 1..16 ranks, channels 0..18; ADSTART=0 required. Writes SQR1..SQR4 fully. */
    /* ERRATA ES0431 2.5.8 / ES0430 2.7.8 (Rev Z only, not in ES0523): a channel switch disturbs
     * running ADCs (DNL); scan each input twice, keep the first (ES text); BULB not exposed. */
    /* Seviye: PARAM[S] STATE[r] BUSY[r] */
    SPL_Status ADC_RegularSequenceConfig(ADC_TypeDef *adc, const uint8_t *channels, uint8_t length);
    /* code 0..7 = 2.5..640.5 ADC cycles; ADSTART=0 and JADSTART=0 required. */
    /* Seviye: PARAM[S] STATE[r] BUSY[r] */
    SPL_Status ADC_SetSamplingTime(ADC_TypeDef *adc, uint8_t channel, uint8_t sample_code);
    /* ADSTART=0 and JADSTART=0 required; DMA, watchdog and injected bits kept.
     * v0.11.1: AWD1EN=1 with LT1/HT1 low bits that the new resolution requires to be 0:
     * ERR_STATE (reprogram AWD1 with ADC_Awd1Config, or disable it, first). */
    /* ERRATA ES0431 2.5.7 / ES0430 2.7.7 (Rev Z only, not in ES0523): EOC at RES 10/8/6 bit
     * disturbs other converting ADCs; use 12 bit, or same RES/sampling and simultaneous start. */
    /* Seviye: PARAM[S] STATE[r] BUSY[r] */
    SPL_Status ADC_RegularConfig(ADC_TypeDef *adc, const ADC_RegularConfigTypeDef *config);

    /* Channels 1..18 as bit mask (bit 0 is read-only); ADC fully disabled required.
     * v0.11.1: internal-channel bits are PARAM (ADC1 16/17/18, ADC3 17/18, ADC4 18,
     * ADC5 4/17/18). OPAMP internal outputs are not checked (caller). */
    /* Seviye: PARAM[S] STATE[r] BUSY[r] */
    SPL_Status ADC_SetDifferentialChannels(ADC_TypeDef *adc, uint32_t mask);
    /* Seviye: PARAM[S] STATE[r] */
    SPL_Status ADC_StartCalibrationMode(ADC_TypeDef *adc, uint32_t mode);
    /* BUSY while ADCAL=1. */
    /* Seviye: PARAM[S] BUSY[r] */
    SPL_Status ADC_GetCalibrationFactors(ADC_TypeDef *adc, uint8_t *single, uint8_t *diff);
    /* 0..127 each; ADEN=1, ADDIS=0, ADSTART=0, JADSTART=0 required. */
    /* Seviye: PARAM[S] STATE[r] BUSY[r] */
    SPL_Status ADC_SetCalibrationFactors(ADC_TypeDef *adc, uint8_t single, uint8_t diff);

    /* Regular oversampling; ADSTART=0 and JADSTART=0 required. */
    /* v0.13.5: PARAM when (resolution bits + ratio + 1 - shift) > 16 with regular or injected
     * oversampling on (RM0440 21.4.29: wider results are truncated without saturation). */
    /* Seviye: PARAM[S] STATE[r] BUSY[r] */
    SPL_Status ADC_OversamplingConfig(ADC_TypeDef *adc, const ADC_OversamplingTypeDef *config);
    /* Analog watchdog 1 on regular channels; ADSTART=0 and JADSTART=0 required. */
    /* ERRATA ES0431 2.5.4 / ES0430 2.7.4 / ES0523 2.6.4: ADC_AWD1_OUT is also reset by non-guarded
     * channels; with AWD_OUT used, convert only guarded channels or use its rising edge. */
    /* Seviye: PARAM[S] STATE[r] BUSY[r] */
    SPL_Status ADC_Awd1Config(ADC_TypeDef *adc, const ADC_Awd1ConfigTypeDef *config);

    /* Raw ADC_ISR flags (no side effect). */
    /* Seviye: PARAM[S] */
    SPL_Status ADC_GetFlags(ADC_TypeDef *adc, uint32_t *flags);
    /* W1C write of a non-empty subset of ADC_FLAGS_CLEARABLE. */
    /* Seviye: PARAM[S] */
    SPL_Status ADC_ClearFlags(ADC_TypeDef *adc, uint32_t flags);

    /* Non-blocking ADSTP command; OK without write if nothing runs or ADSTP is pending. */
    /* Seviye: PARAM[S] STATE[r] */
    SPL_Status ADC_StopRegular(ADC_TypeDef *adc);
    /* Seviye: PARAM[S] */
    SPL_Status ADC_RegularStopped(ADC_TypeDef *adc, uint8_t *stopped);
    /* Non-blocking ADDIS command; conversions must be stopped first (STATE). */
    /* Seviye: PARAM[S] STATE[r] BUSY[r] */
    SPL_Status ADC_Disable(ADC_TypeDef *adc);
    /* Seviye: PARAM[S] */
    SPL_Status ADC_Disabled(ADC_TypeDef *adc, uint8_t *disabled);

#ifdef __cplusplus
}
#endif
#endif
