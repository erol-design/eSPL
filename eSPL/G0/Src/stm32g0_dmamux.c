/* DMAMUX request routing for STM32G0B1 (RM0444 Rev 6 section 11). Ported from the STM32G4 SPL
 * v0.13.3 and rewritten for the contiguous G0B1 channel map (no G431 address hole). */
/* Kutuphane birimi: spl_check.h karisma korumasi referansini bu birim de uretir; uretmeyen tek
 * birim stm32g0_check.c (SPL_CHECK_LEVEL_DEFINER). BULGU-02, CHECK_LEVEL.md 3.2. */
#define SPL_LIBRARY_BUILD 1
#include <stddef.h>
#include "stm32g0_dmamux.h"
#include "stm32g0_dma_priv.h"

/* Denetim seviyesi (A4 dalga 2, Docs/CHECK_LEVEL.md 5.4 DMAMUX tablosu): SPL_REQUIRE = normal sinif
 * (yalniz FULL), SPL_REQUIRE_SAFE = SAFE sinif (FULL ve MIX). Kanal dogrulamasi saf
 * STM32G0_DmaIsChannel, genel indeks STM32G0_DmaGlobalIndex (dogrulama yapmaz). Butun makrolar ilk
 * MMIO yazimindan once (METHOD 1.3). */

SPL_Status DMAMUX_SetRequest(DMA_Channel_TypeDef *dma_channel, uint8_t request_id)
{
    DMAMUX_Channel_TypeDef *mux;

    /* SAFE: maske baska istegi secer; gecersiz kanal rastgele DMAMUX adresine yazar. */
    SPL_REQUIRE_SAFE(request_id <= STM32G0_DMAMUX_REQUEST_MAX, SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(STM32G0_DmaIsChannel(dma_channel) != 0U, SPL_ERR_PARAM);
    /* SAFE: suren aktarimin istegi degisir. */
    SPL_REQUIRE_SAFE((dma_channel->CCR & DMA_CCR_EN) == 0U, SPL_ERR_STATE);
    mux = (DMAMUX_Channel_TypeDef *)(DMAMUX1_Channel0_BASE +
                                     (4U * STM32G0_DmaGlobalIndex(dma_channel)));
    /* ERRATA ES0548 2.5.4: whole-register write, SE = 0 and SPOL = 00. */
    mux->CCR = ((uint32_t)request_id << DMAMUX_CxCR_DMAREQ_ID_Pos) & DMAMUX_CxCR_DMAREQ_ID_Msk;
    return SPL_OK;
}

SPL_Status DMAMUX_ClearOverrun(uint16_t mask)
{
    /* normal: ayrilmis bitlere yazim (komsu birim yok). */
    /* SAFE (EDR-003): ayrilmis bite yazim. */
    SPL_REQUIRE_SAFE(((uint32_t)mask & ~STM32G0_DMAMUX_VALID_MASK) == 0U, SPL_ERR_PARAM);
    DMAMUX1_ChannelStatus->CFR = (uint32_t)mask; /* write-1 clear: no read-modify-write */
    /* Read back: the clear reaches the peripheral before return (METHOD 1.7, issue #23). */
    (void)DMAMUX1_ChannelStatus->CSR;
    return SPL_OK;
}

/* ---- v0.11: synchronization and request generator (Docs/v0.11_TIME_ANALOG_SYSTEM.md) ---- */

#define DMAMUX_SYNC_INPUTS 23U /* RM0444 Table 57: inputs 0..22, 23 reserved */
#define DMAMUX_EXTI_INPUTS 16U /* RM0444 Tables 56 / 57: inputs 0..15 = EXTI0..15 */

/* ERRATA ES0487 2.2.4 (ES0486 / ES0488 / ES0468 / ES0418 / ES0412 2.2.4): the EXTI sync and
 * trigger inputs are routed to it_exti_per(y) and never fire; no workaround -> UNSUPPORTED. */
#if SPL_CHECK_LEVEL <= SPL_CHECK_MIX
/* SAFE (errata): senkron / uretec sessizce sonsuza dek bekler. Yalniz denetimde; saf. */
static uint8_t Dmamux_InputDead(uint32_t input)
{
    uint8_t dead = ((SPL_G0_ERRATA_DMAMUX_EXTI != 0) && (input < DMAMUX_EXTI_INPUTS)) ? 1U : 0U;
#if !defined(LPTIM1)
    /* Inputs 20 / 21 = LPTIM1_OUT / LPTIM2_OUT (RM0444 Tables 56 / 57); reserved on the value line
     * without LPTIM (RM0454 Tables 40 / 41). 2026-11-14 capability audit. */
    if ((input == 20U) || (input == 21U))
    {
        dead = 1U;
    }
#endif
    return dead;
}
#define DMAMUX_INPUT_ALIVE(input) (Dmamux_InputDead(input) == 0U)
#else
#define DMAMUX_INPUT_ALIVE(input) (1) /* kapali seviyede degerlendirilmez */
#endif
#define DMAMUX_GENERATORS 4U /* RM0444 Table 54 / RM0454 Table 38 */

static DMAMUX_Channel_TypeDef *Dmamux_Channel(uint32_t mux_index)
{
    return (DMAMUX_Channel_TypeDef *)(DMAMUX1_Channel0_BASE + (4U * mux_index));
}

static DMAMUX_RequestGen_TypeDef *Dmamux_Generator(uint32_t gen)
{
    return (DMAMUX_RequestGen_TypeDef *)(DMAMUX1_RequestGenerator0_BASE + (4U * gen));
}

SPL_Status DMAMUX_SetSync(DMA_Channel_TypeDef *dma_channel, uint8_t request_id,
                          const DMAMUX_SyncTypeDef *sync)
{
    DMAMUX_Channel_TypeDef *mux;
    uint32_t ccr;

    SPL_REQUIRE_SAFE(sync != NULL, SPL_ERR_PARAM);
    /* normal: 0 / 1 bayraklari 1 gibi davranir. */
    SPL_REQUIRE((sync->enable <= 1U) && (sync->event_out <= 1U) && (sync->overrun_irq <= 1U),
                SPL_ERR_PARAM);
    /* SAFE: komsu alana tasma (requests 0: 0 - 1 butun alanlari doldurur), baska istek, gecersiz
     * kanal. */
    SPL_REQUIRE_SAFE((request_id <= STM32G0_DMAMUX_REQUEST_MAX) && (sync->polarity <= 3U) &&
                         (sync->sync_id < DMAMUX_SYNC_INPUTS) && (sync->requests != 0U) &&
                         (sync->requests <= 32U),
                     SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(STM32G0_DmaIsChannel(dma_channel) != 0U, SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(DMAMUX_INPUT_ALIVE(sync->sync_id), SPL_ERR_UNSUPPORTED);
    /* ERRATA ES0548 2.5.4: synchronization is enabled with SPOL != 00 in the same write. SAFE. */
    SPL_REQUIRE_SAFE((sync->enable == 0U) || (sync->polarity != 0U), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE((dma_channel->CCR & DMA_CCR_EN) == 0U, SPL_ERR_STATE);
    mux = Dmamux_Channel(STM32G0_DmaGlobalIndex(dma_channel));
    /* NBREQ is writable only with SE = 0 and EGE = 0 (RM0444 11.6.1): drop them first (SPOL = 00
     * while SE = 0, ERRATA ES0548 2.5.4). */
    if ((mux->CCR & (DMAMUX_CxCR_SE | DMAMUX_CxCR_EGE)) != 0U)
    {
        mux->CCR = mux->CCR & DMAMUX_CxCR_DMAREQ_ID_Msk;
    }
    ccr = (((uint32_t)request_id << DMAMUX_CxCR_DMAREQ_ID_Pos) & DMAMUX_CxCR_DMAREQ_ID_Msk) |
          ((uint32_t)sync->sync_id << DMAMUX_CxCR_SYNC_ID_Pos) |
          (((uint32_t)sync->requests - 1U) << DMAMUX_CxCR_NBREQ_Pos) |
          ((sync->event_out != 0U) ? DMAMUX_CxCR_EGE : 0U) |
          ((sync->overrun_irq != 0U) ? DMAMUX_CxCR_SOIE : 0U);
    if (sync->enable != 0U)
    {
        ccr |= DMAMUX_CxCR_SE | ((uint32_t)sync->polarity << DMAMUX_CxCR_SPOL_Pos);
    }
    if ((ccr & (DMAMUX_CxCR_SE | DMAMUX_CxCR_EGE)) != 0U)
    {
        /* NBREQ first with SE = EGE = 0, then the enables in a second write. */
        mux->CCR = ccr & ~(DMAMUX_CxCR_SE | DMAMUX_CxCR_EGE | DMAMUX_CxCR_SPOL_Msk);
    }
    mux->CCR = ccr;
    return SPL_OK;
}

SPL_Status DMAMUX_GetSyncOverrun(uint16_t *mask)
{
    SPL_REQUIRE_SAFE(mask != NULL, SPL_ERR_PARAM);
    *mask = (uint16_t)(DMAMUX1_ChannelStatus->CSR & STM32G0_DMAMUX_VALID_MASK);
    return SPL_OK;
}

SPL_Status DMAMUX_RequestGenConfig(uint8_t gen, const DMAMUX_RequestGenTypeDef *cfg)
{
    DMAMUX_RequestGen_TypeDef *rg;

    SPL_REQUIRE_SAFE(gen < DMAMUX_GENERATORS, SPL_ERR_PARAM); /* SAFE: baska register */
    SPL_REQUIRE_SAFE(cfg != NULL, SPL_ERR_PARAM);
    SPL_REQUIRE(cfg->overrun_irq <= 1U, SPL_ERR_PARAM); /* normal: 1 gibi */
    /* SAFE: komsu alana tasma. */
    SPL_REQUIRE_SAFE((cfg->signal_id < DMAMUX_SYNC_INPUTS) && (cfg->polarity <= 3U) &&
                         (cfg->requests != 0U) && (cfg->requests <= 32U),
                     SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(DMAMUX_INPUT_ALIVE(cfg->signal_id), SPL_ERR_UNSUPPORTED);
    rg = Dmamux_Generator(gen);
    /* GNBREQ is writable only with GE = 0 (RM0444 11.6.4). SAFE: yazim sessizce yok sayilir. */
    SPL_REQUIRE_SAFE((rg->RGCR & DMAMUX_RGxCR_GE) == 0U, SPL_ERR_STATE);
    rg->RGCR = ((uint32_t)cfg->signal_id << DMAMUX_RGxCR_SIG_ID_Pos) |
               ((uint32_t)cfg->polarity << DMAMUX_RGxCR_GPOL_Pos) |
               (((uint32_t)cfg->requests - 1U) << DMAMUX_RGxCR_GNBREQ_Pos) |
               ((cfg->overrun_irq != 0U) ? DMAMUX_RGxCR_OIE : 0U);
    return SPL_OK;
}

SPL_Status DMAMUX_RequestGenEnable(uint8_t gen, uint8_t enable)
{
    DMAMUX_RequestGen_TypeDef *rg;

    SPL_REQUIRE_SAFE(gen < DMAMUX_GENERATORS, SPL_ERR_PARAM);
    SPL_REQUIRE(enable <= 1U, SPL_ERR_PARAM); /* normal */
    rg = Dmamux_Generator(gen);
    rg->RGCR = (rg->RGCR & ~DMAMUX_RGxCR_GE) | ((enable != 0U) ? DMAMUX_RGxCR_GE : 0U);
    return SPL_OK;
}

SPL_Status DMAMUX_GetGenOverrun(uint8_t *mask)
{
    SPL_REQUIRE_SAFE(mask != NULL, SPL_ERR_PARAM);
    *mask = (uint8_t)(DMAMUX1_RequestGenStatus->RGSR & 0xFU);
    return SPL_OK;
}

SPL_Status DMAMUX_ClearGenOverrun(uint8_t mask)
{
    SPL_REQUIRE((mask != 0U) && (mask <= 0xFU), SPL_ERR_PARAM); /* normal */
    DMAMUX1_RequestGenStatus->RGCFR = mask;                     /* write-1 clear */
    /* Read back: the clear reaches the peripheral before return (METHOD 1.7, issue #23). */
    (void)DMAMUX1_RequestGenStatus->RGSR;
    return SPL_OK;
}
