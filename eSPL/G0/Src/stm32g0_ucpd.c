/* UCPD driver for STM32G0 (RM0444 Rev 6 section 38), new in v0.9. Docs/v0.9_UCPD_CONTRACT.md. */
/* Kutuphane birimi: spl_check.h karisma korumasi referansini bu birim de uretir; uretmeyen tek
 * birim stm32g0_check.c (SPL_CHECK_LEVEL_DEFINER). BULGU-02, CHECK_LEVEL.md 3.2. */
#define SPL_LIBRARY_BUILD 1
#include <stddef.h>
#include "stm32g0_ucpd.h"

#if SPL_G0_HAS_UCPD

/* Denetim seviyesi (A4 dalga 4b, Docs/CHECK_LEVEL.md 5.4 UCPD tablosu): SPL_REQUIRE = normal sinif
 * (yalniz FULL), SPL_REQUIRE_SAFE = SAFE sinif (FULL ve MIX). Bekleyen istek (BUSY), TXUND /
 * RXOVR ve WOULD_BLOCK result: her seviyede. Yalniz denetimde kullanilan yardimcilar static
 * inline (saf, Tools/pure_checks.txt). */

/* Command bits of CR: set by software, cleared by hardware. Never copied back by a
 * read-modify-write (a stale 1 would re-send). */
#define UCPD_CR_COMMANDS (UCPD_CR_TXSEND | UCPD_CR_TXHRST | UCPD_CR_FRSTX)
#define UCPD_CR_TYPEC                                                                              \
    (UCPD_CR_ANAMODE | UCPD_CR_ANASUBMODE_Msk | UCPD_CR_CCENABLE_Msk | UCPD_CR_CC1TCDIS |          \
     UCPD_CR_CC2TCDIS | UCPD_CR_RDCH)

/* The SR flags that UCPD_ICR clears sit at the same bit positions (RM0444 38.8.5 / 38.8.6). */
typedef char ucpd_icr_matches_sr[((UCPD_ICR_TXMSGDISCCF == UCPD_SR_TXMSGDISC) &&
                                  (UCPD_ICR_TXUNDCF == UCPD_SR_TXUND) &&
                                  (UCPD_ICR_RXORDDETCF == UCPD_SR_RXORDDET) &&
                                  (UCPD_ICR_RXMSGENDCF == UCPD_SR_RXMSGEND) &&
                                  (UCPD_ICR_FRSEVTCF == UCPD_SR_FRSEVT))
                                     ? 1
                                     : -1];

static inline uint8_t Ucpd_Valid(const UCPD_TypeDef *ucpd)
{
    return ((ucpd == UCPD1) || (ucpd == UCPD2)) ? 1U : 0U;
}

static inline uint8_t Ucpd_Enabled(const UCPD_TypeDef *ucpd)
{
    return ((ucpd->CFG1 & UCPD_CFG1_UCPDEN) != 0U) ? 1U : 0U;
}

static uint32_t Ucpd_Cr(const UCPD_TypeDef *ucpd)
{
    return ucpd->CR & ~UCPD_CR_COMMANDS;
}

SPL_Status UCPD_Config(UCPD_TypeDef *ucpd, const UCPD_ConfigTypeDef *cfg)
{
    uint32_t cfg1;
    uint32_t cfg2 = 0U;

    SPL_REQUIRE_SAFE(Ucpd_Valid(ucpd) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE_SAFE(cfg != NULL, SPL_ERR_PARAM);
    /* SAFE: CFG1 alan tasmasi (DMAEN'e tasar), RM yasak 0. */
    SPL_REQUIRE_SAFE((cfg->prescaler <= 4U) && (cfg->transition_win != 0U) &&
                         (cfg->transition_win <= 31U) && (cfg->interframe_gap != 0U) &&
                         (cfg->interframe_gap <= 31U) && (cfg->half_bit_div <= 63U) &&
                         (cfg->rx_ordered_sets <= 0x1FFU),
                     SPL_ERR_PARAM);
    SPL_REQUIRE((cfg->rx_dma <= 1U) && (cfg->tx_dma <= 1U) && (cfg->rx_filter_off <= 1U) &&
                    (cfg->rx_filter_2 <= 1U) && (cfg->force_clock <= 1U) && (cfg->wakeup <= 1U),
                SPL_ERR_PARAM);
    /* CFGR1 / CFGR2 writes are ignored with UCPDEN = 1 (SAFE). */
    SPL_REQUIRE_SAFE(Ucpd_Enabled(ucpd) == 0U, SPL_ERR_STATE);
    cfg1 = ((uint32_t)cfg->half_bit_div << UCPD_CFG1_HBITCLKDIV_Pos) |
           ((uint32_t)cfg->interframe_gap << UCPD_CFG1_IFRGAP_Pos) |
           ((uint32_t)cfg->transition_win << UCPD_CFG1_TRANSWIN_Pos) |
           ((uint32_t)cfg->prescaler << UCPD_CFG1_PSC_UCPDCLK_Pos) |
           ((uint32_t)cfg->rx_ordered_sets << UCPD_CFG1_RXORDSETEN_Pos);
    if (cfg->tx_dma != 0U)
    {
        cfg1 |= UCPD_CFG1_TXDMAEN;
    }
    if (cfg->rx_dma != 0U)
    {
        cfg1 |= UCPD_CFG1_RXDMAEN;
    }
    if (cfg->rx_filter_off != 0U)
    {
        cfg2 |= UCPD_CFG2_RXFILTDIS;
    }
    if (cfg->rx_filter_2 != 0U)
    {
        cfg2 |= UCPD_CFG2_RXFILT2N3;
    }
    if (cfg->force_clock != 0U)
    {
        cfg2 |= UCPD_CFG2_FORCECLK;
    }
    if (cfg->wakeup != 0U)
    {
        cfg2 |= UCPD_CFG2_WUPEN;
    }
    ucpd->CFG1 = cfg1;
    ucpd->CFG2 = cfg2;
    return SPL_OK;
}

SPL_Status UCPD_SetRxExtensions(UCPD_TypeDef *ucpd, uint32_t ext1, uint32_t ext2)
{
    SPL_REQUIRE_SAFE(Ucpd_Valid(ucpd) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    /* SAFE: ayrilmis bitler. */
    SPL_REQUIRE_SAFE((ext1 <= UCPD_ORDSET_MAX) && (ext2 <= UCPD_ORDSET_MAX), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(Ucpd_Enabled(ucpd) == 0U, SPL_ERR_STATE); /* RM0444 38.8.13 / 38.8.14; SAFE */
    ucpd->RX_ORDEXT1 = ext1;
    ucpd->RX_ORDEXT2 = ext2;
    return SPL_OK;
}

SPL_Status UCPD_Enable(UCPD_TypeDef *ucpd)
{
    SPL_REQUIRE_SAFE(Ucpd_Valid(ucpd) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    ucpd->CFG1 = ucpd->CFG1 | UCPD_CFG1_UCPDEN;
    return SPL_OK;
}

SPL_Status UCPD_Disable(UCPD_TypeDef *ucpd)
{
    SPL_REQUIRE_SAFE(Ucpd_Valid(ucpd) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    ucpd->CFG1 = ucpd->CFG1 & ~UCPD_CFG1_UCPDEN;
    return SPL_OK;
}

SPL_Status UCPD_TypeCConfig(UCPD_TypeDef *ucpd, const UCPD_TypeCTypeDef *tc)
{
    uint32_t cr;

    SPL_REQUIRE_SAFE(Ucpd_Valid(ucpd) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE_SAFE(tc != NULL, SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE((tc->rp_current <= 3U) && (tc->cc_enable <= 3U), SPL_ERR_PARAM); /* SAFE */
    SPL_REQUIRE((tc->sink <= 1U) && (tc->cc1_detector_off <= 1U) && (tc->cc2_detector_off <= 1U) &&
                    (tc->rdch <= 1U),
                SPL_ERR_PARAM);
    /* RM0444 Table 248: ANASUBMODE selects the Rp current in source mode only (SAFE). */
    SPL_REQUIRE_SAFE((tc->sink == 0U) || (tc->rp_current == 0U), SPL_ERR_PARAM);
    /* CR writes are effective only with UCPDEN = 1 (38.8.3); SAFE. */
    SPL_REQUIRE_SAFE(Ucpd_Enabled(ucpd) != 0U, SPL_ERR_STATE);
    cr = Ucpd_Cr(ucpd) & ~UCPD_CR_TYPEC;
    cr |= ((uint32_t)tc->rp_current << UCPD_CR_ANASUBMODE_Pos) |
          ((uint32_t)tc->cc_enable << UCPD_CR_CCENABLE_Pos);
    if (tc->sink != 0U)
    {
        cr |= UCPD_CR_ANAMODE;
    }
    if (tc->cc1_detector_off != 0U)
    {
        cr |= UCPD_CR_CC1TCDIS;
    }
    if (tc->cc2_detector_off != 0U)
    {
        cr |= UCPD_CR_CC2TCDIS;
    }
    if (tc->rdch != 0U)
    {
        cr |= UCPD_CR_RDCH;
    }
    ucpd->CR = cr;
    return SPL_OK;
}

SPL_Status UCPD_Strobe(const UCPD_TypeDef *ucpd)
{
    SPL_REQUIRE_SAFE(Ucpd_Valid(ucpd) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SYSCFG->CFGR1 =
        SYSCFG->CFGR1 | ((ucpd == UCPD1) ? SYSCFG_CFGR1_UCPD1_STROBE : SYSCFG_CFGR1_UCPD2_STROBE);
    return SPL_OK;
}

SPL_Status UCPD_GetCcState(UCPD_TypeDef *ucpd, uint8_t *cc1, uint8_t *cc2)
{
    uint32_t sr;

    SPL_REQUIRE_SAFE(Ucpd_Valid(ucpd) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE_SAFE((cc1 != NULL) && (cc2 != NULL), SPL_ERR_PARAM);
    sr = ucpd->SR;
    *cc1 = (uint8_t)((sr & UCPD_SR_TYPEC_VSTATE_CC1_Msk) >> UCPD_SR_TYPEC_VSTATE_CC1_Pos);
    *cc2 = (uint8_t)((sr & UCPD_SR_TYPEC_VSTATE_CC2_Msk) >> UCPD_SR_TYPEC_VSTATE_CC2_Pos);
    return SPL_OK;
}

SPL_Status UCPD_PhyConfig(UCPD_TypeDef *ucpd, uint8_t cc2, uint8_t rx_enable, uint8_t bist_rx)
{
    uint32_t cr;

    SPL_REQUIRE_SAFE(Ucpd_Valid(ucpd) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE((cc2 <= 1U) && (rx_enable <= 1U) && (bist_rx <= 1U), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(Ucpd_Enabled(ucpd) != 0U, SPL_ERR_STATE); /* SAFE: CR yazimi yok sayilir */
    cr = Ucpd_Cr(ucpd) & ~(UCPD_CR_PHYCCSEL | UCPD_CR_PHYRXEN | UCPD_CR_RXMODE);
    if (cc2 != 0U)
    {
        cr |= UCPD_CR_PHYCCSEL;
    }
    if (rx_enable != 0U)
    {
        cr |= UCPD_CR_PHYRXEN;
    }
    if (bist_rx != 0U)
    {
        cr |= UCPD_CR_RXMODE;
    }
    ucpd->CR = cr;
    return SPL_OK;
}

SPL_Status UCPD_FrsConfig(UCPD_TypeDef *ucpd, uint8_t rx_enable)
{
    uint32_t cr;

    SPL_REQUIRE_SAFE(Ucpd_Valid(ucpd) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE(rx_enable <= 1U, SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(Ucpd_Enabled(ucpd) != 0U, SPL_ERR_STATE); /* SAFE: CR yazimi yok sayilir */
    cr = Ucpd_Cr(ucpd) & ~UCPD_CR_FRSRXEN;
    if (rx_enable != 0U)
    {
        cr |= UCPD_CR_FRSRXEN;
    }
    ucpd->CR = cr;
    return SPL_OK;
}

SPL_Status UCPD_FrsSignal(UCPD_TypeDef *ucpd)
{
    SPL_REQUIRE_SAFE(Ucpd_Valid(ucpd) != 0U, SPL_ERR_PARAM);   /* SAFE: rastgele adres */
    SPL_REQUIRE_SAFE(Ucpd_Enabled(ucpd) != 0U, SPL_ERR_STATE); /* SAFE: CR yazimi yok sayilir */
    if ((ucpd->CR & UCPD_CR_FRSTX) != 0U)
    {
        return SPL_BUSY;
    }
    ucpd->CR = Ucpd_Cr(ucpd) | UCPD_CR_FRSTX;
    return SPL_OK;
}

SPL_Status UCPD_InterruptConfig(UCPD_TypeDef *ucpd, uint32_t mask)
{
    SPL_REQUIRE_SAFE(Ucpd_Valid(ucpd) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    /* SAFE (EDR-003): ayrilmis bite yazim. */
    SPL_REQUIRE_SAFE((mask & ~UCPD_IT_ALL) == 0U, SPL_ERR_PARAM);
    /* IMR writes are effective only with UCPDEN = 1 (38.8.4); SAFE. */
    SPL_REQUIRE_SAFE(Ucpd_Enabled(ucpd) != 0U, SPL_ERR_STATE);
    ucpd->IMR = mask;
    return SPL_OK;
}

/* Shared gate of the transmit requests: enabled, nothing pending. */
static SPL_Status Ucpd_TxIdle(const UCPD_TypeDef *ucpd)
{
    SPL_REQUIRE_SAFE(Ucpd_Enabled(ucpd) != 0U, SPL_ERR_STATE); /* SAFE: CR yazimi yok sayilir */
    if ((ucpd->CR & (UCPD_CR_TXSEND | UCPD_CR_TXHRST)) != 0U)
    {
        return SPL_BUSY; /* TX_ORDSET writes are ignored while a request is pending (38.8.7) */
    }
    return SPL_OK;
}

SPL_Status UCPD_StartMessage(UCPD_TypeDef *ucpd, uint32_t ordered_set, uint32_t payload_size)
{
    SPL_Status st;

    SPL_REQUIRE_SAFE(Ucpd_Valid(ucpd) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    /* SAFE: ayrilmis bitler / hatta yanlis cerceve (5.3). */
    SPL_REQUIRE_SAFE((ordered_set <= UCPD_ORDSET_MAX) && (payload_size <= UCPD_PAYLOAD_MAX),
                     SPL_ERR_PARAM);
    st = Ucpd_TxIdle(ucpd);
    if (st != SPL_OK)
    {
        return st;
    }
    ucpd->TX_ORDSET = ordered_set;
    ucpd->TX_PAYSZ = payload_size;
    ucpd->CR = (Ucpd_Cr(ucpd) & ~UCPD_CR_TXMODE_Msk) | UCPD_CR_TXSEND;
    return SPL_OK;
}

SPL_Status UCPD_TryWriteByte(UCPD_TypeDef *ucpd, uint8_t data)
{
    uint32_t sr;

    SPL_REQUIRE_SAFE(Ucpd_Valid(ucpd) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    /* SAFE: DMA akisina CPU bayti girer. */
    SPL_REQUIRE_SAFE((ucpd->CFG1 & UCPD_CFG1_TXDMAEN) == 0U, SPL_ERR_STATE);
    sr = ucpd->SR;
    if ((sr & UCPD_SR_TXUND) != 0U)
    {
        return SPL_ERR_HW;
    }
    if ((sr & UCPD_SR_TXIS) == 0U)
    {
        return SPL_WOULD_BLOCK;
    }
    ucpd->TXDR = data; /* clears TXIS (RM0444 38.8.5) */
    /* Read back: TXIS is low before return (METHOD 1.7, issue #23): a stale TXIS would make an
     * ISR that returns right away write the next byte early. */
    (void)ucpd->SR;
    return SPL_OK;
}

SPL_Status UCPD_SendHardReset(UCPD_TypeDef *ucpd)
{
    SPL_REQUIRE_SAFE(Ucpd_Valid(ucpd) != 0U, SPL_ERR_PARAM);   /* SAFE: rastgele adres */
    SPL_REQUIRE_SAFE(Ucpd_Enabled(ucpd) != 0U, SPL_ERR_STATE); /* SAFE: CR yazimi yok sayilir */
    /* A pending TXSEND is allowed: the Hard Reset takes precedence (RM0444 38.5.3). */
    if ((ucpd->CR & UCPD_CR_TXHRST) != 0U)
    {
        return SPL_BUSY;
    }
    ucpd->CR = Ucpd_Cr(ucpd) | UCPD_CR_TXHRST;
    return SPL_OK;
}

SPL_Status UCPD_SendSpecial(UCPD_TypeDef *ucpd, uint32_t txmode)
{
    SPL_Status st;

    SPL_REQUIRE_SAFE(Ucpd_Valid(ucpd) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    /* SAFE: hatta yanlis cerceve; TXMODE tasmasi. */
    SPL_REQUIRE_SAFE((txmode != 0U) && (txmode <= 2U), SPL_ERR_PARAM);
    st = Ucpd_TxIdle(ucpd);
    if (st != SPL_OK)
    {
        return st;
    }
    ucpd->CR =
        (Ucpd_Cr(ucpd) & ~UCPD_CR_TXMODE_Msk) | (txmode << UCPD_CR_TXMODE_Pos) | UCPD_CR_TXSEND;
    return SPL_OK;
}

SPL_Status UCPD_TryReadByte(UCPD_TypeDef *ucpd, uint8_t *data)
{
    uint32_t sr;

    SPL_REQUIRE_SAFE(Ucpd_Valid(ucpd) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE_SAFE(data != NULL, SPL_ERR_PARAM);
    /* SAFE: DMA akisindan bayt calinir. */
    SPL_REQUIRE_SAFE((ucpd->CFG1 & UCPD_CFG1_RXDMAEN) == 0U, SPL_ERR_STATE);
    sr = ucpd->SR;
    if ((sr & UCPD_SR_RXOVR) != 0U)
    {
        return SPL_ERR_HW;
    }
    if ((sr & UCPD_SR_RXNE) == 0U)
    {
        return SPL_WOULD_BLOCK;
    }
    *data = (uint8_t)(ucpd->RXDR & 0xFFU);
    return SPL_OK;
}

SPL_Status UCPD_GetRxOrderedSet(UCPD_TypeDef *ucpd, uint8_t *code)
{
    SPL_REQUIRE_SAFE(Ucpd_Valid(ucpd) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE_SAFE(code != NULL, SPL_ERR_PARAM);
    if ((ucpd->SR & UCPD_SR_RXORDDET) == 0U)
    {
        return SPL_WOULD_BLOCK;
    }
    *code = (uint8_t)(ucpd->RX_ORDSET & UCPD_RX_ORDSET_RXORDSET_Msk);
    ucpd->ICR = UCPD_ICR_RXORDDETCF;
    /* Read back: the clear reaches the peripheral before return (METHOD 1.7, issue #23). */
    (void)ucpd->SR;
    return SPL_OK;
}

SPL_Status UCPD_GetRxMessageEnd(UCPD_TypeDef *ucpd, uint32_t *size, uint8_t *bad)
{
    uint32_t sr;

    SPL_REQUIRE_SAFE(Ucpd_Valid(ucpd) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE_SAFE((size != NULL) && (bad != NULL), SPL_ERR_PARAM);
    sr = ucpd->SR;
    if ((sr & UCPD_SR_RXMSGEND) == 0U)
    {
        return SPL_WOULD_BLOCK;
    }
    *size = ucpd->RX_PAYSZ & UCPD_RX_PAYSZ_RXPAYSZ_Msk;
    *bad = ((sr & UCPD_SR_RXERR) != 0U) ? 1U : 0U;
    ucpd->ICR = UCPD_ICR_RXMSGENDCF;
    /* Read back: the clear reaches the peripheral before return (METHOD 1.7, issue #23). */
    (void)ucpd->SR;
    return SPL_OK;
}

SPL_Status UCPD_GetFlags(UCPD_TypeDef *ucpd, uint32_t *flags)
{
    SPL_REQUIRE_SAFE(Ucpd_Valid(ucpd) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE_SAFE(flags != NULL, SPL_ERR_PARAM);
    *flags = ucpd->SR & UCPD_FLAGS_ALL;
    return SPL_OK;
}

SPL_Status UCPD_ClearFlags(UCPD_TypeDef *ucpd, uint32_t mask)
{
    SPL_REQUIRE_SAFE(Ucpd_Valid(ucpd) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    /* normal: etkisiz / ICR ayrilmis bitleri. */
    SPL_REQUIRE(mask != 0U, SPL_ERR_PARAM); /* normal: etkisiz */
    /* SAFE (EDR-003): ayrilmis bite yazim. */
    SPL_REQUIRE_SAFE((mask & ~UCPD_FLAGS_CLEARABLE) == 0U, SPL_ERR_PARAM);
    ucpd->ICR = mask;
    /* Read back: the clear reaches the peripheral before return (METHOD 1.7, issue #23). */
    (void)ucpd->SR;
    return SPL_OK;
}

#else
/* ISO C forbids an empty translation unit. */
typedef int stm32g0_ucpd_absent;
#endif /* SPL_G0_HAS_UCPD */
