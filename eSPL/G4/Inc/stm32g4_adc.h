/* SPDX-License-Identifier: MIT */
#ifndef STM32G4_ADC_H
#define STM32G4_ADC_H
#include <stdint.h>
#include "stm32g4xx.h"
#include "stm32g4_status.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* ADC driver for STM32G4 (RM0440 Rev 9 chapter 21): ADC1/ADC2 on every SKU, ADC3/ADC4/ADC5
 * where CMSIS defines them. Common registers per pair block (ADC12_COMMON / ADC345_COMMON).
 * Scope: regulator, calibration, enable/disable, regular group (sequence, sampling, trigger,
 * DMA request), oversampling, analog watchdog 1, internal channels, differential inputs.
 * Not implemented: injected group, dual mode, AWD2/3, offsets, gain compensation (fields kept).
 * Usage: ADC_PowerRegulator, wait tADCVREG_STUP, ADC_StartCalibration / ADC_CalibrationDone,
 * wait 4 ADC clocks, configuration (ADC_CommonClockConfig, ADC_RegularConfig,
 * ADC_RegularSequenceConfig, ADC_SetSamplingTime, ...), ADC_Enable / ADC_Ready, then ADC_Start
 * and ADC_TryRead (or DMA); ADC_StopRegular / ADC_RegularStopped, ADC_Disable / ADC_Disabled.
 * Common rules: no static state, no function waits (start + poll design); no register write on
 * error; output parameters are unchanged unless SPL_OK (METHOD 1.8).
 * Caller duties (all APIs; not done by the driver):
 *  - Clock: RCC bus clock of the ADC pair and, with ADC_CLOCK_ASYNC, the ADC kernel clock.
 *  - Pins: input GPIOs in analog mode; do not select channels not bonded on the package.
 *  - Timing: tADCVREG_STUP after ADC_PowerRegulator, 4 ADC clocks after ADCAL clears before
 *    ADEN, sensor start-up times (datasheet); bound every *Ready / *Done / *Stopped /
 *    *Disabled poll with a caller timeout.
 *  - Trigger source (timer TRGO, EXTI), DMA channel, NVIC and handlers when used; while
 *    DMAEN = 1 the DMA owns DR (do not call ADC_TryRead).
 *  - Serialize calls per ADC instance and common-block APIs per pair (ISR included).
 *  - Errata CALLER items: REV_ID via DBGMCU_GetIdCode (0x2001 Rev Z, 0x2002 Rev Y, 0x2003 Rev X;
 *    G491/G4A1: 0x1000 Rev A, 0x1001 Rev Z); G411/G414 have no errata sheet, assume the items
 *    may apply. Items (ES0431 2.5.x = ES0430 2.7.x Rev 10; ES0523 2.6.x Rev 7) at the APIs below.
 * Contract: Docs/v0.11_ADC_CONTRACT.md (sections "Errata", "Caller notes"). */

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
     * both without register writes.
     * Caller: wait the regulator start-up time tADCVREG_STUP before calibration or ADC_Enable. */
    /* Level: PARAM[F] STATE[r] BUSY[r] */
    SPL_Status ADC_PowerRegulator(ADC_TypeDef *adc);
    /* Single-ended calibration (same as ADC_StartCalibrationMode(adc, ADC_CAL_SINGLE)). */
    /* Caller duty (CCL-2 ADC-01b): CFGR.DMAEN is not checked here or in
     * ADC_StartCalibrationMode; call ADC_DisableDma before calibration if DMA was enabled. */
    /* Level: PARAM[F] STATE[r] */
    SPL_Status ADC_StartCalibration(ADC_TypeDef *adc);
    /* *done = 1 when ADCAL is clear.
     * Caller: bound the poll; after done wait 4 ADC clocks before ADC_Enable. */
    /* Level: PARAM[F] */
    SPL_Status ADC_CalibrationDone(ADC_TypeDef *adc, uint8_t *done);
    /* Legacy one-rank setup: RES/ALIGN/CONT/EXTEN/DMAEN/DMACFG cleared; requires ADEN=0.
     * Caller: call it before ADC_RegularConfig if both are used (it resets those fields). */
    /* Level: PARAM[F] STATE[r] */
    SPL_Status ADC_RegularSingleInit(ADC_TypeDef *adc, uint8_t channel, uint8_t sample_code);
    /* v0.11.1: ADVREGEN=0 (or DEEPPWD/ADCAL set): ERR_STATE. ADRDY never sets if the timing
     * rules below were broken or the ADC clock is missing.
     * Caller: after ADCAL clears observe >= 4 ADC clock cycles and the regulator start-up time
     * before this call (not detectable by software); poll ADC_Ready with a timeout. */
    /* Level: PARAM[F] STATE[r] BUSY[r] */
    SPL_Status ADC_Enable(ADC_TypeDef *adc);
    /* *ready = ADRDY.
     * Caller: bound the poll (ADRDY never sets without the ADC clock). */
    /* Level: PARAM[F] */
    SPL_Status ADC_Ready(ADC_TypeDef *adc, uint8_t *ready);
    /* Pending ADDIS/ADSTP/JADSTP: BUSY, no register writes. ADSTART=1: BUSY in single mode
     * with software trigger (clears at EOS); v0.11.1: ERR_STATE with CONT=1 or EXTEN!=0
     * (never clears by itself; ADC_StopRegular first). */
    /* ERRATA ES0431 2.5.9 / ES0430 2.7.9 / ES0523 2.6.7: a conversion > 1 ms after calibration or
     * the previous conversion may be wrong (all revisions); convert twice, drop the first.
     * Caller: with EXTEN != 0 OK means armed only (completion via EOC/EOS); after calibration,
     * ADC_Enable or a gap > 1 ms drop the first conversion (errata above); with DMA enable the
     * DMA channel first. */
    /* Level: PARAM[F] STATE[r] HW[r] BUSY[r] */
    SPL_Status ADC_Start(ADC_TypeDef *adc);
    /* Caller duty (CCL-2 ADC-08b): on OVR (ERR_HW here, or the OVR flag) the library restarts
     * nothing; the caller clears OVR (ADC_ClearErrors) and restarts the conversion and, when
     * DMA is used, the DMA channel. */
    /* OVR: ERR_HW. EOC=0: disabled -> ERR_STATE; enabled and ADSTART=1 -> WOULD_BLOCK;
     * v0.11.1: enabled and ADSTART=0 (nothing running, sample already read, stopped) ->
     * ERR_STATE. Retained EOC data can be read after disable. Error leaves output unchanged.
     * Caller: not while DMAEN = 1 (the DMA owns DR); differential results are offset binary
     * (0x800 at zero difference, 12 bit), signed conversion is the caller's. */
    /* Level: PARAM[F] STATE[r] HW[r] WOULD_BLOCK[r] */
    SPL_Status ADC_TryRead(ADC_TypeDef *adc, uint16_t *value);
    /* Clears OVR with W1C semantics.
     * Caller: capture the error diagnosis (ADC_GetFlags) before clearing; then restart the
     * conversion (and DMA) itself. */
    /* Level: PARAM[F] */
    SPL_Status ADC_ClearErrors(ADC_TypeDef *adc);
    /* ADSTART=1 or JADSTART=1: ERR_STATE (RM0440 21.7.4 DMAEN/DMACFG). */
    /* Caller duty (CCL-2 ADC-06a, ADC-06b, ADC-06c, ADC-06d): the library does not link the
     * ADC sequence to the DMA channel. The caller maps buffer index n to sequence rank n, sets
     * the DMA count to a multiple of the sequence length, enables the DMA channel before
     * ADC_Start, and with repeated scans uses circular here and DMA circular together (or
     * re-arms ADC and DMA after each one-shot sequence). */
    /* ERRATA ES0431 2.5.10 / ES0430 2.7.10 / ES0523 2.6.8 (Rev Y; G491/G4A1 Rev A): after a
     * one-shot DMA end the first conversion reads channel 0 (result 0); discard it or ADC_Disable +
     * ADC_Enable.
     * Caller: on Rev Y (G491/G4A1 Rev A) discard the first conversion after a one-shot DMA end
     * (errata above). */
    /* Level: PARAM[F] STATE[r] */
    SPL_Status ADC_EnableDma(ADC_TypeDef *adc, uint8_t circular);
    /* Clears DMAEN and DMACFG. ADSTART=1 or JADSTART=1: ERR_STATE.
     * Caller: stop the DMA channel (DMA driver). */
    /* Level: PARAM[F] STATE[r] */
    SPL_Status ADC_DisableDma(ADC_TypeDef *adc);

    /* Common block of the pair containing adc. All ADCs of the pair must be disabled
     * (STATE; pending ADDIS/ADSTP/JADSTP: BUSY). presc only with ADC_CLOCK_ASYNC, else 0. */
    /* ERRATA ES0431 2.5.11 / ES0430 2.7.11 / ES0523 2.6.9: overlapping conversions of two ADCs may
     * lose accuracy except ASYNC with presc 0 (/1) or SYNC_DIVx (trigger prescaler a multiple of
     * the divider); G47x: keep ADC12 and ADC345 blocks consistent (see contract).
     * Caller: SYNC_DIV1 only with AHB prescaler 1 and 50 % duty SYSCLK; ASYNC needs the RCC ADC
     * kernel clock selected; choose an errata-safe clock for overlapping conversions (above). */
    /* Level: PARAM[F] STATE[r] BUSY[r] */
    SPL_Status ADC_CommonClockConfig(ADC_TypeDef *adc, uint32_t ckmode, uint32_t presc);
    /* Sets exactly VREFEN/VSENSESEL/VBATSEL of the pair's CCR from mask (ADC_INTERNAL_*).
     * No state check (RM0440 21.8.2 gives no write condition).
     * Caller: wait the sensor start-up time before converting; clear VBAT when not needed (the
     * bridge draws current); serialize with the other ADC of the pair. */
    /* Level: PARAM[F] */
    SPL_Status ADC_InternalChannelsConfig(ADC_TypeDef *adc, uint32_t mask);

    /* length 1..16 ranks, channels 0..18; ADSTART=0 required. Writes SQR1..SQR4 fully. */
    /* ERRATA ES0431 2.5.8 / ES0430 2.7.8 (Rev Z only, not in ES0523): a channel switch disturbs
     * running ADCs (DNL); scan each input twice, keep the first (ES text); BULB not exposed.
     * Caller: no unbonded channel and no negative input of a differential pair; on Rev Z apply
     * the errata above (combined with 2.5.9 it is unsupported, contract Caller notes). */
    /* Level: PARAM[F] STATE[r] BUSY[r] */
    SPL_Status ADC_RegularSequenceConfig(ADC_TypeDef *adc, const uint8_t *channels, uint8_t length);
    /* code 0..7 = 2.5..640.5 ADC cycles; ADSTART=0 and JADSTART=0 required.
     * Caller: sampling time long enough for the source impedance (datasheet). */
    /* Level: PARAM[F] STATE[r] BUSY[r] */
    SPL_Status ADC_SetSamplingTime(ADC_TypeDef *adc, uint8_t channel, uint8_t sample_code);
    /* ADSTART=0 and JADSTART=0 required; DMA, watchdog and injected bits kept.
     * v0.11.1: AWD1EN=1 with LT1/HT1 low bits that the new resolution requires to be 0:
     * ERR_STATE (reprogram AWD1 with ADC_Awd1Config, or disable it, first). */
    /* ERRATA ES0431 2.5.7 / ES0430 2.7.7 (Rev Z only, not in ES0523): EOC at RES 10/8/6 bit
     * disturbs other converting ADCs; use 12 bit, or same RES/sampling and simultaneous start.
     * Caller: EXTSEL event valid for this ADC/SKU (RM0440 21.4.18); Rev Z errata above. */
    /* Level: PARAM[F] STATE[r] BUSY[r] */
    SPL_Status ADC_RegularConfig(ADC_TypeDef *adc, const ADC_RegularConfigTypeDef *config);

    /* Channels 1..18 as bit mask (bit 0 is read-only); ADC fully disabled required.
     * v0.11.1: internal-channel bits are PARAM (ADC1 16/17/18, ADC3 17/18, ADC4 18,
     * ADC5 4/17/18).
     * Caller: do not set OPAMP internal-output channels or channels bonded to single-ended pads
     * (not checked); recalibrate in differential mode after the change. */
    /* Level: PARAM[F] STATE[r] BUSY[r] */
    SPL_Status ADC_SetDifferentialChannels(ADC_TypeDef *adc, uint32_t mask);
    /* ADC_CAL_SINGLE / ADC_CAL_DIFF; writes ADCALDIF first, then ADCAL. Same preconditions as
     * ADC_StartCalibration (ADEN/ADCAL/DEEPPWD set or ADVREGEN=0: ERR_STATE).
     * Caller: regulator start-up time elapsed; DMA disabled (ADC_DisableDma) as for
     * ADC_StartCalibration; bound the ADC_CalibrationDone poll. */
    /* Level: PARAM[F] STATE[r] */
    SPL_Status ADC_StartCalibrationMode(ADC_TypeDef *adc, uint32_t mode);
    /* BUSY while ADCAL=1.
     * Caller: none beyond the file-header duties. */
    /* Level: PARAM[F] BUSY[r] */
    SPL_Status ADC_GetCalibrationFactors(ADC_TypeDef *adc, uint8_t *single, uint8_t *diff);
    /* 0..127 each; ADEN=1, ADDIS=0, ADSTART=0, JADSTART=0 required.
     * Caller: factors saved from the same ADC and input mode (calibration is lost in
     * Deep-power-down). */
    /* Level: PARAM[F] STATE[r] BUSY[r] */
    SPL_Status ADC_SetCalibrationFactors(ADC_TypeDef *adc, uint8_t single, uint8_t diff);

    /* Regular oversampling; ADSTART=0 and JADSTART=0 required. */
    /* v0.13.5: PARAM when (resolution bits + ratio + 1 - shift) > 16 with regular or injected
     * oversampling on (RM0440 21.4.29: wider results are truncated without saturation).
     * Caller: right alignment with oversampling; TROVS forces discontinuous behaviour. */
    /* Level: PARAM[F] STATE[r] BUSY[r] */
    SPL_Status ADC_OversamplingConfig(ADC_TypeDef *adc, const ADC_OversamplingTypeDef *config);
    /* Analog watchdog 1 on regular channels; ADSTART=0 and JADSTART=0 required. */
    /* ERRATA ES0431 2.5.4 / ES0430 2.7.4 / ES0523 2.6.4: ADC_AWD1_OUT is also reset by non-guarded
     * channels; with AWD_OUT used, convert only guarded channels or use its rising edge.
     * Caller: the guarded channel must also be in the regular sequence; set RES first
     * (thresholds are checked against the current RES); AWD_OUT routing per errata above. */
    /* Level: PARAM[F] STATE[r] BUSY[r] */
    SPL_Status ADC_Awd1Config(ADC_TypeDef *adc, const ADC_Awd1ConfigTypeDef *config);

    /* Raw ADC_ISR flags (no side effect).
     * Caller: none beyond the file-header duties. */
    /* Level: PARAM[F] */
    SPL_Status ADC_GetFlags(ADC_TypeDef *adc, uint32_t *flags);
    /* W1C write of a non-empty subset of ADC_FLAGS_CLEARABLE.
     * Caller: capture the flags first; clear only the flags it owns. */
    /* Level: PARAM[F] */
    SPL_Status ADC_ClearFlags(ADC_TypeDef *adc, uint32_t flags);

    /* Non-blocking ADSTP command; OK without write if nothing runs or ADSTP is pending.
     * Caller: poll ADC_RegularStopped with a timeout; on Rev Y (G491/G4A1 Rev A) discard the
     * first conversion after the stop (ES0431 2.5.10, see ADC_EnableDma). */
    /* Level: PARAM[F] STATE[r] */
    SPL_Status ADC_StopRegular(ADC_TypeDef *adc);
    /* *stopped = (ADSTART == 0 and ADSTP == 0).
     * Caller: bound the poll. */
    /* Level: PARAM[F] */
    SPL_Status ADC_RegularStopped(ADC_TypeDef *adc, uint8_t *stopped);
    /* Non-blocking ADDIS command; conversions must be stopped first (STATE).
     * Caller: poll ADC_Disabled before stopping the ADC clock or the regulator. */
    /* Level: PARAM[F] STATE[r] BUSY[r] */
    SPL_Status ADC_Disable(ADC_TypeDef *adc);
    /* *disabled = (ADEN == 0 and ADDIS == 0).
     * Caller: bound the poll. */
    /* Level: PARAM[F] */
    SPL_Status ADC_Disabled(ADC_TypeDef *adc, uint8_t *disabled);

#ifdef __cplusplus
}
#endif
#endif
