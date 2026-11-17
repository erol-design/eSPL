/* SPDX-License-Identifier: MIT */
/* RCC driver for STM32G0 (RM0444 Rev 6 section 5, DS13560 Rev 6 Table 47); v0.8: every G0 SKU
 * through stm32g0_sku.h (HSI48, PLL Q output, CCIPR2 and kernel selectors only where present).
 * Public contract: stm32g0_rcc.h. */
/* Library unit: this unit also emits the spl_check.h mismatch-guard reference; the only unit
 * that does not is stm32g0_check.c (SPL_CHECK_LEVEL_DEFINER). BULGU-02, CHECK_LEVEL.md 3.2. */
#define SPL_LIBRARY_BUILD 1
#include <stddef.h>
#include "stm32g0_rcc.h"
#include "stm32g0_flash.h"
#include "stm32g0_sku.h"
#include "stm32g0_sku_masks.h"

/* v0.8: enable / reset masks generated per SKU from the CMSIS header (Tools/gen_sku_masks.py);
 * for STM32G0B1 they equal the v0.7 hand-written masks (test_v08_sku.c SKU-T1). RTCAPB and WWDG
 * have no reset bit: the RSTR masks come from the RSTR register definitions. */
#define RCC_IOP_MASK SPL_G0_MASK_RCC_IOPENR
#define RCC_IOP_RESET_MASK SPL_G0_MASK_RCC_IOPRSTR
#define RCC_AHB_MASK SPL_G0_MASK_RCC_AHBENR
#define RCC_AHB_RESET_MASK SPL_G0_MASK_RCC_AHBRSTR
#define RCC_APB1_MASK SPL_G0_MASK_RCC_APBENR1
#define RCC_APB1_RESET_MASK SPL_G0_MASK_RCC_APBRSTR1
#define RCC_APB2_MASK SPL_G0_MASK_RCC_APBENR2
#define RCC_APB2_RESET_MASK SPL_G0_MASK_RCC_APBRSTR2

#define RCC_CFGR_SWS_VAL(cfgr) (((cfgr) & RCC_CFGR_SWS_Msk) >> RCC_CFGR_SWS_Pos)

/* Check level (A4 wave 1, Docs/CHECK_LEVEL.md 5.4 RCC table): SPL_REQUIRE = checked at FULL only
 * (not compiled at FAST). Structural returns (switch default, end of search, selector not on the
 * SKU) and the result returns of the clock calculation are plain ifs at every level. Helpers used
 * only for validation (clock in use, HCLK limit, VOS) are not compiled in FAST. All macros come
 * before the first MMIO write (METHOD 1.3). */

typedef struct
{
    volatile uint32_t *enr;
    volatile uint32_t *rstr;
    uint32_t reset_mask;
} Rcc_Bus;

/* 2026-11-13 (from UFAST): the ENR / RSTR address is computed from the bus number (instead of a
 * switch). IOPRSTR..APBRSTR2 (0x24..0x30) and IOPENR..APBENR2 (0x34..0x40) are contiguous on every
 * G0 SKU (RM0444 / RM0454 5.4); the typedefs below verify this at compile time. Validation is
 * unchanged: id >= 0x80 or a bit not in the SKU mask -> 0. Bus order RCC_BUS_IOP..APB2. */
typedef char Rcc_EnrContiguous[((offsetof(RCC_TypeDef, APBENR2) - offsetof(RCC_TypeDef, IOPENR)) ==
                                (3U * sizeof(uint32_t)))
                                   ? 1
                                   : -1];
typedef char Rcc_RstrContiguous[((offsetof(RCC_TypeDef, APBRSTR2) -
                                  offsetof(RCC_TypeDef, IOPRSTR)) == (3U * sizeof(uint32_t)))
                                    ? 1
                                    : -1];
typedef char Rcc_BusOrder[((RCC_BUS_IOP == 0U) && (RCC_BUS_AHB == 1U) && (RCC_BUS_APB1 == 2U) &&
                           (RCC_BUS_APB2 == 3U))
                              ? 1
                              : -1];

/* A4 (CHECK_LEVEL.md 4.2): the old Rcc_Lookup was split in two. Rcc_IsPeriph is a pure validator
 * (id >= 0x80 is an out-of-table read and a write to an arbitrary address, otherwise a write
 * to a reserved bit); compiled only in FULL. Rcc_Locate does no validation, it computes the
 * path and the bit (with a valid id). */
#if SPL_CHECK_LEVEL == SPL_CHECK_FULL
static uint32_t Rcc_IsPeriph(RCC_PeriphTypeDef periph)
{
    static const uint32_t mask[4] = {RCC_IOP_MASK, RCC_AHB_MASK, RCC_APB1_MASK, RCC_APB2_MASK};
    const uint32_t id = (uint32_t)periph;

    return ((id < 0x80U) && ((mask[id >> 5U] & (1U << (id & 31U))) != 0U)) ? 1U : 0U;
}
#endif

#if SPL_CHECK_LEVEL == SPL_CHECK_FULL
#define RCC_PERIPH_OK(periph) (Rcc_IsPeriph(periph) != 0U)
#else
#define RCC_PERIPH_OK(periph) (1) /* not evaluated at a disabled level */
#endif

/* GPIOE only on SKUs that have GPIOE (macro: an SPL_REQUIRE argument cannot contain #if). */
#if defined(GPIOE)
#define RCC_GPIOE_ABSENT(bit) (0)
#else
#define RCC_GPIOE_ABSENT(bit) ((bit) == 4U)
#endif

static uint32_t Rcc_Locate(RCC_PeriphTypeDef periph, Rcc_Bus *bus)
{
    static const uint32_t reset_mask[4] = {RCC_IOP_RESET_MASK, RCC_AHB_RESET_MASK,
                                           RCC_APB1_RESET_MASK, RCC_APB2_RESET_MASK};
    const uint32_t id = (uint32_t)periph;
    const uint32_t b = id >> 5U;

    bus->enr = &(&RCC->IOPENR)[b];
    bus->rstr = &(&RCC->IOPRSTR)[b];
    bus->reset_mask = reset_mask[b];
    return 1U << (id & 31U);
}

#if SPL_CHECK_LEVEL == SPL_CHECK_FULL
/* 1 = range 1, 2 = range 2, 0 = unreadable value (00/11 cannot be written: treated as an error).
 * Used only by checks (HCLK limit, PLL frequency limits): not compiled in FAST. */
static uint32_t Rcc_VosRange(void)
{
    const uint32_t vos = (PWR->CR1 & PWR_CR1_VOS_Msk) >> PWR_CR1_VOS_Pos;
    return ((vos == 1U) || (vos == 2U)) ? vos : 0U;
}
#endif

/* 2026-11-13 (from UFAST): HPRE / PPRE dividers are powers of 2: a shift instead of a division
 * (M0+ has no divide instruction, x / 2^k == x >> k for unsigned numbers). */
static uint32_t Rcc_HpreShift(uint32_t code)
{
    static const uint8_t shift[8] = {1U, 2U, 3U, 4U, 6U, 7U, 8U, 9U}; /* 2..512 */
    return ((code & 8U) == 0U) ? 0U : (uint32_t)shift[code & 7U];
}

static uint32_t Rcc_PpreShift(uint32_t code)
{
    return ((code & 4U) == 0U) ? 0U : ((code & 3U) + 1U);
}

/* 2026-11-13 (from UFAST): in * n / d without 64 bits and, for EVERY input, equal to the old
 * (uint32_t)(((uint64_t)in * n) / d) (__aeabi_uldivmod 272 B). With in = q * d + rem,
 * in * n / d = q * n + rem * n / d is an exact integer equality; both sides are also equal
 * mod 2^32. rem * n < d * n < 2^32 (d <= 256, n <= 127). */
static uint32_t Rcc_MulDiv(uint32_t in, uint32_t n, uint32_t d)
{
    const uint32_t q = in / d;
    const uint32_t rem = in - (q * d);

    return (q * n) + ((rem * n) / d);
}

static SPL_Status Rcc_PllInputHz(uint32_t pllsrc, uint32_t hse_hz, uint32_t *hz)
{
    if (pllsrc == (uint32_t)RCC_PLLSRC_HSI16)
    {
        *hz = RCC_HSI16_HZ;
        return SPL_OK;
    }
    if (pllsrc == (uint32_t)RCC_PLLSRC_HSE)
    {
        if (hse_hz == 0U)
        {
            return SPL_ERR_STATE;
        }
        *hz = hse_hz;
        return SPL_OK;
    }
    return SPL_ERR_STATE; /* PLLSRC = none or reserved */
}

/* PLLRCLK from the programmed PLLCFGR. */
static SPL_Status Rcc_PllRHz(uint32_t hse_hz, uint32_t *hz)
{
    const uint32_t cfg = RCC->PLLCFGR;
    const uint32_t m = ((cfg & RCC_PLLCFGR_PLLM_Msk) >> RCC_PLLCFGR_PLLM_Pos) + 1U;
    const uint32_t n = (cfg & RCC_PLLCFGR_PLLN_Msk) >> RCC_PLLCFGR_PLLN_Pos;
    const uint32_t r = ((cfg & RCC_PLLCFGR_PLLR_Msk) >> RCC_PLLCFGR_PLLR_Pos) + 1U;
    uint32_t in = 0U;
    SPL_Status st =
        Rcc_PllInputHz((cfg & RCC_PLLCFGR_PLLSRC_Msk) >> RCC_PLLCFGR_PLLSRC_Pos, hse_hz, &in);

    if (st != SPL_OK)
    {
        return st;
    }
    if ((n < 8U) || (r < 2U))
    {
        return SPL_ERR_STATE; /* reset value or reserved encoding: not a usable PLL */
    }
    *hz = Rcc_MulDiv(in, n, m * r);
    return SPL_OK;
}

static SPL_Status Rcc_SourceHz(uint32_t source, uint32_t hse_hz, uint32_t *hz)
{
    switch (source)
    {
    case RCC_SYSCLK_HSISYS:
        *hz = RCC_HSI16_HZ >> ((RCC->CR & RCC_CR_HSIDIV_Msk) >> RCC_CR_HSIDIV_Pos);
        return SPL_OK;
    case RCC_SYSCLK_HSE:
        if (hse_hz == 0U)
        {
            return SPL_ERR_STATE;
        }
        *hz = hse_hz;
        return SPL_OK;
    case RCC_SYSCLK_PLLR:
        return Rcc_PllRHz(hse_hz, hz);
    case RCC_SYSCLK_LSI:
        *hz = RCC_LSI_HZ;
        return SPL_OK;
    case RCC_SYSCLK_LSE:
        *hz = RCC_LSE_HZ;
        return SPL_OK;
    default:
        return SPL_ERR_STATE;
    }
}

#if SPL_CHECK_LEVEL == SPL_CHECK_FULL
/* New HCLK allowed by the voltage range and by the programmed flash latency. Check
 * (CHECK_LEVEL.md 5.3: random HardFault); not compiled in FAST. The "frequency not possible"
 * answer of FLASH_CalcLatency is result class and is returned at every level. */
static SPL_Status Rcc_HclkAllowed(uint32_t hclk_hz)
{
    const uint32_t range = Rcc_VosRange();
    uint32_t needed = 0U;

    SPL_REQUIRE(range != 0U, SPL_ERR_STATE);
    if (FLASH_CalcLatency(hclk_hz, (range == 1U) ? FLASH_RANGE1 : FLASH_RANGE2, &needed) != SPL_OK)
    {
        return SPL_ERR_STATE; /* above the range maximum (64 / 16 MHz) */
    }
    SPL_REQUIRE(needed <= ((FLASH->ACR & FLASH_ACR_LATENCY_Msk) >> FLASH_ACR_LATENCY_Pos),
                SPL_ERR_STATE);
    return SPL_OK;
}

static uint32_t Rcc_CurrentHpreShift(void)
{
    return Rcc_HpreShift((RCC->CFGR & RCC_CFGR_HPRE_Msk) >> RCC_CFGR_HPRE_Pos);
}
#endif

SPL_Status RCC_GpioClockEnable(GPIO_TypeDef *port)
{
    /* IOPENR bit = port letter index (A = 0 .. F = 5). 2026-11-13 (from UFAST): address difference
     * instead of a table; ports are at GPIOA_BASE + 0x400 * k. Accepted ports unchanged: A, B, C,
     * D, (E only on SKUs that have GPIOE), F; NULL and every other address (the unsigned
     * difference overflows) -> PARAM. */
    const uintptr_t offset = (uintptr_t)port - (uintptr_t)GPIOA_BASE;
    const uint32_t bit = (uint32_t)(offset >> 10U);

    /* another address turns into another IOPENR bit or an undefined shift. */
    SPL_REQUIRE(((offset & 0x3FFU) == 0U) && (offset <= ((uintptr_t)5U << 10U)) &&
                    !RCC_GPIOE_ABSENT(bit),
                SPL_ERR_PARAM);
    RCC->IOPENR = RCC->IOPENR | ((uint32_t)1U << bit);
    /* RM0444 5.2.17: the clock starts 2 cycles after the enable bit; the read-back waits for it. */
    /* Read back: the clock is active before the caller's first access (METHOD 1.7, issue #21). */
    (void)RCC->IOPENR;
    return SPL_OK;
}

SPL_Status RCC_EnablePeriphClock(RCC_PeriphTypeDef periph)
{
    Rcc_Bus bus;
    uint32_t bit;

    SPL_REQUIRE(RCC_PERIPH_OK(periph), SPL_ERR_PARAM);
    bit = Rcc_Locate(periph, &bus);
    *bus.enr = *bus.enr | bit;
    /* RM0444 5.2.17: the clock starts 2 cycles after the enable bit; the read-back waits for it. */
    /* Read back: the clock is active before the caller's first access (METHOD 1.7, issue #21). */
    (void)*bus.enr;
    return SPL_OK;
}

SPL_Status RCC_DisablePeriphClock(RCC_PeriphTypeDef periph)
{
    Rcc_Bus bus;
    uint32_t bit;

    SPL_REQUIRE(RCC_PERIPH_OK(periph), SPL_ERR_PARAM);
    bit = Rcc_Locate(periph, &bus);
    *bus.enr = *bus.enr & ~bit;
    return SPL_OK;
}

SPL_Status RCC_GetPeriphClock(RCC_PeriphTypeDef periph, uint8_t *enabled)
{
    Rcc_Bus bus;
    uint32_t bit;

    SPL_REQUIRE(enabled != NULL, SPL_ERR_PARAM);
    SPL_REQUIRE(RCC_PERIPH_OK(periph), SPL_ERR_PARAM);
    bit = Rcc_Locate(periph, &bus);
    *enabled = ((*bus.enr & bit) != 0U) ? 1U : 0U;
    return SPL_OK;
}

static SPL_Status Rcc_SetReset(RCC_PeriphTypeDef periph, uint8_t assert_reset)
{
    Rcc_Bus bus;
    uint32_t bit;

    SPL_REQUIRE(RCC_PERIPH_OK(periph), SPL_ERR_PARAM);
    bit = Rcc_Locate(periph, &bus);
    /* peripheral without a reset bit (RTCAPB, WWDG): write to a reserved RSTR bit. */
    SPL_REQUIRE((bus.reset_mask & bit) != 0U, SPL_ERR_UNSUPPORTED);
    *bus.rstr = (assert_reset != 0U) ? (*bus.rstr | bit) : (*bus.rstr & ~bit);
    return SPL_OK;
}

SPL_Status RCC_AssertPeriphReset(RCC_PeriphTypeDef periph)
{
    return Rcc_SetReset(periph, 1U);
}

SPL_Status RCC_ReleasePeriphReset(RCC_PeriphTypeDef periph)
{
    return Rcc_SetReset(periph, 0U);
}

/* v0.4 (G4 v0.13.4 #7, CLK-09): 1 when src is the kernel clock of a peripheral whose bus clock is
 * on, or (rtcsel != 0) the RTC clock with RTCEN = 1 (RM0444 5.4.21..5.4.23). A selector left on src
 * with the peripheral clock off is not a user. Defined below (uses the kernel table). */
#if SPL_CHECK_LEVEL == SPL_CHECK_FULL
/* A4: the clock-in-use checks are (CHECK_LEVEL.md 5.3); these helpers are used only by those
 * checks and are not compiled in FAST. Pure: reads MMIO, does not write (Tools/pure_checks.txt). */
static uint32_t Rcc_SourceInUse(uint32_t src, uint32_t rtcsel);

static uint32_t Rcc_PllSourceIs(uint32_t pllsrc)
{
    return (((RCC->CR & RCC_CR_PLLON) != 0U) &&
            (((RCC->PLLCFGR & RCC_PLLCFGR_PLLSRC_Msk) >> RCC_PLLCFGR_PLLSRC_Pos) == pllsrc))
               ? 1U
               : 0U;
}
#define RCC_PLL_SOURCE_IS(pllsrc) (Rcc_PllSourceIs(pllsrc) != 0U)
#define RCC_SOURCE_IN_USE(src, rtcsel) (Rcc_SourceInUse((src), (rtcsel)) != 0U)
#else
#define RCC_PLL_SOURCE_IS(pllsrc) (0) /* not evaluated at a disabled level */
#define RCC_SOURCE_IN_USE(src, rtcsel) (0)
#endif

SPL_Status RCC_StartOsc(RCC_OscTypeDef osc)
{
    switch (osc)
    {
    case RCC_OSC_HSI16:
        RCC->CR = RCC->CR | RCC_CR_HSION;
        return SPL_OK;
    case RCC_OSC_HSE:
        RCC->CR = RCC->CR | RCC_CR_HSEON;
        return SPL_OK;
    case RCC_OSC_HSI48:
#if defined(RCC_CR_HSI48ON)
        RCC->CR = RCC->CR | RCC_CR_HSI48ON;
        return SPL_OK;
#else
        return SPL_ERR_UNSUPPORTED; /* HSI48 only on STM32G0B1 / STM32G0C1 */
#endif
    case RCC_OSC_LSI:
        RCC->CSR = (RCC->CSR & ~RCC_CSR_RMVF) | RCC_CSR_LSION;
        return SPL_OK;
    case RCC_OSC_LSE:
        /* while DBP = 0 a BDCR write is silently ignored. */
        SPL_REQUIRE((PWR->CR1 & PWR_CR1_DBP) != 0U, SPL_ERR_STATE);
        RCC->BDCR = RCC->BDCR | RCC_BDCR_LSEON;
        return SPL_OK;
    default:
        return SPL_ERR_PARAM;
    }
}

/* (CHECK_LEVEL.md 5.3): if the oscillator is SYSCLK, the PLL source, the RTC clock or the
 * kernel clock of an enabled peripheral, a clock in use is cut. SWS is read once per case
 * (never in FAST). */
SPL_Status RCC_StopOsc(RCC_OscTypeDef osc)
{
    switch (osc)
    {
    case RCC_OSC_HSI16:
        SPL_REQUIRE((RCC_CFGR_SWS_VAL(RCC->CFGR) != (uint32_t)RCC_SYSCLK_HSISYS) &&
                        !RCC_PLL_SOURCE_IS(RCC_PLLSRC_HSI16) &&
                        !RCC_SOURCE_IN_USE(RCC_KSRC_HSI16, 0U),
                    SPL_ERR_STATE);
        RCC->CR = RCC->CR & ~RCC_CR_HSION;
        return SPL_OK;
    case RCC_OSC_HSE:
        SPL_REQUIRE((RCC_CFGR_SWS_VAL(RCC->CFGR) != (uint32_t)RCC_SYSCLK_HSE) &&
                        !RCC_PLL_SOURCE_IS(RCC_PLLSRC_HSE) &&
                        !RCC_SOURCE_IN_USE(RCC_KSRC_HSE, 3U), /* RTCSEL 11 = HSE / 32 */
                    SPL_ERR_STATE);
        RCC->CR = RCC->CR & ~RCC_CR_HSEON;
        return SPL_OK;
    case RCC_OSC_HSI48:
#if defined(RCC_CR_HSI48ON)
        SPL_REQUIRE(!RCC_SOURCE_IN_USE(RCC_KSRC_HSI48, 0U), SPL_ERR_STATE);
        RCC->CR = RCC->CR & ~RCC_CR_HSI48ON;
        return SPL_OK;
#else
        return SPL_ERR_UNSUPPORTED;
#endif
    case RCC_OSC_LSI:
        SPL_REQUIRE((RCC_CFGR_SWS_VAL(RCC->CFGR) != (uint32_t)RCC_SYSCLK_LSI) &&
                        !RCC_SOURCE_IN_USE(RCC_KSRC_LSI, 2U), /* RTCSEL 10 = LSI */
                    SPL_ERR_STATE);
        RCC->CSR = RCC->CSR & ~(RCC_CSR_LSION | RCC_CSR_RMVF);
        return SPL_OK;
    case RCC_OSC_LSE:
        /* DBP = 0: a BDCR write is silently ignored (same return). */
        SPL_REQUIRE((RCC_CFGR_SWS_VAL(RCC->CFGR) != (uint32_t)RCC_SYSCLK_LSE) &&
                        ((PWR->CR1 & PWR_CR1_DBP) != 0U) &&
                        !RCC_SOURCE_IN_USE(RCC_KSRC_LSE, 1U), /* RTCSEL 01 = LSE */
                    SPL_ERR_STATE);
        RCC->BDCR = RCC->BDCR & ~RCC_BDCR_LSEON;
        return SPL_OK;
    default:
        return SPL_ERR_PARAM;
    }
}

static uint8_t Rcc_OscReady(uint32_t osc)
{
    switch (osc)
    {
    case RCC_OSC_HSI16:
        return ((RCC->CR & RCC_CR_HSIRDY) != 0U) ? 1U : 0U;
    case RCC_OSC_HSE:
        return ((RCC->CR & RCC_CR_HSERDY) != 0U) ? 1U : 0U;
    case RCC_OSC_HSI48:
#if defined(RCC_CR_HSI48RDY)
        return ((RCC->CR & RCC_CR_HSI48RDY) != 0U) ? 1U : 0U;
#else
        return 0U;
#endif
    case RCC_OSC_LSI:
        return ((RCC->CSR & RCC_CSR_LSIRDY) != 0U) ? 1U : 0U;
    default:
        return ((RCC->BDCR & RCC_BDCR_LSERDY) != 0U) ? 1U : 0U;
    }
}

SPL_Status RCC_GetOscReady(RCC_OscTypeDef osc, uint8_t *ready)
{
    SPL_REQUIRE(ready != NULL, SPL_ERR_PARAM);
    /* an invalid osc would silently return the LSE state (Rcc_OscReady default). */
    SPL_REQUIRE((uint32_t)osc <= (uint32_t)RCC_OSC_LSE, SPL_ERR_PARAM);
    *ready = Rcc_OscReady((uint32_t)osc);
    return SPL_OK;
}

SPL_Status RCC_SetHseBypass(uint8_t bypass)
{
    SPL_REQUIRE(bypass <= 1U, SPL_ERR_PARAM); /* acts as 1 */
    /* RM0444 5.4.1: HSEBYP can be written only while the HSE oscillator is disabled. while
     * HSE is on, the write is silently ignored. */
    SPL_REQUIRE((RCC->CR & RCC_CR_HSEON) == 0U, SPL_ERR_STATE);
    RCC->CR = (bypass != 0U) ? (RCC->CR | RCC_CR_HSEBYP) : (RCC->CR & ~RCC_CR_HSEBYP);
    return SPL_OK;
}

SPL_Status RCC_SetHsiDiv(uint32_t divider)
{
    uint32_t code = 0U;

    while ((code < 7U) && ((1U << code) < divider))
    {
        ++code;
    }
    /* a divider that is not a power of 2 is rounded to the nearest code (silently another
     * frequency). */
    SPL_REQUIRE((divider != 0U) && ((1U << code) == divider), SPL_ERR_PARAM);
#if SPL_CHECK_LEVEL == SPL_CHECK_FULL
    /* (5.3): a new HCLK above the VOS / flash wait-state limit gives a random HardFault. */
    if (RCC_CFGR_SWS_VAL(RCC->CFGR) == (uint32_t)RCC_SYSCLK_HSISYS)
    {
        const SPL_Status st = Rcc_HclkAllowed((RCC_HSI16_HZ >> code) >> Rcc_CurrentHpreShift());
        if (st != SPL_OK)
        {
            return st;
        }
    }
#endif
    RCC->CR = (RCC->CR & ~RCC_CR_HSIDIV_Msk) | (code << RCC_CR_HSIDIV_Pos);
    return SPL_OK;
}

#if SPL_CHECK_LEVEL == SPL_CHECK_FULL
/* Output frequency limits in MHz [range-1][output]: P, Q, R. Used only by the check, pure. */
static uint8_t Rcc_OutputOk(uint32_t range, uint32_t out, uint32_t hz)
{
    static const uint8_t max_mhz[2][3] = {{122U, 128U, 64U}, {40U, 33U, 16U}};
    static const uint32_t min_hz[3] = {3090000UL, 12000000UL, 12000000UL};
    return ((hz >= min_hz[out]) && (hz <= ((uint32_t)max_mhz[range - 1U][out] * 1000000UL))) ? 1U
                                                                                             : 0U;
}
#endif

SPL_Status RCC_ConfigPll(const RCC_PllConfigTypeDef *config, uint32_t hse_hz)
{
    uint32_t cfg;

    SPL_REQUIRE(config != NULL, SPL_ERR_PARAM);
    /* an invalid source or an out-of-range M / N / P / Q / R overflows into a neighbouring
     * PLLCFGR field or writes a reserved code. */
    SPL_REQUIRE(((config->source == (uint32_t)RCC_PLLSRC_HSI16) ||
                 (config->source == (uint32_t)RCC_PLLSRC_HSE)) &&
                    (config->m >= 1U) && (config->m <= 8U) && (config->n >= 8U) &&
                    (config->n <= 86U) && (config->p >= 2U) && (config->p <= 32U) &&
                    (config->q >= 2U) && (config->q <= 8U) && (config->r >= 2U) &&
                    (config->r <= 8U),
                SPL_ERR_PARAM);
    /* a PLL without outputs (the SYSCLK switch is refused) / extra bits are dropped. */
    SPL_REQUIRE((config->outputs != 0U) &&
                    ((config->outputs & ~(RCC_PLL_OUT_P | RCC_PLL_OUT_Q | RCC_PLL_OUT_R)) == 0U),
                SPL_ERR_PARAM);
#if SPL_CHECK_LEVEL == SPL_CHECK_FULL
    /* an input / PFD / VCO / output frequency outside the specification gives an unstable
     * clock. The frequency calculation serves only these checks: not compiled in FAST. */
    {
        uint32_t in;
        uint32_t pfd;
        uint32_t vco;
        uint32_t range;

        SPL_REQUIRE((config->source != (uint32_t)RCC_PLLSRC_HSE) ||
                        ((hse_hz >= RCC_HSE_MIN_HZ) && (hse_hz <= RCC_HSE_MAX_HZ)),
                    SPL_ERR_PARAM);
        in = (config->source == (uint32_t)RCC_PLLSRC_HSE) ? hse_hz : RCC_HSI16_HZ;
        pfd = in / config->m;
        /* Multiply before dividing so a fractional PFD (25 MHz / 3) is not truncated (Rcc_MulDiv:
         * 48 MHz * 86 without overflow, no 64 bits). */
        vco = Rcc_MulDiv(in, config->n, config->m);
        range = Rcc_VosRange();
        SPL_REQUIRE(range != 0U, SPL_ERR_STATE);
        SPL_REQUIRE((pfd >= 2660000UL) && (pfd <= 16000000UL) && (vco >= 96000000UL) &&
                        (vco <= ((range == 1U) ? 344000000UL : 128000000UL)),
                    SPL_ERR_PARAM);
        SPL_REQUIRE((((config->outputs & RCC_PLL_OUT_P) == 0U) ||
                     (Rcc_OutputOk(range, 0U, vco / config->p) != 0U)) &&
                        (((config->outputs & RCC_PLL_OUT_Q) == 0U) ||
                         (Rcc_OutputOk(range, 1U, vco / config->q) != 0U)) &&
                        (((config->outputs & RCC_PLL_OUT_R) == 0U) ||
                         (Rcc_OutputOk(range, 2U, vco / config->r) != 0U)),
                    SPL_ERR_PARAM);
    }
#else
    (void)hse_hz;
#endif
#if !defined(RCC_PLLCFGR_PLLQEN)
    if ((config->outputs & RCC_PLL_OUT_Q) != 0U)
    {
        return SPL_ERR_UNSUPPORTED; /* no PLLQCLK on this SKU (CMSIS: no PLLCFGR.PLLQ) */
    }
#endif
    /* RM0444 5.4.4: every bitfield can be written only when the PLL is disabled. while the
     * PLL is on, a PLLCFGR write is silently ignored. */
    SPL_REQUIRE((RCC->CR & RCC_CR_PLLON) == 0U, SPL_ERR_STATE);

    cfg = (config->source << RCC_PLLCFGR_PLLSRC_Pos) | ((config->m - 1U) << RCC_PLLCFGR_PLLM_Pos) |
          (config->n << RCC_PLLCFGR_PLLN_Pos) | ((config->p - 1U) << RCC_PLLCFGR_PLLP_Pos) |
          ((config->r - 1U) << RCC_PLLCFGR_PLLR_Pos);
#if defined(RCC_PLLCFGR_PLLQEN)
    cfg |= (config->q - 1U) << RCC_PLLCFGR_PLLQ_Pos;
#endif
    if ((config->outputs & RCC_PLL_OUT_P) != 0U)
    {
        cfg |= RCC_PLLCFGR_PLLPEN;
    }
#if defined(RCC_PLLCFGR_PLLQEN)
    if ((config->outputs & RCC_PLL_OUT_Q) != 0U)
    {
        cfg |= RCC_PLLCFGR_PLLQEN;
    }
#endif
    if ((config->outputs & RCC_PLL_OUT_R) != 0U)
    {
        cfg |= RCC_PLLCFGR_PLLREN;
    }
    RCC->PLLCFGR = cfg;
    return SPL_OK;
}

#if SPL_CHECK_LEVEL == SPL_CHECK_FULL
/* if the source is not ready the PLL does not lock, RCC_GetPllReady stays 0 (visible at
 * once). Compiled only in FULL. Pure: reads MMIO. */
static uint32_t Rcc_PllSourceReady(void)
{
    const uint32_t src = (RCC->PLLCFGR & RCC_PLLCFGR_PLLSRC_Msk) >> RCC_PLLCFGR_PLLSRC_Pos;

    return (((src == (uint32_t)RCC_PLLSRC_HSI16) && ((RCC->CR & RCC_CR_HSIRDY) != 0U)) ||
            ((src == (uint32_t)RCC_PLLSRC_HSE) && ((RCC->CR & RCC_CR_HSERDY) != 0U)))
               ? 1U
               : 0U;
}
#define RCC_PLL_SOURCE_READY() (Rcc_PllSourceReady() != 0U)
#else
#define RCC_PLL_SOURCE_READY() (1) /* not evaluated at a disabled level */
#endif

SPL_Status RCC_EnablePll(void)
{
    SPL_REQUIRE(RCC_PLL_SOURCE_READY(), SPL_ERR_STATE);
    RCC->CR = RCC->CR | RCC_CR_PLLON;
    return SPL_OK;
}

SPL_Status RCC_DisablePll(void)
{
    /* if the PLL is SYSCLK or the kernel clock of an enabled peripheral, a clock in use is
     * cut. */
    SPL_REQUIRE((RCC_CFGR_SWS_VAL(RCC->CFGR) != (uint32_t)RCC_SYSCLK_PLLR) &&
                    !RCC_SOURCE_IN_USE(RCC_KSRC_PLLP, 0U) && !RCC_SOURCE_IN_USE(RCC_KSRC_PLLQ, 0U),
                SPL_ERR_STATE);
    RCC->CR = RCC->CR & ~RCC_CR_PLLON;
    return SPL_OK;
}

SPL_Status RCC_GetPllReady(uint8_t *ready)
{
    SPL_REQUIRE(ready != NULL, SPL_ERR_PARAM);
    *ready = ((RCC->CR & RCC_CR_PLLRDY) != 0U) ? 1U : 0U;
    return SPL_OK;
}

#if SPL_CHECK_LEVEL == SPL_CHECK_FULL
/* a switch to a source that is not ready stays pending, SWS does not change and OK would be
 * returned. FULL only. Pure: reads MMIO. */
static uint32_t Rcc_SysclkReady(uint32_t s)
{
    switch (s)
    {
    case RCC_SYSCLK_HSISYS:
        return Rcc_OscReady(RCC_OSC_HSI16);
    case RCC_SYSCLK_HSE:
        return Rcc_OscReady(RCC_OSC_HSE);
    case RCC_SYSCLK_PLLR:
        return (((RCC->CR & RCC_CR_PLLRDY) != 0U) && ((RCC->PLLCFGR & RCC_PLLCFGR_PLLREN) != 0U))
                   ? 1U
                   : 0U;
    case RCC_SYSCLK_LSI:
        return Rcc_OscReady(RCC_OSC_LSI);
    default:
        return Rcc_OscReady(RCC_OSC_LSE);
    }
}
#define RCC_SYSCLK_READY(s) (Rcc_SysclkReady(s) != 0U)
#else
#define RCC_SYSCLK_READY(s) (1) /* not evaluated at a disabled level */
#endif

SPL_Status RCC_SetSysclkSource(RCC_SysclkTypeDef source, uint32_t hse_hz)
{
    const uint32_t s = (uint32_t)source;

    SPL_REQUIRE(s <= (uint32_t)RCC_SYSCLK_LSE, SPL_ERR_PARAM); /* SW reserved code */
    SPL_REQUIRE(RCC_SYSCLK_READY(s), SPL_ERR_STATE);
#if SPL_CHECK_LEVEL == SPL_CHECK_FULL
    /* (5.3): a new HCLK above the VOS / flash wait-state limit gives a random HardFault. */
    {
        uint32_t hz = 0U;
        SPL_Status st = Rcc_SourceHz(s, hse_hz, &hz);

        if (st == SPL_OK)
        {
            st = Rcc_HclkAllowed(hz >> Rcc_CurrentHpreShift());
        }
        if (st != SPL_OK)
        {
            return st;
        }
    }
#else
    (void)hse_hz;
#endif
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW_Msk) | (s << RCC_CFGR_SW_Pos);
    return SPL_OK;
}

SPL_Status RCC_GetSysclkSource(RCC_SysclkTypeDef *source)
{
    const uint32_t sws = RCC_CFGR_SWS_VAL(RCC->CFGR);

    SPL_REQUIRE(source != NULL, SPL_ERR_PARAM);
    if (sws > (uint32_t)RCC_SYSCLK_LSE)
    {
        return SPL_ERR_HW; /* reserved SWS encoding */
    }
    *source = (RCC_SysclkTypeDef)sws;
    return SPL_OK;
}

SPL_Status RCC_SetAhbPrescaler(uint32_t divider, uint32_t hse_hz)
{
    static const uint16_t divs[9] = {1U, 2U, 4U, 8U, 16U, 64U, 128U, 256U, 512U};
    uint32_t i;

    for (i = 0U; i < 9U; ++i)
    {
        if (divider == (uint32_t)divs[i])
        {
            break;
        }
    }
    SPL_REQUIRE(i != 9U, SPL_ERR_PARAM); /* i = 9 would write a code overflowing HPRE */
#if SPL_CHECK_LEVEL == SPL_CHECK_FULL
    /* (5.3): a new HCLK above the VOS / flash wait-state limit gives a random HardFault. */
    {
        uint32_t sysclk = 0U;
        SPL_Status st = Rcc_SourceHz(RCC_CFGR_SWS_VAL(RCC->CFGR), hse_hz, &sysclk);

        if (st == SPL_OK)
        {
            st = Rcc_HclkAllowed(sysclk / divider);
        }
        if (st != SPL_OK)
        {
            return st;
        }
    }
#else
    (void)hse_hz;
#endif
    /* HPRE codes: 0000 = 1, 1000 + (i - 1) for 2..512. */
    RCC->CFGR =
        (RCC->CFGR & ~RCC_CFGR_HPRE_Msk) | (((i == 0U) ? 0U : (7U + i)) << RCC_CFGR_HPRE_Pos);
    /* RM0444 3.3.4 "Decreasing the CPU frequency" step 3: CFGR is read to confirm the new divider
     * is in effect; the caller may then reduce the wait states with FLASH_SetLatency. */
    /* Read back: the new HCLK divider is in effect before return (METHOD 1.7, issue #22). */
    (void)RCC->CFGR;
    return SPL_OK;
}

SPL_Status RCC_SetApbPrescaler(uint32_t divider)
{
    uint32_t code;

    switch (divider)
    {
    case 1U:
        code = 0U;
        break;
    case 2U:
        code = 4U;
        break;
    case 4U:
        code = 5U;
        break;
    case 8U:
        code = 6U;
        break;
    case 16U:
        code = 7U;
        break;
    default:
        return SPL_ERR_PARAM;
    }
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_PPRE_Msk) | (code << RCC_CFGR_PPRE_Pos);
    return SPL_OK;
}

SPL_Status RCC_GetClocks(uint32_t hse_hz, RCC_ClocksTypeDef *clocks)
{
    const uint32_t cfgr = RCC->CFGR;
    const uint32_t pshift = Rcc_PpreShift((cfgr & RCC_CFGR_PPRE_Msk) >> RCC_CFGR_PPRE_Pos);
    uint32_t sysclk = 0U;
    SPL_Status st;

    SPL_REQUIRE(clocks != NULL, SPL_ERR_PARAM);
    st = Rcc_SourceHz(RCC_CFGR_SWS_VAL(cfgr), hse_hz, &sysclk);
    if (st != SPL_OK)
    {
        return st;
    }
    clocks->sysclk_hz = sysclk;
    clocks->hclk_hz = sysclk >> Rcc_HpreShift((cfgr & RCC_CFGR_HPRE_Msk) >> RCC_CFGR_HPRE_Pos);
    clocks->pclk_hz = clocks->hclk_hz >> pshift;
    clocks->tim_pclk_hz = (pshift == 0U) ? clocks->pclk_hz : (clocks->pclk_hz * 2U);
    return SPL_OK;
}

/* ---- v0.3 kernel clocks ---- */
#define RCC_KSRC_INVALID 0xFFU

typedef struct
{
    uint8_t present; /* v0.8: 0 = selector not implemented on this SKU (UNSUPPORTED) */
    uint8_t reg;     /* 0 = CCIPR, 1 = CCIPR2 */
    uint8_t pos;     /* field position */
    uint8_t width;   /* v0.9: field width in bits, 1 or 2 */
    uint8_t src[4];  /* RCC_KernelSrcTypeDef per encoding, RCC_KSRC_INVALID = reserved */
} Rcc_KernelSel;

/* One entry per selector; absent fields (CMSIS has no <field>_Pos) become present = 0. */
#define RCC_KSEL_NONE                                                                              \
    {                                                                                              \
        0U, 0U, 0U, 2U,                                                                            \
        {                                                                                          \
            RCC_KSRC_INVALID, RCC_KSRC_INVALID, RCC_KSRC_INVALID, RCC_KSRC_INVALID                 \
        }                                                                                          \
    }
#define RCC_KSEL_UART(pos)                                                                         \
    {                                                                                              \
        1U, 0U, (pos), 2U,                                                                         \
        {                                                                                          \
            RCC_KSRC_PCLK, RCC_KSRC_SYSCLK, RCC_KSRC_HSI16, RCC_KSRC_LSE                           \
        }                                                                                          \
    }
#define RCC_KSEL_I2C(pos)                                                                          \
    {                                                                                              \
        1U, 0U, (pos), 2U,                                                                         \
        {                                                                                          \
            RCC_KSRC_PCLK, RCC_KSRC_SYSCLK, RCC_KSRC_HSI16, RCC_KSRC_INVALID                       \
        }                                                                                          \
    }
#define RCC_KSEL_LPTIM(pos)                                                                        \
    {                                                                                              \
        1U, 0U, (pos), 2U,                                                                         \
        {                                                                                          \
            RCC_KSRC_PCLK, RCC_KSRC_LSI, RCC_KSRC_HSI16, RCC_KSRC_LSE                              \
        }                                                                                          \
    }
#if defined(RCC_CCIPR_USART2SEL_Pos)
#define RCC_KSEL_USART2 RCC_KSEL_UART(RCC_CCIPR_USART2SEL_Pos)
#else
#define RCC_KSEL_USART2 RCC_KSEL_NONE
#endif
#if defined(RCC_CCIPR_USART3SEL_Pos)
#define RCC_KSEL_USART3 RCC_KSEL_UART(RCC_CCIPR_USART3SEL_Pos)
#else
#define RCC_KSEL_USART3 RCC_KSEL_NONE
#endif
#if defined(RCC_CCIPR_LPUART1SEL_Pos)
#define RCC_KSEL_LPUART1 RCC_KSEL_UART(RCC_CCIPR_LPUART1SEL_Pos)
#else
#define RCC_KSEL_LPUART1 RCC_KSEL_NONE
#endif
#if defined(RCC_CCIPR_LPUART2SEL_Pos)
#define RCC_KSEL_LPUART2 RCC_KSEL_UART(RCC_CCIPR_LPUART2SEL_Pos)
#else
#define RCC_KSEL_LPUART2 RCC_KSEL_NONE
#endif
#if defined(RCC_CCIPR_I2C2SEL_Pos)
#define RCC_KSEL_I2C2 RCC_KSEL_I2C(RCC_CCIPR_I2C2SEL_Pos)
#else
#define RCC_KSEL_I2C2 RCC_KSEL_NONE
#endif
#if defined(RCC_CCIPR_LPTIM1SEL_Pos)
#define RCC_KSEL_LPTIM1 RCC_KSEL_LPTIM(RCC_CCIPR_LPTIM1SEL_Pos)
#else
#define RCC_KSEL_LPTIM1 RCC_KSEL_NONE
#endif
#if defined(RCC_CCIPR_LPTIM2SEL_Pos)
#define RCC_KSEL_LPTIM2 RCC_KSEL_LPTIM(RCC_CCIPR_LPTIM2SEL_Pos)
#else
#define RCC_KSEL_LPTIM2 RCC_KSEL_NONE
#endif
#if defined(RCC_CCIPR2_FDCANSEL_Pos)
#define RCC_KSEL_FDCAN                                                                             \
    {                                                                                              \
        1U, 1U, RCC_CCIPR2_FDCANSEL_Pos, 2U,                                                       \
        {                                                                                          \
            RCC_KSRC_PCLK, RCC_KSRC_PLLQ, RCC_KSRC_HSE, RCC_KSRC_INVALID                           \
        }                                                                                          \
    }
#else
#define RCC_KSEL_FDCAN RCC_KSEL_NONE
#endif
#if defined(RCC_CCIPR2_USBSEL_Pos) && defined(RCC_CR_HSI48ON)
/* G0B1 / G0C1 (RM0444 5.4.22): 00 HSI48, 01 HSE, 10 PLLQCLK, 11 reserved. */
#define RCC_KSEL_USB                                                                               \
    {                                                                                              \
        1U, 1U, RCC_CCIPR2_USBSEL_Pos, 2U,                                                         \
        {                                                                                          \
            RCC_KSRC_HSI48, RCC_KSRC_HSE, RCC_KSRC_PLLQ, RCC_KSRC_INVALID                          \
        }                                                                                          \
    }
#elif defined(RCC_CCIPR2_USBSEL_Pos)
/* G0B0 (RM0454 5.4.21, no HSI48): 00 reserved, 01 PLLQCLK, 10 HSE, 11 reserved. The G0B1 table
 * was used here before the 2026-11-14 capability audit: HSE and PLLQ were swapped. */
#define RCC_KSEL_USB                                                                               \
    {                                                                                              \
        1U, 1U, RCC_CCIPR2_USBSEL_Pos, 2U,                                                         \
        {                                                                                          \
            RCC_KSRC_INVALID, RCC_KSRC_PLLQ, RCC_KSRC_HSE, RCC_KSRC_INVALID                        \
        }                                                                                          \
    }
#else
#define RCC_KSEL_USB RCC_KSEL_NONE
#endif
#if defined(RCC_CCIPR_RNGSEL_Pos)
#define RCC_KSEL_RNG                                                                               \
    {                                                                                              \
        1U, 0U, RCC_CCIPR_RNGSEL_Pos, 2U,                                                          \
        {                                                                                          \
            RCC_KSRC_NONE, RCC_KSRC_HSI16, RCC_KSRC_SYSCLK, RCC_KSRC_PLLQ                          \
        }                                                                                          \
    }
#else
#define RCC_KSEL_RNG RCC_KSEL_NONE
#endif
#if defined(RCC_CCIPR_CECSEL_Pos)
#define RCC_KSEL_CEC                                                                               \
    {                                                                                              \
        1U, 0U, RCC_CCIPR_CECSEL_Pos, 1U,                                                          \
        {                                                                                          \
            RCC_KSRC_HSI16_DIV488, RCC_KSRC_LSE, RCC_KSRC_INVALID, RCC_KSRC_INVALID                \
        }                                                                                          \
    }
#else
#define RCC_KSEL_CEC RCC_KSEL_NONE
#endif
/* I2S1: CCIPR2.I2S1SEL on group 0BX, CCIPR[15:14] (I2C2I2S1SEL, I2S1 column) elsewhere; same
 * encoding (RM0444 5.4.21 / 5.4.22). I2S2: CCIPR2.I2S2SEL, 0BX only. */
#define RCC_KSEL_I2S_SRC {RCC_KSRC_SYSCLK, RCC_KSRC_PLLP, RCC_KSRC_HSI16, RCC_KSRC_I2S_CKIN}
#if defined(RCC_CCIPR2_I2S1SEL_Pos)
#define RCC_KSEL_I2S1 {1U, 1U, RCC_CCIPR2_I2S1SEL_Pos, 2U, RCC_KSEL_I2S_SRC}
#elif defined(RCC_CCIPR_I2S1SEL_Pos)
#define RCC_KSEL_I2S1 {1U, 0U, RCC_CCIPR_I2S1SEL_Pos, 2U, RCC_KSEL_I2S_SRC}
#else
#define RCC_KSEL_I2S1 RCC_KSEL_NONE
#endif
#if defined(RCC_CCIPR2_I2S2SEL_Pos)
#define RCC_KSEL_I2S2 {1U, 1U, RCC_CCIPR2_I2S2SEL_Pos, 2U, RCC_KSEL_I2S_SRC}
#else
#define RCC_KSEL_I2S2 RCC_KSEL_NONE
#endif
#define RCC_KERNEL_COUNT 16U

/* RM0444 Rev 6 5.4.21 / 5.4.22. */
static const Rcc_KernelSel rcc_kernel_sel[RCC_KERNEL_COUNT] = {
    RCC_KSEL_UART(RCC_CCIPR_USART1SEL_Pos),
    RCC_KSEL_USART2,
    RCC_KSEL_USART3,
    RCC_KSEL_LPUART1,
    RCC_KSEL_LPUART2,
    RCC_KSEL_I2C(RCC_CCIPR_I2C1SEL_Pos),
    RCC_KSEL_I2C2,
    RCC_KSEL_LPTIM1,
    RCC_KSEL_LPTIM2,
    {1U,
     0U,
     RCC_CCIPR_ADCSEL_Pos,
     2U,
     {RCC_KSRC_SYSCLK, RCC_KSRC_PLLP, RCC_KSRC_HSI16, RCC_KSRC_INVALID}},
    RCC_KSEL_FDCAN,
    RCC_KSEL_USB,
    RCC_KSEL_RNG,
    RCC_KSEL_CEC,
    RCC_KSEL_I2S1,
    RCC_KSEL_I2S2};

static uint32_t Rcc_FieldMask(const Rcc_KernelSel *sel)
{
    return ((uint32_t)1U << sel->width) - 1U;
}

static volatile uint32_t *Rcc_KernelReg(const Rcc_KernelSel *sel)
{
#if defined(RCC_CCIPR2_FDCANSEL_Pos) || defined(RCC_CCIPR2_USBSEL_Pos) ||                          \
    defined(RCC_CCIPR2_I2S1SEL_Pos)
    return (sel->reg == 0U) ? &RCC->CCIPR : &RCC->CCIPR2;
#else
    (void)sel;
    return &RCC->CCIPR; /* no CCIPR2 on this SKU; only present = 1 entries reach here */
#endif
}

/* PLLPCLK (out = 0) or PLLQCLK (out = 1) from the programmed PLLCFGR. */
static SPL_Status Rcc_PllPqHz(uint32_t out, uint32_t hse_hz, uint32_t *hz)
{
    const uint32_t cfg = RCC->PLLCFGR;
    const uint32_t m = ((cfg & RCC_PLLCFGR_PLLM_Msk) >> RCC_PLLCFGR_PLLM_Pos) + 1U;
    const uint32_t n = (cfg & RCC_PLLCFGR_PLLN_Msk) >> RCC_PLLCFGR_PLLN_Pos;
#if defined(RCC_PLLCFGR_PLLQEN)
    const uint32_t en = (out == 0U) ? RCC_PLLCFGR_PLLPEN : RCC_PLLCFGR_PLLQEN;
    const uint32_t div = (out == 0U)
                             ? (((cfg & RCC_PLLCFGR_PLLP_Msk) >> RCC_PLLCFGR_PLLP_Pos) + 1U)
                             : (((cfg & RCC_PLLCFGR_PLLQ_Msk) >> RCC_PLLCFGR_PLLQ_Pos) + 1U);
#else
    const uint32_t en = (out == 0U) ? RCC_PLLCFGR_PLLPEN : 0U; /* no PLLQ: never enabled */
    const uint32_t div = ((cfg & RCC_PLLCFGR_PLLP_Msk) >> RCC_PLLCFGR_PLLP_Pos) + 1U;
#endif
    uint32_t in = 0U;
    SPL_Status st;

    if (((cfg & en) == 0U) || (n < 8U) || (div < 2U))
    {
        return SPL_ERR_STATE; /* output disabled or reserved encoding */
    }
    st = Rcc_PllInputHz((cfg & RCC_PLLCFGR_PLLSRC_Msk) >> RCC_PLLCFGR_PLLSRC_Pos, hse_hz, &in);
    if (st != SPL_OK)
    {
        return st;
    }
    *hz = Rcc_MulDiv(in, n, m * div);
    return SPL_OK;
}

static SPL_Status Rcc_KernelSourceHz(uint32_t src, uint32_t hse_hz, uint32_t *hz)
{
    RCC_ClocksTypeDef c;
    SPL_Status st = SPL_OK;

    switch (src)
    {
    case RCC_KSRC_PCLK:
    case RCC_KSRC_SYSCLK:
        st = RCC_GetClocks(hse_hz, &c);
        if (st == SPL_OK)
        {
            *hz = (src == (uint32_t)RCC_KSRC_PCLK) ? c.pclk_hz : c.sysclk_hz;
        }
        break;
    case RCC_KSRC_HSI16:
        *hz = RCC_HSI16_HZ;
        break;
    case RCC_KSRC_LSE:
        *hz = RCC_LSE_HZ;
        break;
    case RCC_KSRC_LSI:
        *hz = RCC_LSI_HZ;
        break;
    case RCC_KSRC_HSE:
        if (hse_hz == 0U)
        {
            st = SPL_ERR_STATE;
        }
        else
        {
            *hz = hse_hz;
        }
        break;
    case RCC_KSRC_HSI48:
        *hz = RCC_HSI48_HZ;
        break;
    case RCC_KSRC_PLLP:
        st = Rcc_PllPqHz(0U, hse_hz, hz);
        break;
    case RCC_KSRC_NONE:
        st = SPL_ERR_STATE; /* v0.9: RNGSEL = 00, the RNG has no kernel clock */
        break;
    case RCC_KSRC_HSI16_DIV488:
        *hz = RCC_HSI16_HZ / 488U; /* compile-time constant: no runtime division */
        break;
    case RCC_KSRC_I2S_CKIN:
        st = SPL_ERR_STATE; /* v0.10: external I2S_CKIN, frequency not known to the driver */
        break;
    default:
        st = Rcc_PllPqHz(1U, hse_hz, hz);
        break;
    }
    return st;
}

/* Peripheral clocked by each kernel selector (same order as rcc_kernel_sel). */
#if SPL_CHECK_LEVEL == SPL_CHECK_FULL
static const RCC_PeriphTypeDef rcc_kernel_user[RCC_KERNEL_COUNT] = {
    RCC_PERIPH_USART1,  RCC_PERIPH_USART2, RCC_PERIPH_USART3, RCC_PERIPH_LPUART1,
    RCC_PERIPH_LPUART2, RCC_PERIPH_I2C1,   RCC_PERIPH_I2C2,   RCC_PERIPH_LPTIM1,
    RCC_PERIPH_LPTIM2,  RCC_PERIPH_ADC,    RCC_PERIPH_FDCAN,  RCC_PERIPH_USB,
    RCC_PERIPH_RNG,     RCC_PERIPH_CEC,    RCC_PERIPH_SPI1,   RCC_PERIPH_SPI2};

static uint32_t Rcc_SourceInUse(uint32_t src, uint32_t rtcsel)
{
    uint32_t k;

    if ((rtcsel != 0U) && ((RCC->BDCR & RCC_BDCR_RTCEN) != 0U) &&
        (((RCC->BDCR & RCC_BDCR_RTCSEL_Msk) >> RCC_BDCR_RTCSEL_Pos) == rtcsel))
    {
        return 1U;
    }
    for (k = 0U; k < RCC_KERNEL_COUNT; ++k)
    {
        const Rcc_KernelSel *sel = &rcc_kernel_sel[k];
        const uint32_t code = (*Rcc_KernelReg(sel) >> sel->pos) & Rcc_FieldMask(sel);
        uint8_t enabled = 0U;

        if ((sel->src[code] == (uint8_t)src) &&
            (RCC_GetPeriphClock(rcc_kernel_user[k], &enabled) == SPL_OK) && (enabled != 0U))
        {
            return 1U;
        }
    }
    return 0U;
}
#endif /* SPL_CHECK_LEVEL == SPL_CHECK_FULL: Rcc_SourceInUse */

SPL_Status RCC_SetKernelClock(RCC_KernelTypeDef kernel, RCC_KernelSrcTypeDef source)
{
    const Rcc_KernelSel *sel;
    volatile uint32_t *reg;
    uint32_t code;

    /* out-of-table read and a write to an arbitrary register. */
    SPL_REQUIRE((uint32_t)kernel < RCC_KERNEL_COUNT, SPL_ERR_PARAM);
    sel = &rcc_kernel_sel[(uint32_t)kernel];
    if (sel->present == 0U)
    {
        return SPL_ERR_UNSUPPORTED; /* v0.8: selector not on this SKU */
    }
    for (code = 0U; code <= Rcc_FieldMask(sel); ++code)
    {
        if (sel->src[code] == (uint8_t)source)
        {
            break;
        }
    }
    /* a source not in the selector would write a code wider than the field. */
    SPL_REQUIRE(code <= Rcc_FieldMask(sel), SPL_ERR_PARAM);
    reg = Rcc_KernelReg(sel);
    *reg = (*reg & ~(Rcc_FieldMask(sel) << sel->pos)) | (code << sel->pos);
    return SPL_OK;
}

SPL_Status RCC_GetKernelSource(RCC_KernelTypeDef kernel, RCC_KernelSrcTypeDef *source)
{
    const Rcc_KernelSel *sel;
    uint32_t code;

    SPL_REQUIRE((uint32_t)kernel < RCC_KERNEL_COUNT, SPL_ERR_PARAM); /* out-of-table read */
    SPL_REQUIRE(source != NULL, SPL_ERR_PARAM);
    sel = &rcc_kernel_sel[(uint32_t)kernel];
    if (sel->present == 0U)
    {
        return SPL_ERR_UNSUPPORTED;
    }
    code = (*Rcc_KernelReg(sel) >> sel->pos) & Rcc_FieldMask(sel);
    if (sel->src[code] == RCC_KSRC_INVALID)
    {
        return SPL_ERR_HW;
    }
    *source = (RCC_KernelSrcTypeDef)sel->src[code];
    return SPL_OK;
}

SPL_Status RCC_GetKernelClock(RCC_KernelTypeDef kernel, uint32_t hse_hz, uint32_t *hz)
{
    RCC_KernelSrcTypeDef src = RCC_KSRC_PCLK;
    SPL_Status st;

    SPL_REQUIRE(hz != NULL, SPL_ERR_PARAM);
    st = RCC_GetKernelSource(kernel, &src);
    if (st == SPL_OK)
    {
        st = Rcc_KernelSourceHz((uint32_t)src, hse_hz, hz);
    }
#if defined(RCC_CCIPR_RNGDIV_Pos)
    if ((st == SPL_OK) && (kernel == RCC_KERNEL_RNG))
    {
        /* RNGDIV: 00 = 1, 01 = 2, 10 = 4, 11 = 8 (RM0444 5.4.21): a shift, no division. */
        *hz = *hz >> ((RCC->CCIPR & RCC_CCIPR_RNGDIV_Msk) >> RCC_CCIPR_RNGDIV_Pos);
    }
#endif
    return st;
}

SPL_Status RCC_SetRngDivider(uint32_t divider)
{
#if defined(RCC_CCIPR_RNGDIV_Pos)
    uint32_t code;

    if (divider == 1U)
    {
        code = 0U;
    }
    else if (divider == 2U)
    {
        code = 1U;
    }
    else if (divider == 4U)
    {
        code = 2U;
    }
    else if (divider == 8U)
    {
        code = 3U;
    }
    else
    {
        return SPL_ERR_PARAM;
    }
    RCC->CCIPR = (RCC->CCIPR & ~RCC_CCIPR_RNGDIV_Msk) | (code << RCC_CCIPR_RNGDIV_Pos);
    return SPL_OK;
#else
    (void)divider;
    return SPL_ERR_UNSUPPORTED;
#endif
}

SPL_Status RCC_GetRngDivider(uint32_t *divider)
{
    SPL_REQUIRE(divider != NULL, SPL_ERR_PARAM);
#if defined(RCC_CCIPR_RNGDIV_Pos)
    *divider = (uint32_t)1U << ((RCC->CCIPR & RCC_CCIPR_RNGDIV_Msk) >> RCC_CCIPR_RNGDIV_Pos);
    return SPL_OK;
#else
    return SPL_ERR_UNSUPPORTED;
#endif
}

SPL_Status RCC_GetUsartKernelClock(const USART_TypeDef *instance, uint32_t hse_hz,
                                   uint32_t *kernel_hz)
{
    /* Instances with a kernel selector (index = RCC_KernelTypeDef); v0.8: only those the SKU
     * has, and only where CCIPR has the field (USART2 on G03x/G05x and USART3 on G07x are
     * clocked by PCLK). */
    static const struct
    {
        const USART_TypeDef *inst;
        uint32_t kernel;
    } sel_inst[] = {
        {USART1, 0U},
#if defined(RCC_CCIPR_USART2SEL_Pos)
        {USART2, 1U},
#endif
#if defined(RCC_CCIPR_USART3SEL_Pos)
        {USART3, 2U},
#endif
#if defined(LPUART1)
        {LPUART1, 3U},
#endif
#if defined(LPUART2)
        {LPUART2, 4U},
#endif
    };
    static const USART_TypeDef *const pclk_inst[] = {
#if !defined(RCC_CCIPR_USART2SEL_Pos)
        USART2,
#endif
#if defined(USART3) && !defined(RCC_CCIPR_USART3SEL_Pos)
        USART3,
#endif
#if defined(USART4)
        USART4,
#endif
#if defined(USART5)
        USART5,
#endif
#if defined(USART6)
        USART6,
#endif
    };
    uint32_t i;

    SPL_REQUIRE(kernel_hz != NULL, SPL_ERR_PARAM);
    for (i = 0U; i < (uint32_t)(sizeof(sel_inst) / sizeof(sel_inst[0])); ++i)
    {
        if (instance == sel_inst[i].inst)
        {
            return RCC_GetKernelClock((RCC_KernelTypeDef)sel_inst[i].kernel, hse_hz, kernel_hz);
        }
    }
    for (i = 0U; i < (uint32_t)(sizeof(pclk_inst) / sizeof(pclk_inst[0])); ++i)
    {
        if (instance == pclk_inst[i])
        {
            return Rcc_KernelSourceHz(RCC_KSRC_PCLK, hse_hz, kernel_hz);
        }
    }
    return SPL_ERR_PARAM;
}

SPL_Status RCC_EnableCss(void)
{
    RCC->CR = RCC->CR | RCC_CR_CSSON;
    return SPL_OK;
}

SPL_Status RCC_GetCssFlag(uint8_t *set)
{
    SPL_REQUIRE(set != NULL, SPL_ERR_PARAM);
    *set = ((RCC->CIFR & RCC_CIFR_CSSF) != 0U) ? 1U : 0U;
    return SPL_OK;
}

SPL_Status RCC_ClearCssFlag(void)
{
    RCC->CICR = RCC_CICR_CSSC; /* write-1 clear register: never read-modify-write */
    /* Read back: the clear reaches the peripheral before return (METHOD 1.7, issue #23). */
    (void)RCC->CIFR;
    return SPL_OK;
}

SPL_Status RCC_GetResetFlags(uint32_t *flags)
{
    SPL_REQUIRE(flags != NULL, SPL_ERR_PARAM);
    *flags = RCC->CSR & RCC_RESET_FLAGS;
    return SPL_OK;
}

SPL_Status RCC_ClearResetFlags(void)
{
    RCC->CSR = RCC->CSR | RCC_CSR_RMVF;
    /* Read back: the clear reaches the peripheral before return (METHOD 1.7, issue #23). */
    (void)RCC->CSR;
    return SPL_OK;
}
