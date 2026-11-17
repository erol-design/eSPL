#ifndef STM32G0_RCC_H
#define STM32G0_RCC_H
#include <stdint.h>
#include "stm32g0xx.h"
#include "stm32g0_status.h"

/* Reset and clock control (RM0444 Rev 6 section 5, Docs/v0.2_RCC_CONTRACT.md).
 * Written for G0 (not a port: the G4 RCC differs in buses, PLL and prescalers).
 * No static state, no busy-wait: oscillator, PLL and SYSCLK changes are start + poll.
 * Peripheral kernel clocks (RCC_CCIPR/CCIPR2): RCC_SetKernelClock (since v0.3). MCO is not
 * supported.
 * Frequencies are in Hz. hse_hz is the caller's HSE frequency (crystal or bypass clock); pass 0
 * when HSE is not used: an HSE-based calculation then returns SPL_ERR_STATE.
 * Caller duties (all APIs): serialise RCC calls with every ISR or context that also writes RCC
 * (enable, reset, prescaler and selector writes are read-modify-write); hse_hz must be the real
 * HSE frequency (the driver cannot measure it); oscillator, PLL and SYSCLK changes are start +
 * poll: the wait and its timeout belong to the caller. */

#ifdef __cplusplus
extern "C"
{
#endif

#define RCC_HSI16_HZ 16000000UL
#define RCC_HSI48_HZ 48000000UL
#define RCC_LSI_HZ 32000UL /* nominal (DS13560: 29.5..34 kHz) */
#define RCC_LSE_HZ 32768UL
#define RCC_HSE_MIN_HZ 4000000UL /* DS13560 HSE oscillator / bypass range */
#define RCC_HSE_MAX_HZ 48000000UL

/* Peripheral identifier: bus in bits 7:5, bit position in bits 4:0. */
#define RCC_PERIPH_ID(bus, bit) ((((uint32_t)(bus)) << 5U) | ((uint32_t)(bit)))
#define RCC_BUS_IOP 0U
#define RCC_BUS_AHB 1U
#define RCC_BUS_APB1 2U
#define RCC_BUS_APB2 3U

typedef enum
{
    RCC_PERIPH_GPIOA = RCC_PERIPH_ID(RCC_BUS_IOP, 0),
    RCC_PERIPH_GPIOB = RCC_PERIPH_ID(RCC_BUS_IOP, 1),
    RCC_PERIPH_GPIOC = RCC_PERIPH_ID(RCC_BUS_IOP, 2),
    RCC_PERIPH_GPIOD = RCC_PERIPH_ID(RCC_BUS_IOP, 3),
    RCC_PERIPH_GPIOE = RCC_PERIPH_ID(RCC_BUS_IOP, 4),
    RCC_PERIPH_GPIOF = RCC_PERIPH_ID(RCC_BUS_IOP, 5),
    RCC_PERIPH_DMA1 = RCC_PERIPH_ID(RCC_BUS_AHB, 0),
    RCC_PERIPH_DMA2 = RCC_PERIPH_ID(RCC_BUS_AHB, 1),
    RCC_PERIPH_FLASH = RCC_PERIPH_ID(RCC_BUS_AHB, 8),
    RCC_PERIPH_CRC = RCC_PERIPH_ID(RCC_BUS_AHB, 12),
    RCC_PERIPH_AES = RCC_PERIPH_ID(RCC_BUS_AHB, 16), /* v0.9: G041/G061/G081/G0C1 only */
    RCC_PERIPH_RNG = RCC_PERIPH_ID(RCC_BUS_AHB, 18), /* v0.9: G041/G061/G081/G0C1 only */
    RCC_PERIPH_TIM2 = RCC_PERIPH_ID(RCC_BUS_APB1, 0),
    RCC_PERIPH_TIM3 = RCC_PERIPH_ID(RCC_BUS_APB1, 1),
    RCC_PERIPH_TIM4 = RCC_PERIPH_ID(RCC_BUS_APB1, 2),
    RCC_PERIPH_TIM6 = RCC_PERIPH_ID(RCC_BUS_APB1, 4),
    RCC_PERIPH_TIM7 = RCC_PERIPH_ID(RCC_BUS_APB1, 5),
    RCC_PERIPH_LPUART2 = RCC_PERIPH_ID(RCC_BUS_APB1, 7),
    RCC_PERIPH_USART5 = RCC_PERIPH_ID(RCC_BUS_APB1, 8),
    RCC_PERIPH_USART6 = RCC_PERIPH_ID(RCC_BUS_APB1, 9),
    RCC_PERIPH_RTCAPB = RCC_PERIPH_ID(RCC_BUS_APB1, 10), /* clock only, no reset bit */
    RCC_PERIPH_WWDG = RCC_PERIPH_ID(RCC_BUS_APB1, 11),   /* clock only, no reset bit */
    RCC_PERIPH_FDCAN = RCC_PERIPH_ID(RCC_BUS_APB1, 12),  /* FDCAN1 and FDCAN2 together */
    RCC_PERIPH_USB = RCC_PERIPH_ID(RCC_BUS_APB1, 13),
    RCC_PERIPH_SPI2 = RCC_PERIPH_ID(RCC_BUS_APB1, 14),
    RCC_PERIPH_SPI3 = RCC_PERIPH_ID(RCC_BUS_APB1, 15),
    RCC_PERIPH_CRS = RCC_PERIPH_ID(RCC_BUS_APB1, 16),
    RCC_PERIPH_USART2 = RCC_PERIPH_ID(RCC_BUS_APB1, 17),
    RCC_PERIPH_USART3 = RCC_PERIPH_ID(RCC_BUS_APB1, 18),
    RCC_PERIPH_USART4 = RCC_PERIPH_ID(RCC_BUS_APB1, 19),
    RCC_PERIPH_LPUART1 = RCC_PERIPH_ID(RCC_BUS_APB1, 20),
    RCC_PERIPH_I2C1 = RCC_PERIPH_ID(RCC_BUS_APB1, 21),
    RCC_PERIPH_I2C2 = RCC_PERIPH_ID(RCC_BUS_APB1, 22),
    RCC_PERIPH_I2C3 = RCC_PERIPH_ID(RCC_BUS_APB1, 23),
    RCC_PERIPH_CEC = RCC_PERIPH_ID(RCC_BUS_APB1, 24),
    RCC_PERIPH_UCPD1 = RCC_PERIPH_ID(RCC_BUS_APB1, 25),
    RCC_PERIPH_UCPD2 = RCC_PERIPH_ID(RCC_BUS_APB1, 26),
    RCC_PERIPH_DBG = RCC_PERIPH_ID(RCC_BUS_APB1, 27),
    RCC_PERIPH_PWR = RCC_PERIPH_ID(RCC_BUS_APB1, 28),
    RCC_PERIPH_DAC1 = RCC_PERIPH_ID(RCC_BUS_APB1, 29),
    RCC_PERIPH_LPTIM2 = RCC_PERIPH_ID(RCC_BUS_APB1, 30),
    RCC_PERIPH_LPTIM1 = RCC_PERIPH_ID(RCC_BUS_APB1, 31),
    RCC_PERIPH_SYSCFG = RCC_PERIPH_ID(RCC_BUS_APB2, 0), /* also COMP and VREFBUF */
    RCC_PERIPH_TIM1 = RCC_PERIPH_ID(RCC_BUS_APB2, 11),
    RCC_PERIPH_SPI1 = RCC_PERIPH_ID(RCC_BUS_APB2, 12),
    RCC_PERIPH_USART1 = RCC_PERIPH_ID(RCC_BUS_APB2, 14),
    RCC_PERIPH_TIM14 = RCC_PERIPH_ID(RCC_BUS_APB2, 15),
    RCC_PERIPH_TIM15 = RCC_PERIPH_ID(RCC_BUS_APB2, 16),
    RCC_PERIPH_TIM16 = RCC_PERIPH_ID(RCC_BUS_APB2, 17),
    RCC_PERIPH_TIM17 = RCC_PERIPH_ID(RCC_BUS_APB2, 18),
    RCC_PERIPH_ADC = RCC_PERIPH_ID(RCC_BUS_APB2, 20)
} RCC_PeriphTypeDef;

typedef enum
{
    RCC_OSC_HSI16 = 0,
    RCC_OSC_HSE = 1,
    RCC_OSC_HSI48 = 2,
    RCC_OSC_LSI = 3,
    RCC_OSC_LSE = 4
} RCC_OscTypeDef;

/* SYSCLK sources, RCC_CFGR.SW / SWS encoding. */
typedef enum
{
    RCC_SYSCLK_HSISYS = 0,
    RCC_SYSCLK_HSE = 1,
    RCC_SYSCLK_PLLR = 2,
    RCC_SYSCLK_LSI = 3,
    RCC_SYSCLK_LSE = 4
} RCC_SysclkTypeDef;

typedef enum
{
    RCC_PLLSRC_HSI16 = 2, /* RCC_PLLCFGR.PLLSRC = 10 */
    RCC_PLLSRC_HSE = 3    /* RCC_PLLCFGR.PLLSRC = 11 */
} RCC_PllSourceTypeDef;

#define RCC_PLL_OUT_P 1U
#define RCC_PLL_OUT_Q 2U
#define RCC_PLL_OUT_R 4U

typedef struct
{
    uint32_t source;  /* RCC_PllSourceTypeDef */
    uint32_t m;       /* 1..8, fPLLIN / m in 2.66..16 MHz */
    uint32_t n;       /* 8..86, VCO = fPLLIN / m * n */
    uint32_t p;       /* 2..32 */
    uint32_t q;       /* 2..8 */
    uint32_t r;       /* 2..8 */
    uint32_t outputs; /* RCC_PLL_OUT_* mask of enabled outputs */
} RCC_PllConfigTypeDef;

typedef struct
{
    uint32_t sysclk_hz;
    uint32_t hclk_hz;
    uint32_t pclk_hz;
    uint32_t tim_pclk_hz; /* timer kernel clock: pclk, x2 when the APB prescaler is not 1 */
} RCC_ClocksTypeDef;

/* Reset flags in RCC_CSR bit positions (RCC_GetResetFlags). */
#define RCC_RESET_FLAGS                                                                            \
    (RCC_CSR_OBLRSTF | RCC_CSR_PINRSTF | RCC_CSR_PWRRSTF | RCC_CSR_SFTRSTF | RCC_CSR_IWDGRSTF |    \
     RCC_CSR_WWDGRSTF | RCC_CSR_LPWRRSTF)

/* IOPENR bit of a GPIO port (GPIOA..GPIOF). */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status RCC_GpioClockEnable(GPIO_TypeDef *port);

/* Bus clock enable / disable / query and peripheral reset assert / release. Identifier not in the
 * table -> PARAM. Reset of RTCAPB or WWDG (no reset bit) -> UNSUPPORTED. Disabling PWR, FLASH or
 * SYSCFG is allowed: the caller owns the consequences (documented, not blocked). */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status RCC_EnablePeriphClock(RCC_PeriphTypeDef periph);
/* Caller: stop the peripheral first (no transfer, DMA request or interrupt in flight); the
 * driver does not check it, also not for PWR, FLASH or SYSCFG. */
/* Level: PARAM[F] */
SPL_Status RCC_DisablePeriphClock(RCC_PeriphTypeDef periph);
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status RCC_GetPeriphClock(RCC_PeriphTypeDef periph, uint8_t *enabled);
/* Caller: the peripheral loses its registers and any transfer in flight: stop its DMA channel
 * and mask its NVIC line first; release with RCC_ReleasePeriphReset. */
/* Level: PARAM[F] UNSUPPORTED[F] */
SPL_Status RCC_AssertPeriphReset(RCC_PeriphTypeDef periph);
/* Caller: reconfigure the peripheral after the release (registers at reset values). */
/* Level: PARAM[F] UNSUPPORTED[F] */
SPL_Status RCC_ReleasePeriphReset(RCC_PeriphTypeDef periph);

/* Oscillator on (Start) / off (Stop) / ready flag. LSE: RCC_BDCR, needs PWR DBP = 1 (else
 * STATE). Stop of the SYSCLK source, or of the PLL source while PLLON = 1 -> STATE. */
/* Caller: poll RCC_GetOscReady until ready (HSE / LSE need the crystal or the bypass clock on
 * the board; RCC_SetHseBypass before HSE). ERRATA ES0548 2.2.1: LSI is unstable as RTC or LSE
 * CSS clock; prefer LSE. */
/* Level: PARAM[r] STATE[F] UNSUPPORTED[r] */
SPL_Status RCC_StartOsc(RCC_OscTypeDef osc);
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[r] STATE[F] UNSUPPORTED[r] */
SPL_Status RCC_StopOsc(RCC_OscTypeDef osc);
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status RCC_GetOscReady(RCC_OscTypeDef osc, uint8_t *ready);
/* HSEBYP; HSE must be off (HSEON = 0) -> else STATE. */
/* Caller: bypass = 1 only with an external clock on OSC_IN, bypass = 0 only with a crystal
 * (board choice, not detectable). */
/* Level: PARAM[F] STATE[F] */
SPL_Status RCC_SetHseBypass(uint8_t bypass);
/* HSISYS divider (1, 2, 4 .. 128). When HSISYS is SYSCLK and the new HCLK needs more flash wait
 * states than FLASH_ACR.LATENCY -> STATE (program the latency first).
 * ERRATA ES0548 2.2.4: a divider other than 1 prevents Stop entry with SYSCLK = HSE and blocks
 * wake-up by peripheral clock requests; PWR_SetLowPowerMode(Stop) checks it. */
/* Caller: when HCLK rises, FLASH_SetLatency first; when HCLK drops, the latency may be lowered
 * only afterwards (RM0444 3.3.4). */
/* Level: PARAM[F] STATE[F,r] */
SPL_Status RCC_SetHsiDiv(uint32_t divider);

/* PLL configuration (PLLON must be 0, else STATE). Limits from DS13560 Table 47 for the current
 * voltage range (PWR_CR1.VOS): range 1 VCO 96..344 MHz, P <= 122, Q <= 128, R <= 64 MHz;
 * range 2 VCO <= 128, P <= 40, Q <= 33, R <= 16 MHz; P/Q/R minimum 3.09/12/12 MHz. Limits are
 * checked only for the enabled outputs; the VCO and the input are always checked. */
/* Caller: set the voltage range (PWR VOS) before: the limits are those of the current range;
 * start the PLL source oscillator before RCC_EnablePll. */
/* Level: PARAM[F] STATE[F] UNSUPPORTED[r] */
SPL_Status RCC_ConfigPll(const RCC_PllConfigTypeDef *config, uint32_t hse_hz);
/* PLLON = 1; PLL source oscillator not ready, or PLLSRC = none -> STATE. Poll RCC_GetPllReady. */
/* Caller: poll RCC_GetPllReady until 1 before RCC_SetSysclkSource(RCC_SYSCLK_PLLR). */
/* Level: STATE[F] */
SPL_Status RCC_EnablePll(void);
/* PLLON = 0; PLL is the SYSCLK source -> STATE. */
/* Caller: none beyond the file-header duties. */
/* Level: STATE[F] */
SPL_Status RCC_DisablePll(void);
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status RCC_GetPllReady(uint8_t *ready);

/* SYSCLK switch request (CFGR.SW). The source must be ready (PLLR: PLLRDY and PLLREN). The new
 * HCLK must be allowed in the current voltage range and by FLASH_ACR.LATENCY, else STATE
 * (RM0444 3.3.4: program the latency first when the frequency increases). Poll
 * RCC_GetSysclkSource until it reports the new source. */
/* Caller: FLASH_SetLatency first when HCLK rises; poll RCC_GetSysclkSource until it reports the
 * new source; after a drop lower the latency; clock-derived settings (baud rates, SysTick
 * reload) are not updated. */
/* Level: PARAM[F] STATE[F,r] */
SPL_Status RCC_SetSysclkSource(RCC_SysclkTypeDef source, uint32_t hse_hz);
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] HW[r] */
SPL_Status RCC_GetSysclkSource(RCC_SysclkTypeDef *source);
/* HPRE: 1, 2, 4, 8, 16, 64, 128, 256, 512. Same latency / range rule as RCC_SetSysclkSource. */
/* Caller: same latency order as RCC_SetSysclkSource; clock-derived settings (baud rates, SysTick
 * reload) are not updated. */
/* Level: PARAM[F] STATE[F,r] */
SPL_Status RCC_SetAhbPrescaler(uint32_t divider, uint32_t hse_hz);
/* PPRE: 1, 2, 4, 8, 16.
 * ERRATA ES0487 2.6.6 (family, see stm32g0_tim.h): on G030..G081 TIM16 / TIM17 run on SYSCLK and
 * are functional only while SYSCLK / PCLK <= 4 (caller rule; not checked here). */
/* Caller: keep SYSCLK / PCLK <= 4 while TIM16 / TIM17 are used on G030..G081 (ERRATA ES0487
 * 2.6.6); PCLK-derived settings (baud rates) are not updated. */
/* Level: PARAM[r] */
SPL_Status RCC_SetApbPrescaler(uint32_t divider);
/* Current clocks from SWS, HSIDIV, PLLCFGR, HPRE and PPRE. */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] STATE[r] */
SPL_Status RCC_GetClocks(uint32_t hse_hz, RCC_ClocksTypeDef *clocks);

/* ---- v0.3: peripheral kernel clocks (RCC_CCIPR / RCC_CCIPR2, RM0444 5.4.21 / 5.4.22) ----
 * USART4/5/6 and I2C3 have no selector: their kernel clock is PCLK (RCC_GetUsartKernelClock
 * reports it). v0.9: RNG (RNGSEL, 00 = no clock -> RCC_KSRC_NONE; divider RNGDIV) and CEC
 * (1-bit CECSEL: HSI16 / 488 or LSE). v0.10: I2S1 / I2S2 (SYSCLK, PLLP, HSI16, I2S_CKIN).
 * TIM1 / TIM15 selectors are not provided yet. */
typedef enum
{
    RCC_KERNEL_USART1 = 0,
    RCC_KERNEL_USART2 = 1,
    RCC_KERNEL_USART3 = 2,
    RCC_KERNEL_LPUART1 = 3,
    RCC_KERNEL_LPUART2 = 4,
    RCC_KERNEL_I2C1 = 5,
    RCC_KERNEL_I2C2 = 6, /* CCIPR bits 15:14 (I2C2I2S1SEL) select I2C2 on G0B1 */
    RCC_KERNEL_LPTIM1 = 7,
    RCC_KERNEL_LPTIM2 = 8,
    RCC_KERNEL_ADC = 9,
    RCC_KERNEL_FDCAN = 10,
    RCC_KERNEL_USB = 11,
    RCC_KERNEL_RNG = 12,  /* v0.9 */
    RCC_KERNEL_CEC = 13,  /* v0.9 */
    RCC_KERNEL_I2S1 = 14, /* v0.10: CCIPR[15:14] (G03x..G08x) or CCIPR2.I2S1SEL (0BX) */
    RCC_KERNEL_I2S2 = 15  /* v0.10: CCIPR2.I2S2SEL, G0B0 / G0B1 / G0C1 only */
} RCC_KernelTypeDef;

typedef enum
{
    RCC_KSRC_PCLK = 0,
    RCC_KSRC_SYSCLK = 1,
    RCC_KSRC_HSI16 = 2,
    RCC_KSRC_LSE = 3,
    RCC_KSRC_LSI = 4,
    RCC_KSRC_HSE = 5,
    RCC_KSRC_HSI48 = 6,
    RCC_KSRC_PLLP = 7,
    RCC_KSRC_PLLQ = 8,
    RCC_KSRC_NONE = 9,          /* v0.9: RNGSEL = 00, no kernel clock */
    RCC_KSRC_HSI16_DIV488 = 10, /* v0.9: CEC, HSI16 / 488 = 32 786 Hz */
    RCC_KSRC_I2S_CKIN = 11      /* v0.10: external I2S_CKIN pin (frequency unknown -> STATE) */
} RCC_KernelSrcTypeDef;

/* Writes the selector field. A source the selector does not offer -> PARAM. The peripheral should
 * be disabled while its kernel clock changes (caller's duty, not checked). */
/* Caller: disable the peripheral while its kernel clock changes; start the selected oscillator
 * first (readiness not checked). */
/* Level: PARAM[F] UNSUPPORTED[r] */
SPL_Status RCC_SetKernelClock(RCC_KernelTypeDef kernel, RCC_KernelSrcTypeDef source);
/* Reserved encoding read back -> ERR_HW. */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] UNSUPPORTED[r] HW[r] */
SPL_Status RCC_GetKernelSource(RCC_KernelTypeDef kernel, RCC_KernelSrcTypeDef *source);
/* Configuration-derived frequency (oscillator readiness is not checked). PLLP/PLLQ with the output
 * disabled (PLLxEN = 0) or an unusable PLL configuration -> STATE; HSE with hse_hz = 0 -> STATE;
 * v0.9: RCC_KSRC_NONE (RNG without clock) -> STATE; RNG: source divided by RNGDIV.
 * v0.10: RCC_KSRC_I2S_CKIN (external pin, frequency not known to the driver) -> STATE. */
/* Caller: the result assumes the selected oscillator runs (readiness not checked). */
/* Level: PARAM[F] STATE[r] UNSUPPORTED[r] HW[r] */
SPL_Status RCC_GetKernelClock(RCC_KernelTypeDef kernel, uint32_t hse_hz, uint32_t *hz);
/* v0.9: RNG kernel clock divider RNGDIV (RM0444 5.4.21): divider 1, 2, 4 or 8, else PARAM. No RNG
 * on this SKU -> UNSUPPORTED. Change it with the RNG disabled (caller's duty, not checked). */
/* Caller: disable the RNG before changing the divider. */
/* Level: PARAM[r] UNSUPPORTED[r] */
SPL_Status RCC_SetRngDivider(uint32_t divider);
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] UNSUPPORTED[r] */
SPL_Status RCC_GetRngDivider(uint32_t *divider);
/* Kernel clock of any USART/LPUART instance (USART4/5/6: PCLK). Not a USART instance -> PARAM. */
/* Caller: the result assumes the selected oscillator runs (readiness not checked). */
/* Level: PARAM[F,r] STATE[r] UNSUPPORTED[r] HW[r] */
SPL_Status RCC_GetUsartKernelClock(const USART_TypeDef *instance, uint32_t hse_hz,
                                   uint32_t *kernel_hz);

/* HSE clock security system: CSSON is set-only (cleared by reset). It may be set before HSE is
 * ready: the hardware enables the detector when HSERDY = 1 (RM0444 5.4.1). */
/* Caller: an HSE failure raises the NMI: the application NMI_Handler switches the clocks and
 * clears CSSF (RCC_ClearCssFlag); CSSON stays set until reset. */
/* Level: - */
SPL_Status RCC_EnableCss(void);
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status RCC_GetCssFlag(uint8_t *set);
/* Caller: none beyond the file-header duties. */
/* Level: - */
SPL_Status RCC_ClearCssFlag(void);

/* RCC_CSR reset flags (RCC_RESET_FLAGS mask) and RMVF. */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status RCC_GetResetFlags(uint32_t *flags);
/* Caller: read the flags (RCC_GetResetFlags) first; RMVF clears all of them. */
/* Level: - */
SPL_Status RCC_ClearResetFlags(void);

#ifdef __cplusplus
}
#endif
#endif /* STM32G0_RCC_H */
