/* SPDX-License-Identifier: MIT */
#ifndef STM32G0_FAST_H
#define STM32G0_FAST_H
#include <stdint.h>
#include "stm32g0xx.h"
#include "stm32g0_status.h"
#include "stm32g0_diag.h"
#include "stm32g0_iwdg.h"

/* Hot-path accessors (Docs/FAST_PATH.md).
 *
 * Simple accesses that run on every call in loops and interrupts. All are static inline: they
 * expand to a few instructions instead of a call. Configuration (Init, Config) is NOT here; that
 * is the job of the checked API.
 *
 * Rules (METHOD exception, Docs/FAST_PATH.md section 2):
 *  1. Access only: a single register read / write or a flag clear. No mode change and no
 *     multi-step sequence.
 *  2. "Fast" in the name: the call site shows that an unchecked access is made.
 *  3. The channel is in the name (TIM_FastSetCompare2), or a scalar argument is checked at
 *     compile time with SPL_DIAG (stm32g0_diag.h).
 *  4. Checks that are never removed: hardware state (lost capture) and the read-back after a
 *     flag clear (METHOD 1.7). DAC value / DMA state and DMA channel range / ES0548 2.4.1 are
 *     checked at FULL (Docs/CHECK_LEVEL.md 5.3, 5.4): not compiled at SPL_CHECK_FAST. Checked
 *     accessors return SPL_Status.
 *  5. An accessor that returns no status gives its value as the return value (no output
 *     pointer).
 *
 * Caller duties (all APIs): Y1..Y6 below; none of them is checked at run time.
 *  Y1. The instance (GPIOx, TIMx, DAC1, FDCANx) is set up with the checked Init / Config API and
 *      its clock is on.
 *  Y2. The instance pointer is a CMSIS constant used directly (GPIOD, TIM1), not taken from a
 *      table or a variable. The board mapping header provides this.
 *  Y3. TIM_FastSetCompareN: the value fits the timer width (TIM2 32 bit, others 16 bit). Excess
 *      bits are silently truncated; the bound is ensured where the value is produced.
 *  Y4. TIM_FastGetCounter: CR1.UIFREMAP = 0 (else bit 31 is the UIF copy, not the counter).
 *  Y5. Channel and line arguments (DMA1_Fast*, EXTI_Fast*) are CONSTANTS; SPL_DIAG can check
 *      constants only.
 *  Y6. TIM_FastSetCompareN / TIM_FastGetCaptureN: the channel exists on that timer (TIM14, TIM16,
 *      TIM17 have one channel; TIM15 has two). */

#ifdef __cplusplus
extern "C"
{
#endif

/* ---- GPIO ---- */

/* Declarations: the SPL_DIAG attribute sits on the declaration. */
static inline uint16_t GPIO_FastRead(const GPIO_TypeDef *port, uint16_t pins)
    SPL_DIAG(pins == 0U, "GPIO_FastRead: empty pin mask");
static inline void GPIO_FastSet(GPIO_TypeDef *port, uint16_t pins)
    SPL_DIAG(pins == 0U, "GPIO_FastSet: empty pin mask");
static inline void GPIO_FastReset(GPIO_TypeDef *port, uint16_t pins)
    SPL_DIAG(pins == 0U, "GPIO_FastReset: empty pin mask");

/* IDR & pins. */
static inline uint16_t GPIO_FastRead(const GPIO_TypeDef *port, uint16_t pins)
{
    return (uint16_t)(port->IDR & (uint32_t)pins);
}

/* BSRR set half: single write, no read-modify-write, safe from any context. */
static inline void GPIO_FastSet(GPIO_TypeDef *port, uint16_t pins)
{
    port->BSRR = (uint32_t)pins;
}

/* BRR: single write. */
static inline void GPIO_FastReset(GPIO_TypeDef *port, uint16_t pins)
{
    port->BRR = (uint32_t)pins;
}

/* ---- TIM: compare, counter, direction ---- */

/* CCRx single write. Y3: the value must fit the width. With OCxPE = 1 it takes effect at the
 * next update. */
static inline void TIM_FastSetCompare1(TIM_TypeDef *t, uint32_t value)
{
    t->CCR1 = value;
}
static inline void TIM_FastSetCompare2(TIM_TypeDef *t, uint32_t value)
{
    t->CCR2 = value;
}
static inline void TIM_FastSetCompare3(TIM_TypeDef *t, uint32_t value)
{
    t->CCR3 = value;
}
static inline void TIM_FastSetCompare4(TIM_TypeDef *t, uint32_t value)
{
    t->CCR4 = value;
}

/* CNT. Y4: with UIFREMAP = 0, bits 31..16 read as zero on 16-bit timers. */
static inline uint32_t TIM_FastGetCounter(const TIM_TypeDef *t)
{
    return t->CNT;
}

/* CR1.DIR: 1 = counting down. On a timer without counter mode the bit is reserved and reads 0.
 */
static inline uint8_t TIM_FastIsCountingDown(const TIM_TypeDef *t)
{
    return (uint8_t)(((t->CR1 & TIM_CR1_DIR) != 0U) ? 1U : 0U);
}

/* ---- TIM: capture (state check kept) ---- */

/* Shared body. CCxOF (SR bit ch + 8) = lost capture: the value is unreliable -> SPL_ERR_HW, the
 * flag is cleared with TIM_FastClearFlags. Without CCxIF (SR bit ch) there is no new capture ->
 * SPL_WOULD_BLOCK instead of a stale value. Reading CCRx clears CCxIF in hardware. */
/* Caller: internal helper of TIM_FastGetCaptureN; none beyond the file-header duties. */
/* Level: PARAM[F] HW[r] WOULD_BLOCK[r] */
static inline SPL_Status Tim_FastCapture(const TIM_TypeDef *t, uint32_t ch_bit,
                                         const volatile uint32_t *ccr, uint32_t *value)
{
    uint32_t sr;

    SPL_REQUIRE(value != (uint32_t *)0, SPL_ERR_PARAM); /* (BULGU-03) */
    sr = t->SR;
    if ((sr & (ch_bit << 8U)) != 0U)
    {
        return SPL_ERR_HW;
    }
    if ((sr & ch_bit) == 0U)
    {
        return SPL_WOULD_BLOCK;
    }
    *value = *ccr;
    return SPL_OK;
}
/* Caller: the channel is an input capture channel (CCxS != 00, not checked); after SPL_ERR_HW
 * clear CCxOF with TIM_FastClearFlags; one reader per channel (Y1, Y6). */
/* Level: PARAM[F] HW[r] WOULD_BLOCK[r] */
static inline SPL_Status TIM_FastGetCapture1(const TIM_TypeDef *t, uint32_t *value)
{
    return Tim_FastCapture(t, TIM_SR_CC1IF, &t->CCR1, value);
}
/* Caller: the channel is an input capture channel (CCxS != 00, not checked); after SPL_ERR_HW
 * clear CCxOF with TIM_FastClearFlags; one reader per channel (Y1, Y6). */
/* Level: PARAM[F] HW[r] WOULD_BLOCK[r] */
static inline SPL_Status TIM_FastGetCapture2(const TIM_TypeDef *t, uint32_t *value)
{
    return Tim_FastCapture(t, TIM_SR_CC2IF, &t->CCR2, value);
}
/* Caller: the channel is an input capture channel (CCxS != 00, not checked); after SPL_ERR_HW
 * clear CCxOF with TIM_FastClearFlags; one reader per channel (Y1, Y6). */
/* Level: PARAM[F] HW[r] WOULD_BLOCK[r] */
static inline SPL_Status TIM_FastGetCapture3(const TIM_TypeDef *t, uint32_t *value)
{
    return Tim_FastCapture(t, TIM_SR_CC3IF, &t->CCR3, value);
}
/* Caller: the channel is an input capture channel (CCxS != 00, not checked); after SPL_ERR_HW
 * clear CCxOF with TIM_FastClearFlags; one reader per channel (Y1, Y6). */
/* Level: PARAM[F] HW[r] WOULD_BLOCK[r] */
static inline SPL_Status TIM_FastGetCapture4(const TIM_TypeDef *t, uint32_t *value)
{
    return Tim_FastCapture(t, TIM_SR_CC4IF, &t->CCR4, value);
}

/* ---- TIM: flags ---- */

/* Declarations: the SPL_DIAG attribute sits on the declaration. */
static inline void TIM_FastClearFlags(TIM_TypeDef *t, uint32_t flags)
    SPL_DIAG(flags == 0U, "TIM_FastClearFlags: empty flag mask");

/* Valid SR bits per timer type (RM0444 21.4.5, 22.4.5, 24.4.3, 25.5.5, 25.6.4); same as the
 * masks of the checked SPL in stm32g0_tim.c. With a constant instance the compiler folds the
 * comparisons. */
static inline uint32_t Tim_FastSrMask(const TIM_TypeDef *t)
{
    const uint32_t general = TIM_SR_UIF | TIM_SR_CC1IF | TIM_SR_CC2IF | TIM_SR_CC3IF |
                             TIM_SR_CC4IF | TIM_SR_TIF | TIM_SR_CC1OF | TIM_SR_CC2OF |
                             TIM_SR_CC3OF | TIM_SR_CC4OF;
    uint32_t mask = TIM_SR_UIF; /* TIM6 / TIM7 */

    if (t == TIM1)
    {
        mask = general | TIM_SR_COMIF | TIM_SR_BIF | TIM_SR_B2IF | TIM_SR_SBIF | TIM_SR_CC5IF |
               TIM_SR_CC6IF;
    }
    else if (t == TIM3)
    {
        mask = general;
    }
#if defined(TIM2)
    else if (t == TIM2)
    {
        mask = general;
    }
#endif
#if defined(TIM4)
    else if (t == TIM4)
    {
        mask = general;
    }
#endif
#if defined(TIM15)
    else if (t == TIM15)
    {
        mask = TIM_SR_UIF | TIM_SR_CC1IF | TIM_SR_CC2IF | TIM_SR_COMIF | TIM_SR_TIF | TIM_SR_BIF |
               TIM_SR_CC1OF | TIM_SR_CC2OF;
    }
#endif
    else if ((t == TIM16) || (t == TIM17))
    {
        mask = TIM_SR_UIF | TIM_SR_CC1IF | TIM_SR_COMIF | TIM_SR_BIF | TIM_SR_CC1OF;
    }
    else if (t == TIM14)
    {
        mask = TIM_SR_UIF | TIM_SR_CC1IF | TIM_SR_CC1OF;
    }
    else
    {
        /* TIM6 / TIM7: UIF only */
    }
    return mask;
}

/* SR & valid bits. */
static inline uint32_t TIM_FastReadFlags(const TIM_TypeDef *t)
{
    return t->SR & Tim_FastSrMask(t);
}

/* SR rc_w0: 0 is written to the requested bits, 1 to the other valid bits, the reset value 0 to
 * reserved bits; no read-modify-write (a flag arriving in between is not cleared). */
static inline void TIM_FastClearFlags(TIM_TypeDef *t, uint32_t flags)
{
    t->SR = Tim_FastSrMask(t) & ~flags;
    /* issue #23: no return before the clear reaches the peripheral (METHOD 1.7) */
    (void)t->SR;
}

/* UIF clear: the most frequent interrupt path. */
static inline void TIM_FastClearUpdate(TIM_TypeDef *t)
{
    t->SR = Tim_FastSrMask(t) & ~TIM_SR_UIF;
    /* issue #23: no return before the clear reaches the peripheral (METHOD 1.7) */
    (void)t->SR;
}

#if defined(DAC1)
/* ---- DAC (value and DMA state checks kept) ---- */

/* Declarations: the SPL_DIAG attribute sits on the declaration. */
/* Caller: none beyond the file-header duties (Y1: channel configured and enabled). */
static inline SPL_Status DAC_FastWrite12R1(DAC_TypeDef *dac, uint32_t value)
    SPL_DIAG(value > 0xFFFU, "DAC_FastWrite12R1: value must fit in 12 bits");
/* Caller: none beyond the file-header duties (Y1: channel configured and enabled). */
static inline SPL_Status DAC_FastWrite12R2(DAC_TypeDef *dac, uint32_t value)
    SPL_DIAG(value > 0xFFFU, "DAC_FastWrite12R2: value must fit in 12 bits");

/* DHR12Rx. Value > 0xFFF: the upper bits would be dropped and the output wrong -> SPL_ERR_PARAM
 * (no write). DMAENx = 1: a manual write races the DMA -> SPL_ERR_STATE (no write). Both are
 * checked (A4 wave 5, CHECK_LEVEL.md 5.3): at FULL, at FAST the UFAST form (write only). */
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
#endif /* DAC1 */

/* ---- IWDG ---- */

static inline void IWDG_FastReload(void)
{
    IWDG->KR = IWDG_KEY_RELOAD;
}

#if defined(FDCAN1)
/* ---- FDCAN ---- */

/* Declarations: the SPL_DIAG attribute sits on the declaration. */
static inline void FDCAN_FastClearIrq(FDCAN_GlobalTypeDef *can, uint32_t mask)
    SPL_DIAG(mask == 0U, "FDCAN_FastClearIrq: empty flag mask");

/* Reading PSR resets LEC/DLEC to 7 (destructive read): read the value once and keep it. */
static inline uint32_t FDCAN_FastReadPsr(const FDCAN_GlobalTypeDef *can)
{
    return can->PSR;
}

/* Reading ECR clears CEL (destructive read). */
static inline uint32_t FDCAN_FastReadEcr(const FDCAN_GlobalTypeDef *can)
{
    return can->ECR;
}

static inline uint32_t FDCAN_FastReadIrq(const FDCAN_GlobalTypeDef *can)
{
    return can->IR;
}

/* IR w1c: only the requested bits are written. */
static inline void FDCAN_FastClearIrq(FDCAN_GlobalTypeDef *can, uint32_t mask)
{
    can->IR = mask;
    /* issue #23: no return before the clear reaches the peripheral (METHOD 1.7) */
    (void)can->IR;
}
#endif /* FDCAN1 */

/* ---- DMA1 (channel 1..DMA1_FAST_CHANNELS) ---- */

/* G05x / G06x / G07x / G08x / G0Bx / G0C1: 7 channels; G030 / G031 / G041: 5 channels (RM0444
 * Table 49, RM0454 Table 33; CMSIS DMA1_Channel6 / 7). 2026-11-14: comment corrected, the code was
 * right. */
#if defined(DMA1_Channel6)
#define DMA1_FAST_CHANNELS 7U
#else
#define DMA1_FAST_CHANNELS 5U
#endif

/* Declarations: the SPL_DIAG attribute sits on the declaration. */
static inline uint32_t DMA1_FastFlags(uint32_t channel)
    SPL_DIAG((channel < 1U) || (channel > DMA1_FAST_CHANNELS),
             "DMA1_FastFlags: channel outside the SKU");
/* Caller: none beyond the file-header duties (Y5: channel and flags constant). */
static inline SPL_Status DMA1_FastClearFlags(uint32_t channel, uint32_t flags)
    SPL_DIAG((channel < 1U) || (channel > DMA1_FAST_CHANNELS),
             "DMA1_FastClearFlags: channel outside the SKU")
        SPL_DIAG((flags == 0U) || (flags > 0xFU), "DMA1_FastClearFlags: flags 1..15");

/* The 4 status bits of channel x (GIF, TCIF, HTIF, TEIF). Y5: the channel must be constant. */
static inline uint32_t DMA1_FastFlags(uint32_t channel)
{
    return (DMA1->ISR >> ((channel - 1U) * 4U)) & 0xFU;
}

/* IFCR w1c. ERRATA ES0548 2.4.1: clearing GIF while the channel is enabled breaks the automatic
 * channel disable on a transfer error in the same cycle and TEIF is lost -> SPL_ERR_STATE (no
 * write). The channel range is also checked at run time: a value outside it would make the shift
 * undefined. */
/* Caller: none beyond the file-header duties (Y5: channel and flags constant). */
/* Level: PARAM[F] STATE[F] */
static inline SPL_Status DMA1_FastClearFlags(uint32_t channel, uint32_t flags)
{
    const DMA_Channel_TypeDef *ch;

    /* Check level (A3, Docs/CHECK_LEVEL.md 5.4): a channel out of range makes the shift undefined
     * and clears the flags of an adjacent channel; flags = 0 is a write with no effect
     *; flags > 15 spills into the adjacent channel. */
    SPL_REQUIRE((channel >= 1U) && (channel <= DMA1_FAST_CHANNELS), SPL_ERR_PARAM);
    SPL_REQUIRE(flags != 0U, SPL_ERR_PARAM);
    SPL_REQUIRE(flags <= 0xFU, SPL_ERR_PARAM);
    ch = (const DMA_Channel_TypeDef *)(DMA1_Channel1_BASE + ((channel - 1U) * 0x14U));
    /* ERRATA ES0548 2.4.1. */
    SPL_REQUIRE(((flags & DMA_IFCR_CGIF1) == 0U) || ((ch->CCR & DMA_CCR_EN) == 0U), SPL_ERR_STATE);
    DMA1->IFCR = flags << ((channel - 1U) * 4U);
    /* issue #23: no return before the clear reaches the peripheral (METHOD 1.7) */
    (void)DMA1->ISR;
    return SPL_OK;
}

/* ---- EXTI (GPIO lines 0..15) ---- */

/* Declarations: the SPL_DIAG attribute sits on the declaration. */
static inline uint32_t EXTI_FastPending(uint32_t lines)
    SPL_DIAG((lines == 0U) || (lines > 0xFFFFU), "EXTI_FastPending: GPIO lines 0..15");
static inline void EXTI_FastClearPending(uint32_t lines)
    SPL_DIAG((lines == 0U) || (lines > 0xFFFFU), "EXTI_FastClearPending: GPIO lines 0..15");

/* Lines with a pending rising or falling edge. Y5: the line mask must be constant. */
static inline uint32_t EXTI_FastPending(uint32_t lines)
{
    return (EXTI->RPR1 | EXTI->FPR1) & lines;
}

/* RPR1 / FPR1 w1c: only the requested lines. */
static inline void EXTI_FastClearPending(uint32_t lines)
{
    EXTI->RPR1 = lines;
    EXTI->FPR1 = lines;
    /* issue #23: no return before the clear reaches the peripheral (METHOD 1.7) */
    (void)EXTI->RPR1;
}

#ifdef __cplusplus
}
#endif

#endif /* STM32G0_FAST_H */
