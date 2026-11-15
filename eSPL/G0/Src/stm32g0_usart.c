/* Ported from STM32G4 SPL v0.13.3 (Autometry_Mcu_Spl 130de7a); reviewed against RM0444 Rev 6
 * sections 33 (USART) and 34 (LPUART), Tables 183/184, and ES0548 2.12.x / 2.13.1.
 * G0 changes: instance classes FULL, BASIC (no FIFO, no prescaler, no smartcard) per SKU group
 * (v0.8; G0B1: FULL USART1/2/3, BASIC USART4/5/6), LP (LPUART1/2 where present); PRESC is never
 * written on a BASIC instance (the register does not exist there). */
/* Kutuphane birimi: spl_check.h karisma korumasi referansini bu birim de uretir; uretmeyen tek
 * birim stm32g0_check.c (SPL_CHECK_LEVEL_DEFINER). BULGU-02, CHECK_LEVEL.md 3.2. */
#define SPL_LIBRARY_BUILD 1
#include <stddef.h>
#include "stm32g0_usart.h"
#include "stm32g0_sku.h"
#include "stm32g0_sku_inst.h"

/* Denetim seviyesi (A4 dalga 4a, Docs/CHECK_LEVEL.md 5.4 USART tablosu): SPL_REQUIRE = normal sinif
 * (yalniz FULL), SPL_REQUIRE_SAFE = SAFE sinif (FULL ve MIX). Alim hatalari, WOULD_BLOCK, BUSY
 * result: her seviyede. Yardimcilar static inline (saf, Tools/pure_checks.txt). */

/* RM0444 Table 183 per SKU group (v0.8, stm32g0_sku.h): USART1 is FULL everywhere; USART2 is FULL
 * from G07x on, BASIC on G03x/G05x; USART3 is FULL on group 0BX, BASIC on G07x; USART4..6 BASIC. */
static inline uint8_t Usart_IsFull(const USART_TypeDef *instance)
{
    return ((instance == USART1) || ((SPL_G0_USART2_FULL != 0) && (instance == USART2)) ||
            ((SPL_G0_USART3_FULL != 0) && SPL_G0_IS_USART3(instance)))
               ? 1U
               : 0U;
}

static inline uint8_t Usart_IsBasic(const USART_TypeDef *instance)
{
    return (((SPL_G0_USART2_FULL == 0) && (instance == USART2)) ||
            ((SPL_G0_USART3_FULL == 0) && SPL_G0_IS_USART3(instance)) ||
            SPL_G0_IS_USART4(instance) || SPL_G0_IS_USART5(instance) || SPL_G0_IS_USART6(instance))
               ? 1U
               : 0U;
}

/* Legacy x16 init accepts the USARTs (FULL and BASIC), not the LPUARTs. */
static inline uint8_t Usart_Valid(const USART_TypeDef *instance)
{
    return ((Usart_IsFull(instance) != 0U) || (Usart_IsBasic(instance) != 0U)) ? 1U : 0U;
}

/* Any instance sharing the USART register layout, including LPUART1/2. */
static inline uint8_t Usart_AnyValid(const USART_TypeDef *instance)
{
    return ((Usart_Valid(instance) != 0U) || SPL_G0_IS_LPUART1(instance) ||
            SPL_G0_IS_LPUART2(instance))
               ? 1U
               : 0U;
}

/* 1 when the data field is 9 bits wide (M0=1, M1=0, PCE=0, RM0444 33.8.1):
 * the 8-bit Try* services would drop or zero bit 8. */
static inline uint32_t Usart_NineDataBits(uint32_t cr1)
{
    return (((cr1 & (USART_CR1_M0 | USART_CR1_M1 | USART_CR1_PCE)) == USART_CR1_M0) ? 1U : 0U);
}

SPL_Status USART_Init(USART_TypeDef *instance, const USART_InitTypeDef *cfg)
{
    uint32_t divisor;
    SPL_REQUIRE_SAFE(Usart_Valid(instance) != 0U, SPL_ERR_PARAM); /* SAFE (LPUART da reddedilir) */
    SPL_REQUIRE_SAFE(cfg != (const USART_InitTypeDef *)0, SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE((cfg->baudrate != 0U) && (cfg->kernel_hz != 0U),
                     SPL_ERR_PARAM); /* SAFE: sifira bolme */
    /* Round kernel_hz / baudrate without overflow. For OVER8=0,
     * hardware requires USARTDIV >= 16, and BRR is a 16-bit divisor. */
    divisor = (cfg->kernel_hz / cfg->baudrate);
    if ((cfg->kernel_hz % cfg->baudrate) >= ((cfg->baudrate / 2U) + (cfg->baudrate & 1U)))
    {
        ++divisor;
    }
    SPL_REQUIRE_SAFE((divisor >= 16U) && (divisor <= 65535U), SPL_ERR_PARAM); /* SAFE: BRR yasak */
    SPL_REQUIRE_SAFE((instance->CR1 & USART_CR1_UE) == 0U, SPL_ERR_STATE); /* SAFE: BRR yazilamaz */
    /* Explicitly reset the supported control registers. Do not use while
     * peripheral is active or controlled by another module. */
    instance->CR1 = 0U;
    instance->CR2 = 0U;
    instance->CR3 = 0U;
    if (Usart_IsBasic(instance) == 0U)
    {
        instance->PRESC = 0U; /* division by one; BASIC instances have no PRESC register */
    }
    instance->BRR = divisor;
    instance->ICR = USART_ICR_PECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_ORECF;
    instance->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
    return SPL_OK;
}

SPL_Status USART_TryWrite(USART_TypeDef *instance, uint8_t byte)
{
    SPL_REQUIRE_SAFE(Usart_AnyValid(instance) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    /* SAFE: UE / TE kapali, bayt / istek sessizce kaybolur. */
    SPL_REQUIRE_SAFE((instance->CR1 & (USART_CR1_UE | USART_CR1_TE)) ==
                         (USART_CR1_UE | USART_CR1_TE),
                     SPL_ERR_STATE);
    /* 9-bit data frame: use USART_TryWrite9. SAFE: bit 8 sessizce 0. */
    SPL_REQUIRE_SAFE(Usart_NineDataBits(instance->CR1) == 0U, SPL_ERR_STATE);
    if ((instance->ISR & USART_ISR_TXE_TXFNF) == 0U)
    {
        return SPL_WOULD_BLOCK;
    }
    instance->TDR = (uint32_t)byte;
    return SPL_OK;
}

SPL_Status USART_TryRead(USART_TypeDef *instance, uint8_t *byte)
{
    SPL_REQUIRE_SAFE(Usart_AnyValid(instance) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE_SAFE(byte != (uint8_t *)0, SPL_ERR_PARAM);
    /* SAFE (K-A4-4 ilkesi): RE kapali, alim bayragi hic kurulmaz, bekleme dongusu takilir. */
    SPL_REQUIRE_SAFE((instance->CR1 & (USART_CR1_UE | USART_CR1_RE)) ==
                         (USART_CR1_UE | USART_CR1_RE),
                     SPL_ERR_STATE);
    /* 9-bit data frame: use USART_TryRead9. SAFE: bit 8 sessizce atilir. */
    SPL_REQUIRE_SAFE(Usart_NineDataBits(instance->CR1) == 0U, SPL_ERR_STATE);
    if ((instance->ISR & (USART_ISR_PE | USART_ISR_FE | USART_ISR_NE | USART_ISR_ORE)) != 0U)
    {
        return SPL_ERR_HW;
    }
    if ((instance->ISR & USART_ISR_RXNE_RXFNE) == 0U)
    {
        return SPL_WOULD_BLOCK;
    }
    *byte = (uint8_t)instance->RDR;
    return SPL_OK;
}

SPL_Status USART_ClearErrors(USART_TypeDef *instance)
{
    SPL_REQUIRE_SAFE(Usart_AnyValid(instance) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    instance->ICR = USART_ICR_PECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_ORECF;
    /* Read back: the clear reaches the peripheral before return (METHOD 1.7, issue #23). */
    (void)instance->ISR;
    return SPL_OK;
}

SPL_Status USART_TxComplete(USART_TypeDef *instance, uint8_t *complete)
{
    SPL_REQUIRE_SAFE(Usart_AnyValid(instance) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE_SAFE(complete != (uint8_t *)0, SPL_ERR_PARAM);
    *complete = (uint8_t)(((instance->ISR & USART_ISR_TC) != 0U) ? 1U : 0U);
    return SPL_OK;
}

SPL_Status USART_Ready(USART_TypeDef *instance, uint8_t *tx_ready, uint8_t *rx_ready)
{
    uint32_t isr;

    SPL_REQUIRE_SAFE(Usart_AnyValid(instance) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE_SAFE((tx_ready != (uint8_t *)0) && (rx_ready != (uint8_t *)0), SPL_ERR_PARAM);
    /* SAFE (K-A4-4 ilkesi): UE = 0 iken hazir bayraklari hic kurulmaz. */
    SPL_REQUIRE_SAFE((instance->CR1 & USART_CR1_UE) != 0U, SPL_ERR_STATE);

    isr = instance->ISR;
    *tx_ready =
        (uint8_t)((((instance->CR1 & USART_CR1_TE) != 0U) && ((isr & USART_ISR_TEACK) != 0U)) ? 1U
                                                                                              : 0U);
    *rx_ready =
        (uint8_t)((((instance->CR1 & USART_CR1_RE) != 0U) && ((isr & USART_ISR_REACK) != 0U)) ? 1U
                                                                                              : 0U);
    return SPL_OK;
}

SPL_Status USART_ErrorFlags(USART_TypeDef *instance, uint32_t *errors)
{
    SPL_REQUIRE_SAFE(Usart_AnyValid(instance) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE_SAFE(errors != (uint32_t *)0, SPL_ERR_PARAM);

    *errors = instance->ISR & (USART_ISR_PE | USART_ISR_FE | USART_ISR_NE | USART_ISR_ORE);
    return SPL_OK;
}

/* ---- extended services (from G4 v0.11) ---- */

/* USART_ERRORS_ALL (header, no CMSIS dependency) must match ISR PE..ORE, and
 * each ICR clear bit sits at the same position as its ISR flag (RM0444
 * 33.8.9 / 33.8.11). */
typedef char Usart_ErrorsAllCheck
    [((USART_ERRORS_ALL == (USART_ISR_PE | USART_ISR_FE | USART_ISR_NE | USART_ISR_ORE)) &&
      ((USART_ICR_PECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_ORECF) == USART_ERRORS_ALL))
         ? 1
         : -1];

#define USART_FIFO_THRESHOLD_MAX USART_FIFO_THRESHOLD_EMPTY_FULL
#define USART_DE_TIME_MAX 31U

static inline uint8_t Usart_IsLpuart(const USART_TypeDef *instance)
{
    (void)instance; /* v0.8: unused where the SKU has no LPUART */
    return (SPL_G0_IS_LPUART1(instance) || SPL_G0_IS_LPUART2(instance)) ? 1U : 0U;
}

/* 0.5 / 1.5 stop bits belong to the smartcard feature set (RM0444 33.5.5, Table 184):
 * FULL instances only. */
static inline uint8_t Usart_HasSmartcard(const USART_TypeDef *instance)
{
    return Usart_IsFull(instance);
}

/* Clearable mode flags per instance class (RM0444 Table 184; ICR 33.8.11 / LPUART 34.7.10):
 * FULL all of USART_MODE_FLAGS_CLEARABLE, LPUART CMF + WUF, BASIC CMF only. */
static inline uint32_t Usart_ModeFlagsOf(const USART_TypeDef *instance)
{
    uint32_t mask = USART_ISR_CMF; /* BASIC: yalniz CMF */

    if (Usart_IsFull(instance) != 0U)
    {
        mask = USART_MODE_FLAGS_CLEARABLE;
    }
    else if (Usart_IsLpuart(instance) != 0U)
    {
        mask = USART_ISR_CMF | USART_ISR_WUF;
    }
    else
    {
        /* BASIC: ilk deger (MISRA 15.7: else zorunlu, bos degil) */
    }
    return mask;
}

/* 2026-11-13 (UFAST'tan, genisletildi): 64 bitlik bolme yerine 32 bit; kernel_hz ve baudrate'in
 * HER uint32 degerinde eski (num + den / 2) / den 64 bit sonucuyla ayni (esdegerlik denemesi:
 * CHANGELOG 2026-11-13). round(f / den), f + den / 2 tasmadan: kalan r >= den - den / 2 ise +1. */
static uint32_t Usart_DivRound(uint32_t f, uint32_t den)
{
    const uint32_t q = f / den;
    const uint32_t r = f - (q * den);

    return q + ((r >= (den - (den / 2U))) ? 1U : 0U);
}

/* round(f * 256 / den): ikili uzun bolme, 8 adim. r < den <= f / 3 < 2^31 -> r * 2 tasmaz.
 * Sonuc (f * 256 + den / 2) / den ile ayni. */
static uint32_t Usart_DivRound256(uint32_t f, uint32_t den)
{
    uint32_t q = f / den;
    uint32_t r = f - (q * den);
    uint32_t i;

    for (i = 0U; i < 8U; ++i)
    {
        r <<= 1U;
        q <<= 1U;
        if (r >= den)
        {
            r -= den;
            q |= 1U;
        }
    }
    return q + ((r >= (den - (den / 2U))) ? 1U : 0U);
}

/* BRR for the given configuration; 0 if the baud rate cannot be reached. SPL_NOINLINE: bolme / 64
 * bit yardimcilari yalniz bu yapilandirma fonksiyonunda kalsin (Tools/m0plus_audit.sh); FAST'ta
 * denetimler kalkinca derleyici bunu USART_InitEx icine gomuyordu (A4 dalga 4a). */
SPL_NOINLINE static uint32_t Usart_ComputeBrr(uint8_t lpuart, const USART_InitExTypeDef *cfg)
{
    /* PRESC.PRESCALER code -> division factor (RM0444 33.8.14 / 34.7.12). */
    static const uint16_t usart_presc_div[12] = {1U,  2U,  4U,  6U,  8U,   10U,
                                                 12U, 16U, 32U, 64U, 128U, 256U};
    const uint32_t presc = usart_presc_div[cfg->prescaler];
    const uint32_t f = cfg->kernel_hz;
    uint32_t den;
    uint32_t div;

    /* baudrate * presc 32 biti asarsa den > f: her yolda sonuc 0 olurdu (LPUART f < 3 * den,
     * USART yuvarlanmis bolum <= 1 < 16). */
    if (cfg->baudrate > (0xFFFFFFFFUL / presc))
    {
        return 0U;
    }
    den = cfg->baudrate * presc;
    if (lpuart != 0U)
    {
        /* RM0444 34.4.7 / 34.7.5: baud = 256 * f / LPUARTDIV, BRR >= 0x300,
         * BRR is 20 bits, f in [3 x baud, 4096 x baud]. f < 3 * den <=> f / 3 < den;
         * den >= 2^20 ise 4096 * den > f (tasma yok). */
        if (((f / 3U) < den) || ((den < 0x100000UL) && (f > (den << 12U))))
        {
            return 0U;
        }
        div = Usart_DivRound256(f, den);
        if ((div < 0x300U) || (div > 0xFFFFFU))
        {
            return 0U;
        }
        /* ERRATA ES0548 2.13.1: TX bit-length jitter when the
         * kernel/baud ratio (BRR / 256) is non-integer and in the 3..4 range. */
        if ((div < 0x400U) && ((div & 0xFFU) != 0U))
        {
            return 0U;
        }
        return div;
    }
    if (cfg->oversampling == USART_OVERSAMPLING_8)
    {
        /* RM0444 33.5.7: USARTDIV = 2 * f / baud; BRR[15:4] = USARTDIV[15:4],
         * BRR[3] = 0, BRR[2:0] = USARTDIV[3:0] >> 1. USARTDIV[0] is lost, so
         * the reachable divisors are even: USARTDIV = 2 * round(f / baud). */
        div = Usart_DivRound(f, den);
        if (div > 0x7FFFU)
        {
            return 0U; /* 2 * div > 0xFFFF; once denetlenir, 2 * div 32 bitte tasmasin */
        }
        div = 2U * div;
        if ((div < 16U) || (div > 0xFFFFU))
        {
            return 0U;
        }
        return (div & 0xFFF0U) | ((div & 0x000FU) >> 1U);
    }
    /* RM0444 33.5.7: OVER8 = 0, BRR = USARTDIV = f / baud, USARTDIV >= 16. */
    div = Usart_DivRound(f, den);
    if ((div < 16U) || (div > 0xFFFFU))
    {
        return 0U;
    }
    return div;
}

SPL_Status USART_InitEx(USART_TypeDef *instance, const USART_InitExTypeDef *cfg)
{
    uint8_t lpuart;
    uint32_t brr;
    uint32_t cr1;
    uint32_t cr2;
    uint32_t cr3 = 0U;

    SPL_REQUIRE_SAFE(Usart_AnyValid(instance) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE_SAFE(cfg != (const USART_InitExTypeDef *)0, SPL_ERR_PARAM);
    /* SAFE: sifira bolme; PRESC tablosu disi okuma; STOP / M tasmasi. */
    SPL_REQUIRE_SAFE(
        (cfg->kernel_hz != 0U) && (cfg->baudrate != 0U) &&
            (cfg->word_length >= USART_WORDLENGTH_7B) &&
            (cfg->word_length <= USART_WORDLENGTH_9B) && (cfg->parity <= USART_PARITY_ODD) &&
            (cfg->stop_bits <= USART_STOPBITS_1_5) && (cfg->oversampling <= USART_OVERSAMPLING_8) &&
            (cfg->prescaler <= USART_PRESC_DIV256),
        SPL_ERR_PARAM);
    /* normal: TE / RE yok (gorunur), fazla bitler kullanilmaz. */
    SPL_REQUIRE(
        (cfg->direction != 0U) && ((cfg->direction & ~(USART_DIR_TX | USART_DIR_RX)) == 0U) &&
            (cfg->flow_control <= USART_FLOW_RTS_CTS) &&
            ((cfg->options & ~(USART_OPT_MSBFIRST | USART_OPT_SWAP | USART_OPT_TXINV |
                               USART_OPT_RXINV | USART_OPT_DATAINV | USART_OPT_HALF_DUPLEX)) == 0U),
        SPL_ERR_PARAM);
    lpuart = Usart_IsLpuart(instance);
    /* LPUART_CR1 bit 15 (OVER8) and STOP 01/11 are reserved (RM0444 34.7.1,
     * 34.7.3). 0.5/1.5 stop bits need the smartcard-capable USARTs. */
    SPL_REQUIRE_SAFE(
        ((cfg->stop_bits != USART_STOPBITS_0_5) && (cfg->stop_bits != USART_STOPBITS_1_5)) ||
            (Usart_HasSmartcard(instance) != 0U),
        SPL_ERR_UNSUPPORTED); /* SAFE: ayrilmis STOP kodu */
    SPL_REQUIRE_SAFE((lpuart == 0U) || (cfg->oversampling != USART_OVERSAMPLING_8),
                     SPL_ERR_UNSUPPORTED); /* SAFE: LPUART OVER8 ayrilmis */
    /* ERRATA ES0548 2.12.3 + RM0444 Table 184: no prescaler on BASIC instances. SAFE. */
    SPL_REQUIRE_SAFE((Usart_IsBasic(instance) == 0U) || (cfg->prescaler == USART_PRESC_DIV1),
                     SPL_ERR_UNSUPPORTED);
    brr = Usart_ComputeBrr(lpuart, cfg);
    /* SAFE: baud ulasilamaz / errata ES0548 2.13.1: BRR yasak deger. */
    SPL_REQUIRE_SAFE(brr != 0U, SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE((instance->CR1 & USART_CR1_UE) == 0U, SPL_ERR_STATE); /* SAFE */

    /* CR1: M[1:0] = 10 (7 bits), 00 (8 bits), 01 (9 bits) (RM0444 33.8.1). */
    cr1 = 0U;
    if (cfg->word_length == USART_WORDLENGTH_7B)
    {
        cr1 |= USART_CR1_M1;
    }
    else if (cfg->word_length == USART_WORDLENGTH_9B)
    {
        cr1 |= USART_CR1_M0;
    }
    else
    {
        /* 8 bits: M = 00. */
    }
    if (cfg->parity != USART_PARITY_NONE)
    {
        cr1 |= USART_CR1_PCE;
        if (cfg->parity == USART_PARITY_ODD)
        {
            cr1 |= USART_CR1_PS;
        }
    }
    if (cfg->oversampling == USART_OVERSAMPLING_8)
    {
        cr1 |= USART_CR1_OVER8;
    }
    if ((cfg->direction & USART_DIR_TX) != 0U)
    {
        cr1 |= USART_CR1_TE;
    }
    if ((cfg->direction & USART_DIR_RX) != 0U)
    {
        cr1 |= USART_CR1_RE;
    }

    /* CR2: STOP[1:0] encoding equals USART_STOPBITS_* (RM0444 33.8.3). */
    cr2 = ((uint32_t)cfg->stop_bits << USART_CR2_STOP_Pos) & USART_CR2_STOP;
    if ((cfg->options & USART_OPT_MSBFIRST) != 0U)
    {
        cr2 |= USART_CR2_MSBFIRST;
    }
    if ((cfg->options & USART_OPT_SWAP) != 0U)
    {
        cr2 |= USART_CR2_SWAP;
    }
    if ((cfg->options & USART_OPT_TXINV) != 0U)
    {
        cr2 |= USART_CR2_TXINV;
    }
    if ((cfg->options & USART_OPT_RXINV) != 0U)
    {
        cr2 |= USART_CR2_RXINV;
    }
    if ((cfg->options & USART_OPT_DATAINV) != 0U)
    {
        cr2 |= USART_CR2_DATAINV;
    }

    /* CR3: hardware flow control and half duplex (RM0444 33.8.4). HDSEL
     * requires LINEN/CLKEN/SCEN/IREN = 0, which this full write guarantees. */
    if ((cfg->flow_control & USART_FLOW_RTS) != 0U)
    {
        cr3 |= USART_CR3_RTSE;
    }
    if ((cfg->flow_control & USART_FLOW_CTS) != 0U)
    {
        cr3 |= USART_CR3_CTSE;
    }
    if ((cfg->options & USART_OPT_HALF_DUPLEX) != 0U)
    {
        cr3 |= USART_CR3_HDSEL;
    }

    instance->CR1 = cr1; /* UE stays 0 */
    instance->CR2 = cr2;
    instance->CR3 = cr3;
    if (Usart_IsBasic(instance) == 0U)
    {
        instance->PRESC = (uint32_t)cfg->prescaler;
    }
    instance->BRR = brr;
    return SPL_OK;
}

SPL_Status USART_Enable(USART_TypeDef *instance)
{
    uint32_t cr1;
    uint32_t brr_min;

    SPL_REQUIRE_SAFE(Usart_AnyValid(instance) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    cr1 = instance->CR1;
    if ((cr1 & USART_CR1_UE) != 0U)
    {
        return SPL_OK;
    }
    /* Forbidden BRR values (reset BRR = 0): LPUART_BRR >= 0x300 (RM0444
     * 34.7.5); USART USARTDIV >= 16 (33.5.7), i.e. BRR >= 0x10 for OVER8=0
     * and OVER8=1 (BRR[15:4] = USARTDIV[15:4]). */
    brr_min = (Usart_IsLpuart(instance) != 0U) ? 0x300U : 0x10U;
    SPL_REQUIRE_SAFE((instance->BRR & 0xFFFFFU) >= brr_min, SPL_ERR_STATE); /* SAFE: BRR yasak */
    instance->CR1 = cr1 | USART_CR1_UE; /* CR1 has no W1C/command bits */
    return SPL_OK;
}

SPL_Status USART_Disable(USART_TypeDef *instance)
{
    SPL_REQUIRE_SAFE(Usart_AnyValid(instance) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    /* v0.4 (G4 v0.13.4 #9, UART-03): RM0444 33.5.5 step 8: wait TC = 1 before UE = 0, else the
     * last frame is cut. */
    if (((instance->CR1 & (USART_CR1_UE | USART_CR1_TE)) == (USART_CR1_UE | USART_CR1_TE)) &&
        ((instance->ISR & USART_ISR_TC) == 0U))
    {
        return SPL_BUSY;
    }
    instance->CR1 &= ~USART_CR1_UE;
    return SPL_OK;
}

SPL_Status USART_Abort(USART_TypeDef *instance)
{
    SPL_REQUIRE_SAFE(Usart_AnyValid(instance) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    instance->CR1 &= ~USART_CR1_UE; /* frames in progress are discarded */
    return SPL_OK;
}

SPL_Status USART_FifoConfig(USART_TypeDef *instance, uint8_t enable, uint8_t tx_threshold,
                            uint8_t rx_threshold)
{
    uint32_t cr1;
    uint32_t cr3;

    SPL_REQUIRE_SAFE(Usart_AnyValid(instance) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE(enable <= 1U, SPL_ERR_PARAM);                        /* normal */
    SPL_REQUIRE_SAFE((tx_threshold <= USART_FIFO_THRESHOLD_MAX) &&
                         (rx_threshold <= USART_FIFO_THRESHOLD_MAX),
                     SPL_ERR_PARAM); /* SAFE: maskeyle kesilir */
    /* RM0444 Table 184: BASIC instances have no TX/RX FIFO (FIFOEN, TXFTCFG, RXFTCFG reserved).
     * SAFE. */
    SPL_REQUIRE_SAFE(Usart_IsBasic(instance) == 0U, SPL_ERR_UNSUPPORTED);
    cr1 = instance->CR1;
    SPL_REQUIRE_SAFE((cr1 & USART_CR1_UE) == 0U, SPL_ERR_STATE); /* SAFE */
    cr1 &= ~USART_CR1_FIFOEN;
    if (enable != 0U)
    {
        cr1 |= USART_CR1_FIFOEN;
    }
    cr3 = instance->CR3 & ~(USART_CR3_TXFTCFG | USART_CR3_RXFTCFG);
    cr3 |= ((uint32_t)tx_threshold << USART_CR3_TXFTCFG_Pos) & USART_CR3_TXFTCFG;
    cr3 |= ((uint32_t)rx_threshold << USART_CR3_RXFTCFG_Pos) & USART_CR3_RXFTCFG;
    instance->CR3 = cr3;
    instance->CR1 = cr1;
    return SPL_OK;
}

SPL_Status USART_DriverEnableConfig(USART_TypeDef *instance, uint8_t enable, uint8_t active_low,
                                    uint8_t assert_time, uint8_t deassert_time)
{
    uint32_t cr1;
    uint32_t cr3;

    SPL_REQUIRE_SAFE(Usart_AnyValid(instance) != 0U, SPL_ERR_PARAM);  /* SAFE: rastgele adres */
    SPL_REQUIRE((enable <= 1U) && (active_low <= 1U), SPL_ERR_PARAM); /* normal */
    SPL_REQUIRE_SAFE((assert_time <= USART_DE_TIME_MAX) && (deassert_time <= USART_DE_TIME_MAX),
                     SPL_ERR_PARAM); /* SAFE: maskeyle kesilir */
    cr1 = instance->CR1;
    cr3 = instance->CR3;
    /* DE is output on the RTS pin (RM0444 33.8.4 DEM): RTS flow control and
     * DE cannot be used together. */
    /* SAFE: UE = 1 yazim yok sayilir; DE ve RTS ayni pin. */
    SPL_REQUIRE_SAFE(((cr1 & USART_CR1_UE) == 0U) &&
                         ((enable == 0U) || ((cr3 & USART_CR3_RTSE) == 0U)),
                     SPL_ERR_STATE);
    cr1 &= ~(USART_CR1_DEAT | USART_CR1_DEDT);
    cr1 |= ((uint32_t)assert_time << USART_CR1_DEAT_Pos) & USART_CR1_DEAT;
    cr1 |= ((uint32_t)deassert_time << USART_CR1_DEDT_Pos) & USART_CR1_DEDT;
    cr3 &= ~(USART_CR3_DEM | USART_CR3_DEP);
    if (enable != 0U)
    {
        cr3 |= USART_CR3_DEM;
    }
    if (active_low != 0U)
    {
        cr3 |= USART_CR3_DEP;
    }
    instance->CR1 = cr1;
    instance->CR3 = cr3;
    return SPL_OK;
}

/* Largest value of the data field for the current CR1 M/PCE setting. */
static inline uint32_t Usart_DataMask(uint32_t cr1)
{
    uint32_t mask = 0xFFU;

    if ((cr1 & USART_CR1_M0) != 0U)
    {
        mask = 0x1FFU;
    }
    else if ((cr1 & USART_CR1_M1) != 0U)
    {
        mask = 0x7FU;
    }
    else
    {
        /* 8 bits. */
    }
    if ((cr1 & USART_CR1_PCE) != 0U)
    {
        mask >>= 1U; /* parity replaces the MSB (RM0444 33.8.1 PCE) */
    }
    return mask;
}

SPL_Status USART_TryWrite9(USART_TypeDef *instance, uint16_t data)
{
    uint32_t cr1;

    SPL_REQUIRE_SAFE(Usart_AnyValid(instance) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    cr1 = instance->CR1;
    SPL_REQUIRE_SAFE((cr1 & (USART_CR1_UE | USART_CR1_TE)) == (USART_CR1_UE | USART_CR1_TE),
                     SPL_ERR_STATE); /* SAFE */
    SPL_REQUIRE_SAFE(((uint32_t)data & ~Usart_DataMask(cr1)) == 0U,
                     SPL_ERR_PARAM); /* SAFE: kesilir */
    if ((instance->ISR & USART_ISR_TXE_TXFNF) == 0U)
    {
        return SPL_WOULD_BLOCK;
    }
    instance->TDR = (uint32_t)data;
    return SPL_OK;
}

SPL_Status USART_TryRead9(USART_TypeDef *instance, uint16_t *data)
{
    uint32_t isr;

    SPL_REQUIRE_SAFE(Usart_AnyValid(instance) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE_SAFE(data != (uint16_t *)0, SPL_ERR_PARAM);
    /* SAFE (K-A4-4 ilkesi): RE kapali, alim bayragi hic kurulmaz, bekleme dongusu takilir. */
    SPL_REQUIRE_SAFE((instance->CR1 & (USART_CR1_UE | USART_CR1_RE)) ==
                         (USART_CR1_UE | USART_CR1_RE),
                     SPL_ERR_STATE);
    isr = instance->ISR;
    if ((isr & USART_ERRORS_ALL) != 0U)
    {
        return SPL_ERR_HW;
    }
    if ((isr & USART_ISR_RXNE_RXFNE) == 0U)
    {
        return SPL_WOULD_BLOCK;
    }
    *data = (uint16_t)(instance->RDR & USART_RDR_RDR);
    return SPL_OK;
}

SPL_Status USART_ITConfig(USART_TypeDef *instance, uint32_t it_mask, uint8_t enable)
{
    uint32_t cr1_bits = 0U;
    uint32_t cr3_bits = 0U;

    SPL_REQUIRE_SAFE(Usart_AnyValid(instance) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE((it_mask != 0U) && ((it_mask & ~USART_IT_ALL) == 0U) && (enable <= 1U),
                SPL_ERR_PARAM); /* normal */
    if ((it_mask & USART_IT_RXNE) != 0U)
    {
        cr1_bits |= USART_CR1_RXNEIE_RXFNEIE;
    }
    if ((it_mask & USART_IT_TXE) != 0U)
    {
        cr1_bits |= USART_CR1_TXEIE_TXFNFIE;
    }
    if ((it_mask & USART_IT_TC) != 0U)
    {
        cr1_bits |= USART_CR1_TCIE;
    }
    if ((it_mask & USART_IT_IDLE) != 0U)
    {
        cr1_bits |= USART_CR1_IDLEIE;
    }
    if ((it_mask & USART_IT_PE) != 0U)
    {
        cr1_bits |= USART_CR1_PEIE;
    }
    if ((it_mask & USART_IT_ERR) != 0U)
    {
        cr3_bits |= USART_CR3_EIE;
    }
    if (enable != 0U)
    {
        if (cr1_bits != 0U)
        {
            instance->CR1 |= cr1_bits;
        }
        if (cr3_bits != 0U)
        {
            instance->CR3 |= cr3_bits;
        }
    }
    else
    {
        if (cr1_bits != 0U)
        {
            instance->CR1 &= ~cr1_bits;
        }
        if (cr3_bits != 0U)
        {
            instance->CR3 &= ~cr3_bits;
        }
    }
    return SPL_OK;
}

SPL_Status USART_DmaConfig(USART_TypeDef *instance, uint32_t dma_flags)
{
    uint32_t cr3;
    uint32_t want = 0U;

    SPL_REQUIRE_SAFE(Usart_AnyValid(instance) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE((dma_flags & ~(USART_DMA_TX | USART_DMA_RX | USART_DMA_DDRE)) == 0U,
                SPL_ERR_PARAM); /* normal */
    if ((dma_flags & USART_DMA_TX) != 0U)
    {
        want |= USART_CR3_DMAT;
    }
    if ((dma_flags & USART_DMA_RX) != 0U)
    {
        want |= USART_CR3_DMAR;
    }
    if ((dma_flags & USART_DMA_DDRE) != 0U)
    {
        want |= USART_CR3_DDRE;
    }
    cr3 = instance->CR3;
    /* DDRE can only be written with UE=0 (RM0444 33.8.4). */
    SPL_REQUIRE_SAFE(((instance->CR1 & USART_CR1_UE) == 0U) ||
                         ((cr3 & USART_CR3_DDRE) == (want & USART_CR3_DDRE)),
                     SPL_ERR_STATE); /* SAFE: DDRE yazimi yok sayilir */
    cr3 &= ~(USART_CR3_DMAT | USART_CR3_DMAR | USART_CR3_DDRE);
    instance->CR3 = cr3 | want;
    return SPL_OK;
}

SPL_Status USART_IdleDetected(USART_TypeDef *instance, uint8_t *idle)
{
    SPL_REQUIRE_SAFE(Usart_AnyValid(instance) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE_SAFE(idle != (uint8_t *)0, SPL_ERR_PARAM);
    *idle = (uint8_t)(((instance->ISR & USART_ISR_IDLE) != 0U) ? 1U : 0U);
    return SPL_OK;
}

SPL_Status USART_ClearIdle(USART_TypeDef *instance)
{
    SPL_REQUIRE_SAFE(Usart_AnyValid(instance) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    instance->ICR = USART_ICR_IDLECF;
    /* Read back: the clear reaches the peripheral before return (METHOD 1.7, issue #23). */
    (void)instance->ISR;
    return SPL_OK;
}

SPL_Status USART_ClearTxComplete(USART_TypeDef *instance)
{
    SPL_REQUIRE_SAFE(Usart_AnyValid(instance) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    instance->ICR = USART_ICR_TCCF;
    /* Read back: the clear reaches the peripheral before return (METHOD 1.7, issue #23). */
    (void)instance->ISR;
    return SPL_OK;
}

SPL_Status USART_ClearErrorFlags(USART_TypeDef *instance, uint32_t errors)
{
    SPL_REQUIRE_SAFE(Usart_AnyValid(instance) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE(errors != 0U, SPL_ERR_PARAM);                        /* normal */
    /* SAFE: ICR'de baska bayraklar (TC, IDLE ...) sessizce silinir. */
    SPL_REQUIRE_SAFE((errors & ~USART_ERRORS_ALL) == 0U, SPL_ERR_PARAM);
    instance->ICR = errors; /* ICR PECF..ORECF share the ISR bit positions */
    /* Read back: the clear reaches the peripheral before return (METHOD 1.7, issue #23). */
    (void)instance->ISR;
    return SPL_OK;
}

/* ---- v0.10: communication modes (Docs/v0.10_COMM_MODES.md, RM0444 33.5.9 .. 33.5.21) ---- */

/* The clearable mode flags have their ICR clear bit at the same position (RM0444 33.8.9). */
typedef char usart_mode_icr_matches_isr
    [((USART_ICR_LBDCF == USART_ISR_LBDF) && (USART_ICR_CMCF == USART_ISR_CMF) &&
      (USART_ICR_WUCF == USART_ISR_WUF) && (USART_ICR_RTOCF == USART_ISR_RTOF) &&
      (USART_ICR_EOBCF == USART_ISR_EOBF) && (USART_ICR_UDRCF == USART_ISR_UDR))
         ? 1
         : -1];

#define USART_STOP_ONE 0U
#define USART_CR2_MODE_CONFLICT_SYNC USART_CR2_LINEN
#define USART_CR3_MODE_CONFLICT_SYNC (USART_CR3_SCEN | USART_CR3_HDSEL | USART_CR3_IREN)

/* Mode settings change only with UE = 0 (CR2 / CR3 / GTPR fields, RM0444 33.8.2 .. 33.8.6). */
static inline uint8_t Usart_Enabled(const USART_TypeDef *instance)
{
    return ((instance->CR1 & USART_CR1_UE) != 0U) ? 1U : 0U;
}

static inline uint32_t Usart_Stop(const USART_TypeDef *instance)
{
    return (instance->CR2 & USART_CR2_STOP_Msk) >> USART_CR2_STOP_Pos;
}

SPL_Status USART_SyncConfig(USART_TypeDef *instance, const USART_SyncTypeDef *cfg)
{
    uint32_t cr2;

    SPL_REQUIRE_SAFE(Usart_AnyValid(instance) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE_SAFE(cfg != NULL, SPL_ERR_PARAM);
    SPL_REQUIRE((cfg->clock_out <= 1U) && (cfg->cpol <= 1U) && (cfg->cpha <= 1U) &&
                    (cfg->last_bit_clock <= 1U) && (cfg->slave <= 1U) && (cfg->ignore_nss <= 1U),
                SPL_ERR_PARAM); /* normal */
    /* RM0444 Table 184: no synchronous mode on LPUART. SAFE. */
    SPL_REQUIRE_SAFE(Usart_IsLpuart(instance) == 0U, SPL_ERR_UNSUPPORTED);
    /* master (CK output) and slave (CK input) are exclusive. SAFE. */
    SPL_REQUIRE_SAFE((cfg->clock_out == 0U) || (cfg->slave == 0U), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(Usart_Enabled(instance) == 0U,
                     SPL_ERR_STATE); /* SAFE: UE = 1 yazim yok sayilir */
    /* RM0444 33.5.14: LINEN, SCEN, HDSEL and IREN must be kept cleared. SAFE. */
    SPL_REQUIRE_SAFE(((cfg->clock_out | cfg->slave) == 0U) ||
                         (((instance->CR2 & USART_CR2_MODE_CONFLICT_SYNC) == 0U) &&
                          ((instance->CR3 & USART_CR3_MODE_CONFLICT_SYNC) == 0U)),
                     SPL_ERR_STATE);
    cr2 = instance->CR2 & ~(USART_CR2_CLKEN | USART_CR2_CPOL | USART_CR2_CPHA | USART_CR2_LBCL |
                            USART_CR2_SLVEN | USART_CR2_DIS_NSS);
    cr2 |= ((cfg->clock_out != 0U) ? USART_CR2_CLKEN : 0U) |
           ((cfg->cpol != 0U) ? USART_CR2_CPOL : 0U) | ((cfg->cpha != 0U) ? USART_CR2_CPHA : 0U) |
           ((cfg->last_bit_clock != 0U) ? USART_CR2_LBCL : 0U) |
           ((cfg->slave != 0U) ? USART_CR2_SLVEN : 0U) |
           ((cfg->ignore_nss != 0U) ? USART_CR2_DIS_NSS : 0U);
    instance->CR2 = cr2;
    return SPL_OK;
}

SPL_Status USART_SmartcardConfig(USART_TypeDef *instance, const USART_SmartcardTypeDef *cfg)
{
    uint32_t cr3;

    SPL_REQUIRE_SAFE(Usart_AnyValid(instance) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE_SAFE(cfg != NULL, SPL_ERR_PARAM);
    SPL_REQUIRE((cfg->enable <= 1U) && (cfg->nack <= 1U) && (cfg->clock_out <= 1U), SPL_ERR_PARAM);
    /* PSC[7:5] must be 0 in smartcard mode (RM0444 33.8.6). SAFE: GTPR / SCARCNT tasmasi. */
    SPL_REQUIRE_SAFE((cfg->prescaler <= 31U) && (cfg->retries <= 7U) &&
                         ((cfg->enable == 0U) || (cfg->prescaler != 0U)),
                     SPL_ERR_PARAM);
    /* RM0444 Table 184: smartcard on FULL instances only. SAFE. */
    SPL_REQUIRE_SAFE(Usart_IsFull(instance) != 0U, SPL_ERR_UNSUPPORTED);
    SPL_REQUIRE_SAFE(Usart_Enabled(instance) == 0U,
                     SPL_ERR_STATE); /* SAFE: UE = 1 yazim yok sayilir */
    cr3 = instance->CR3 & ~(USART_CR3_SCEN | USART_CR3_NACK | USART_CR3_SCARCNT_Msk);
    if (cfg->enable != 0U)
    {
        const uint32_t stop = Usart_Stop(instance);
        /* RM0444 33.5.17: LINEN, HDSEL, IREN cleared; 8 data bits + parity (M = 9-bit word,
         * PCE = 1); 1.5 stop bits (0.5 accepted for reception); OVER8 = 0 (33.8.1 note). */
        SPL_REQUIRE_SAFE(((instance->CR2 & USART_CR2_LINEN) == 0U) &&
                             ((instance->CR3 & (USART_CR3_HDSEL | USART_CR3_IREN)) == 0U) &&
                             ((instance->CR1 & USART_CR1_PCE) != 0U) &&
                             ((instance->CR1 & (USART_CR1_M0 | USART_CR1_M1)) == USART_CR1_M0) &&
                             ((stop == 1U) || (stop == 3U)) &&
                             ((instance->CR1 & USART_CR1_OVER8) == 0U),
                         SPL_ERR_STATE); /* SAFE: RM0444 33.5.17 on kosullari */
        cr3 |= USART_CR3_SCEN | ((cfg->nack != 0U) ? USART_CR3_NACK : 0U) |
               ((uint32_t)cfg->retries << USART_CR3_SCARCNT_Pos);
        instance->GTPR = ((uint32_t)cfg->guard_time << USART_GTPR_GT_Pos) | cfg->prescaler;
        instance->CR2 =
            (instance->CR2 & ~USART_CR2_CLKEN) | ((cfg->clock_out != 0U) ? USART_CR2_CLKEN : 0U);
    }
    instance->CR3 = cr3;
    return SPL_OK;
}

SPL_Status USART_IrdaConfig(USART_TypeDef *instance, uint8_t enable, uint8_t low_power,
                            uint8_t prescaler)
{
    uint32_t cr3;

    SPL_REQUIRE_SAFE(Usart_AnyValid(instance) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE((enable <= 1U) && (low_power <= 1U), SPL_ERR_PARAM); /* normal */
    /* PSC = 0 is reserved in IrDA mode (RM0444 33.8.6). SAFE. */
    SPL_REQUIRE_SAFE((enable == 0U) || (prescaler != 0U), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(Usart_IsFull(instance) != 0U, SPL_ERR_UNSUPPORTED); /* SAFE: FULL disi */
    SPL_REQUIRE_SAFE(Usart_Enabled(instance) == 0U,
                     SPL_ERR_STATE); /* SAFE: UE = 1 yazim yok sayilir */
    cr3 = instance->CR3 & ~(USART_CR3_IREN | USART_CR3_IRLP);
    if (enable != 0U)
    {
        /* RM0444 33.5.18: LINEN, STOP, CLKEN, SCEN, HDSEL cleared; OVER8 = 0. */
        SPL_REQUIRE_SAFE(
            ((instance->CR2 & (USART_CR2_LINEN | USART_CR2_STOP_Msk | USART_CR2_CLKEN)) == 0U) &&
                ((instance->CR3 & (USART_CR3_SCEN | USART_CR3_HDSEL)) == 0U) &&
                ((instance->CR1 & USART_CR1_OVER8) == 0U),
            SPL_ERR_STATE); /* SAFE: RM0444 33.5.18 on kosullari */
        cr3 |= USART_CR3_IREN | ((low_power != 0U) ? USART_CR3_IRLP : 0U);
        instance->GTPR = (instance->GTPR & USART_GTPR_GT_Msk) | prescaler;
    }
    instance->CR3 = cr3;
    return SPL_OK;
}

SPL_Status USART_LinConfig(USART_TypeDef *instance, uint8_t enable, uint8_t break_11bit,
                           uint8_t break_irq)
{
    uint32_t cr2;

    SPL_REQUIRE_SAFE(Usart_AnyValid(instance) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE((enable <= 1U) && (break_11bit <= 1U) && (break_irq <= 1U),
                SPL_ERR_PARAM);                                          /* normal */
    SPL_REQUIRE_SAFE(Usart_IsFull(instance) != 0U, SPL_ERR_UNSUPPORTED); /* SAFE: FULL disi */
    SPL_REQUIRE_SAFE(Usart_Enabled(instance) == 0U,
                     SPL_ERR_STATE); /* SAFE: UE = 1 yazim yok sayilir */
    if (enable != 0U)
    {
        /* RM0444 33.5.13: STOP, CLKEN, SCEN, HDSEL, IREN cleared; 8-bit words; OVER8 = 0. */
        SPL_REQUIRE_SAFE(
            (Usart_Stop(instance) == USART_STOP_ONE) && ((instance->CR2 & USART_CR2_CLKEN) == 0U) &&
                ((instance->CR3 & (USART_CR3_SCEN | USART_CR3_HDSEL | USART_CR3_IREN)) == 0U) &&
                ((instance->CR1 & (USART_CR1_M0 | USART_CR1_M1 | USART_CR1_OVER8)) == 0U),
            SPL_ERR_STATE); /* SAFE: RM0444 33.5.13 on kosullari */
    }
    cr2 = instance->CR2 & ~(USART_CR2_LINEN | USART_CR2_LBDL | USART_CR2_LBDIE);
    cr2 |= ((enable != 0U) ? USART_CR2_LINEN : 0U) | ((break_11bit != 0U) ? USART_CR2_LBDL : 0U) |
           ((break_irq != 0U) ? USART_CR2_LBDIE : 0U);
    instance->CR2 = cr2;
    return SPL_OK;
}

SPL_Status USART_SendBreak(USART_TypeDef *instance)
{
    SPL_REQUIRE_SAFE(Usart_AnyValid(instance) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    /* SAFE: UE / TE kapali, bayt / istek sessizce kaybolur. */
    SPL_REQUIRE_SAFE((instance->CR1 & (USART_CR1_UE | USART_CR1_TE)) ==
                         (USART_CR1_UE | USART_CR1_TE),
                     SPL_ERR_STATE);
    if ((instance->ISR & USART_ISR_SBKF) != 0U)
    {
        return SPL_BUSY; /* previous break still pending */
    }
    instance->RQR = USART_RQR_SBKRQ;
    return SPL_OK;
}

SPL_Status USART_AutoBaudConfig(USART_TypeDef *instance, uint8_t enable, uint8_t mode)
{
    SPL_REQUIRE_SAFE(Usart_AnyValid(instance) != 0U, SPL_ERR_PARAM);     /* SAFE: rastgele adres */
    SPL_REQUIRE(enable <= 1U, SPL_ERR_PARAM);                            /* normal */
    SPL_REQUIRE_SAFE(mode <= 3U, SPL_ERR_PARAM);                         /* SAFE: ABRMODE tasmasi */
    SPL_REQUIRE_SAFE(Usart_IsFull(instance) != 0U, SPL_ERR_UNSUPPORTED); /* SAFE: FULL disi */
    /* ERRATA ES0548 2.12.4 (family): clearing ABREN during a reception corrupts data. ABREN and
     * ABRMODE change only with UE = 0 here, so no reception can be ongoing. SAFE. */
    SPL_REQUIRE_SAFE(Usart_Enabled(instance) == 0U,
                     SPL_ERR_STATE); /* SAFE: UE = 1 yazim yok sayilir */
    instance->CR2 = (instance->CR2 & ~(USART_CR2_ABREN | USART_CR2_ABRMODE_Msk)) |
                    ((enable != 0U) ? USART_CR2_ABREN : 0U) |
                    ((uint32_t)mode << USART_CR2_ABRMODE_Pos);
    return SPL_OK;
}

SPL_Status USART_AutoBaudRequest(USART_TypeDef *instance)
{
    SPL_REQUIRE_SAFE(Usart_AnyValid(instance) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE_SAFE((instance->CR2 & USART_CR2_ABREN) != 0U,
                     SPL_ERR_STATE); /* SAFE: istek yok sayilir */
    instance->RQR = USART_RQR_ABRRQ;
    return SPL_OK;
}

SPL_Status USART_AutoBaudStatus(USART_TypeDef *instance, uint8_t *done, uint8_t *error)
{
    uint32_t isr;

    SPL_REQUIRE_SAFE(Usart_AnyValid(instance) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE_SAFE((done != NULL) && (error != NULL), SPL_ERR_PARAM);
    isr = instance->ISR;
    *done = ((isr & USART_ISR_ABRF) != 0U) ? 1U : 0U;
    *error = ((isr & USART_ISR_ABRE) != 0U) ? 1U : 0U;
    return SPL_OK;
}

SPL_Status USART_WakeupConfig(USART_TypeDef *instance, uint8_t enable, uint8_t source, uint8_t irq)
{
    uint32_t cr3;

    SPL_REQUIRE_SAFE(Usart_AnyValid(instance) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE((enable <= 1U) && (irq <= 1U), SPL_ERR_PARAM);       /* normal */
    /* WUS = 01 is reserved (RM0444 33.8.3). SAFE. */
    SPL_REQUIRE_SAFE((source != 1U) && (source <= 3U), SPL_ERR_PARAM);
    /* RM0444 Table 184: no wake-up on BASIC instances. SAFE. */
    SPL_REQUIRE_SAFE(Usart_IsBasic(instance) == 0U, SPL_ERR_UNSUPPORTED);
    SPL_REQUIRE_SAFE(Usart_Enabled(instance) == 0U,
                     SPL_ERR_STATE); /* SAFE: UE = 1 yazim yok sayilir */
    cr3 = instance->CR3 & ~(USART_CR3_WUS_Msk | USART_CR3_WUFIE);
    cr3 |= ((uint32_t)source << USART_CR3_WUS_Pos) | ((irq != 0U) ? USART_CR3_WUFIE : 0U);
    instance->CR3 = cr3;
    instance->CR1 = (instance->CR1 & ~USART_CR1_UESM) | ((enable != 0U) ? USART_CR1_UESM : 0U);
    return SPL_OK;
}

SPL_Status USART_RxTimeoutConfig(USART_TypeDef *instance, uint8_t enable, uint32_t timeout,
                                 uint8_t block_length)
{
    SPL_REQUIRE_SAFE(Usart_AnyValid(instance) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE(enable <= 1U, SPL_ERR_PARAM);                        /* normal */
    SPL_REQUIRE_SAFE(timeout <= (USART_RTOR_RTO_Msk >> USART_RTOR_RTO_Pos),
                     SPL_ERR_PARAM); /* SAFE */
    /* RM0444 Table 184: receiver timeout on FULL only. SAFE. */
    SPL_REQUIRE_SAFE(Usart_IsFull(instance) != 0U, SPL_ERR_UNSUPPORTED);
    SPL_REQUIRE_SAFE(Usart_Enabled(instance) == 0U,
                     SPL_ERR_STATE); /* SAFE: UE = 1 yazim yok sayilir */
    instance->RTOR =
        (timeout << USART_RTOR_RTO_Pos) | ((uint32_t)block_length << USART_RTOR_BLEN_Pos);
    instance->CR2 = (instance->CR2 & ~USART_CR2_RTOEN) | ((enable != 0U) ? USART_CR2_RTOEN : 0U);
    return SPL_OK;
}

SPL_Status USART_MuteConfig(USART_TypeDef *instance, uint8_t enable, uint8_t address_mark,
                            uint8_t address, uint8_t addr7)
{
    uint32_t cr1;
    uint32_t cr2;

    SPL_REQUIRE_SAFE(Usart_AnyValid(instance) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE((enable <= 1U) && (address_mark <= 1U) && (addr7 <= 1U),
                SPL_ERR_PARAM); /* normal */
    /* 4-bit address detection compares ADD[3:0] only (33.5.10). SAFE. */
    SPL_REQUIRE_SAFE((addr7 != 0U) || (address <= 15U), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(Usart_Enabled(instance) == 0U,
                     SPL_ERR_STATE); /* SAFE: UE = 1 yazim yok sayilir */
    cr2 = instance->CR2 & ~(USART_CR2_ADD_Msk | USART_CR2_ADDM7);
    cr2 |= ((uint32_t)address << USART_CR2_ADD_Pos) | ((addr7 != 0U) ? USART_CR2_ADDM7 : 0U);
    cr1 = instance->CR1 & ~(USART_CR1_MME | USART_CR1_WAKE);
    cr1 |= ((enable != 0U) ? USART_CR1_MME : 0U) | ((address_mark != 0U) ? USART_CR1_WAKE : 0U);
    instance->CR2 = cr2;
    instance->CR1 = cr1;
    return SPL_OK;
}

SPL_Status USART_EnterMute(USART_TypeDef *instance)
{
    SPL_REQUIRE_SAFE(Usart_AnyValid(instance) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE_SAFE((instance->CR1 & USART_CR1_MME) != 0U,
                     SPL_ERR_STATE); /* SAFE: istek yok sayilir */
    instance->RQR = USART_RQR_MMRQ;
    return SPL_OK;
}

SPL_Status USART_ModeFlags(USART_TypeDef *instance, uint32_t *flags)
{
    SPL_REQUIRE_SAFE(Usart_AnyValid(instance) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE_SAFE(flags != NULL, SPL_ERR_PARAM);
    *flags = instance->ISR & USART_MODE_FLAGS;
    return SPL_OK;
}

SPL_Status USART_ClearModeFlags(USART_TypeDef *instance, uint32_t flags)
{
    SPL_REQUIRE_SAFE(Usart_AnyValid(instance) != 0U, SPL_ERR_PARAM); /* SAFE: rastgele adres */
    SPL_REQUIRE(flags != 0U, SPL_ERR_PARAM);                         /* normal */
    /* SAFE: ICR'de baska bayraklar (TC, IDLE ...) sessizce silinir. */
    SPL_REQUIRE_SAFE((flags & ~USART_MODE_FLAGS_CLEARABLE) == 0U, SPL_ERR_PARAM);
    /* SAFE (ornek yetenegi): LBDCF / RTOCF / EOBCF / UDRCF yalniz FULL USART'ta; WUCF FULL ve
     * LPUART'ta; BASIC'te yalniz CMCF (RM0444 Table 184, 33.8.11 / 34.7.10 ICR; LPUART ICR'de
     * bu bitler yok). 2026-11-14 yetenek taramasi. */
    SPL_REQUIRE_SAFE((flags & ~Usart_ModeFlagsOf(instance)) == 0U, SPL_ERR_UNSUPPORTED);
    instance->ICR = flags;
    /* Read back: the clear reaches the peripheral before return (METHOD 1.7, issue #23). */
    (void)instance->ISR;
    return SPL_OK;
}
