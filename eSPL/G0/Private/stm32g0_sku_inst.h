/* SPDX-License-Identifier: MIT */
/* Internal per-SKU instance tests (v0.8, Docs/v0.8_SKU_PROFILES.md). Not a public API.
 * SPL_G0_IS_<X>(p) is ((p) == X) when the CMSIS header of the selected SKU defines instance X and
 * the constant 0 otherwise, so that a driver can name every family instance without #if blocks
 * and a pointer can never match an instance the device does not have. */
#ifndef STM32G0_SKU_INST_H
#define STM32G0_SKU_INST_H

#include "stm32g0xx.h"

#if defined(TIM2)
#define SPL_G0_IS_TIM2(p) ((p) == TIM2)
#else
#define SPL_G0_IS_TIM2(p) (0)
#endif
#if defined(TIM4)
#define SPL_G0_IS_TIM4(p) ((p) == TIM4)
#else
#define SPL_G0_IS_TIM4(p) (0)
#endif
#if defined(TIM6)
#define SPL_G0_IS_TIM6(p) ((p) == TIM6)
#else
#define SPL_G0_IS_TIM6(p) (0)
#endif
#if defined(TIM7)
#define SPL_G0_IS_TIM7(p) ((p) == TIM7)
#else
#define SPL_G0_IS_TIM7(p) (0)
#endif
#if defined(TIM15)
#define SPL_G0_IS_TIM15(p) ((p) == TIM15)
#else
#define SPL_G0_IS_TIM15(p) (0)
#endif
#if defined(USART3)
#define SPL_G0_IS_USART3(p) ((p) == USART3)
#else
#define SPL_G0_IS_USART3(p) (0)
#endif
#if defined(USART4)
#define SPL_G0_IS_USART4(p) ((p) == USART4)
#else
#define SPL_G0_IS_USART4(p) (0)
#endif
#if defined(USART5)
#define SPL_G0_IS_USART5(p) ((p) == USART5)
#else
#define SPL_G0_IS_USART5(p) (0)
#endif
#if defined(USART6)
#define SPL_G0_IS_USART6(p) ((p) == USART6)
#else
#define SPL_G0_IS_USART6(p) (0)
#endif
#if defined(LPUART1)
#define SPL_G0_IS_LPUART1(p) ((p) == LPUART1)
#else
#define SPL_G0_IS_LPUART1(p) (0)
#endif
#if defined(LPUART2)
#define SPL_G0_IS_LPUART2(p) ((p) == LPUART2)
#else
#define SPL_G0_IS_LPUART2(p) (0)
#endif
#if defined(I2C3)
#define SPL_G0_IS_I2C3(p) ((p) == I2C3)
#else
#define SPL_G0_IS_I2C3(p) (0)
#endif
#if defined(COMP3)
#define SPL_G0_IS_COMP3(p) ((p) == COMP3)
#else
#define SPL_G0_IS_COMP3(p) (0)
#endif
#if defined(SPI3)
#define SPL_G0_IS_SPI3(p) ((p) == SPI3)
#else
#define SPL_G0_IS_SPI3(p) (0)
#endif

#endif /* STM32G0_SKU_INST_H */
