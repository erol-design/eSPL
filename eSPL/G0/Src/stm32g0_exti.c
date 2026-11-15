/* EXTI driver for STM32G0 (RM0444 Rev 6 section 13). Bank 1 = lines 0..31, bank 2 = lines 32..36;
 * only IMR2/EMR2 of bank 2 are used (HW-001: the CMSIS RTSR2..FPR2 offsets are not trusted).
 * v0.8: the lines that exist and the configurable ones come from the generated per-SKU masks. */
/* Kutuphane birimi: spl_check.h karisma korumasi referansini bu birim de uretir; uretmeyen tek
 * birim stm32g0_check.c (SPL_CHECK_LEVEL_DEFINER). BULGU-02, CHECK_LEVEL.md 3.2. */
#define SPL_LIBRARY_BUILD 1
#include <stddef.h>
#include "stm32g0_exti.h"
#include "stm32g0_sku_masks.h"

#define EXTI_LINE_VDDIO2 34U
/* Configurable lines of bank 1 (RM0444 Table 65; G0B1: 0..18 and 20) = RTSR1 bits of the SKU. */
#define EXTI_CONFIGURABLE1 SPL_G0_MASK_EXTI_RTSR1
/* Lines that exist: bank 1 = IMR1 bits, bank 2 = IMR2 bits (0 on G03x/G05x, G0B0 only line 36). */
#define EXTI_PRESENT1 SPL_G0_MASK_EXTI_IMR1
#define EXTI_PRESENT2 SPL_G0_MASK_EXTI_IMR2

/* Denetim seviyesi (A4 dalga 2, Docs/CHECK_LEVEL.md 5.4 EXTI tablosu): SPL_REQUIRE = normal sinif
 * (yalniz FULL), SPL_REQUIRE_SAFE = SAFE sinif (FULL ve MIX). Butun makrolar ilk MMIO yazimindan
 * once (METHOD 1.3). */

static uint8_t Exti_IsConfigurable(uint32_t line)
{
    return ((line < 32U) && (((EXTI_CONFIGURABLE1 >> line) & 1U) != 0U)) ? 1U : 0U;
}

/* PARAM for line > 36; UNSUPPORTED for a line this SKU does not have and for the bank-2
 * configurable line 34 (HW-001); else OK. */
static SPL_Status Exti_LineCheck(uint32_t line)
{
    uint32_t present;

    /* SAFE: line >= 37 tanimsiz kaydirma / baska hattin biti. "Bu SKU'da yok" ve hat 34 yapisal. */
    SPL_REQUIRE_SAFE(line < EXTI_LINE_COUNT, SPL_ERR_PARAM);
    if (line == EXTI_LINE_VDDIO2)
    {
        return SPL_ERR_UNSUPPORTED;
    }
    present =
        (line < 32U) ? ((EXTI_PRESENT1 >> line) & 1U) : ((EXTI_PRESENT2 >> (line - 32U)) & 1U);
    return (present != 0U) ? SPL_OK : SPL_ERR_UNSUPPORTED;
}

SPL_Status EXTI_SetGpioSource(GPIO_TypeDef *port, uint8_t line)
{
    /* EXTIm codes are fixed (0 = PA .. 5 = PF); GPIOE only where the SKU has it (v0.8). */
    static const struct
    {
        const GPIO_TypeDef *port;
        uint32_t code;
    } ports[] = {{GPIOA, 0U}, {GPIOB, 1U}, {GPIOC, 2U}, {GPIOD, 3U},
#if defined(GPIOE)
                 {GPIOE, 4U},
#endif
                 {GPIOF, 5U}};
    const uint32_t n = (uint32_t)(sizeof(ports) / sizeof(ports[0]));
    uint32_t k;
    uint32_t code = 0U;
    uint32_t shift;

    SPL_REQUIRE_SAFE(line <= 15U, SPL_ERR_PARAM); /* SAFE: EXTICR dizisinin disina yazim */
    for (k = 0U; k < n; ++k)
    {
        if (port == ports[k].port)
        {
            code = ports[k].code;
            break;
        }
    }
    if (k == n)
    {
        return SPL_ERR_PARAM; /* yapisal: arama sonu (kod 0 = PA sessizce secilirdi), her seviyede
                               */
    }
    /* RM0444 13.5.11: EXTICRx holds four 8-bit EXTIm fields (0 = PA .. 5 = PF). */
    shift = ((uint32_t)line & 3U) * 8U;
    EXTI->EXTICR[line >> 2U] = (EXTI->EXTICR[line >> 2U] & ~(0xFFU << shift)) | (code << shift);
    return SPL_OK;
}

SPL_Status EXTI_LineConfig(uint8_t line, EXTI_TriggerTypeDef trigger, uint32_t mode)
{
    const uint32_t t = (uint32_t)trigger;
    const SPL_Status st = Exti_LineCheck(line);
    uint32_t bit;

    if (st != SPL_OK)
    {
        return st;
    }
    SPL_REQUIRE_SAFE(t <= (uint32_t)EXTI_TRIGGER_BOTH, SPL_ERR_PARAM); /* SAFE: kenar secilmez */
    /* normal: mode = 0 hat maskeli (gorunur), fazla bit atilir. */
    SPL_REQUIRE((mode != 0U) && ((mode & ~(EXTI_MODE_INTERRUPT | EXTI_MODE_EVENT)) == 0U),
                SPL_ERR_PARAM);
    /* direct line: no edge selection. SAFE: kenar sessizce yok sayilir. */
    SPL_REQUIRE_SAFE((Exti_IsConfigurable(line) != 0U) || (t == (uint32_t)EXTI_TRIGGER_NONE),
                     SPL_ERR_PARAM);
    if (line < 32U)
    {
        bit = (uint32_t)1U << line;
        if (Exti_IsConfigurable(line) != 0U)
        {
            EXTI->RTSR1 = ((t & 1U) != 0U) ? (EXTI->RTSR1 | bit) : (EXTI->RTSR1 & ~bit);
            EXTI->FTSR1 = ((t & 2U) != 0U) ? (EXTI->FTSR1 | bit) : (EXTI->FTSR1 & ~bit);
        }
        EXTI->IMR1 =
            ((mode & EXTI_MODE_INTERRUPT) != 0U) ? (EXTI->IMR1 | bit) : (EXTI->IMR1 & ~bit);
        EXTI->EMR1 = ((mode & EXTI_MODE_EVENT) != 0U) ? (EXTI->EMR1 | bit) : (EXTI->EMR1 & ~bit);
    }
#if EXTI_PRESENT2 != 0UL
    else
    {
        bit = (uint32_t)1U << ((uint32_t)line - 32U);
        EXTI->IMR2 =
            ((mode & EXTI_MODE_INTERRUPT) != 0U) ? (EXTI->IMR2 | bit) : (EXTI->IMR2 & ~bit);
        EXTI->EMR2 = ((mode & EXTI_MODE_EVENT) != 0U) ? (EXTI->EMR2 | bit) : (EXTI->EMR2 & ~bit);
    }
#endif
    return SPL_OK;
}

SPL_Status EXTI_LineDisable(uint8_t line)
{
    const SPL_Status st = Exti_LineCheck(line);
    uint32_t bit;

    if (st != SPL_OK)
    {
        return st;
    }
    if (line < 32U)
    {
        bit = (uint32_t)1U << line;
        EXTI->IMR1 = EXTI->IMR1 & ~bit;
        EXTI->EMR1 = EXTI->EMR1 & ~bit;
        if (Exti_IsConfigurable(line) != 0U)
        {
            EXTI->RTSR1 = EXTI->RTSR1 & ~bit;
            EXTI->FTSR1 = EXTI->FTSR1 & ~bit;
        }
    }
#if EXTI_PRESENT2 != 0UL
    else
    {
        bit = (uint32_t)1U << ((uint32_t)line - 32U);
        EXTI->IMR2 = EXTI->IMR2 & ~bit;
        EXTI->EMR2 = EXTI->EMR2 & ~bit;
    }
#endif
    return SPL_OK;
}

/* Configurable bank-1 line check shared by the pending / software-trigger services. */
static SPL_Status Exti_ConfigurableCheck(uint8_t line)
{
    const SPL_Status st = Exti_LineCheck(line);

    if (st != SPL_OK)
    {
        return st;
    }
    /* SAFE: dogrudan hatta yazim etkisiz / okuma sessizce 0. */
    SPL_REQUIRE_SAFE(Exti_IsConfigurable(line) != 0U, SPL_ERR_PARAM);
    return SPL_OK;
}

SPL_Status EXTI_SoftwareTrigger(uint8_t line)
{
    const SPL_Status st = Exti_ConfigurableCheck(line);

    if (st != SPL_OK)
    {
        return st;
    }
    EXTI->SWIER1 = (uint32_t)1U << line; /* other written zeros have no effect (RM0444 13.5.3) */
    return SPL_OK;
}

SPL_Status EXTI_GetPendingEdges(uint8_t line, uint8_t *rising, uint8_t *falling)
{
    const SPL_Status st = Exti_ConfigurableCheck(line);

    SPL_REQUIRE_SAFE((rising != NULL) && (falling != NULL), SPL_ERR_PARAM);
    if (st != SPL_OK)
    {
        return st;
    }
    *rising = (((EXTI->RPR1 >> line) & 1U) != 0U) ? 1U : 0U;
    *falling = (((EXTI->FPR1 >> line) & 1U) != 0U) ? 1U : 0U;
    return SPL_OK;
}

SPL_Status EXTI_GetPending(uint8_t line, uint8_t *pending)
{
    uint8_t r = 0U;
    uint8_t f = 0U;
    SPL_Status st;

    SPL_REQUIRE_SAFE(pending != NULL, SPL_ERR_PARAM);
    st = EXTI_GetPendingEdges(line, &r, &f);
    if (st == SPL_OK)
    {
        *pending = ((r != 0U) || (f != 0U)) ? 1U : 0U;
    }
    return st;
}

SPL_Status EXTI_ClearPending(uint8_t line)
{
    const SPL_Status st = Exti_ConfigurableCheck(line);

    if (st != SPL_OK)
    {
        return st;
    }
    EXTI->RPR1 = (uint32_t)1U << line; /* write-1-to-clear: only this line */
    EXTI->FPR1 = (uint32_t)1U << line;
    /* Read back: the clear reaches the peripheral before return (METHOD 1.7, issue #23). */
    (void)EXTI->RPR1;
    return SPL_OK;
}
