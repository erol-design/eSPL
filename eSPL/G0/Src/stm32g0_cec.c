/* SPDX-License-Identifier: MIT */
/* HDMI-CEC driver for STM32G0 (RM0444 Rev 6 section 39), new in v0.9; public contract:
 * stm32g0_cec.h, Docs/v0.9_CEC_CONTRACT.md. */
/* Library unit: this unit also emits the spl_check.h mismatch-guard reference; the only unit that
 * does not is stm32g0_check.c (SPL_CHECK_LEVEL_DEFINER). BULGU-02, CHECK_LEVEL.md 3.2. */
#define SPL_LIBRARY_BUILD 1
#include <stddef.h>
#include "stm32g0_cec.h"

#if SPL_G0_HAS_CEC

/* Check level (A4 wave 4b, Docs/CHECK_LEVEL.md 5.4 CEC table): SPL_REQUIRE = checked at FULL only
 * (not compiled at FAST). TXSOM pending (BUSY), TX / RX errors and WOULD_BLOCK: result, at every
 * level. */

#define CEC_OAR_MAX 0x7FFFU

SPL_Status CEC_Config(const CEC_ConfigTypeDef *cfg)
{
    uint32_t cfgr;

    SPL_REQUIRE(cfg != NULL, SPL_ERR_PARAM);
    /* OAR overflows into LSTN, SFT into RXTOL. */
    SPL_REQUIRE((cfg->own_addresses <= CEC_OAR_MAX) && (cfg->signal_free <= 7U), SPL_ERR_PARAM);
    SPL_REQUIRE((cfg->listen <= 1U) && (cfg->sft_option <= 1U) && (cfg->rx_tolerance <= 1U) &&
                    (cfg->bre_stop <= 1U) && (cfg->bre_error_bit <= 1U) &&
                    (cfg->lbpe_error_bit <= 1U) && (cfg->no_broadcast_error_bit <= 1U),
                SPL_ERR_PARAM);
    /* RM0444 39.7.2: BREGEN generates the error bit only if BRESTP is set. */
    SPL_REQUIRE((cfg->bre_error_bit == 0U) || (cfg->bre_stop != 0U), SPL_ERR_PARAM);
    /* ERRATA ES0548 2.18.1 (ES0418 2.16.1, ES0412 2.17.1, ES0549 2.19.1): normal receiving mode
     * loses messages in a multi-node bus; listen mode is the workaround (errata refusal). */
    SPL_REQUIRE(cfg->listen != 0U, SPL_ERR_UNSUPPORTED);
    /* RM0444 39.7.2 caution: CFGR is written only with CECEN = 0. */
    SPL_REQUIRE((CEC->CR & CEC_CR_CECEN) == 0U, SPL_ERR_STATE);
    cfgr = CEC_CFGR_LSTN | ((uint32_t)cfg->own_addresses << CEC_CFGR_OAR_Pos) |
           ((uint32_t)cfg->signal_free << CEC_CFGR_SFT_Pos);
    if (cfg->sft_option != 0U)
    {
        cfgr |= CEC_CFGR_SFTOPT;
    }
    if (cfg->rx_tolerance != 0U)
    {
        cfgr |= CEC_CFGR_RXTOL;
    }
    if (cfg->bre_stop != 0U)
    {
        cfgr |= CEC_CFGR_BRESTP;
    }
    if (cfg->bre_error_bit != 0U)
    {
        cfgr |= CEC_CFGR_BREGEN;
    }
    if (cfg->lbpe_error_bit != 0U)
    {
        cfgr |= CEC_CFGR_LBPEGEN;
    }
    if (cfg->no_broadcast_error_bit != 0U)
    {
        cfgr |= CEC_CFGR_BRDNOGEN;
    }
    CEC->CFGR = cfgr;
    return SPL_OK;
}

SPL_Status CEC_InterruptConfig(uint32_t mask)
{
    /* (EDR-003): write to a reserved bit. */
    SPL_REQUIRE((mask & ~CEC_FLAGS_ALL) == 0U, SPL_ERR_PARAM);
    /* RM0444 39.7.6 caution: IER is written only with CECEN = 0. */
    SPL_REQUIRE((CEC->CR & CEC_CR_CECEN) == 0U, SPL_ERR_STATE);
    CEC->IER = mask; /* IER bit n enables ISR bit n */
    return SPL_OK;
}

SPL_Status CEC_Enable(void)
{
    /* TXSOM / TXEOM are trigger bits: masked out of the read value (writing 0 has no effect), so
     * a call while a message runs cannot restart it (CCL-2 KSM-34, rmw_check). */
    CEC->CR = (CEC->CR & ~(CEC_CR_TXSOM | CEC_CR_TXEOM)) | CEC_CR_CECEN;
    return SPL_OK;
}

SPL_Status CEC_Disable(void)
{
    CEC->CR = 0U; /* CECEN = 0; TXSOM / TXEOM are cleared by hardware anyway (RM0444 39.7.1) */
    return SPL_OK;
}

SPL_Status CEC_StartMessage(uint8_t header, uint8_t last)
{
    const uint32_t cr = CEC->CR;

    SPL_REQUIRE(last <= 1U, SPL_ERR_PARAM);
    SPL_REQUIRE((cr & CEC_CR_CECEN) != 0U, SPL_ERR_STATE); /* TXSOM ignored */
    if ((cr & CEC_CR_TXSOM) != 0U)
    {
        return SPL_BUSY;
    }
    /* RM0444 39.7.1: a one-byte message needs TXEOM before TXSOM; TXDR before TXSOM. */
    if (last != 0U)
    {
        CEC->CR = cr | CEC_CR_TXEOM;
    }
    CEC->TXDR = header;
    /* RMW-OK: TXSOM = 0 was checked above, so no message runs and hardware does not change
     * TXSOM / TXEOM between this read and write; TXEOM read back is the value set for this
     * message. */
    CEC->CR = CEC->CR | CEC_CR_TXSOM;
    return SPL_OK;
}

SPL_Status CEC_TryWriteByte(uint8_t data, uint8_t last)
{
    uint32_t isr;

    SPL_REQUIRE(last <= 1U, SPL_ERR_PARAM);
    isr = CEC->ISR;
    if ((isr & CEC_TX_ERRORS) != 0U)
    {
        return SPL_ERR_HW;
    }
    SPL_REQUIRE((CEC->CR & CEC_CR_TXSOM) != 0U, SPL_ERR_STATE); /* the byte is lost */
    if ((isr & CEC_ISR_TXBR) == 0U)
    {
        return SPL_WOULD_BLOCK;
    }
    /* RM0444 39.7.1: TXEOM must be set before the last byte is written to TXDR. */
    if (last != 0U)
    {
        /* TXSOM is a trigger bit cleared by hardware: mask it out of the read value. Writing it
         * back as 1 would start a new message if the current one ended between read and write
         * (CCL-2 KSM-34); writing 0 has no effect. */
        CEC->CR = (CEC->CR & ~CEC_CR_TXSOM) | CEC_CR_TXEOM;
    }
    CEC->TXDR = data;
    CEC->ISR = CEC_ISR_TXBR; /* rc_w1 */
    /* Read back: the clear reaches the peripheral before return (METHOD 1.7, issue #23). */
    (void)CEC->ISR;
    return SPL_OK;
}

SPL_Status CEC_TxComplete(uint8_t *done)
{
    uint32_t isr;

    SPL_REQUIRE(done != NULL, SPL_ERR_PARAM);
    isr = CEC->ISR;
    if ((isr & CEC_TX_ERRORS) != 0U)
    {
        return SPL_ERR_HW;
    }
    *done = ((isr & CEC_ISR_TXEND) != 0U) ? 1U : 0U;
    if (*done != 0U)
    {
        CEC->ISR = CEC_ISR_TXEND;
        /* Read back: the clear reaches the peripheral before return (METHOD 1.7, issue #23). */
        (void)CEC->ISR;
    }
    return SPL_OK;
}

SPL_Status CEC_TryReadByte(uint8_t *data, uint8_t *end)
{
    uint32_t isr;

    SPL_REQUIRE((data != NULL) && (end != NULL), SPL_ERR_PARAM);
    isr = CEC->ISR;
    if ((isr & CEC_RX_ERRORS) != 0U)
    {
        return SPL_ERR_HW;
    }
    if ((isr & CEC_ISR_RXBR) == 0U)
    {
        return SPL_WOULD_BLOCK;
    }
    *data = (uint8_t)(CEC->RXDR & CEC_RXDR_RXD_Msk);
    *end = ((isr & CEC_ISR_RXEND) != 0U) ? 1U : 0U;
    CEC->ISR = isr & (CEC_ISR_RXBR | CEC_ISR_RXEND); /* rc_w1: only what was consumed */
    /* Read back: the clear reaches the peripheral before return (METHOD 1.7, issue #23). */
    (void)CEC->ISR;
    return SPL_OK;
}

SPL_Status CEC_GetFlags(uint32_t *flags)
{
    SPL_REQUIRE(flags != NULL, SPL_ERR_PARAM);
    *flags = CEC->ISR & CEC_FLAGS_ALL;
    return SPL_OK;
}

SPL_Status CEC_ClearFlags(uint32_t mask)
{
    SPL_REQUIRE(mask != 0U, SPL_ERR_PARAM); /* no effect */
    /* (EDR-003): write to a reserved bit. */
    SPL_REQUIRE((mask & ~CEC_FLAGS_ALL) == 0U, SPL_ERR_PARAM);
    CEC->ISR = mask;
    /* Read back: the clear reaches the peripheral before return (METHOD 1.7, issue #23). */
    (void)CEC->ISR;
    return SPL_OK;
}

SPL_Status CEC_HeaderForUs(uint8_t header, uint16_t own_addresses, uint8_t *for_us)
{
    uint32_t dest;

    /* bit 15 is read only for the broadcast destination, the answer does not change. */
    SPL_REQUIRE(for_us != NULL, SPL_ERR_PARAM);
    SPL_REQUIRE(own_addresses <= CEC_OAR_MAX, SPL_ERR_PARAM);
    dest = (uint32_t)header & 0xFU;
    *for_us =
        ((dest == CEC_BROADCAST) || ((((uint32_t)own_addresses >> dest) & 1U) != 0U)) ? 1U : 0U;
    return SPL_OK;
}

#else
/* ISO C forbids an empty translation unit. */
typedef int stm32g0_cec_absent;
#endif /* SPL_G0_HAS_CEC */
