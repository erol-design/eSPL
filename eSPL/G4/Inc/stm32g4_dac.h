/* SPDX-License-Identifier: MIT */
#ifndef STM32G4_DAC_H
#define STM32G4_DAC_H
#include <stdint.h>
#include "stm32g4xx.h"
#include "stm32g4_status.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* Digital-to-analog converter driver (DAC, RM0440 section 22).
 * Scope: DAC1..DAC4 as present in CMSIS. Channel 1 always; channel 2 on dual-channel DACs only
 * (DAC2 is single-channel: SPL_ERR_UNSUPPORTED). DAC3/DAC4 have no GPIO connection and no
 * output buffer (RM0440 22.3): only DAC_MODE_INT_UNBUFFERED is accepted there. Wave generation,
 * sample-and-hold, dual-channel registers, DMA double data, signed format and calibration are
 * not provided.
 * Usage: DAC_SetHighFrequencyMode and DAC_ChannelConfig (channels disabled), DAC_Enable, poll
 * DAC_Ready, then DAC_WriteData (+ DAC_SoftwareTrigger with TSEL=SWTRIG) or the DMA request.
 * Common rules: no function waits; no static state; no clock is enabled; GPIO is not touched.
 * Caller duties (all APIs):
 *  - Clock: DAC AHB clock (RCC module).
 *  - Pin (CCL-2 ANL-08): with an external-pin mode (DAC_MODE_EXT_*) put the DAC output pin
 *    (DAC1 OUT1 PA4, OUT2 PA5; DAC2 OUT1 PA6) in analog mode (GPIO_MODE_ANALOG) before
 *    DAC_Enable and assign no other function (alternate function, GPIO output, another analog
 *    output) to it while the channel is enabled; otherwise two drivers fight on the pin.
 *    DAC_MODE_INT_UNBUFFERED leaves the pin free.
 *  - Concurrency: not reentrant per instance; serialize calls (ISR included).
 * Contract: Docs/v0.10_DAC_CONTRACT.md. */

/* Channel numbers. */
#define DAC_CHANNEL_1 1U
#define DAC_CHANNEL_2 2U

/* DAC_MCR.MODEx normal modes (RM0440 22.7.16). Sample-and-hold modes are not offered. */
/* EXT_BUFFERED / EXT_INT_BUFFERED drive the output pin (CCL-2 ANL-08): no other driver on it. */
#define DAC_MODE_EXT_BUFFERED 0U     /* external pin, buffer enabled */
#define DAC_MODE_EXT_INT_BUFFERED 1U /* external pin + on-chip peripherals, buffer enabled */
/* EXT_UNBUFFERED (caller duty, CCL-2 ANL-03): without the output buffer the pin has the DAC's
 * own output impedance (datasheet R_BOFF 9.6..13.8 kOhm): connect only a high-impedance load
 * (op-amp input, ADC with enough sampling time); a resistive load divides the output voltage.
 * Use a buffered mode for any other load. */
#define DAC_MODE_EXT_UNBUFFERED 2U /* external pin, buffer disabled */
#define DAC_MODE_INT_UNBUFFERED 3U /* on-chip peripherals only, buffer disabled */

/* Data alignment for DAC_WriteData. */
#define DAC_ALIGN_12R 0U /* value 0..0xFFF -> DHR12Rx */
#define DAC_ALIGN_12L 1U /* value 0..0xFFF0, bits 3:0 must be 0 -> DHR12Lx */
#define DAC_ALIGN_8R 2U  /* value 0..0xFF -> DHR8Rx */

    typedef struct
    {
        uint8_t trigger_enable;   /* TENx: 0 = DOR loads one clock after DHR write */
        uint8_t trigger_sel;      /* TSELx 0..15 (0 = SWTRIGx); must be 0 if trigger_enable = 0 */
        uint8_t mode;             /* DAC_MODE_* */
        uint8_t dma_enable;       /* DMAENx 0/1 */
        uint8_t dma_underrun_irq; /* DMAUDRIEx 0/1 */
    } DAC_ChannelConfigTypeDef;

    /* EN=0 and CEN=0 required (STATE). Wave generation is forced off (WAVEx=0, MAMPx=0);
     * MCR DMADOUBLEx=0, SINFORMATx=0 (unsigned). */
    /* Caller: pick a trigger_sel valid for this instance and SKU (TSEL mapping is not checked,
     * RM0440 22.4.2); with dma_enable set up the DMA channel and DMAMUX request first; with
     * dma_underrun_irq the NVIC vector and handler (DAC_ClearUnderrun). */
    /* Level: PARAM[F] STATE[r] UNSUPPORTED[r] */
    SPL_Status DAC_ChannelConfig(DAC_TypeDef *dac, uint32_t channel,
                                 const DAC_ChannelConfigTypeDef *config);
    /* MCR.HFSEL from the AHB clock: >160 MHz -> 10, >80 MHz -> 01, else 00 (RM0440 22.7.16).
     * Both channels must be disabled (EN=0, CEN=0). */
    /* Caller: pass the actual AHB clock; call again after an HCLK change (with the channels
     * disabled). */
    /* Level: PARAM[F] STATE[r] */
    SPL_Status DAC_SetHighFrequencyMode(DAC_TypeDef *dac, uint32_t hclk_hz);
    /* Caller: external-pin mode: pin in analog mode first (file header); wait for DAC_Ready
     * before relying on the output. */
    /* Level: PARAM[F] STATE[r] UNSUPPORTED[r] */
    SPL_Status DAC_Enable(DAC_TypeDef *dac, uint32_t channel);
    /* Caller: stop the trigger / DMA request feeding this channel first. */
    /* Level: PARAM[F] UNSUPPORTED[r] */
    SPL_Status DAC_Disable(DAC_TypeDef *dac, uint32_t channel);
    /* DACxRDY after tWAKEUP; STATE if the channel is disabled. */
    /* Caller: bound the poll. */
    /* Level: PARAM[F] STATE[r] UNSUPPORTED[r] */
    SPL_Status DAC_Ready(DAC_TypeDef *dac, uint32_t channel, uint8_t *ready);
    /* WOULD_BLOCK while EN=1 and DACxRDY=0; STATE if DMAENx=1. */
    /* Caller: none beyond the file-header duties. */
    /* Level: PARAM[F,r] STATE[r] UNSUPPORTED[r] WOULD_BLOCK[r] */
    SPL_Status DAC_WriteData(DAC_TypeDef *dac, uint32_t channel, uint32_t align, uint32_t value);
    /* Requires TENx=1 and TSELx=0 (SWTRIG), else STATE. */
    /* Caller: none beyond the file-header duties. */
    /* Level: PARAM[F] STATE[r] UNSUPPORTED[r] WOULD_BLOCK[r] */
    SPL_Status DAC_SoftwareTrigger(DAC_TypeDef *dac, uint32_t channel);
    /* Caller: none beyond the file-header duties. */
    /* Level: PARAM[F] UNSUPPORTED[r] */
    SPL_Status DAC_ReadOutput(DAC_TypeDef *dac, uint32_t channel, uint32_t *value);
    /* Caller: none beyond the file-header duties. */
    /* Level: PARAM[F] UNSUPPORTED[r] */
    SPL_Status DAC_UnderrunFlag(DAC_TypeDef *dac, uint32_t channel, uint8_t *set);
    /* SR.DMAUDRx is rc_w1: writes only that bit. */
    /* Caller: after an underrun (no new DMA request, the channel keeps converting old data) clear
     * DMAEN, re-initialize the DMA and the DAC channel, lower the trigger rate or the DMA load,
     * then re-enable DMA and trigger (RM0440 22.4.8). */
    /* Level: PARAM[F] UNSUPPORTED[r] */
    SPL_Status DAC_ClearUnderrun(DAC_TypeDef *dac, uint32_t channel);

#ifdef __cplusplus
}
#endif
#endif
