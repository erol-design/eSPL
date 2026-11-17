/* SPDX-License-Identifier: MIT */
/* GPIO driver (RM0440 Rev 9 section 9). Same GPIO IP on every G4 SKU; GPIOG only where CMSIS
 * defines it. Public contract: stm32g4_gpio.h. */
#include <stddef.h>
#include "stm32g4_gpio.h"
#if SPL_CHECK_LEVEL == SPL_CHECK_FULL
static uint8_t Gpio_PortValid(const GPIO_TypeDef *port)
{
    uint8_t valid = ((port == GPIOA) || (port == GPIOB) || (port == GPIOC) || (port == GPIOD) ||
                     (port == GPIOE) || (port == GPIOF))
                        ? 1U
                        : 0U;
#if defined(GPIOG)
    valid = ((valid != 0U) || (port == GPIOG)) ? 1U : 0U;
#endif
    return valid;
}
#endif
void GPIO_StructInit(GPIO_InitTypeDef *cfg)
{
    if (cfg != NULL)
    {
        cfg->pins = 0U;
        cfg->mode = (uint32_t)GPIO_MODE_INPUT;
        cfg->output_type = (uint32_t)GPIO_OUTPUT_PUSH_PULL;
        cfg->speed = (uint32_t)GPIO_SPEED_LOW;
        cfg->pull = (uint32_t)GPIO_NO_PULL;
        cfg->alternate = 0U;
        cfg->initial_level = 0U;
    }
}
SPL_Status GPIO_Init(GPIO_TypeDef *port, const GPIO_InitTypeDef *cfg)
{
    uint32_t pin;
#if SPL_CHECK_LEVEL == SPL_CHECK_FULL
    SPL_REQUIRE(Gpio_PortValid(port) != 0U, SPL_ERR_PARAM); /* Invalid MMIO base address */
#endif
    SPL_REQUIRE(cfg != NULL, SPL_ERR_PARAM);     /* Invalid configuration pointer */
    SPL_REQUIRE(cfg->pins != 0U, SPL_ERR_PARAM); /* No pin selected */
    SPL_REQUIRE((cfg->pins & 0xFFFF0000UL) == 0U, SPL_ERR_PARAM); /* Pin mask outside 16 bits */
    SPL_REQUIRE(cfg->mode <= 3U, SPL_ERR_PARAM);                  /* Field encoding */
    SPL_REQUIRE(cfg->output_type <= 1U, SPL_ERR_PARAM);           /* Neighbour pin fields */
    SPL_REQUIRE(cfg->speed <= 3U, SPL_ERR_PARAM);                 /* Field encoding */
    SPL_REQUIRE(cfg->pull <= 2U, SPL_ERR_PARAM);                  /* Reserved PUPDR code */
    SPL_REQUIRE(cfg->alternate <= 15U, SPL_ERR_PARAM);            /* Alternate function field */
    SPL_REQUIRE(cfg->initial_level <= 1U, SPL_ERR_PARAM);         /* Binary value */
    /* Analog + pull-up is "Reserved" in RM0440 Table 58: the weak pull-up is
     * disabled by hardware in analog mode (9.3.12). Analog + pull-down is valid. */
    SPL_REQUIRE(
        !((cfg->mode == (uint32_t)GPIO_MODE_ANALOG) && (cfg->pull == (uint32_t)GPIO_PULL_UP)),
        SPL_ERR_PARAM);
    /* LCKK blocks reconfiguration; otherwise a false success would be returned. */
    SPL_REQUIRE(
        !(((port->LCKR & GPIO_LCKR_LCKK) != 0U) && ((port->LCKR & cfg->pins & 0xFFFFUL) != 0U)),
        SPL_ERR_STATE);

    for (pin = 0U; pin < 16U; ++pin)
    {
        uint32_t mask = (uint32_t)1U << pin;
        if (((uint32_t)cfg->pins & mask) != 0U)
        {
            uint32_t pos = pin * 2U;
            uint32_t field = (uint32_t)3U << pos;
            uint32_t afshift = (pin & 7U) * 4U;
            uint32_t afmask = (uint32_t)15U << afshift;
            /* Configure output data type/speed/pull/AF before enabling mode. */
            port->OTYPER = (port->OTYPER & ~mask) | ((uint32_t)cfg->output_type << pin);
            port->OSPEEDR = (port->OSPEEDR & ~field) | ((uint32_t)cfg->speed << pos);
            port->PUPDR = (port->PUPDR & ~field) | ((uint32_t)cfg->pull << pos);
            if (cfg->mode == (uint32_t)GPIO_MODE_AF)
            {
                port->AFR[pin >> 3U] =
                    (port->AFR[pin >> 3U] & ~afmask) | ((uint32_t)cfg->alternate << afshift);
            }
            /* Preload output latch before exposing the pin as an output. */
            if (cfg->mode == (uint32_t)GPIO_MODE_OUTPUT)
            {
                port->BSRR = (cfg->initial_level != 0U) ? mask : (mask << 16U);
            }
            port->MODER = (port->MODER & ~field) | ((uint32_t)cfg->mode << pos);
        }
    }
    return SPL_OK;
}
SPL_Status GPIO_Write(GPIO_TypeDef *port, uint16_t pins, uint8_t high)
{
#if SPL_CHECK_LEVEL == SPL_CHECK_FULL
    SPL_REQUIRE(Gpio_PortValid(port) != 0U, SPL_ERR_PARAM); /* Invalid MMIO base address */
#endif
    SPL_REQUIRE(pins != 0U, SPL_ERR_PARAM); /* No pin selected */
    SPL_REQUIRE(high <= 1U, SPL_ERR_PARAM); /* Binary value */
    port->BSRR = (high != 0U) ? (uint32_t)pins : ((uint32_t)pins << 16U);
    return SPL_OK;
}
SPL_Status GPIO_Read(GPIO_TypeDef *port, uint16_t pins, uint16_t *value)
{
#if SPL_CHECK_LEVEL == SPL_CHECK_FULL
    SPL_REQUIRE(Gpio_PortValid(port) != 0U, SPL_ERR_PARAM); /* Invalid MMIO base address */
#endif
    SPL_REQUIRE(pins != 0U, SPL_ERR_PARAM);    /* No pin selected */
    SPL_REQUIRE(value != NULL, SPL_ERR_PARAM); /* Invalid output pointer */
    *value = (uint16_t)(port->IDR & (uint32_t)pins);
    return SPL_OK;
}
