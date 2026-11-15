#ifndef STM32G0_DAC_H
#define STM32G0_DAC_H
#include <stdint.h>
#include "stm32g0xx.h"
#include "stm32g0_status.h"
#include "stm32g0_sku.h"
#ifdef __cplusplus
extern "C"
{
#endif

/* v0.8 SKU profile: this API exists only where the device has the peripheral (SPL_G0_HAS_DAC);
 * absent on G030 / G031 / G041 / G050 / G070 / G0B0. */
#if SPL_G0_HAS_DAC
/* DAC1 of STM32G0B1, two channels (RM0444 Rev 6 section 16; Docs/v0.5_DAC_CONTRACT.md). Ported
 * from the G4 SPL v0.13.5 DAC driver. G0 differences: one DAC; no DACxRDY flag (the caller waits
 * tWAKEUP after DAC_Enable, datasheet), so G4 DAC_Ready does not exist and DAC_WriteData never
 * returns WOULD_BLOCK; no MCR.HFSEL (no DAC_SetHighFrequencyMode), no DMADOUBLE / SINFORMAT.
 * The DAC APB clock (RCC_PERIPH_DAC) and the analog GPIO mode are caller duties.
 * Not reentrant: serialize calls (ISR included). */
/* Channel numbers. */
#define DAC_CHANNEL_1 1U
#define DAC_CHANNEL_2 2U
/* DAC_MCR.MODEx normal modes (RM0444 16.7.16). v0.11: sample and hold = DAC_SampleHoldConfig. */
#define DAC_MODE_EXT_BUFFERED 0U     /* external pin, buffer enabled */
#define DAC_MODE_EXT_INT_BUFFERED 1U /* external pin + on-chip peripherals, buffer enabled */
#define DAC_MODE_EXT_UNBUFFERED 2U   /* external pin, buffer disabled */
#define DAC_MODE_INT_UNBUFFERED 3U   /* on-chip peripherals only, buffer disabled */
/* ERRATA ES0418 2.3.1 / ES0412 2.3.1 (G071 / G081): with DAC_MODE_INT_UNBUFFERED the DAC pin
 * (PA4 / PA5) output buffer stays disabled: the pin works only as input or analog, not as GPIO /
 * AF output (no workaround). Caller: do not plan another output function on that pin. */
#define DAC_MODE_SAMPLE_HOLD 4U /* MODEx bit 2: sample and hold on top of a normal mode code */
/* Wave generation (WAVEx). */
#define DAC_WAVE_NONE 0U
#define DAC_WAVE_NOISE 1U
#define DAC_WAVE_TRIANGLE 2U
/* Data alignment for DAC_WriteData. */
#define DAC_ALIGN_12R 0U /* value 0..0xFFF -> DHR12Rx */
#define DAC_ALIGN_12L 1U /* value 0..0xFFF0, bits 3:0 must be 0 -> DHR12Lx */
#define DAC_ALIGN_8R 2U  /* value 0..0xFF -> DHR8Rx */
typedef struct
{
    uint8_t trigger_enable;   /* TENx: 0 = DOR loads one clock after DHR write */
    uint8_t trigger_sel;      /* TSELx (0 = SWTRIGx); must be 0 if trigger_enable = 0. Connected
                               * dac_chx_trg: 1, 2, 3, 5, 6, 8, 11, 12, 13 (RM0444 Table 85);
                               * 4, 7, 9, 10, 14, 15 are unconnected -> PARAM */
    uint8_t mode;             /* DAC_MODE_* */
    uint8_t dma_enable;       /* DMAENx 0/1 */
    uint8_t dma_underrun_irq; /* DMAUDRIEx 0/1 */
} DAC_ChannelConfigTypeDef;
/* EN=0 and CEN=0 required (STATE). Wave generation is forced off (WAVEx=0, MAMPx=0).
 * ERRATA ES0548 2.7.1: MODE programmed before any data write gives an invalid output; the
 * driver writes DHR12Rx (its current value) before MCR.MODEx, then CR. */
/* Seviye: PARAM[S,n] STATE[S] */
SPL_Status DAC_ChannelConfig(DAC_TypeDef *dac, uint32_t channel,
                             const DAC_ChannelConfigTypeDef *config);
/* ENx = 1; STATE in calibration mode (CENx = 1). Output valid after tWAKEUP. */
/* Seviye: PARAM[S] STATE[S] */
SPL_Status DAC_Enable(DAC_TypeDef *dac, uint32_t channel);
/* Seviye: PARAM[S] */
SPL_Status DAC_Disable(DAC_TypeDef *dac, uint32_t channel);
/* STATE if DMAENx=1 (the DMA owns the data register). */
/* Seviye: PARAM[S,r] STATE[S] */
SPL_Status DAC_WriteData(DAC_TypeDef *dac, uint32_t channel, uint32_t align, uint32_t value);
/* Requires TENx=1 and TSELx=0 (SWTRIG), else STATE.
 * ERRATA ES0548 2.7.2: with DMA, a trigger on the DMA acknowledge cycle can hide DMAUDR; do not
 * mix software and hardware triggers with DMA. */
/* Seviye: PARAM[S] STATE[S] */
SPL_Status DAC_SoftwareTrigger(DAC_TypeDef *dac, uint32_t channel);
/* Seviye: PARAM[S] */
SPL_Status DAC_ReadOutput(DAC_TypeDef *dac, uint32_t channel, uint32_t *value);
/* Seviye: PARAM[S] */
SPL_Status DAC_UnderrunFlag(DAC_TypeDef *dac, uint32_t channel, uint8_t *set);
/* SR.DMAUDRx is rc_w1: writes only that bit. */
/* Seviye: PARAM[S] */
SPL_Status DAC_ClearUnderrun(DAC_TypeDef *dac, uint32_t channel);
/* ---- v0.11: wave generation, sample and hold, dual channel, calibration ---- */
/* WAVEx = wave (DAC_WAVE_*), MAMPx = amplitude 0..11 (noise: unmasked LFSR bits - 1; triangle:
 * 2^(amplitude + 1) - 1; RM0444 16.4.9 / 16.4.10). ENx / CENx = 1 -> STATE; wave without TENx = 1
 * -> STATE (wave generation is used only with the trigger enabled, RM0444 16.7.1). */
/* Seviye: PARAM[S,n] STATE[S] */
SPL_Status DAC_WaveConfig(DAC_TypeDef *dac, uint32_t channel, uint32_t wave, uint32_t amplitude);
/* Sample and hold (RM0444 16.4.11): MODEx = DAC_MODE_SAMPLE_HOLD | mode (DAC_MODE_* 0..3), SHSRx =
 * sample (0..1023 LSI periods + 1), SHHR hold (0..1023), SHRR refresh (0..255). ENx / CENx = 1 ->
 * STATE. ERRATA ES0548 2.7.1 handled as in DAC_ChannelConfig. LSI / LSE clock: caller. */
/* Seviye: PARAM[S] STATE[S] */
SPL_Status DAC_SampleHoldConfig(DAC_TypeDef *dac, uint32_t channel, uint32_t mode, uint32_t sample,
                                uint32_t hold, uint32_t refresh);
/* Both channels in one access: DHR12RD / DHR12LD / DHR8RD (align as DAC_WriteData). */
/* Seviye: PARAM[S] */
SPL_Status DAC_WriteDual(DAC_TypeDef *dac, uint32_t align, uint32_t value1, uint32_t value2);
/* SWTRIG1 | SWTRIG2 in one write. */
/* Seviye: PARAM[S] */
SPL_Status DAC_SoftwareTriggerDual(DAC_TypeDef *dac);
/* Buffer offset trim CCR.OTRIMx = trim (0..31); ENx = 1 -> STATE (RM0444 16.4.12). */
/* Seviye: PARAM[S] STATE[S] */
SPL_Status DAC_SetOffsetTrim(DAC_TypeDef *dac, uint32_t channel, uint32_t trim);
/* CENx = enable (calibration mode); ENx = 1 -> STATE. The trim search (OTRIM steps, wait, CAL_FLAG)
 * is the caller's loop (RM0444 16.4.12). */
/* Seviye: PARAM[S,n] STATE[S] */
SPL_Status DAC_CalibrationMode(DAC_TypeDef *dac, uint32_t channel, uint8_t enable);
/* *high = CAL_FLAGx. */
/* Seviye: PARAM[S] */
SPL_Status DAC_CalibrationFlag(DAC_TypeDef *dac, uint32_t channel, uint8_t *high);

#endif /* SPL_G0_HAS_DAC */

#ifdef __cplusplus
}
#endif
#endif /* STM32G0_DAC_H */
