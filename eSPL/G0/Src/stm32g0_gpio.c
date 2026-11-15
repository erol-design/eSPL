/* Ported from STM32G4 SPL Src/stm32g4_gpio.c (commit 5bcc2d7 tree, G4 v0.13.2); reviewed against
 * RM0444 Rev 6 section 7. G0 differences: AF0..AF7 only, analog + pull Reserved. */
/* Kutuphane birimi: spl_check.h karisma korumasi referansini bu birim de uretir; uretmeyen tek
 * birim stm32g0_check.c (SPL_CHECK_LEVEL_DEFINER). BULGU-02, CHECK_LEVEL.md 3.2. */
#define SPL_LIBRARY_BUILD 1
#include <stddef.h>
#include "stm32g0_gpio.h"

#define GPIO_AF_MAX 7U

/* Denetim seviyesi (A4 dalga 2, Docs/CHECK_LEVEL.md 5.4 GPIO tablosu): SPL_REQUIRE = normal sinif
 * (yalniz FULL), SPL_REQUIRE_SAFE = SAFE sinif (FULL ve MIX). Butun makrolar ilk MMIO yazimindan
 * once (METHOD 1.3). */

#if SPL_CHECK_LEVEL <= SPL_CHECK_MIX
/* SAFE: baska adres rastgele yazim / okuma. Yalniz denetimde; FAST'ta derlenmez. Saf. */
static uint8_t Gpio_PortValid(const GPIO_TypeDef *port)
{
    /* v0.8: GPIOE only where the CMSIS header defines it (group 0BX). */
#if defined(GPIOE)
    if (port == GPIOE)
    {
        return 1U;
    }
#endif
    return ((port == GPIOA) || (port == GPIOB) || (port == GPIOC) || (port == GPIOD) ||
            (port == GPIOF))
               ? 1U
               : 0U;
}
#define GPIO_PORT_OK(port) (Gpio_PortValid(port) != 0U)
#else
#define GPIO_PORT_OK(port) (1) /* kapali seviyede degerlendirilmez */
#endif

void GPIO_StructInit(GPIO_InitTypeDef *cfg)
{
    if (cfg != NULL)
    {
        cfg->pins = 0U;
        cfg->mode = (uint32_t)GPIO_MODE_INPUT;
        cfg->output_type = (uint32_t)GPIO_OUTPUT_PUSH_PULL;
        cfg->speed = (uint32_t)GPIO_SPEED_VERY_LOW;
        cfg->pull = (uint32_t)GPIO_NO_PULL;
        cfg->alternate = 0U;
        cfg->initial_level = 0U;
    }
}

SPL_Status GPIO_Init(GPIO_TypeDef *port, const GPIO_InitTypeDef *cfg)
{
    uint32_t pin;

    SPL_REQUIRE_SAFE(GPIO_PORT_OK(port), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(cfg != NULL, SPL_ERR_PARAM);
    /* normal: pins = 0 etkisiz; ust 16 bit dongude atlanir; initial_level > 1 yuksek gibi. */
    SPL_REQUIRE((cfg->pins != 0U) && ((cfg->pins & 0xFFFF0000UL) == 0U) &&
                    (cfg->initial_level <= 1U),
                SPL_ERR_PARAM);
    /* SAFE: komsu pinin alanina tasma ya da ayrilmis kod (PUPD 11, AF8..15). */
    SPL_REQUIRE_SAFE((cfg->mode <= 3U) && (cfg->output_type <= 1U) && (cfg->speed <= 3U) &&
                         (cfg->pull <= 2U) && (cfg->alternate <= GPIO_AF_MAX),
                     SPL_ERR_PARAM);
    /* RM0444 Table 44: MODE = 11 (analog) with PUPD 01, 10 or 11 is Reserved. SAFE. */
    SPL_REQUIRE_SAFE((cfg->mode != (uint32_t)GPIO_MODE_ANALOG) ||
                         (cfg->pull == (uint32_t)GPIO_NO_PULL),
                     SPL_ERR_PARAM);
    /* LCKK = 1: the lock sequence completed; locked pins stay locked until reset. SAFE: yazim yok
     * sayilir, OK donerdi (CHECK_LEVEL.md 5.3). */
    SPL_REQUIRE_SAFE(((port->LCKR & GPIO_LCKR_LCKK) == 0U) ||
                         ((port->LCKR & cfg->pins & 0xFFFFUL) == 0U),
                     SPL_ERR_STATE);

    for (pin = 0U; pin < 16U; ++pin)
    {
        const uint32_t mask = (uint32_t)1U << pin;
        if ((cfg->pins & mask) != 0U)
        {
            const uint32_t pos = pin * 2U;
            const uint32_t field = 3U << pos;
            const uint32_t afshift = (pin & 7U) * 4U;
            const uint32_t afmask = 15U << afshift;

            port->OTYPER = (port->OTYPER & ~mask) | (cfg->output_type << pin);
            port->OSPEEDR = (port->OSPEEDR & ~field) | (cfg->speed << pos);
            port->PUPDR = (port->PUPDR & ~field) | (cfg->pull << pos);
            if (cfg->mode == (uint32_t)GPIO_MODE_AF)
            {
                port->AFR[pin >> 3U] =
                    (port->AFR[pin >> 3U] & ~afmask) | (cfg->alternate << afshift);
            }
            /* Preload the output latch before the pin becomes an output. */
            if (cfg->mode == (uint32_t)GPIO_MODE_OUTPUT)
            {
                port->BSRR = (cfg->initial_level != 0U) ? mask : (mask << 16U);
            }
            port->MODER = (port->MODER & ~field) | (cfg->mode << pos);
        }
    }
    return SPL_OK;
}

SPL_Status GPIO_Write(GPIO_TypeDef *port, uint16_t pins, uint8_t high)
{
    SPL_REQUIRE_SAFE(GPIO_PORT_OK(port), SPL_ERR_PARAM);
    SPL_REQUIRE((pins != 0U) && (high <= 1U), SPL_ERR_PARAM); /* normal */
    port->BSRR = (high != 0U) ? (uint32_t)pins : ((uint32_t)pins << 16U);
    return SPL_OK;
}

SPL_Status GPIO_Read(GPIO_TypeDef *port, uint16_t pins, uint16_t *value)
{
    SPL_REQUIRE_SAFE(GPIO_PORT_OK(port), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(value != NULL, SPL_ERR_PARAM);
    SPL_REQUIRE(pins != 0U, SPL_ERR_PARAM); /* normal */
    *value = (uint16_t)(port->IDR & (uint32_t)pins);
    return SPL_OK;
}
