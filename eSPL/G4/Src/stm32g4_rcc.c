/* SPDX-License-Identifier: MIT */
/* Reset and clock control driver (RM0440 Rev 9 chapter 7). Variants: peripheral enable /
 * reset bits and kernel selectors absent from the CMSIS header of the SKU return
 * SPL_ERR_UNSUPPORTED; the v0.11 clock-tree APIs need the full CMSIS header (RCC_CR_HSION).
 * Public contract: stm32g4_rcc.h, Docs/v0.11_RCC_CONTRACT.md. */
#define SPL_LIBRARY_BUILD 1
#include "stm32g4_rcc.h"
SPL_Status RCC_GpioClockEnable(GPIO_TypeDef *port)
{
    uint32_t bit;

    if (port == GPIOA)
    {
        bit = RCC_AHB2ENR_GPIOAEN;
    }
    else if (port == GPIOB)
    {
        bit = RCC_AHB2ENR_GPIOBEN;
    }
    else if (port == GPIOC)
    {
        bit = RCC_AHB2ENR_GPIOCEN;
    }
    else if (port == GPIOD)
    {
        bit = RCC_AHB2ENR_GPIODEN;
    }
    else if (port == GPIOE)
    {
        bit = RCC_AHB2ENR_GPIOEEN;
    }
    else if (port == GPIOF)
    {
        bit = RCC_AHB2ENR_GPIOFEN;
    }
#if defined(GPIOG) && defined(RCC_AHB2ENR_GPIOGEN)
    else if (port == GPIOG)
    {
        bit = RCC_AHB2ENR_GPIOGEN;
    }
#endif
    else
    {
        return SPL_ERR_PARAM;
    }

    RCC->AHB2ENR |= bit;

    /* The read-back completes the bus clock-enable propagation. */
    (void)RCC->AHB2ENR;

    return SPL_OK;
}

/* STM32G4 AHB/APB prescaler encodings (RM0440 RCC_CFGR). */
static uint32_t Rcc_AhbDivider(uint32_t code)
{
    static const uint16_t divisors[16] = {1U, 1U, 1U, 1U,  1U,  1U,   1U,   1U,
                                          2U, 4U, 8U, 16U, 64U, 128U, 256U, 512U};
    return (uint32_t)divisors[code & 15U];
}

static uint32_t Rcc_ApbDivider(uint32_t code)
{
    static const uint8_t divisors[8] = {1U, 1U, 1U, 1U, 2U, 4U, 8U, 16U};
    return (uint32_t)divisors[code & 7U];
}

SPL_Status RCC_GetClocks(uint32_t hse_hz, RCC_ClocksTypeDef *clocks)
{
    uint32_t cfgr;
    uint32_t src;
    uint32_t sysclk;
    uint32_t ahb_div;
    uint32_t apb1_div;
    uint32_t apb2_div;

    SPL_REQUIRE(clocks != (RCC_ClocksTypeDef *)0, SPL_ERR_PARAM); /* Invalid output pointer */

    cfgr = RCC->CFGR;
    src = cfgr & RCC_CFGR_SWS;
    if (src == RCC_CFGR_SWS_HSI)
    {
        sysclk = 16000000UL;
    }
    else if (src == RCC_CFGR_SWS_HSE)
    {
        if (hse_hz == 0U)
        {
            return SPL_ERR_PARAM;
        }
        sysclk = hse_hz;
    }
    else if (src == RCC_CFGR_SWS_PLL)
    {
        uint32_t pllcfgr;
        uint32_t pll_src;
        uint32_t pll_m;
        uint32_t pll_n;
        uint32_t pll_r;
        uint64_t pll_hz;

        pllcfgr = RCC->PLLCFGR;
        pll_src = pllcfgr & RCC_PLLCFGR_PLLSRC;
        if (pll_src == RCC_PLLCFGR_PLLSRC_HSI)
        {
            pll_hz = 16000000ULL;
        }
        else if (pll_src == RCC_PLLCFGR_PLLSRC_HSE)
        {
            if (hse_hz == 0U)
            {
                return SPL_ERR_PARAM;
            }
            pll_hz = hse_hz;
        }
        else
        {
            return SPL_ERR_UNSUPPORTED;
        }

        pll_m = (uint32_t)(((pllcfgr & RCC_PLLCFGR_PLLM) >> RCC_PLLCFGR_PLLM_Pos) + 1U);
        pll_n = (uint32_t)((pllcfgr & RCC_PLLCFGR_PLLN) >> RCC_PLLCFGR_PLLN_Pos);
        pll_r = (uint32_t)(2U * (((pllcfgr & RCC_PLLCFGR_PLLR) >> RCC_PLLCFGR_PLLR_Pos) + 1U));
        if ((pll_n < 8U) || (pll_n > 127U) || ((pllcfgr & RCC_PLLCFGR_PLLREN) == 0U))
        {
            return SPL_ERR_UNSUPPORTED;
        }
        pll_hz = (pll_hz * (uint64_t)pll_n) / ((uint64_t)pll_m * (uint64_t)pll_r);
        if ((pll_hz == 0ULL) || (pll_hz > UINT32_MAX))
        {
            return SPL_ERR_UNSUPPORTED;
        }
        sysclk = (uint32_t)pll_hz;
    }
    else
    {
        return SPL_ERR_UNSUPPORTED;
    }

    ahb_div = Rcc_AhbDivider((uint32_t)((cfgr & RCC_CFGR_HPRE) >> RCC_CFGR_HPRE_Pos));
    apb1_div = Rcc_ApbDivider((uint32_t)((cfgr & RCC_CFGR_PPRE1) >> RCC_CFGR_PPRE1_Pos));
    apb2_div = Rcc_ApbDivider((uint32_t)((cfgr & RCC_CFGR_PPRE2) >> RCC_CFGR_PPRE2_Pos));

    clocks->sysclk_hz = sysclk;
    clocks->hclk_hz = sysclk / ahb_div;
    clocks->pclk1_hz = clocks->hclk_hz / apb1_div;
    clocks->pclk2_hz = clocks->hclk_hz / apb2_div;
    /* Internal APB timer clocks double on APB prescaler != 1. */
    clocks->tim_apb1_hz = clocks->pclk1_hz * ((apb1_div == 1U) ? 1U : 2U);
    clocks->tim_apb2_hz = clocks->pclk2_hz * ((apb2_div == 1U) ? 1U : 2U);
    return SPL_OK;
}

SPL_Status RCC_GetUsartKernelClock(const USART_TypeDef *instance, uint32_t hse_hz,
                                   uint32_t *kernel_hz)
{
    uint32_t shift;
    uint32_t selector;
    uint32_t selected_hz;
    uint32_t pclk;
    RCC_ClocksTypeDef clocks;
    SPL_Status result;

    SPL_REQUIRE(instance != (const USART_TypeDef *)0,
                SPL_ERR_PARAM);                             /* Invalid MMIO base address */
    SPL_REQUIRE(kernel_hz != (uint32_t *)0, SPL_ERR_PARAM); /* Invalid output pointer */

    if (instance == USART1)
    {
        shift = RCC_CCIPR_USART1SEL_Pos;
    }
    else if (instance == USART2)
    {
        shift = RCC_CCIPR_USART2SEL_Pos;
    }
#if defined(USART3) && defined(RCC_CCIPR_USART3SEL_Pos)
    else if (instance == USART3)
    {
        shift = RCC_CCIPR_USART3SEL_Pos;
    }
#endif
#if defined(UART4) && defined(RCC_CCIPR_UART4SEL_Pos)
    else if (instance == UART4)
    {
        shift = RCC_CCIPR_UART4SEL_Pos;
    }
#endif
#if defined(UART5) && defined(RCC_CCIPR_UART5SEL_Pos)
    else if (instance == UART5)
    {
        shift = RCC_CCIPR_UART5SEL_Pos;
    }
#endif
    else
    {
        return SPL_ERR_PARAM;
    }

    selector = (uint32_t)((RCC->CCIPR >> shift) & 3UL);
    if (selector == 2U)
    {
        *kernel_hz = 16000000UL;
        return SPL_OK;
    }
    if (selector == 3U)
    {
        *kernel_hz = 32768UL;
        return SPL_OK;
    }

    result = RCC_GetClocks(hse_hz, &clocks);
    if (result != SPL_OK)
    {
        return result;
    }

    pclk = (instance == USART1) ? clocks.pclk2_hz : clocks.pclk1_hz;
    if (selector == 0U)
    {
        selected_hz = pclk;
    }
    else
    {
        /* Selector values 2 and 3 returned before clock-tree decoding.
         * Only the SYSCLK selection remains here. */
        selected_hz = clocks.sysclk_hz;
    }

    *kernel_hz = selected_hz;
    return SPL_OK;
}

#if defined(RCC_CR_HSION)
/* ------------------------------------------------------------------------
 * v0.11 clock-tree programming. Contract: Docs/v0.11_RCC_CONTRACT.md.
 * Register references: RM0440 Rev 9 section 7.4.
 * ---------------------------------------------------------------------- */

/* Bus register pairs (ENR / RSTR). */
#define RCC_BUS_AHB1 1U
#define RCC_BUS_AHB2 2U
#define RCC_BUS_AHB3 3U
#define RCC_BUS_APB1R1 4U
#define RCC_BUS_APB1R2 5U
#define RCC_BUS_APB2 6U

typedef struct
{
    uint32_t en;  /* ENR bit (0: peripheral absent on this SKU) */
    uint32_t rst; /* RSTR bit (0: no reset bit) */
    uint32_t bus; /* RCC_BUS_xxx */
} Rcc_PeriphEntry;

/* One entry per identifier, present only where CMSIS defines the enable bit. */
#define RCC_ENTRY(id, en, rst, bus) [id] = {(en), (rst), (bus)}

static const Rcc_PeriphEntry rcc_periph[RCC_PERIPH_COUNT] = {
#if defined(RCC_AHB1ENR_DMA1EN)
    RCC_ENTRY(RCC_PERIPH_DMA1, RCC_AHB1ENR_DMA1EN, RCC_AHB1RSTR_DMA1RST, RCC_BUS_AHB1),
#endif
#if defined(RCC_AHB1ENR_DMA2EN)
    RCC_ENTRY(RCC_PERIPH_DMA2, RCC_AHB1ENR_DMA2EN, RCC_AHB1RSTR_DMA2RST, RCC_BUS_AHB1),
#endif
#if defined(RCC_AHB1ENR_DMAMUX1EN)
    RCC_ENTRY(RCC_PERIPH_DMAMUX1, RCC_AHB1ENR_DMAMUX1EN, RCC_AHB1RSTR_DMAMUX1RST, RCC_BUS_AHB1),
#endif
#if defined(RCC_AHB1ENR_CORDICEN)
    RCC_ENTRY(RCC_PERIPH_CORDIC, RCC_AHB1ENR_CORDICEN, RCC_AHB1RSTR_CORDICRST, RCC_BUS_AHB1),
#endif
#if defined(RCC_AHB1ENR_FMACEN)
    RCC_ENTRY(RCC_PERIPH_FMAC, RCC_AHB1ENR_FMACEN, RCC_AHB1RSTR_FMACRST, RCC_BUS_AHB1),
#endif
#if defined(RCC_AHB1ENR_FLASHEN)
    RCC_ENTRY(RCC_PERIPH_FLASH, RCC_AHB1ENR_FLASHEN, RCC_AHB1RSTR_FLASHRST, RCC_BUS_AHB1),
#endif
#if defined(RCC_AHB1ENR_CRCEN)
    RCC_ENTRY(RCC_PERIPH_CRC, RCC_AHB1ENR_CRCEN, RCC_AHB1RSTR_CRCRST, RCC_BUS_AHB1),
#endif
#if defined(RCC_AHB2ENR_GPIOAEN)
    RCC_ENTRY(RCC_PERIPH_GPIOA, RCC_AHB2ENR_GPIOAEN, RCC_AHB2RSTR_GPIOARST, RCC_BUS_AHB2),
#endif
#if defined(RCC_AHB2ENR_GPIOBEN)
    RCC_ENTRY(RCC_PERIPH_GPIOB, RCC_AHB2ENR_GPIOBEN, RCC_AHB2RSTR_GPIOBRST, RCC_BUS_AHB2),
#endif
#if defined(RCC_AHB2ENR_GPIOCEN)
    RCC_ENTRY(RCC_PERIPH_GPIOC, RCC_AHB2ENR_GPIOCEN, RCC_AHB2RSTR_GPIOCRST, RCC_BUS_AHB2),
#endif
#if defined(RCC_AHB2ENR_GPIODEN)
    RCC_ENTRY(RCC_PERIPH_GPIOD, RCC_AHB2ENR_GPIODEN, RCC_AHB2RSTR_GPIODRST, RCC_BUS_AHB2),
#endif
#if defined(RCC_AHB2ENR_GPIOEEN)
    RCC_ENTRY(RCC_PERIPH_GPIOE, RCC_AHB2ENR_GPIOEEN, RCC_AHB2RSTR_GPIOERST, RCC_BUS_AHB2),
#endif
#if defined(RCC_AHB2ENR_GPIOFEN)
    RCC_ENTRY(RCC_PERIPH_GPIOF, RCC_AHB2ENR_GPIOFEN, RCC_AHB2RSTR_GPIOFRST, RCC_BUS_AHB2),
#endif
#if defined(RCC_AHB2ENR_GPIOGEN)
    RCC_ENTRY(RCC_PERIPH_GPIOG, RCC_AHB2ENR_GPIOGEN, RCC_AHB2RSTR_GPIOGRST, RCC_BUS_AHB2),
#endif
#if defined(RCC_AHB2ENR_ADC12EN)
    RCC_ENTRY(RCC_PERIPH_ADC12, RCC_AHB2ENR_ADC12EN, RCC_AHB2RSTR_ADC12RST, RCC_BUS_AHB2),
#endif
#if defined(RCC_AHB2ENR_ADC345EN)
    RCC_ENTRY(RCC_PERIPH_ADC345, RCC_AHB2ENR_ADC345EN, RCC_AHB2RSTR_ADC345RST, RCC_BUS_AHB2),
#endif
#if defined(RCC_AHB2ENR_DAC1EN)
    RCC_ENTRY(RCC_PERIPH_DAC1, RCC_AHB2ENR_DAC1EN, RCC_AHB2RSTR_DAC1RST, RCC_BUS_AHB2),
#endif
#if defined(RCC_AHB2ENR_DAC2EN)
    RCC_ENTRY(RCC_PERIPH_DAC2, RCC_AHB2ENR_DAC2EN, RCC_AHB2RSTR_DAC2RST, RCC_BUS_AHB2),
#endif
#if defined(RCC_AHB2ENR_DAC3EN)
    RCC_ENTRY(RCC_PERIPH_DAC3, RCC_AHB2ENR_DAC3EN, RCC_AHB2RSTR_DAC3RST, RCC_BUS_AHB2),
#endif
#if defined(RCC_AHB2ENR_DAC4EN)
    RCC_ENTRY(RCC_PERIPH_DAC4, RCC_AHB2ENR_DAC4EN, RCC_AHB2RSTR_DAC4RST, RCC_BUS_AHB2),
#endif
#if defined(RCC_AHB2ENR_AESEN)
    RCC_ENTRY(RCC_PERIPH_AES, RCC_AHB2ENR_AESEN, RCC_AHB2RSTR_AESRST, RCC_BUS_AHB2),
#endif
#if defined(RCC_AHB2ENR_RNGEN)
    RCC_ENTRY(RCC_PERIPH_RNG, RCC_AHB2ENR_RNGEN, RCC_AHB2RSTR_RNGRST, RCC_BUS_AHB2),
#endif
#if defined(RCC_AHB3ENR_FMCEN)
    RCC_ENTRY(RCC_PERIPH_FMC, RCC_AHB3ENR_FMCEN, RCC_AHB3RSTR_FMCRST, RCC_BUS_AHB3),
#endif
#if defined(RCC_AHB3ENR_QSPIEN)
    RCC_ENTRY(RCC_PERIPH_QSPI, RCC_AHB3ENR_QSPIEN, RCC_AHB3RSTR_QSPIRST, RCC_BUS_AHB3),
#endif
#if defined(RCC_APB1ENR1_TIM2EN)
    RCC_ENTRY(RCC_PERIPH_TIM2, RCC_APB1ENR1_TIM2EN, RCC_APB1RSTR1_TIM2RST, RCC_BUS_APB1R1),
#endif
#if defined(RCC_APB1ENR1_TIM3EN)
    RCC_ENTRY(RCC_PERIPH_TIM3, RCC_APB1ENR1_TIM3EN, RCC_APB1RSTR1_TIM3RST, RCC_BUS_APB1R1),
#endif
#if defined(RCC_APB1ENR1_TIM4EN)
    RCC_ENTRY(RCC_PERIPH_TIM4, RCC_APB1ENR1_TIM4EN, RCC_APB1RSTR1_TIM4RST, RCC_BUS_APB1R1),
#endif
#if defined(RCC_APB1ENR1_TIM5EN)
    RCC_ENTRY(RCC_PERIPH_TIM5, RCC_APB1ENR1_TIM5EN, RCC_APB1RSTR1_TIM5RST, RCC_BUS_APB1R1),
#endif
#if defined(RCC_APB1ENR1_TIM6EN)
    RCC_ENTRY(RCC_PERIPH_TIM6, RCC_APB1ENR1_TIM6EN, RCC_APB1RSTR1_TIM6RST, RCC_BUS_APB1R1),
#endif
#if defined(RCC_APB1ENR1_TIM7EN)
    RCC_ENTRY(RCC_PERIPH_TIM7, RCC_APB1ENR1_TIM7EN, RCC_APB1RSTR1_TIM7RST, RCC_BUS_APB1R1),
#endif
#if defined(RCC_APB1ENR1_CRSEN)
    RCC_ENTRY(RCC_PERIPH_CRS, RCC_APB1ENR1_CRSEN, RCC_APB1RSTR1_CRSRST, RCC_BUS_APB1R1),
#endif
#if defined(RCC_APB1ENR1_RTCAPBEN)
    RCC_ENTRY(RCC_PERIPH_RTCAPB, RCC_APB1ENR1_RTCAPBEN, 0U, RCC_BUS_APB1R1),
#endif
#if defined(RCC_APB1ENR1_WWDGEN)
    RCC_ENTRY(RCC_PERIPH_WWDG, RCC_APB1ENR1_WWDGEN, 0U, RCC_BUS_APB1R1),
#endif
#if defined(RCC_APB1ENR1_SPI2EN)
    RCC_ENTRY(RCC_PERIPH_SPI2, RCC_APB1ENR1_SPI2EN, RCC_APB1RSTR1_SPI2RST, RCC_BUS_APB1R1),
#endif
#if defined(RCC_APB1ENR1_SPI3EN)
    RCC_ENTRY(RCC_PERIPH_SPI3, RCC_APB1ENR1_SPI3EN, RCC_APB1RSTR1_SPI3RST, RCC_BUS_APB1R1),
#endif
#if defined(RCC_APB1ENR1_USART2EN)
    RCC_ENTRY(RCC_PERIPH_USART2, RCC_APB1ENR1_USART2EN, RCC_APB1RSTR1_USART2RST, RCC_BUS_APB1R1),
#endif
#if defined(RCC_APB1ENR1_USART3EN)
    RCC_ENTRY(RCC_PERIPH_USART3, RCC_APB1ENR1_USART3EN, RCC_APB1RSTR1_USART3RST, RCC_BUS_APB1R1),
#endif
#if defined(RCC_APB1ENR1_UART4EN)
    RCC_ENTRY(RCC_PERIPH_UART4, RCC_APB1ENR1_UART4EN, RCC_APB1RSTR1_UART4RST, RCC_BUS_APB1R1),
#endif
#if defined(RCC_APB1ENR1_UART5EN)
    RCC_ENTRY(RCC_PERIPH_UART5, RCC_APB1ENR1_UART5EN, RCC_APB1RSTR1_UART5RST, RCC_BUS_APB1R1),
#endif
#if defined(RCC_APB1ENR1_I2C1EN)
    RCC_ENTRY(RCC_PERIPH_I2C1, RCC_APB1ENR1_I2C1EN, RCC_APB1RSTR1_I2C1RST, RCC_BUS_APB1R1),
#endif
#if defined(RCC_APB1ENR1_I2C2EN)
    RCC_ENTRY(RCC_PERIPH_I2C2, RCC_APB1ENR1_I2C2EN, RCC_APB1RSTR1_I2C2RST, RCC_BUS_APB1R1),
#endif
#if defined(RCC_APB1ENR1_USBEN)
    RCC_ENTRY(RCC_PERIPH_USB, RCC_APB1ENR1_USBEN, RCC_APB1RSTR1_USBRST, RCC_BUS_APB1R1),
#endif
#if defined(RCC_APB1ENR1_FDCANEN)
    RCC_ENTRY(RCC_PERIPH_FDCAN, RCC_APB1ENR1_FDCANEN, RCC_APB1RSTR1_FDCANRST, RCC_BUS_APB1R1),
#endif
#if defined(RCC_APB1ENR1_PWREN)
    RCC_ENTRY(RCC_PERIPH_PWR, RCC_APB1ENR1_PWREN, RCC_APB1RSTR1_PWRRST, RCC_BUS_APB1R1),
#endif
#if defined(RCC_APB1ENR1_I2C3EN)
    RCC_ENTRY(RCC_PERIPH_I2C3, RCC_APB1ENR1_I2C3EN, RCC_APB1RSTR1_I2C3RST, RCC_BUS_APB1R1),
#endif
#if defined(RCC_APB1ENR1_LPTIM1EN)
    RCC_ENTRY(RCC_PERIPH_LPTIM1, RCC_APB1ENR1_LPTIM1EN, RCC_APB1RSTR1_LPTIM1RST, RCC_BUS_APB1R1),
#endif
#if defined(RCC_APB1ENR2_LPUART1EN)
    RCC_ENTRY(RCC_PERIPH_LPUART1, RCC_APB1ENR2_LPUART1EN, RCC_APB1RSTR2_LPUART1RST, RCC_BUS_APB1R2),
#endif
#if defined(RCC_APB1ENR2_I2C4EN)
    RCC_ENTRY(RCC_PERIPH_I2C4, RCC_APB1ENR2_I2C4EN, RCC_APB1RSTR2_I2C4RST, RCC_BUS_APB1R2),
#endif
#if defined(RCC_APB1ENR2_UCPD1EN)
    RCC_ENTRY(RCC_PERIPH_UCPD1, RCC_APB1ENR2_UCPD1EN, RCC_APB1RSTR2_UCPD1RST, RCC_BUS_APB1R2),
#endif
#if defined(RCC_APB2ENR_SYSCFGEN)
    RCC_ENTRY(RCC_PERIPH_SYSCFG, RCC_APB2ENR_SYSCFGEN, RCC_APB2RSTR_SYSCFGRST, RCC_BUS_APB2),
#endif
#if defined(RCC_APB2ENR_TIM1EN)
    RCC_ENTRY(RCC_PERIPH_TIM1, RCC_APB2ENR_TIM1EN, RCC_APB2RSTR_TIM1RST, RCC_BUS_APB2),
#endif
#if defined(RCC_APB2ENR_SPI1EN)
    RCC_ENTRY(RCC_PERIPH_SPI1, RCC_APB2ENR_SPI1EN, RCC_APB2RSTR_SPI1RST, RCC_BUS_APB2),
#endif
#if defined(RCC_APB2ENR_TIM8EN)
    RCC_ENTRY(RCC_PERIPH_TIM8, RCC_APB2ENR_TIM8EN, RCC_APB2RSTR_TIM8RST, RCC_BUS_APB2),
#endif
#if defined(RCC_APB2ENR_USART1EN)
    RCC_ENTRY(RCC_PERIPH_USART1, RCC_APB2ENR_USART1EN, RCC_APB2RSTR_USART1RST, RCC_BUS_APB2),
#endif
#if defined(RCC_APB2ENR_SPI4EN)
    RCC_ENTRY(RCC_PERIPH_SPI4, RCC_APB2ENR_SPI4EN, RCC_APB2RSTR_SPI4RST, RCC_BUS_APB2),
#endif
#if defined(RCC_APB2ENR_TIM15EN)
    RCC_ENTRY(RCC_PERIPH_TIM15, RCC_APB2ENR_TIM15EN, RCC_APB2RSTR_TIM15RST, RCC_BUS_APB2),
#endif
#if defined(RCC_APB2ENR_TIM16EN)
    RCC_ENTRY(RCC_PERIPH_TIM16, RCC_APB2ENR_TIM16EN, RCC_APB2RSTR_TIM16RST, RCC_BUS_APB2),
#endif
#if defined(RCC_APB2ENR_TIM17EN)
    RCC_ENTRY(RCC_PERIPH_TIM17, RCC_APB2ENR_TIM17EN, RCC_APB2RSTR_TIM17RST, RCC_BUS_APB2),
#endif
#if defined(RCC_APB2ENR_TIM20EN)
    RCC_ENTRY(RCC_PERIPH_TIM20, RCC_APB2ENR_TIM20EN, RCC_APB2RSTR_TIM20RST, RCC_BUS_APB2),
#endif
#if defined(RCC_APB2ENR_SAI1EN)
    RCC_ENTRY(RCC_PERIPH_SAI1, RCC_APB2ENR_SAI1EN, RCC_APB2RSTR_SAI1RST, RCC_BUS_APB2),
#endif
#if defined(RCC_APB2ENR_HRTIM1EN)
    RCC_ENTRY(RCC_PERIPH_HRTIM1, RCC_APB2ENR_HRTIM1EN, RCC_APB2RSTR_HRTIM1RST, RCC_BUS_APB2),
#endif
};

/* Kernel selector classes; each row lists the source per 2-bit encoding
 * (0 = reserved encoding). RM0440 7.4.26 (CCIPR) and 7.4.30 (CCIPR2). */
#define RCC_KCLASS_UART 1U  /* USART1..3, UART4/5, LPUART1 */
#define RCC_KCLASS_I2C 2U   /* I2C1..4 */
#define RCC_KCLASS_LPTIM 3U /* LPTIM1 */
#define RCC_KCLASS_SAI 4U   /* SAI1, I2S23 */
#define RCC_KCLASS_FDCAN 5U /* FDCAN */
#define RCC_KCLASS_CLK48 6U /* USB / RNG 48 MHz */
#define RCC_KCLASS_ADC 7U   /* ADC12, ADC345 */
#define RCC_KCLASS_QSPI 8U  /* QUADSPI */
#define RCC_KCLASS_COUNT 9U

static const uint8_t rcc_kernel_map[][4] = {
    {0U, 0U, 0U, 0U},
    {RCC_KCLK_PCLK, RCC_KCLK_SYSCLK, RCC_KCLK_HSI16, RCC_KCLK_LSE},
    {RCC_KCLK_PCLK, RCC_KCLK_SYSCLK, RCC_KCLK_HSI16, 0U},
    {RCC_KCLK_PCLK, RCC_KCLK_LSI, RCC_KCLK_HSI16, RCC_KCLK_LSE},
    {RCC_KCLK_SYSCLK, RCC_KCLK_PLLQ, RCC_KCLK_I2S_CKIN, RCC_KCLK_HSI16},
    {RCC_KCLK_HSE, RCC_KCLK_PLLQ, RCC_KCLK_PCLK, 0U},
    {RCC_KCLK_HSI48, 0U, RCC_KCLK_PLLQ, 0U},
    {RCC_KCLK_NONE, RCC_KCLK_PLLP, RCC_KCLK_SYSCLK, 0U},
    {RCC_KCLK_SYSCLK, RCC_KCLK_HSI16, RCC_KCLK_PLLQ, 0U},
};

/* Compile-time check (CCL-2 YAP-03): one rcc_kernel_map row per class 0..RCC_KCLASS_COUNT-1,
 * every RCC_KCLASS_* value indexes a row, and each row has one entry per code of a 2-bit
 * selector field (mask 3, codes 0..3, as used by RCC_SetKernelClock/RCC_GetKernelSource). */
typedef char Rcc_KernelMapCheck
    [(((sizeof(rcc_kernel_map) / sizeof(rcc_kernel_map[0])) == RCC_KCLASS_COUNT) &&
      (RCC_KCLASS_UART < RCC_KCLASS_COUNT) && (RCC_KCLASS_I2C < RCC_KCLASS_COUNT) &&
      (RCC_KCLASS_LPTIM < RCC_KCLASS_COUNT) && (RCC_KCLASS_SAI < RCC_KCLASS_COUNT) &&
      (RCC_KCLASS_FDCAN < RCC_KCLASS_COUNT) && (RCC_KCLASS_CLK48 < RCC_KCLASS_COUNT) &&
      (RCC_KCLASS_ADC < RCC_KCLASS_COUNT) && (RCC_KCLASS_QSPI < RCC_KCLASS_COUNT) &&
      ((sizeof(rcc_kernel_map[0]) / sizeof(rcc_kernel_map[0][0])) == 4U) &&
      ((RCC_CCIPR_I2C1SEL_Msk >> RCC_CCIPR_I2C1SEL_Pos) == 3U))
         ? 1
         : -1];

typedef struct
{
    uint32_t cls;    /* RCC_KCLASS_xxx (0: selector absent on this SKU) */
    uint32_t pos;    /* 2-bit field position (CMSIS _Pos) */
    uint32_t ccipr2; /* 1: field in CCIPR2 */
    uint32_t apb;    /* PCLK source: 1 = PCLK1, 2 = PCLK2 (0: no PCLK option) */
} Rcc_KernelEntry;

#define RCC_KENTRY(id, cls, pos, reg2, apb) [id] = {(cls), (pos), (reg2), (apb)}

static const Rcc_KernelEntry rcc_kernel[RCC_KERNEL_COUNT] = {
#if defined(RCC_CCIPR_USART1SEL_Pos)
    RCC_KENTRY(RCC_KERNEL_USART1, RCC_KCLASS_UART, RCC_CCIPR_USART1SEL_Pos, 0U, 2U),
#endif
#if defined(RCC_CCIPR_USART2SEL_Pos)
    RCC_KENTRY(RCC_KERNEL_USART2, RCC_KCLASS_UART, RCC_CCIPR_USART2SEL_Pos, 0U, 1U),
#endif
#if defined(RCC_CCIPR_USART3SEL_Pos)
    RCC_KENTRY(RCC_KERNEL_USART3, RCC_KCLASS_UART, RCC_CCIPR_USART3SEL_Pos, 0U, 1U),
#endif
#if defined(RCC_CCIPR_UART4SEL_Pos)
    RCC_KENTRY(RCC_KERNEL_UART4, RCC_KCLASS_UART, RCC_CCIPR_UART4SEL_Pos, 0U, 1U),
#endif
#if defined(RCC_CCIPR_UART5SEL_Pos)
    RCC_KENTRY(RCC_KERNEL_UART5, RCC_KCLASS_UART, RCC_CCIPR_UART5SEL_Pos, 0U, 1U),
#endif
#if defined(RCC_CCIPR_LPUART1SEL_Pos)
    RCC_KENTRY(RCC_KERNEL_LPUART1, RCC_KCLASS_UART, RCC_CCIPR_LPUART1SEL_Pos, 0U, 1U),
#endif
#if defined(RCC_CCIPR_I2C1SEL_Pos)
    RCC_KENTRY(RCC_KERNEL_I2C1, RCC_KCLASS_I2C, RCC_CCIPR_I2C1SEL_Pos, 0U, 1U),
#endif
#if defined(RCC_CCIPR_I2C2SEL_Pos)
    RCC_KENTRY(RCC_KERNEL_I2C2, RCC_KCLASS_I2C, RCC_CCIPR_I2C2SEL_Pos, 0U, 1U),
#endif
#if defined(RCC_CCIPR_I2C3SEL_Pos)
    RCC_KENTRY(RCC_KERNEL_I2C3, RCC_KCLASS_I2C, RCC_CCIPR_I2C3SEL_Pos, 0U, 1U),
#endif
#if defined(RCC_CCIPR2_I2C4SEL_Pos)
    RCC_KENTRY(RCC_KERNEL_I2C4, RCC_KCLASS_I2C, RCC_CCIPR2_I2C4SEL_Pos, 1U, 1U),
#endif
#if defined(RCC_CCIPR_LPTIM1SEL_Pos)
    RCC_KENTRY(RCC_KERNEL_LPTIM1, RCC_KCLASS_LPTIM, RCC_CCIPR_LPTIM1SEL_Pos, 0U, 1U),
#endif
#if defined(RCC_CCIPR_SAI1SEL_Pos)
    RCC_KENTRY(RCC_KERNEL_SAI1, RCC_KCLASS_SAI, RCC_CCIPR_SAI1SEL_Pos, 0U, 0U),
#endif
#if defined(RCC_CCIPR_I2S23SEL_Pos)
    RCC_KENTRY(RCC_KERNEL_I2S23, RCC_KCLASS_SAI, RCC_CCIPR_I2S23SEL_Pos, 0U, 0U),
#endif
#if defined(RCC_CCIPR_FDCANSEL_Pos)
    RCC_KENTRY(RCC_KERNEL_FDCAN, RCC_KCLASS_FDCAN, RCC_CCIPR_FDCANSEL_Pos, 0U, 1U),
#endif
#if defined(RCC_CCIPR_CLK48SEL_Pos)
    RCC_KENTRY(RCC_KERNEL_CLK48, RCC_KCLASS_CLK48, RCC_CCIPR_CLK48SEL_Pos, 0U, 0U),
#endif
#if defined(RCC_CCIPR_ADC12SEL_Pos)
    RCC_KENTRY(RCC_KERNEL_ADC12, RCC_KCLASS_ADC, RCC_CCIPR_ADC12SEL_Pos, 0U, 0U),
#endif
#if defined(RCC_CCIPR_ADC345SEL_Pos)
    RCC_KENTRY(RCC_KERNEL_ADC345, RCC_KCLASS_ADC, RCC_CCIPR_ADC345SEL_Pos, 0U, 0U),
#endif
#if defined(RCC_CCIPR2_QSPISEL_Pos)
    RCC_KENTRY(RCC_KERNEL_QSPI, RCC_KCLASS_QSPI, RCC_CCIPR2_QSPISEL_Pos, 1U, 0U),
#endif
};

static volatile uint32_t *Rcc_EnrRegister(uint32_t bus)
{
    volatile uint32_t *reg;

    switch (bus)
    {
    case RCC_BUS_AHB1:
        reg = &RCC->AHB1ENR;
        break;
    case RCC_BUS_AHB2:
        reg = &RCC->AHB2ENR;
        break;
    case RCC_BUS_AHB3:
        reg = &RCC->AHB3ENR;
        break;
    case RCC_BUS_APB1R1:
        reg = &RCC->APB1ENR1;
        break;
    case RCC_BUS_APB1R2:
        reg = &RCC->APB1ENR2;
        break;
    default:
        reg = &RCC->APB2ENR;
        break;
    }
    return reg;
}

static volatile uint32_t *Rcc_RstrRegister(uint32_t bus)
{
    volatile uint32_t *reg;

    switch (bus)
    {
    case RCC_BUS_AHB1:
        reg = &RCC->AHB1RSTR;
        break;
    case RCC_BUS_AHB2:
        reg = &RCC->AHB2RSTR;
        break;
    case RCC_BUS_AHB3:
        reg = &RCC->AHB3RSTR;
        break;
    case RCC_BUS_APB1R1:
        reg = &RCC->APB1RSTR1;
        break;
    case RCC_BUS_APB1R2:
        reg = &RCC->APB1RSTR2;
        break;
    default:
        reg = &RCC->APB2RSTR;
        break;
    }
    return reg;
}

/* Validates a peripheral identifier and returns its table entry. */
static SPL_Status Rcc_PeriphLookup(RCC_PeriphTypeDef periph, const Rcc_PeriphEntry **entry)
{
    if ((uint32_t)periph >= (uint32_t)RCC_PERIPH_COUNT)
    {
        return SPL_ERR_PARAM;
    }
    if (rcc_periph[periph].en == 0U)
    {
        return SPL_ERR_UNSUPPORTED;
    }
    *entry = &rcc_periph[periph];
    return SPL_OK;
}

/* ---- Oscillators ---- */

/* 1 when the PLL is on or still locked (PLLON or PLLRDY). */
static uint32_t Rcc_PllActive(void)
{
    return ((RCC->CR & (RCC_CR_PLLON | RCC_CR_PLLRDY)) != 0U) ? 1U : 0U;
}

/* 1 when SYSCLK is, or is being switched to, sw_value (CFGR.SW or CFGR.SWS). */
static uint32_t Rcc_SysclkUses(uint32_t sw_value)
{
    uint32_t cfgr = RCC->CFGR;
    uint32_t sw = (cfgr & RCC_CFGR_SW) >> RCC_CFGR_SW_Pos;
    uint32_t sws = (cfgr & RCC_CFGR_SWS) >> RCC_CFGR_SWS_Pos;

    return ((sw == sw_value) || (sws == sw_value)) ? 1U : 0U;
}

SPL_Status RCC_StartOsc(RCC_OscTypeDef osc)
{
    switch (osc)
    {
    case RCC_OSC_HSI16:
        RCC->CR |= RCC_CR_HSION;
        break;
    case RCC_OSC_HSE:
        RCC->CR |= RCC_CR_HSEON;
        break;
    case RCC_OSC_LSI:
        /* RMVF is masked so the write cannot clear the reset flags. */
        RCC->CSR = (RCC->CSR & ~RCC_CSR_RMVF) | RCC_CSR_LSION;
        break;
    case RCC_OSC_HSI48:
        RCC->CRRCR |= RCC_CRRCR_HSI48ON;
        break;
    default:
        return SPL_ERR_PARAM;
    }
    return SPL_OK;
}

/* Peripherals clocked by each kernel selector (RCC_PERIPH_COUNT = none). */
static const uint8_t rcc_kernel_users[RCC_KERNEL_COUNT][2] = {
    [RCC_KERNEL_USART1] = {RCC_PERIPH_USART1, RCC_PERIPH_COUNT},
    [RCC_KERNEL_USART2] = {RCC_PERIPH_USART2, RCC_PERIPH_COUNT},
    [RCC_KERNEL_USART3] = {RCC_PERIPH_USART3, RCC_PERIPH_COUNT},
    [RCC_KERNEL_UART4] = {RCC_PERIPH_UART4, RCC_PERIPH_COUNT},
    [RCC_KERNEL_UART5] = {RCC_PERIPH_UART5, RCC_PERIPH_COUNT},
    [RCC_KERNEL_LPUART1] = {RCC_PERIPH_LPUART1, RCC_PERIPH_COUNT},
    [RCC_KERNEL_I2C1] = {RCC_PERIPH_I2C1, RCC_PERIPH_COUNT},
    [RCC_KERNEL_I2C2] = {RCC_PERIPH_I2C2, RCC_PERIPH_COUNT},
    [RCC_KERNEL_I2C3] = {RCC_PERIPH_I2C3, RCC_PERIPH_COUNT},
    [RCC_KERNEL_I2C4] = {RCC_PERIPH_I2C4, RCC_PERIPH_COUNT},
    [RCC_KERNEL_LPTIM1] = {RCC_PERIPH_LPTIM1, RCC_PERIPH_COUNT},
    [RCC_KERNEL_SAI1] = {RCC_PERIPH_SAI1, RCC_PERIPH_COUNT},
    [RCC_KERNEL_I2S23] = {RCC_PERIPH_SPI2, RCC_PERIPH_SPI3},
    [RCC_KERNEL_FDCAN] = {RCC_PERIPH_FDCAN, RCC_PERIPH_COUNT},
    [RCC_KERNEL_CLK48] = {RCC_PERIPH_USB, RCC_PERIPH_RNG},
    [RCC_KERNEL_ADC12] = {RCC_PERIPH_ADC12, RCC_PERIPH_COUNT},
    [RCC_KERNEL_ADC345] = {RCC_PERIPH_ADC345, RCC_PERIPH_COUNT},
    [RCC_KERNEL_QSPI] = {RCC_PERIPH_QSPI, RCC_PERIPH_COUNT},
};

/* Compile-time check (CCL-2 YAP-03): every RCC_PERIPH_* id and the RCC_PERIPH_COUNT "none"
 * marker fit the uint8_t entries of rcc_kernel_users. */
typedef char Rcc_KernelUsersCheck[((uint32_t)RCC_PERIPH_COUNT <= UINT8_MAX) ? 1 : -1];

/* 1 when src is the kernel clock of a peripheral whose bus clock is on, or
 * (rtcsel != 0) the RTC clock with RTCEN = 1 (RM0440 7.4.26, 7.4.30, 7.4.27).
 * A selector left on src with the peripheral clock off is not a user. */
static uint32_t Rcc_SourceInUse(RCC_KernelSrcTypeDef src, uint32_t rtcsel)
{
    uint32_t k;
    uint32_t u;
    RCC_KernelSrcTypeDef current;
    uint8_t enabled;

    if ((rtcsel != 0U) && ((RCC->BDCR & RCC_BDCR_RTCEN) != 0U) &&
        (((RCC->BDCR & RCC_BDCR_RTCSEL_Msk) >> RCC_BDCR_RTCSEL_Pos) == rtcsel))
    {
        return 1U;
    }
    for (k = 0U; k < (uint32_t)RCC_KERNEL_COUNT; ++k)
    {
        if ((RCC_GetKernelSource((RCC_KernelTypeDef)k, &current) != SPL_OK) || (current != src))
        {
            continue;
        }
        for (u = 0U; u < 2U; ++u)
        {
            enabled = 0U;
            if ((rcc_kernel_users[k][u] != (uint8_t)RCC_PERIPH_COUNT) &&
                (RCC_GetPeriphClock((RCC_PeriphTypeDef)rcc_kernel_users[k][u], &enabled) ==
                 SPL_OK) &&
                (enabled != 0U))
            {
                return 1U;
            }
        }
    }
    return 0U;
}

SPL_Status RCC_StopOsc(RCC_OscTypeDef osc)
{
    uint32_t pllsrc = RCC->PLLCFGR & RCC_PLLCFGR_PLLSRC;

    switch (osc)
    {
    case RCC_OSC_HSI16:
        if ((Rcc_SysclkUses(RCC_CFGR_SW_HSI) != 0U) ||
            ((Rcc_PllActive() != 0U) && (pllsrc == RCC_PLLCFGR_PLLSRC_HSI)) ||
            (Rcc_SourceInUse(RCC_KCLK_HSI16, 0U) != 0U))
        {
            return SPL_ERR_STATE;
        }
        RCC->CR &= ~RCC_CR_HSION;
        break;
    case RCC_OSC_HSE:
        if ((Rcc_SysclkUses(RCC_CFGR_SW_HSE) != 0U) ||
            ((Rcc_PllActive() != 0U) && (pllsrc == RCC_PLLCFGR_PLLSRC_HSE)) ||
            (Rcc_SourceInUse(RCC_KCLK_HSE, 3U) != 0U)) /* RTCSEL 11 = HSE / 32 */
        {
            return SPL_ERR_STATE;
        }
        RCC->CR &= ~RCC_CR_HSEON;
        break;
    case RCC_OSC_LSI:
        if (Rcc_SourceInUse(RCC_KCLK_LSI, 2U) != 0U) /* RTCSEL 10 = LSI */
        {
            return SPL_ERR_STATE;
        }
        RCC->CSR = RCC->CSR & ~(RCC_CSR_RMVF | RCC_CSR_LSION);
        break;
    case RCC_OSC_HSI48:
        if (Rcc_SourceInUse(RCC_KCLK_HSI48, 0U) != 0U)
        {
            return SPL_ERR_STATE;
        }
        RCC->CRRCR &= ~RCC_CRRCR_HSI48ON;
        break;
    default:
        return SPL_ERR_PARAM;
    }
    return SPL_OK;
}

SPL_Status RCC_GetOscReady(RCC_OscTypeDef osc, uint8_t *ready)
{
    uint32_t value;
    uint32_t on_bit;
    uint32_t rdy_bit;

    SPL_REQUIRE(ready != (uint8_t *)0, SPL_ERR_PARAM); /* Invalid output address */
    switch (osc)
    {
    case RCC_OSC_HSI16:
        value = RCC->CR;
        on_bit = RCC_CR_HSION;
        rdy_bit = RCC_CR_HSIRDY;
        break;
    case RCC_OSC_HSE:
        value = RCC->CR;
        on_bit = RCC_CR_HSEON;
        rdy_bit = RCC_CR_HSERDY;
        break;
    case RCC_OSC_LSI:
        value = RCC->CSR;
        on_bit = RCC_CSR_LSION;
        rdy_bit = RCC_CSR_LSIRDY;
        break;
    case RCC_OSC_HSI48:
        value = RCC->CRRCR;
        on_bit = RCC_CRRCR_HSI48ON;
        rdy_bit = RCC_CRRCR_HSI48RDY;
        break;
    default:
        return SPL_ERR_PARAM;
    }

    *ready = ((value & rdy_bit) != 0U) ? 1U : 0U;
    if ((value & (on_bit | rdy_bit)) == 0U)
    {
        return SPL_ERR_STATE;
    }
    return SPL_OK;
}

SPL_Status RCC_SetHseBypass(uint8_t bypass)
{
    uint32_t cr;
    uint32_t want;

    SPL_REQUIRE(bypass <= 1U, SPL_ERR_PARAM); /* HSEBYP accepts 0/1 only */
    cr = RCC->CR;
    want = (bypass != 0U) ? RCC_CR_HSEBYP : 0U;
    if ((cr & RCC_CR_HSEBYP) == want)
    {
        return SPL_OK;
    }
    /* RM0440 7.4.1: HSEBYP can be written only if the HSE oscillator is disabled. */
    if ((cr & RCC_CR_HSEON) != 0U)
    {
        return SPL_ERR_STATE;
    }
    if ((cr & RCC_CR_HSERDY) != 0U)
    {
        return SPL_BUSY;
    }
    /* Fresh read-modify-write: CR bits changed by hardware or another writer
     * since the check above must not be overwritten by the snapshot. */
    RCC->CR = (RCC->CR & ~RCC_CR_HSEBYP) | want;
    return SPL_OK;
}

/* ---- PLL ---- */

static uint32_t Rcc_IsEvenDivider(uint32_t value)
{
    return ((value == 2U) || (value == 4U) || (value == 6U) || (value == 8U)) ? 1U : 0U;
}

SPL_Status RCC_ConfigPll(const RCC_PllConfigTypeDef *config, uint32_t hse_hz)
{
    uint64_t src_hz;
    uint64_t m;
    uint64_t src_n;
    uint32_t pllcfgr;
    uint32_t cr;

    SPL_REQUIRE(config != (const RCC_PllConfigTypeDef *)0,
                SPL_ERR_PARAM); /* Invalid PLL config address */
    if (config->source == (uint32_t)RCC_PLLSRC_HSI16)
    {
        src_hz = RCC_HSI16_HZ;
    }
    else if (config->source == (uint32_t)RCC_PLLSRC_HSE)
    {
        SPL_REQUIRE((hse_hz >= RCC_HSE_MIN_HZ) && (hse_hz <= RCC_HSE_MAX_HZ),
                    SPL_ERR_PARAM); /* HSE frequency sets the PLL operating limits. */
        src_hz = hse_hz;
    }
    else
    {
        return SPL_ERR_PARAM;
    }
    SPL_REQUIRE((config->m >= 1U) && (config->m <= 16U) && (config->n >= 8U) && (config->n <= 127U),
                SPL_ERR_PARAM); /* PLLM/PLLN encoding and divide-by-zero risk. */
    SPL_REQUIRE((config->p >= 2U) && (config->p <= 31U) && (Rcc_IsEvenDivider(config->q) != 0U) &&
                    (Rcc_IsEvenDivider(config->r) != 0U),
                SPL_ERR_PARAM); /* PLL output dividers must be valid. */
    SPL_REQUIRE((config->p_enable <= 1U) && (config->q_enable <= 1U) && (config->r_enable <= 1U),
                SPL_ERR_PARAM); /* PLL output selection must be unambiguous. */

    /* Exact integer range checks: f_in = src / m, f_vco = src * n / m,
     * f_out = f_vco / div. Every output divider is checked, enabled or not. */
    m = config->m;
    src_n = src_hz * (uint64_t)config->n;
    SPL_REQUIRE((src_hz >= (uint64_t)RCC_PLL_VCO_IN_MIN_HZ * m) &&
                    (src_hz <= (uint64_t)RCC_PLL_VCO_IN_MAX_HZ * m) &&
                    (src_n >= (uint64_t)RCC_PLL_VCO_MIN_HZ * m) &&
                    (src_n <= (uint64_t)RCC_PLL_VCO_MAX_HZ * m) &&
                    (src_n <= (uint64_t)RCC_PLL_OUT_MAX_HZ * m * config->p) &&
                    (src_n <= (uint64_t)RCC_PLL_OUT_MAX_HZ * m * config->q) &&
                    (src_n <= (uint64_t)RCC_PLL_OUT_MAX_HZ * m * config->r),
                SPL_ERR_PARAM); /* VCO and all PLL outputs within electrical limits. */

    /* RM0440 7.2.4: configuration only while the PLL is fully stopped. */
    cr = RCC->CR;
    if ((cr & RCC_CR_PLLON) != 0U)
    {
        return SPL_ERR_STATE;
    }
    if ((cr & RCC_CR_PLLRDY) != 0U)
    {
        return SPL_BUSY;
    }

    pllcfgr = (config->source == (uint32_t)RCC_PLLSRC_HSE) ? RCC_PLLCFGR_PLLSRC_HSE
                                                           : RCC_PLLCFGR_PLLSRC_HSI;
    pllcfgr |= ((config->m - 1U) << RCC_PLLCFGR_PLLM_Pos) & RCC_PLLCFGR_PLLM;
    pllcfgr |= (config->n << RCC_PLLCFGR_PLLN_Pos) & RCC_PLLCFGR_PLLN;
    pllcfgr |= (config->p << RCC_PLLCFGR_PLLPDIV_Pos) & RCC_PLLCFGR_PLLPDIV;
    pllcfgr |= (((config->q / 2U) - 1U) << RCC_PLLCFGR_PLLQ_Pos) & RCC_PLLCFGR_PLLQ;
    pllcfgr |= (((config->r / 2U) - 1U) << RCC_PLLCFGR_PLLR_Pos) & RCC_PLLCFGR_PLLR;
    if (config->p_enable != 0U)
    {
        pllcfgr |= RCC_PLLCFGR_PLLPEN;
    }
    if (config->q_enable != 0U)
    {
        pllcfgr |= RCC_PLLCFGR_PLLQEN;
    }
    if (config->r_enable != 0U)
    {
        pllcfgr |= RCC_PLLCFGR_PLLREN;
    }
    RCC->PLLCFGR = pllcfgr;
    return SPL_OK;
}

SPL_Status RCC_EnablePll(void)
{
    uint32_t cr = RCC->CR;
    uint32_t pllcfgr = RCC->PLLCFGR;
    uint32_t pllsrc = pllcfgr & RCC_PLLCFGR_PLLSRC;
    uint32_t pll_n = (pllcfgr & RCC_PLLCFGR_PLLN) >> RCC_PLLCFGR_PLLN_Pos;
    uint32_t on_bit;
    uint32_t rdy_bit;

    if ((cr & RCC_CR_PLLON) != 0U)
    {
        return SPL_OK;
    }
    if (pllsrc == RCC_PLLCFGR_PLLSRC_HSI)
    {
        on_bit = RCC_CR_HSION;
        rdy_bit = RCC_CR_HSIRDY;
    }
    else if (pllsrc == RCC_PLLCFGR_PLLSRC_HSE)
    {
        on_bit = RCC_CR_HSEON;
        rdy_bit = RCC_CR_HSERDY;
    }
    else
    {
        return SPL_ERR_STATE;
    }
    if ((pll_n < 8U) || (pll_n > 127U))
    {
        return SPL_ERR_STATE;
    }
    if ((cr & rdy_bit) == 0U)
    {
        return ((cr & on_bit) != 0U) ? SPL_BUSY : SPL_ERR_STATE;
    }
    /* Fresh read-modify-write of PLLON only: a CSS event after the snapshot
     * clears HSEON / sets HSION (RM0440 7.2.10, 7.4.1); writing the snapshot
     * back would re-enable the failed HSE. */
    RCC->CR |= RCC_CR_PLLON;
    return SPL_OK;
}

SPL_Status RCC_DisablePll(void)
{
    /* RM0440 7.4.1: PLLON cannot be reset while the PLL clock is SYSCLK. */
    if ((Rcc_SysclkUses(RCC_CFGR_SW_PLL) != 0U) || (Rcc_SourceInUse(RCC_KCLK_PLLP, 0U) != 0U) ||
        (Rcc_SourceInUse(RCC_KCLK_PLLQ, 0U) != 0U))
    {
        return SPL_ERR_STATE;
    }
    RCC->CR &= ~RCC_CR_PLLON;
    return SPL_OK;
}

SPL_Status RCC_GetPllReady(uint8_t *ready)
{
    uint32_t cr;

    SPL_REQUIRE(ready != (uint8_t *)0, SPL_ERR_PARAM); /* Invalid output address */
    cr = RCC->CR;
    *ready = ((cr & RCC_CR_PLLRDY) != 0U) ? 1U : 0U;
    if ((cr & (RCC_CR_PLLON | RCC_CR_PLLRDY)) == 0U)
    {
        return SPL_ERR_STATE;
    }
    return SPL_OK;
}

/* ---- SYSCLK and prescalers ---- */

/* Status of a pending switch (SW != SWS) to sw: BUSY while its target can
 * still become ready, STATE when the target is off (PLL: PLLON=0 or
 * PLLREN=0) or SW holds the reserved code 00. */
static SPL_Status Rcc_PendingSwitchStatus(uint32_t cr, uint32_t sw)
{
    uint32_t on;

    if (sw == RCC_CFGR_SW_HSI)
    {
        on = cr & RCC_CR_HSION;
    }
    else if (sw == RCC_CFGR_SW_HSE)
    {
        on = cr & RCC_CR_HSEON;
    }
    else if (sw == RCC_CFGR_SW_PLL)
    {
        on = ((RCC->PLLCFGR & RCC_PLLCFGR_PLLREN) != 0U) ? (cr & RCC_CR_PLLON) : 0U;
    }
    else
    {
        on = 0U;
    }
    return (on != 0U) ? SPL_BUSY : SPL_ERR_STATE;
}

SPL_Status RCC_SetSysclkSource(RCC_SysclkTypeDef source)
{
    uint32_t cr;
    uint32_t cfgr;
    uint32_t sw_value;
    uint32_t on_bit;
    uint32_t rdy_bit;

    switch (source)
    {
    case RCC_SYSCLK_HSI16:
        sw_value = RCC_CFGR_SW_HSI;
        on_bit = RCC_CR_HSION;
        rdy_bit = RCC_CR_HSIRDY;
        break;
    case RCC_SYSCLK_HSE:
        sw_value = RCC_CFGR_SW_HSE;
        on_bit = RCC_CR_HSEON;
        rdy_bit = RCC_CR_HSERDY;
        break;
    case RCC_SYSCLK_PLL:
        sw_value = RCC_CFGR_SW_PLL;
        on_bit = RCC_CR_PLLON;
        rdy_bit = RCC_CR_PLLRDY;
        break;
    default:
        return SPL_ERR_PARAM;
    }

    cr = RCC->CR;
    cfgr = RCC->CFGR;
    if (((cfgr & RCC_CFGR_SW) >> RCC_CFGR_SW_Pos) != ((cfgr & RCC_CFGR_SWS) >> RCC_CFGR_SWS_Pos))
    {
        /* RM0440 7.2.7: a pending switch completes only when its target becomes
         * ready. Target off -> it never completes: STATE, not BUSY. */
        return Rcc_PendingSwitchStatus(cr, (cfgr & RCC_CFGR_SW) >> RCC_CFGR_SW_Pos);
    }
    if ((cr & rdy_bit) == 0U)
    {
        return ((cr & on_bit) != 0U) ? SPL_BUSY : SPL_ERR_STATE;
    }
    if ((source == RCC_SYSCLK_PLL) && ((RCC->PLLCFGR & RCC_PLLCFGR_PLLREN) == 0U))
    {
        return SPL_ERR_STATE;
    }
    RCC->CFGR = (cfgr & ~RCC_CFGR_SW) | (sw_value << RCC_CFGR_SW_Pos);
    return SPL_OK;
}

SPL_Status RCC_GetSysclkSource(RCC_SysclkTypeDef *source)
{
    uint32_t sws;

    SPL_REQUIRE(source != (RCC_SysclkTypeDef *)0, SPL_ERR_PARAM); /* Output pointer invalid */
    sws = RCC->CFGR & RCC_CFGR_SWS;
    if (sws == RCC_CFGR_SWS_HSI)
    {
        *source = RCC_SYSCLK_HSI16;
    }
    else if (sws == RCC_CFGR_SWS_HSE)
    {
        *source = RCC_SYSCLK_HSE;
    }
    else if (sws == RCC_CFGR_SWS_PLL)
    {
        *source = RCC_SYSCLK_PLL;
    }
    else
    {
        return SPL_ERR_UNSUPPORTED;
    }
    return SPL_OK;
}

SPL_Status RCC_SetAhbPrescaler(uint32_t divider)
{
    uint32_t code;

    switch (divider)
    {
    case 1U:
        code = RCC_CFGR_HPRE_DIV1;
        break;
    case 2U:
        code = RCC_CFGR_HPRE_DIV2;
        break;
    case 4U:
        code = RCC_CFGR_HPRE_DIV4;
        break;
    case 8U:
        code = RCC_CFGR_HPRE_DIV8;
        break;
    case 16U:
        code = RCC_CFGR_HPRE_DIV16;
        break;
    case 64U:
        code = RCC_CFGR_HPRE_DIV64;
        break;
    case 128U:
        code = RCC_CFGR_HPRE_DIV128;
        break;
    case 256U:
        code = RCC_CFGR_HPRE_DIV256;
        break;
    case 512U:
        code = RCC_CFGR_HPRE_DIV512;
        break;
    default:
        return SPL_ERR_PARAM;
    }
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_HPRE) | code;
    /* RM0440 7.4.3 HPRE caution: read back so the new value is taken into account. */
    (void)RCC->CFGR;
    return SPL_OK;
}

/* PPRE1 encoding for an APB divider; returns 0 for an invalid divider. */
static uint32_t Rcc_ApbCode(uint32_t divider, uint32_t *code)
{
    switch (divider)
    {
    case 1U:
        *code = RCC_CFGR_PPRE1_DIV1;
        break;
    case 2U:
        *code = RCC_CFGR_PPRE1_DIV2;
        break;
    case 4U:
        *code = RCC_CFGR_PPRE1_DIV4;
        break;
    case 8U:
        *code = RCC_CFGR_PPRE1_DIV8;
        break;
    case 16U:
        *code = RCC_CFGR_PPRE1_DIV16;
        break;
    default:
        return 0U;
    }
    return 1U;
}

SPL_Status RCC_SetApbPrescalers(uint32_t apb1_divider, uint32_t apb2_divider)
{
    uint32_t code1 = 0U;
    uint32_t code2 = 0U;

    if ((Rcc_ApbCode(apb1_divider, &code1) == 0U) || (Rcc_ApbCode(apb2_divider, &code2) == 0U))
    {
        return SPL_ERR_PARAM;
    }
    /* PPRE1 and PPRE2 share the same 3-bit encoding (RM0440 7.4.3). */
    code2 = (code2 >> RCC_CFGR_PPRE1_Pos) << RCC_CFGR_PPRE2_Pos;
    RCC->CFGR = (RCC->CFGR & ~(RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2)) | code1 | code2;
    (void)RCC->CFGR;
    return SPL_OK;
}

/* ---- Peripheral clocks and resets ---- */

SPL_Status RCC_EnablePeriphClock(RCC_PeriphTypeDef periph)
{
    const Rcc_PeriphEntry *entry = (const Rcc_PeriphEntry *)0;
    volatile uint32_t *enr;
    SPL_Status result = Rcc_PeriphLookup(periph, &entry);

    if (result != SPL_OK)
    {
        return result;
    }
    enr = Rcc_EnrRegister(entry->bus);
    *enr |= entry->en;
    /* The read-back completes the bus clock-enable propagation. */
    (void)*enr;
    return SPL_OK;
}

SPL_Status RCC_DisablePeriphClock(RCC_PeriphTypeDef periph)
{
    const Rcc_PeriphEntry *entry = (const Rcc_PeriphEntry *)0;
    volatile uint32_t *enr;
    SPL_Status result = Rcc_PeriphLookup(periph, &entry);

    if (result != SPL_OK)
    {
        return result;
    }
    /* RM0440 7.4.14: FLASHEN may be cleared only in flash power-down mode. */
    if (periph == RCC_PERIPH_FLASH)
    {
        return SPL_ERR_PARAM;
    }
    enr = Rcc_EnrRegister(entry->bus);
    *enr &= ~entry->en;
    return SPL_OK;
}

SPL_Status RCC_GetPeriphClock(RCC_PeriphTypeDef periph, uint8_t *enabled)
{
    const Rcc_PeriphEntry *entry = (const Rcc_PeriphEntry *)0;
    SPL_Status result;

    SPL_REQUIRE(enabled != (uint8_t *)0,
                SPL_ERR_PARAM); /* Peripheral clock output pointer invalid */
    result = Rcc_PeriphLookup(periph, &entry);
    if (result != SPL_OK)
    {
        return result;
    }
    *enabled = ((*Rcc_EnrRegister(entry->bus) & entry->en) != 0U) ? 1U : 0U;
    return SPL_OK;
}

static SPL_Status Rcc_PeriphReset(RCC_PeriphTypeDef periph, uint32_t assert_reset)
{
    const Rcc_PeriphEntry *entry = (const Rcc_PeriphEntry *)0;
    volatile uint32_t *rstr;
    SPL_Status result = Rcc_PeriphLookup(periph, &entry);

    if (result != SPL_OK)
    {
        return result;
    }
    /* RM0440 7.4.8: FLASHRST may be set only in flash power-down mode. */
    if (periph == RCC_PERIPH_FLASH)
    {
        return SPL_ERR_PARAM;
    }
    if (entry->rst == 0U)
    {
        return SPL_ERR_UNSUPPORTED;
    }
    rstr = Rcc_RstrRegister(entry->bus);
    if (assert_reset != 0U)
    {
        *rstr |= entry->rst;
    }
    else
    {
        *rstr &= ~entry->rst;
    }
    return SPL_OK;
}

SPL_Status RCC_AssertPeriphReset(RCC_PeriphTypeDef periph)
{
    return Rcc_PeriphReset(periph, 1U);
}

SPL_Status RCC_ReleasePeriphReset(RCC_PeriphTypeDef periph)
{
    return Rcc_PeriphReset(periph, 0U);
}

/* ---- Kernel clocks ---- */

static SPL_Status Rcc_KernelLookup(RCC_KernelTypeDef kernel, const Rcc_KernelEntry **entry)
{
    if ((uint32_t)kernel >= (uint32_t)RCC_KERNEL_COUNT)
    {
        return SPL_ERR_PARAM;
    }
    if (rcc_kernel[kernel].cls == 0U)
    {
        return SPL_ERR_UNSUPPORTED;
    }
    *entry = &rcc_kernel[kernel];
    return SPL_OK;
}

SPL_Status RCC_SetKernelClock(RCC_KernelTypeDef kernel, RCC_KernelSrcTypeDef source)
{
    const Rcc_KernelEntry *entry = (const Rcc_KernelEntry *)0;
    volatile uint32_t *reg;
    uint32_t code;
    SPL_Status result = Rcc_KernelLookup(kernel, &entry);

    if (result != SPL_OK)
    {
        return result;
    }
    for (code = 0U; code < 4U; ++code)
    {
        if ((rcc_kernel_map[entry->cls][code] != 0U) &&
            ((uint32_t)rcc_kernel_map[entry->cls][code] == (uint32_t)source))
        {
            break;
        }
    }
    if (code == 4U)
    {
        return SPL_ERR_PARAM;
    }
    reg = (entry->ccipr2 != 0U) ? &RCC->CCIPR2 : &RCC->CCIPR;
    *reg = (*reg & ~((uint32_t)3U << entry->pos)) | (code << entry->pos);
    return SPL_OK;
}

SPL_Status RCC_GetKernelSource(RCC_KernelTypeDef kernel, RCC_KernelSrcTypeDef *source)
{
    const Rcc_KernelEntry *entry = (const Rcc_KernelEntry *)0;
    uint32_t value;
    uint32_t src;
    SPL_Status result;

    SPL_REQUIRE(source != (RCC_KernelSrcTypeDef *)0,
                SPL_ERR_PARAM); /* Kernel source output pointer invalid */
    result = Rcc_KernelLookup(kernel, &entry);
    if (result != SPL_OK)
    {
        return result;
    }
    value = (entry->ccipr2 != 0U) ? RCC->CCIPR2 : RCC->CCIPR;
    src = rcc_kernel_map[entry->cls][(value >> entry->pos) & 3U];
    if (src == 0U)
    {
        return SPL_ERR_UNSUPPORTED;
    }
    *source = (RCC_KernelSrcTypeDef)src;
    return SPL_OK;
}

/* PLL "P" (q_output = 0) or "Q" (q_output = 1) frequency from PLLCFGR. */
static SPL_Status Rcc_PllOutputHz(uint32_t q_output, uint32_t hse_hz, uint32_t *hz)
{
    uint32_t pllcfgr = RCC->PLLCFGR;
    uint32_t pllsrc = pllcfgr & RCC_PLLCFGR_PLLSRC;
    uint32_t pll_m = ((pllcfgr & RCC_PLLCFGR_PLLM) >> RCC_PLLCFGR_PLLM_Pos) + 1U;
    uint32_t pll_n = (pllcfgr & RCC_PLLCFGR_PLLN) >> RCC_PLLCFGR_PLLN_Pos;
    uint32_t div;
    uint64_t src_hz;

    if (q_output != 0U)
    {
        if ((pllcfgr & RCC_PLLCFGR_PLLQEN) == 0U)
        {
            return SPL_ERR_STATE;
        }
        div = 2U * (((pllcfgr & RCC_PLLCFGR_PLLQ) >> RCC_PLLCFGR_PLLQ_Pos) + 1U);
    }
    else
    {
        if ((pllcfgr & RCC_PLLCFGR_PLLPEN) == 0U)
        {
            return SPL_ERR_STATE;
        }
        div = (pllcfgr & RCC_PLLCFGR_PLLPDIV) >> RCC_PLLCFGR_PLLPDIV_Pos;
        if (div == 0U)
        {
            /* RM0440 7.4.4: PLLPDIV = 0 -> the PLLP bit selects 7 or 17. */
            div = ((pllcfgr & RCC_PLLCFGR_PLLP) != 0U) ? 17U : 7U;
        }
        else if (div == 1U)
        {
            return SPL_ERR_UNSUPPORTED; /* reserved encoding */
        }
        else
        {
            /* PLLPDIV = 2..31 is the divider itself. */
        }
    }
    if (pllsrc == RCC_PLLCFGR_PLLSRC_HSI)
    {
        src_hz = RCC_HSI16_HZ;
    }
    else if (pllsrc == RCC_PLLCFGR_PLLSRC_HSE)
    {
        if (hse_hz == 0U)
        {
            return SPL_ERR_PARAM;
        }
        src_hz = hse_hz;
    }
    else
    {
        return SPL_ERR_STATE;
    }
    if ((pll_n < 8U) || (pll_n > 127U))
    {
        return SPL_ERR_UNSUPPORTED;
    }
    *hz = (uint32_t)((src_hz * (uint64_t)pll_n) / ((uint64_t)pll_m * (uint64_t)div));
    return SPL_OK;
}

SPL_Status RCC_GetKernelClock(RCC_KernelTypeDef kernel, uint32_t hse_hz, uint32_t *hz)
{
    const Rcc_KernelEntry *entry = (const Rcc_KernelEntry *)0;
    RCC_KernelSrcTypeDef source = RCC_KCLK_NONE;
    RCC_ClocksTypeDef clocks;
    uint32_t value = 0U;
    SPL_Status result;

    SPL_REQUIRE(hz != (uint32_t *)0, SPL_ERR_PARAM); /* Kernel frequency output pointer invalid */
    result = Rcc_KernelLookup(kernel, &entry);
    if (result != SPL_OK)
    {
        return result;
    }
    result = RCC_GetKernelSource(kernel, &source);
    if (result != SPL_OK)
    {
        return result;
    }

    switch (source)
    {
    case RCC_KCLK_PCLK:
    case RCC_KCLK_SYSCLK:
        result = RCC_GetClocks(hse_hz, &clocks);
        if (result == SPL_OK)
        {
            if (source == RCC_KCLK_SYSCLK)
            {
                value = clocks.sysclk_hz;
            }
            else
            {
                value = (entry->apb == 2U) ? clocks.pclk2_hz : clocks.pclk1_hz;
            }
        }
        break;
    case RCC_KCLK_HSI16:
        value = RCC_HSI16_HZ;
        break;
    case RCC_KCLK_LSE:
        value = RCC_LSE_HZ;
        break;
    case RCC_KCLK_LSI:
        value = RCC_LSI_HZ;
        break;
    case RCC_KCLK_HSI48:
        value = RCC_HSI48_HZ;
        break;
    case RCC_KCLK_HSE:
        if (hse_hz == 0U)
        {
            result = SPL_ERR_PARAM;
        }
        value = hse_hz;
        break;
    case RCC_KCLK_PLLQ:
        result = Rcc_PllOutputHz(1U, hse_hz, &value);
        break;
    case RCC_KCLK_PLLP:
        result = Rcc_PllOutputHz(0U, hse_hz, &value);
        break;
    case RCC_KCLK_I2S_CKIN:
        result = SPL_ERR_UNSUPPORTED;
        break;
    default:
        /* RCC_KCLK_NONE: ADC kernel clock switched off. */
        result = SPL_ERR_STATE;
        break;
    }
    if (result != SPL_OK)
    {
        return result;
    }
    *hz = value;
    return SPL_OK;
}

/* ---- MCO, CSS, reset flags ---- */

SPL_Status RCC_ConfigMco(RCC_McoSourceTypeDef source, uint32_t divider)
{
    uint32_t pre;
    uint32_t cfgr;

    switch (source)
    {
    case RCC_MCO_OFF:
    case RCC_MCO_SYSCLK:
    case RCC_MCO_HSI16:
    case RCC_MCO_HSE:
    case RCC_MCO_PLL:
    case RCC_MCO_LSI:
    case RCC_MCO_LSE:
    case RCC_MCO_HSI48:
        break;
    default:
        return SPL_ERR_PARAM;
    }
    switch (divider)
    {
    case 1U:
        pre = RCC_CFGR_MCOPRE_DIV1;
        break;
    case 2U:
        pre = RCC_CFGR_MCOPRE_DIV2;
        break;
    case 4U:
        pre = RCC_CFGR_MCOPRE_DIV4;
        break;
    case 8U:
        pre = RCC_CFGR_MCOPRE_DIV8;
        break;
    case 16U:
        pre = RCC_CFGR_MCOPRE_DIV16;
        break;
    default:
        return SPL_ERR_PARAM;
    }
    /* RM0440 7.4.3: change MCOPRE while MCO is off, then select the source. */
    cfgr = RCC->CFGR & ~(RCC_CFGR_MCOSEL | RCC_CFGR_MCOPRE);
    RCC->CFGR = cfgr | pre;
    RCC->CFGR = cfgr | pre | (((uint32_t)source << RCC_CFGR_MCOSEL_Pos) & RCC_CFGR_MCOSEL);
    return SPL_OK;
}

SPL_Status RCC_EnableCss(void)
{
    RCC->CR |= RCC_CR_CSSON;
    return SPL_OK;
}

SPL_Status RCC_GetCssFlag(uint8_t *set)
{
    SPL_REQUIRE(set != (uint8_t *)0, SPL_ERR_PARAM); /* CSS fault output pointer invalid */
    *set = ((RCC->CIFR & RCC_CIFR_CSSF) != 0U) ? 1U : 0U;
    return SPL_OK;
}

SPL_Status RCC_ClearCssFlag(void)
{
    /* CICR: write 1 to clear, 0 has no effect - write the single bit only. */
    RCC->CICR = RCC_CICR_CSSC;
    /* Read back: the clear reaches the peripheral before return (METHOD 1.7, issue #23). */
    (void)RCC->CIFR;
    return SPL_OK;
}

SPL_Status RCC_GetResetFlags(uint32_t *flags)
{
    SPL_REQUIRE(flags != (uint32_t *)0,
                SPL_ERR_PARAM); /* Reset cause output pointer invalid */
    *flags = RCC->CSR & RCC_RESET_FLAGS;
    return SPL_OK;
}

SPL_Status RCC_ClearResetFlags(void)
{
    /* Besides RMVF only LSION is writable in CSR; the flags are read-only. */
    RCC->CSR = (RCC->CSR & RCC_CSR_LSION) | RCC_CSR_RMVF;
    /* Read back: the clear reaches the peripheral before return (METHOD 1.7, issue #23). */
    (void)RCC->CSR;
    return SPL_OK;
}

#endif /* RCC_CR_HSION */
