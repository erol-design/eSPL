/* SPDX-License-Identifier: MIT */
#ifndef STM32G4_FAST_H
#define STM32G4_FAST_H

#include <stdint.h>
#include "stm32g4xx.h"
#include "stm32g4_status.h"
#include "stm32g4_diag.h"
#include "stm32g4_iwdg.h"
#include "stm32g4_tim.h"

/* Hot-path accessors (G4 counterpart of eSPL G0 stm32g0_fast.h).
 * Scope: single register reads / writes and flag clears for GPIO, TIM, DAC, IWDG, FDCAN, DMA1
 * and EXTI, all static inline. Configuration (Init, Config) stays in the checked module APIs.
 * Usage: configure the instance with its module API, then call the Fast accessor in loops and
 * interrupt handlers.
 * Common rules: "Fast" in the name makes the direct, unchecked MMIO access visible at the call
 * site; scalar arguments with a fixed range are checked at compile time with SPL_DIAG
 * (stm32g4_diag.h) when they are constants. Never removed: hardware state (lost capture) and
 * the read-back after a flag clear. DAC value / DMA state and the DMA channel range are checked
 * at FULL only (SPL_REQUIRE). Accessors without a status return their value directly. No
 * function waits; no static state.
 * Caller duties (all APIs): Y1..Y6 below; none of them is checked at run time.
 *  Y1. The instance (GPIOx, TIMx, DAC1, FDCANx) is set up with the checked Init / Config API and
 *      its clock is on.
 *  Y2. The instance pointer is a CMSIS constant used directly (GPIOA, TIM1), not taken from a
 *      table or a variable.
 *  Y3. TIM_FastSetCompareN: the value fits the timer width (TIM2 / TIM5 32 bit, others 16 bit);
 *      excess bits are silently truncated.
 *  Y4. TIM_FastGetCounter: CR1.UIFREMAP = 0 (else bit 31 is the UIF copy, not the counter).
 *  Y5. Channel and line arguments (DMA1_Fast*, EXTI_Fast*) are constants; SPL_DIAG can check
 *      constants only.
 *  Y6. TIM_FastSetCompareN / TIM_FastGetCaptureN: the channel exists on that timer (TIM15 has
 *      two, TIM16 / TIM17 one, TIM6 / TIM7 none).
 * Contract: none of its own; the module contracts of the accessed peripherals apply. */

#ifdef __cplusplus
extern "C"
{
#endif

    static inline uint16_t GPIO_FastRead(const GPIO_TypeDef *port, uint16_t pins)
        SPL_DIAG(pins == 0U, "GPIO_FastRead: empty pin mask");
    static inline void GPIO_FastSet(GPIO_TypeDef *port, uint16_t pins)
        SPL_DIAG(pins == 0U, "GPIO_FastSet: empty pin mask");
    static inline void GPIO_FastReset(GPIO_TypeDef *port, uint16_t pins)
        SPL_DIAG(pins == 0U, "GPIO_FastReset: empty pin mask");

    static inline uint16_t GPIO_FastRead(const GPIO_TypeDef *port, uint16_t pins)
    {
        return (uint16_t)(port->IDR & (uint32_t)pins);
    }

    static inline void GPIO_FastSet(GPIO_TypeDef *port, uint16_t pins)
    {
        port->BSRR = (uint32_t)pins;
    }

    static inline void GPIO_FastReset(GPIO_TypeDef *port, uint16_t pins)
    {
        port->BRR = (uint32_t)pins;
    }

    static inline void TIM_FastSetCompare1(TIM_TypeDef *tim, uint32_t value)
    {
        tim->CCR1 = value;
    }
    static inline void TIM_FastSetCompare2(TIM_TypeDef *tim, uint32_t value)
    {
        tim->CCR2 = value;
    }
    static inline void TIM_FastSetCompare3(TIM_TypeDef *tim, uint32_t value)
    {
        tim->CCR3 = value;
    }
    static inline void TIM_FastSetCompare4(TIM_TypeDef *tim, uint32_t value)
    {
        tim->CCR4 = value;
    }

    static inline uint32_t TIM_FastGetCounter(const TIM_TypeDef *tim)
    {
        return tim->CNT;
    }

    static inline uint8_t TIM_FastIsCountingDown(const TIM_TypeDef *tim)
    {
        return (uint8_t)(((tim->CR1 & TIM_CR1_DIR) != 0U) ? 1U : 0U);
    }

    /* Caller: internal helper of TIM_FastGetCaptureN; none beyond the file-header duties. */
    /* Level: PARAM[F] HW[r] WOULD_BLOCK[r] */
    static inline SPL_Status Tim_FastCapture(const TIM_TypeDef *tim, uint32_t flag,
                                             const volatile uint32_t *ccr, uint32_t *value)
    {
        uint32_t status;

        SPL_REQUIRE(value != (uint32_t *)0, SPL_ERR_PARAM);
        status = tim->SR;
        if ((status & (flag << 8U)) != 0U)
        {
            return SPL_ERR_HW;
        }
        if ((status & flag) == 0U)
        {
            return SPL_WOULD_BLOCK;
        }
        *value = *ccr;
        return SPL_OK;
    }

    /* Caller: the channel is an input capture channel (CCxS != 00, not checked); after SPL_ERR_HW
     * clear CCxOF with TIM_FastClearFlags; one reader per channel (Y1, Y6). */
    /* Level: PARAM[F] HW[r] WOULD_BLOCK[r] */
    static inline SPL_Status TIM_FastGetCapture1(const TIM_TypeDef *tim, uint32_t *value)
    {
        return Tim_FastCapture(tim, TIM_SR_CC1IF, &tim->CCR1, value);
    }
    /* Caller: the channel is an input capture channel (CCxS != 00, not checked); after SPL_ERR_HW
     * clear CCxOF with TIM_FastClearFlags; one reader per channel (Y1, Y6). */
    /* Level: PARAM[F] HW[r] WOULD_BLOCK[r] */
    static inline SPL_Status TIM_FastGetCapture2(const TIM_TypeDef *tim, uint32_t *value)
    {
        return Tim_FastCapture(tim, TIM_SR_CC2IF, &tim->CCR2, value);
    }
    /* Caller: the channel is an input capture channel (CCxS != 00, not checked); after SPL_ERR_HW
     * clear CCxOF with TIM_FastClearFlags; one reader per channel (Y1, Y6). */
    /* Level: PARAM[F] HW[r] WOULD_BLOCK[r] */
    static inline SPL_Status TIM_FastGetCapture3(const TIM_TypeDef *tim, uint32_t *value)
    {
        return Tim_FastCapture(tim, TIM_SR_CC3IF, &tim->CCR3, value);
    }
    /* Caller: the channel is an input capture channel (CCxS != 00, not checked); after SPL_ERR_HW
     * clear CCxOF with TIM_FastClearFlags; one reader per channel (Y1, Y6). */
    /* Level: PARAM[F] HW[r] WOULD_BLOCK[r] */
    static inline SPL_Status TIM_FastGetCapture4(const TIM_TypeDef *tim, uint32_t *value)
    {
        return Tim_FastCapture(tim, TIM_SR_CC4IF, &tim->CCR4, value);
    }

#if defined(TIM_SPL_EXTENDED)
#define TIM_FAST_FLAGS_ENCODER (TIM_SR_IDXF | TIM_SR_DIRF | TIM_SR_IERRF | TIM_SR_TERRF)
#define TIM_FAST_FLAGS_GENERAL                                                                     \
    (TIM_SR_UIF | TIM_SR_CC1IF | TIM_SR_CC2IF | TIM_SR_CC3IF | TIM_SR_CC4IF | TIM_SR_TIF |         \
     TIM_SR_CC1OF | TIM_SR_CC2OF | TIM_SR_CC3OF | TIM_SR_CC4OF | TIM_FAST_FLAGS_ENCODER)
#define TIM_FAST_FLAGS_ADVANCED                                                                    \
    (TIM_FAST_FLAGS_GENERAL | TIM_SR_COMIF | TIM_SR_BIF | TIM_SR_B2IF | TIM_SR_SBIF |              \
     TIM_SR_CC5IF | TIM_SR_CC6IF)

    static inline uint32_t Tim_FastFlagMask(const TIM_TypeDef *tim)
    {
        uint32_t mask = TIM_SR_UIF;

        if (tim == TIM1
#if defined(TIM8)
            || tim == TIM8
#endif
#if defined(TIM20)
            || tim == TIM20
#endif
        )
        {
            mask = TIM_FAST_FLAGS_ADVANCED;
        }
        else if ((tim == TIM2) || (tim == TIM3)
#if defined(TIM4)
                 || (tim == TIM4)
#endif
#if defined(TIM5)
                 || (tim == TIM5)
#endif
        )
        {
            mask = TIM_FAST_FLAGS_GENERAL;
        }
        else if (tim == TIM15)
        {
            mask = TIM_SR_UIF | TIM_SR_CC1IF | TIM_SR_CC2IF | TIM_SR_COMIF | TIM_SR_TIF |
                   TIM_SR_BIF | TIM_SR_CC1OF | TIM_SR_CC2OF;
        }
        else if ((tim == TIM16) || (tim == TIM17))
        {
            mask = TIM_SR_UIF | TIM_SR_CC1IF | TIM_SR_COMIF | TIM_SR_BIF | TIM_SR_CC1OF;
        }
        else
        {
            /* TIM6/TIM7: UIF only. */
        }
        return mask;
    }
#else
static inline uint32_t Tim_FastFlagMask(const TIM_TypeDef *tim)
{
    (void)tim;
    return TIM_SR_UIF | TIM_SR_CC1IF | TIM_SR_CC2IF | TIM_SR_CC3IF | TIM_SR_CC4IF | TIM_SR_TIF |
           TIM_SR_CC1OF | TIM_SR_CC2OF | TIM_SR_CC3OF | TIM_SR_CC4OF;
}
#endif

    static inline void TIM_FastClearFlags(TIM_TypeDef *tim, uint32_t flags)
        SPL_DIAG(flags == 0U, "TIM_FastClearFlags: empty flag mask");

    static inline uint32_t TIM_FastReadFlags(const TIM_TypeDef *tim)
    {
        return tim->SR & Tim_FastFlagMask(tim);
    }

    static inline void TIM_FastClearFlags(TIM_TypeDef *tim, uint32_t flags)
    {
        tim->SR = Tim_FastFlagMask(tim) & ~flags;
        (void)tim->SR;
    }

    static inline void TIM_FastClearUpdate(TIM_TypeDef *tim)
    {
        tim->SR = Tim_FastFlagMask(tim) & ~(uint32_t)TIM_SR_UIF;
        (void)tim->SR;
    }

#if defined(DAC1)
    /* Caller: none beyond the file-header duties (Y1: channel configured and enabled). */
    static inline SPL_Status DAC_FastWrite12R1(DAC_TypeDef *dac, uint32_t value)
        SPL_DIAG(value > 0xFFFU, "DAC_FastWrite12R1: value must fit 12 bits");
    /* Caller: none beyond the file-header duties (Y1: channel configured and enabled). */
    static inline SPL_Status DAC_FastWrite12R2(DAC_TypeDef *dac, uint32_t value)
        SPL_DIAG(value > 0xFFFU, "DAC_FastWrite12R2: value must fit 12 bits");

    /* Caller: none beyond the file-header duties (Y1: channel configured and enabled). */
    /* Level: PARAM[F] STATE[F] */
    static inline SPL_Status DAC_FastWrite12R1(DAC_TypeDef *dac, uint32_t value)
    {
        SPL_REQUIRE(value <= 0xFFFU, SPL_ERR_PARAM);
        SPL_REQUIRE((dac->CR & DAC_CR_DMAEN1) == 0U, SPL_ERR_STATE);
        dac->DHR12R1 = value;
        return SPL_OK;
    }

    /* Caller: none beyond the file-header duties (Y1: channel configured and enabled). */
    /* Level: PARAM[F] STATE[F] */
    static inline SPL_Status DAC_FastWrite12R2(DAC_TypeDef *dac, uint32_t value)
    {
        SPL_REQUIRE(value <= 0xFFFU, SPL_ERR_PARAM);
        SPL_REQUIRE((dac->CR & DAC_CR_DMAEN2) == 0U, SPL_ERR_STATE);
        dac->DHR12R2 = value;
        return SPL_OK;
    }
#endif

    static inline void IWDG_FastReload(void)
    {
        IWDG->KR = IWDG_KEY_RELOAD;
    }

#if defined(FDCAN1)
    static inline void FDCAN_FastClearIrq(FDCAN_GlobalTypeDef *can, uint32_t mask)
        SPL_DIAG(mask == 0U, "FDCAN_FastClearIrq: empty flag mask");
    static inline uint32_t FDCAN_FastReadPsr(const FDCAN_GlobalTypeDef *can)
    {
        return can->PSR;
    }
    static inline uint32_t FDCAN_FastReadEcr(const FDCAN_GlobalTypeDef *can)
    {
        return can->ECR;
    }
    static inline uint32_t FDCAN_FastReadIrq(const FDCAN_GlobalTypeDef *can)
    {
        return can->IR;
    }
    static inline void FDCAN_FastClearIrq(FDCAN_GlobalTypeDef *can, uint32_t mask)
    {
        can->IR = mask;
        (void)can->IR;
    }
#endif

#if defined(DMA1_Channel8)
#define DMA1_FAST_CHANNELS 8U
#else
#define DMA1_FAST_CHANNELS 6U
#endif

/* Compile-time check (CCL-2 YAP-03): DMA1_Channel1_BASE + (channel - 1) * 0x14 reaches exactly
 * the last CMSIS DMA1 channel, and its ISR/IFCR shift (channel - 1) * 4 stays <= 28. */
#if defined(DMA1_Channel8)
    typedef char Dma1_FastChannelCheck[(((DMA1_Channel2_BASE - DMA1_Channel1_BASE) == 0x14U) &&
                                        ((DMA1_Channel1_BASE + ((DMA1_FAST_CHANNELS - 1U) *
                                                                0x14U)) == DMA1_Channel8_BASE) &&
                                        (((DMA1_FAST_CHANNELS - 1U) * 4U) <= 28U))
                                           ? 1
                                           : -1];
#else
typedef char Dma1_FastChannelCheck[(((DMA1_Channel2_BASE - DMA1_Channel1_BASE) == 0x14U) &&
                                    ((DMA1_Channel1_BASE + ((DMA1_FAST_CHANNELS - 1U) * 0x14U)) ==
                                     DMA1_Channel6_BASE) &&
                                    (((DMA1_FAST_CHANNELS - 1U) * 4U) <= 28U))
                                       ? 1
                                       : -1];
#endif

    static inline uint32_t DMA1_FastFlags(uint32_t channel)
        SPL_DIAG((channel < 1U) || (channel > DMA1_FAST_CHANNELS),
                 "DMA1_FastFlags: channel outside SKU");
    /* Caller: none beyond the file-header duties (Y5: channel and flags constant). */
    static inline SPL_Status DMA1_FastClearFlags(uint32_t channel, uint32_t flags)
        SPL_DIAG((channel < 1U) || (channel > DMA1_FAST_CHANNELS),
                 "DMA1_FastClearFlags: channel outside SKU")
            SPL_DIAG((flags == 0U) || (flags > 0xFU), "DMA1_FastClearFlags: flags must be 1..15");

    static inline uint32_t DMA1_FastFlags(uint32_t channel)
    {
        return (DMA1->ISR >> ((channel - 1U) * 4U)) & 0xFU;
    }

    /* GIF (bit0) with the channel enabled -> SPL_ERR_STATE, nothing written (DMA errata, see
     * DMA_ClearFlags). */
    /* Caller: none beyond the file-header duties (Y5: channel and flags constant). */
    /* Level: PARAM[F] STATE[F] */
    static inline SPL_Status DMA1_FastClearFlags(uint32_t channel, uint32_t flags)
    {
        const DMA_Channel_TypeDef *dma_channel;

        SPL_REQUIRE((channel >= 1U) && (channel <= DMA1_FAST_CHANNELS), SPL_ERR_PARAM);
        SPL_REQUIRE(flags != 0U, SPL_ERR_PARAM);
        SPL_REQUIRE(flags <= 0xFU, SPL_ERR_PARAM);
        dma_channel = (const DMA_Channel_TypeDef *)(DMA1_Channel1_BASE + ((channel - 1U) * 0x14U));
        SPL_REQUIRE(((flags & DMA_IFCR_CGIF1) == 0U) || ((dma_channel->CCR & DMA_CCR_EN) == 0U),
                    SPL_ERR_STATE);
        DMA1->IFCR = flags << ((channel - 1U) * 4U);
        (void)DMA1->ISR;
        return SPL_OK;
    }

    static inline uint32_t EXTI_FastPending(uint32_t lines)
        SPL_DIAG((lines == 0U) || (lines > 0xFFFFU), "EXTI_FastPending: GPIO lines 0..15");
    static inline void EXTI_FastClearPending(uint32_t lines)
        SPL_DIAG((lines == 0U) || (lines > 0xFFFFU), "EXTI_FastClearPending: GPIO lines 0..15");

    static inline uint32_t EXTI_FastPending(uint32_t lines)
    {
        return EXTI->PR1 & lines;
    }

    static inline void EXTI_FastClearPending(uint32_t lines)
    {
        EXTI->PR1 = lines;
        (void)EXTI->PR1;
    }

#ifdef __cplusplus
}
#endif

#endif /* STM32G4_FAST_H */
