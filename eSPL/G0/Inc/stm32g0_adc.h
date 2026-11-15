#ifndef STM32G0_ADC_H
#define STM32G0_ADC_H
#include <stdint.h>
#include "stm32g0xx.h"
#include "stm32g0_status.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* ADC1 of STM32G0B1 (RM0444 Rev 6 section 15; Docs/v0.5_ADC_CONTRACT.md). Written for G0: the
 * G4 ADC model (SQR1..4, injected group, ADC pairs, DEEPPWD, differential inputs) does not apply.
 * G0 model: one ADC, CFGR1/CFGR2 writable only with ADEN = 0, channel selection in CHSELR as a bit
 * mask (fixed order, forward/backward) or as an ordered list of up to 8 channels 0..14, two
 * sampling times (SMP1/SMP2 + SMPSEL), CCRDY handshake after every channel configuration.
 * RCC clock (RCC_PERIPH_ADC, RCC_KERNEL_ADC), analog GPIO mode, the regulator start-up time
 * (tADCVREG_STUP) and sensor start-up times are the caller's duty. AUTOFF / WAIT / LFTRIG: v0.11
 * ADC_LowPowerConfig (call it after ADC_RegularConfig, which writes AUTOFF / WAIT = 0). Not
 * reentrant: serialise calls. No register write on error. Errata ES0548 Rev 4: 2.6.2 / 2.6.4 (CFGR1
 * / CFGR2 written only with ADEN = 0, never with an automatic disable/enable), 2.6.3 (AWD1 single
 * channel in single mode, see ADC_Awd1Config), 2.6.1 / 2.6.5 / 2.6.6 caller notes below. */

/* ADC_CFGR2.CKMODE (RM0444 15.12.5): raw G0 codes (they differ from G4). */
#define ADC_CLOCK_ASYNC 0U     /* ADCCLK from RCC (RCC_KERNEL_ADC), divided by PRESC */
#define ADC_CLOCK_SYNC_DIV2 1U /* PCLK/2 */
#define ADC_CLOCK_SYNC_DIV4 2U /* PCLK/4 */
#define ADC_CLOCK_SYNC_DIV1 3U /* PCLK: APB prescaler 1 and 50 % duty system clock only */
#define ADC_PRESC_MAX 11U      /* PRESC code 0..11 = /1,/2,/4,/6,/8,/10,/12,/16,/32,/64,/128,/256 */

/* Internal channels (ADC_CCR): VSENSE = channel 12, VREFINT = 13, VBAT/3 = 14 (RM0444 15.3.8).
 * With TSEN = 0 / VBATEN = 0 channel 12 / 14 reads DAC1 OUT1 / OUT2 instead. */
#define ADC_INTERNAL_VREFINT ADC_CCR_VREFEN
#define ADC_INTERNAL_VTS ADC_CCR_TSEN
#define ADC_INTERNAL_VBAT ADC_CCR_VBATEN

#define ADC_RES_12BIT 0U
#define ADC_RES_10BIT 1U
#define ADC_RES_8BIT 2U
#define ADC_RES_6BIT 3U
#define ADC_ALIGN_RIGHT 0U
#define ADC_ALIGN_LEFT 1U
/* ES0487 2.5.6 (documentation, G030..G081 sheets): trigger latency for CKMODE 01 / 10 / 11 is
 * 6.5 / 12.5 / 3.5 PCLK cycles typical, not the datasheet 2.75 / 2.63 / 3 ADC clock cycles. */
#define ADC_EDGE_SOFTWARE 0U /* hardware trigger disabled, ADSTART starts */
#define ADC_EDGE_RISING 1U
#define ADC_EDGE_FALLING 2U
#define ADC_EDGE_BOTH 3U

/* Channel sequencer modes (CFGR1 CHSELRMOD / SCANDIR, RM0444 15.3.8). */
#define ADC_SEQ_FORWARD 0U  /* bit mask, channel 0 -> 18 */
#define ADC_SEQ_BACKWARD 1U /* bit mask, channel 18 -> 0 */
#define ADC_SEQ_ORDERED 2U  /* SQ1..SQ8: up to 8 channels 0..14 in any order */

#define ADC_CHANNEL_MAX 18U
#define ADC_ORDERED_CHANNEL_MAX 14U
#define ADC_CHANNEL_MASK 0x7FFFFUL /* channels 0..18 */

/* ADC_ISR flags that ADC_ClearFlags accepts (W1C). ADRDY (ADC_Enable) and CCRDY (channel
 * configuration handshake) are owned by the driver. */
#define ADC_FLAGS_CLEARABLE                                                                        \
    (ADC_ISR_EOSMP | ADC_ISR_EOC | ADC_ISR_EOS | ADC_ISR_OVR | ADC_ISR_AWD1 | ADC_ISR_AWD2 |       \
     ADC_ISR_AWD3 | ADC_ISR_EOCAL)

typedef struct
{
    uint8_t resolution;        /* ADC_RES_* */
    uint8_t align;             /* ADC_ALIGN_* (ignored by hardware with oversampling) */
    uint8_t continuous;        /* CONT 0/1; 1 not allowed with discontinuous = 1 */
    uint8_t discontinuous;     /* DISCEN 0/1: one channel per trigger */
    uint8_t ext_sel;           /* EXTSEL TRG0..TRG7; must be 0 with ADC_EDGE_SOFTWARE */
    uint8_t ext_edge;          /* ADC_EDGE_* */
    uint8_t overrun_overwrite; /* OVRMOD: 0 keep old DR, 1 overwrite */
} ADC_RegularConfigTypeDef;

typedef struct
{
    uint32_t mask;       /* ADC_SEQ_FORWARD / BACKWARD: non-empty subset of ADC_CHANNEL_MASK */
    uint8_t channels[8]; /* ADC_SEQ_ORDERED: channel 0..14 per rank */
    uint8_t mode;        /* ADC_SEQ_* */
    uint8_t length;      /* ADC_SEQ_ORDERED: 1..8 entries of channels[] */
    uint8_t reserved[2]; /* padding, ignored */
} ADC_ChannelConfigTypeDef;

typedef struct
{
    uint8_t enable;    /* OVSE 0/1 */
    uint8_t ratio;     /* OVSR code 0..7 = 2x..256x */
    uint8_t shift;     /* OVSS 0..8 bits */
    uint8_t triggered; /* TOVS 0/1: one trigger per oversampled conversion */
} ADC_OversamplingTypeDef;

typedef struct
{
    uint16_t low;     /* LT1 0..0xFFF; low bits 0 for RES < 12 bit (RM0444 Table 77) */
    uint16_t high;    /* HT1 0..0xFFF, >= low */
    uint8_t enable;   /* AWD1EN 0/1 */
    uint8_t single;   /* AWD1SGL: 0 all channels, 1 channel only */
    uint8_t channel;  /* AWD1CH 0..18 (used with single = 1) */
    uint8_t reserved; /* padding, ignored */
} ADC_Awd1ConfigTypeDef;

/* ADVREGEN = 1 (RM0444 15.3.2). ADEN/ADSTART: ERR_STATE; ADCAL/ADSTP/ADDIS pending: BUSY.
 * Caller waits tADCVREG_STUP (datasheet) before calibration or ADC_Enable. */
/* Seviye: PARAM[S] STATE[S] BUSY[r] */
SPL_Status ADC_PowerRegulator(ADC_TypeDef *adc);
/* ADVREGEN = 0 (before low-power modes); same state rules. The calibration is kept. */
/* Seviye: PARAM[S] STATE[S] BUSY[r] */
SPL_Status ADC_PowerRegulatorDisable(ADC_TypeDef *adc);
/* RM0444 15.3.3 conditions: ADVREGEN = 1, ADEN = 0, AUTOFF = 0, DMAEN = 0 (else ERR_STATE).
 * Clears EOCAL, sets ADCAL. */
/* Seviye: PARAM[S] STATE[S] BUSY[r] */
SPL_Status ADC_StartCalibration(ADC_TypeDef *adc);
/* Seviye: PARAM[S] */
SPL_Status ADC_CalibrationDone(ADC_TypeDef *adc, uint8_t *done);
/* CALFACT[6:0]; BUSY while ADCAL = 1. RM0444 15.3.3: the value read is the factor minus one;
 * the caller adds 1 (0x7F at most) and writes it back with ADC_SetCalibrationFactor.
 * ERRATA ES0548 2.6.6 (Rev A): CALFACT 0x00 or 0x7F after calibration with VREF+ < 3 V means
 * an uncompensated offset; the ES workaround (measure VREF+ / VSSA) is the caller's. */
/* Seviye: PARAM[S] BUSY[r] */
SPL_Status ADC_GetCalibrationFactor(ADC_TypeDef *adc, uint8_t *factor);
/* factor 0..127; ADEN = 1, ADSTART = 0, ADCAL = 0 required (RM0444 15.12.15). */
/* Seviye: PARAM[S] STATE[S] BUSY[r] */
SPL_Status ADC_SetCalibrationFactor(ADC_TypeDef *adc, uint8_t factor);

/* CFGR2.CKMODE + CCR.PRESC; ADC fully disabled required (STATE; pending: BUSY).
 * presc only with ADC_CLOCK_ASYNC, else it must be 0. */
/* Seviye: PARAM[S] STATE[S] BUSY[r] */
SPL_Status ADC_CommonClockConfig(ADC_TypeDef *adc, uint32_t ckmode, uint32_t presc);
/* Sets exactly VREFEN/TSEN/VBATEN from mask (ADC_INTERNAL_*); ADSTART = 0 required. */
/* Seviye: PARAM[S] STATE[S] BUSY[r] */
SPL_Status ADC_InternalChannelsConfig(ADC_TypeDef *adc, uint32_t mask);

/* CFGR1 except sequencer mode, DMA and AWD1 bits (kept); AUTOFF/WAIT written 0.
 * ADEN = 0 required (RM0444 15.12.4, ERRATA ES0548 2.6.2). ERR_STATE when the new resolution
 * makes the enabled oversampling overflow 16 bits (G4 v0.13.5 #19) or breaks the low-bit
 * rule of an enabled analog watchdog threshold (RM0444 Table 77). ERR_PARAM when CONT = 0
 * would create the ES0548 2.6.3 case (see ADC_Awd1Config). */
/* Seviye: PARAM[S] STATE[S] BUSY[r] */
SPL_Status ADC_RegularConfig(ADC_TypeDef *adc, const ADC_RegularConfigTypeDef *config);
/* Channel selection with the CCRDY handshake (RM0444 15.3.8). ADSTART = 0 required.
 *  - A previous configuration not yet applied (CCRDY = 0 after a write): BUSY.
 *  - Sequencer mode change (CHSELRMOD/SCANDIR): needs ADEN = 0 (STATE); writes CFGR1 only and
 *    returns SPL_WOULD_BLOCK: wait for ADC_ChannelConfigReady, then call again with the same
 *    configuration to write CHSELR.
 *  - Otherwise clears CCRDY and writes CHSELR (OK); wait for ADC_ChannelConfigReady before
 *    ADC_Start or the next configuration (ADSTART is ignored by hardware until then).
 * ERR_PARAM for the ES0548 2.6.3 case with the current CONT/AWD1 setting. */
/* Seviye: PARAM[S,n] STATE[S] BUSY[r] WOULD_BLOCK[r] */
SPL_Status ADC_ChannelConfig(ADC_TypeDef *adc, const ADC_ChannelConfigTypeDef *config);
/* Seviye: PARAM[S] */
SPL_Status ADC_ChannelConfigReady(ADC_TypeDef *adc, uint8_t *ready);
/* One channel 0..18, forward mask mode: CFGR1 to 12 bit, right, single, software trigger,
 * no DMA (AWD1 bits kept), SMP1 = sample_code for every channel, CHSELR = channel. ADEN = 0
 * required; ordered/backward mode set: ERR_STATE (use ADC_ChannelConfig). CCRDY as above. */
/* Seviye: PARAM[S] STATE[S] BUSY[r] */
SPL_Status ADC_RegularSingleInit(ADC_TypeDef *adc, uint8_t channel, uint8_t sample_code);
/* SMP1/SMP2 codes 0..7 = 1.5, 3.5, 7.5, 12.5, 19.5, 39.5, 79.5, 160.5 ADC cycles;
 * smp2_channels: channels using SMP2 (SMPSEL), others SMP1. ADSTART = 0 required.
 * ERRATA ES0548 2.6.5: 1.5 / 3.5 cycles take one cycle more on the first conversion. */
/* Seviye: PARAM[S] STATE[S] BUSY[r] */
SPL_Status ADC_SamplingConfig(ADC_TypeDef *adc, uint8_t smp1, uint8_t smp2, uint32_t smp2_channels);

/* ADVREGEN = 0 or ADCAL = 1: ERR_STATE; pending ADDIS/ADSTP: BUSY; already enabled: OK without
 * write. Clears ADRDY, sets ADEN; poll ADC_Ready with a timeout. v0.11: AUTOFF = 1 -> ADEN only
 * (ADRDY never sets, RM0444 15.6.2); ADC_Ready then reports ADEN and ADC_Start does not need ADRDY.
 */
/* Seviye: PARAM[S] STATE[S] BUSY[r] */
SPL_Status ADC_Enable(ADC_TypeDef *adc);
/* Seviye: PARAM[S] */
SPL_Status ADC_Ready(ADC_TypeDef *adc, uint8_t *ready);
/* Pending ADDIS/ADSTP: BUSY. Not ready: ERR_STATE. ADSTART = 1: BUSY in single mode with
 * software trigger, ERR_STATE with CONT = 1 or EXTEN != 0 (ADC_StopRegular first). OVR set:
 * ERR_HW. No channel selected: ERR_STATE; channel configuration not applied (CCRDY = 0):
 * BUSY. Clears EOC/EOS, sets ADSTART. */
/* Seviye: PARAM[S] STATE[S] HW[r] BUSY[r] */
SPL_Status ADC_Start(ADC_TypeDef *adc);
/* OVR: ERR_HW. EOC = 0: disabled -> ERR_STATE; ADSTART = 1 -> WOULD_BLOCK; enabled and
 * ADSTART = 0 -> ERR_STATE. ERRATA ES0548 2.6.1: read DR within one conversion time, or a
 * coincident EOC clear can hide an overrun. */
/* Seviye: PARAM[S] STATE[S] HW[r] WOULD_BLOCK[r] */
SPL_Status ADC_TryRead(ADC_TypeDef *adc, uint16_t *value);
/* Clears OVR (W1C). */
/* Seviye: PARAM[S] */
SPL_Status ADC_ClearErrors(ADC_TypeDef *adc);
/* DMAEN/DMACFG are CFGR1 bits: ADEN = 0 required on G0 (G4: ADSTART = 0). */
/* Seviye: PARAM[S] STATE[S] BUSY[r] */
SPL_Status ADC_EnableDma(ADC_TypeDef *adc, uint8_t circular);
/* Seviye: PARAM[S] STATE[S] BUSY[r] */
SPL_Status ADC_DisableDma(ADC_TypeDef *adc);

/* CFGR2 (CKMODE, LFTRIG kept); ADEN = 0 required (ERRATA ES0548 2.6.4). PARAM when enabled
 * and (resolution bits + ratio + 1 - shift) > 16 (RM0444 15.8: wider results truncated). */
/* Seviye: PARAM[S] STATE[S] BUSY[r] */
SPL_Status ADC_OversamplingConfig(ADC_TypeDef *adc, const ADC_OversamplingTypeDef *config);
/* AWD1TR + CFGR1 AWD1 bits; ADEN = 0 required. Thresholds: low bits 0 for RES < 12 bit unless
 * oversampling is on (RM0444 Table 77, 15.8.2), else PARAM.
 * ERRATA ES0548 2.6.3: CONT = 0, sequence of more than one channel, AWD1SGL = 1 and the
 * guarded channel not converted first -> PARAM (the out-of-window value would be missed). */
/* Seviye: PARAM[S] STATE[S] BUSY[r] */
SPL_Status ADC_Awd1Config(ADC_TypeDef *adc, const ADC_Awd1ConfigTypeDef *config);
/* Analog watchdog 2 or 3 (index 2/3): AWDxTR and AWDxCR (channel mask, 0 = off). ADEN = 0
 * required for AWDxCR (RM0444 15.12.13); same threshold rules as AWD1. */
/* Seviye: PARAM[S] STATE[S] BUSY[r] */
SPL_Status ADC_AwdConfig(ADC_TypeDef *adc, uint8_t index, uint16_t low, uint16_t high,
                         uint32_t channels);

/* Raw ADC_ISR (no side effect). */
/* Seviye: PARAM[S] */
SPL_Status ADC_GetFlags(ADC_TypeDef *adc, uint32_t *flags);
/* W1C write of a non-empty subset of ADC_FLAGS_CLEARABLE. */
/* Seviye: PARAM[S,n] */
SPL_Status ADC_ClearFlags(ADC_TypeDef *adc, uint32_t flags);

/* Non-blocking ADSTP; OK without write if nothing runs or ADSTP is pending. */
/* Seviye: PARAM[S] STATE[S] */
SPL_Status ADC_StopRegular(ADC_TypeDef *adc);
/* Seviye: PARAM[S] */
SPL_Status ADC_RegularStopped(ADC_TypeDef *adc, uint8_t *stopped);
/* Non-blocking ADDIS; conversions must be stopped first (STATE). */
/* Seviye: PARAM[S] STATE[S] BUSY[r] */
SPL_Status ADC_Disable(ADC_TypeDef *adc);
/* Seviye: PARAM[S] */
SPL_Status ADC_Disabled(ADC_TypeDef *adc, uint8_t *disabled);

/* v0.11: CFGR1.AUTOFF (auto-off: power-on per conversion, ADRDY never set), CFGR1.WAIT (next
 * conversion waits for the DR read), CFGR2.LFTRIG (low-frequency trigger mode). ADEN = 1 -> STATE.
 */
/* Seviye: PARAM[S,n] STATE[S] */
SPL_Status ADC_LowPowerConfig(ADC_TypeDef *adc, uint8_t auto_off, uint8_t wait, uint8_t lftrig);

#ifdef __cplusplus
}
#endif
#endif /* STM32G0_ADC_H */
