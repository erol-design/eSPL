#ifndef STM32G4_RCC_H
#define STM32G4_RCC_H
#include "stm32g4xx.h"
#include "stm32g4_status.h"

#ifdef __cplusplus
extern "C"
{
#endif
    /* Enables and read-backs a GPIO port bus clock. Does not configure PLL/system clock. */
    /* Seviye: PARAM[r] */
    SPL_Status RCC_GpioClockEnable(GPIO_TypeDef *port);

    typedef struct
    {
        uint32_t sysclk_hz;
        uint32_t hclk_hz;
        uint32_t pclk1_hz;
        uint32_t pclk2_hz;
        uint32_t tim_apb1_hz;
        uint32_t tim_apb2_hz;
    } RCC_ClocksTypeDef;

    /* Read-only frequency computation from current RCC registers.
     * hse_hz must match actual oscillator; 0 is accepted only if HSE is unused.
     * Does not configure PLL, Flash latency or voltage scaling.
     * Returns SPL_ERR_UNSUPPORTED for unrecognized clock settings.
     */
    /* Seviye: PARAM[S,r] UNSUPPORTED[r] */
    SPL_Status RCC_GetClocks(uint32_t hse_hz, RCC_ClocksTypeDef *clocks);
    /* Read selected USART/UART kernel clock from RCC_CCIPR.
     * LSE source is nominally 32768 Hz; caller must ensure it is running.
     * An instance that is not USART1/2/3 or UART4/5 present on this SKU returns
     * SPL_ERR_PARAM (an absent instance has no CMSIS macro and cannot be named).
     */
    /* Seviye: PARAM[S,r] UNSUPPORTED[r] */
    SPL_Status RCC_GetUsartKernelClock(const USART_TypeDef *instance, uint32_t hse_hz,
                                       uint32_t *kernel_hz);

/* ---- v0.11 clock-tree programming (Docs/v0.11_RCC_CONTRACT.md) ----
 * Needs the full CMSIS device header. The legacy host mock (Tests/mock) models
 * only four RCC registers and does not define RCC_CR_HSION, so these APIs are
 * compiled out there. Rejected calls write no register. Read-modify-write APIs
 * must be serialised by the caller (not ISR-safe against each other). */
#if defined(RCC_CR_HSION)

/* Nominal internal / low-speed clock frequencies (RM0440 7.2). */
#define RCC_HSI16_HZ 16000000UL
#define RCC_HSI48_HZ 48000000UL
#define RCC_LSI_HZ 32000UL
#define RCC_LSE_HZ 32768UL

/* HSE crystal / external clock range (RM0440 7.2.2: 4 to 48 MHz). */
#define RCC_HSE_MIN_HZ 4000000UL
#define RCC_HSE_MAX_HZ 48000000UL

/* PLL limits. Output <= 170 MHz is RM0440 7.2.4 / 7.4.4. The VCO input and VCO
 * output ranges are only "defined in the datasheet" by RM0440 7.4.4; values
 * below are from the STM32G431/G474 datasheet PLL characteristics table and
 * are an open item to confirm against the datasheet revision in use. */
#define RCC_PLL_VCO_IN_MIN_HZ 2660000UL
#define RCC_PLL_VCO_IN_MAX_HZ 16000000UL
#define RCC_PLL_VCO_MIN_HZ 96000000UL
#define RCC_PLL_VCO_MAX_HZ 344000000UL
#define RCC_PLL_OUT_MAX_HZ 170000000UL

    typedef enum
    {
        RCC_OSC_HSI16 = 0,
        RCC_OSC_HSE = 1,
        RCC_OSC_LSI = 2,
        RCC_OSC_HSI48 = 3 /* LSE: see RTC module (RTC_LSEStart). */
    } RCC_OscTypeDef;

    /* SYSCLK source, RCC_CFGR.SW / SWS encoding (RM0440 7.4.3). */
    typedef enum
    {
        RCC_SYSCLK_HSI16 = 1,
        RCC_SYSCLK_HSE = 2,
        RCC_SYSCLK_PLL = 3
    } RCC_SysclkTypeDef;

    typedef enum
    {
        RCC_PLLSRC_HSI16 = 0,
        RCC_PLLSRC_HSE = 1
    } RCC_PllSourceTypeDef;

    /* PLL configuration; f_vco = f_src * n / m; outputs f_vco / p, q, r. */
    typedef struct
    {
        uint32_t source; /* RCC_PllSourceTypeDef */
        uint32_t m;      /* 1..16 */
        uint32_t n;      /* 8..127 */
        uint32_t p;      /* 2..31 (written to PLLPDIV) */
        uint32_t q;      /* 2, 4, 6 or 8 */
        uint32_t r;      /* 2, 4, 6 or 8 */
        uint8_t p_enable;
        uint8_t q_enable;
        uint8_t r_enable;
        uint8_t reserved; /* ignored */
    } RCC_PllConfigTypeDef;

    /* Peripheral clock / reset identifiers. Every identifier exists on every SKU;
     * one whose CMSIS enable bit is absent returns SPL_ERR_UNSUPPORTED. */
    typedef enum
    {
        /* AHB1 */
        RCC_PERIPH_DMA1 = 0,
        RCC_PERIPH_DMA2,
        RCC_PERIPH_DMAMUX1,
        RCC_PERIPH_CORDIC,
        RCC_PERIPH_FMAC,
        RCC_PERIPH_FLASH,
        RCC_PERIPH_CRC,
        /* AHB2 */
        RCC_PERIPH_GPIOA,
        RCC_PERIPH_GPIOB,
        RCC_PERIPH_GPIOC,
        RCC_PERIPH_GPIOD,
        RCC_PERIPH_GPIOE,
        RCC_PERIPH_GPIOF,
        RCC_PERIPH_GPIOG,
        RCC_PERIPH_ADC12,
        RCC_PERIPH_ADC345,
        RCC_PERIPH_DAC1,
        RCC_PERIPH_DAC2,
        RCC_PERIPH_DAC3,
        RCC_PERIPH_DAC4,
        RCC_PERIPH_AES,
        RCC_PERIPH_RNG,
        /* AHB3 */
        RCC_PERIPH_FMC,
        RCC_PERIPH_QSPI,
        /* APB1 register 1 */
        RCC_PERIPH_TIM2,
        RCC_PERIPH_TIM3,
        RCC_PERIPH_TIM4,
        RCC_PERIPH_TIM5,
        RCC_PERIPH_TIM6,
        RCC_PERIPH_TIM7,
        RCC_PERIPH_CRS,
        RCC_PERIPH_RTCAPB,
        RCC_PERIPH_WWDG,
        RCC_PERIPH_SPI2,
        RCC_PERIPH_SPI3,
        RCC_PERIPH_USART2,
        RCC_PERIPH_USART3,
        RCC_PERIPH_UART4,
        RCC_PERIPH_UART5,
        RCC_PERIPH_I2C1,
        RCC_PERIPH_I2C2,
        RCC_PERIPH_USB,
        RCC_PERIPH_FDCAN,
        RCC_PERIPH_PWR,
        RCC_PERIPH_I2C3,
        RCC_PERIPH_LPTIM1,
        /* APB1 register 2 */
        RCC_PERIPH_LPUART1,
        RCC_PERIPH_I2C4,
        RCC_PERIPH_UCPD1,
        /* APB2 */
        RCC_PERIPH_SYSCFG,
        RCC_PERIPH_TIM1,
        RCC_PERIPH_SPI1,
        RCC_PERIPH_TIM8,
        RCC_PERIPH_USART1,
        RCC_PERIPH_SPI4,
        RCC_PERIPH_TIM15,
        RCC_PERIPH_TIM16,
        RCC_PERIPH_TIM17,
        RCC_PERIPH_TIM20,
        RCC_PERIPH_SAI1,
        RCC_PERIPH_HRTIM1,
        RCC_PERIPH_COUNT
    } RCC_PeriphTypeDef;

    /* Kernel clock selectors in RCC_CCIPR / RCC_CCIPR2 (RM0440 7.4.26, 7.4.30). */
    typedef enum
    {
        RCC_KERNEL_USART1 = 0,
        RCC_KERNEL_USART2,
        RCC_KERNEL_USART3,
        RCC_KERNEL_UART4,
        RCC_KERNEL_UART5,
        RCC_KERNEL_LPUART1,
        RCC_KERNEL_I2C1,
        RCC_KERNEL_I2C2,
        RCC_KERNEL_I2C3,
        RCC_KERNEL_I2C4,
        RCC_KERNEL_LPTIM1,
        RCC_KERNEL_SAI1,
        RCC_KERNEL_I2S23,
        RCC_KERNEL_FDCAN,
        RCC_KERNEL_CLK48,
        RCC_KERNEL_ADC12,
        RCC_KERNEL_ADC345,
        RCC_KERNEL_QSPI,
        RCC_KERNEL_COUNT
    } RCC_KernelTypeDef;

    /* Kernel clock sources; the RM encoding per selector is resolved internally. */
    typedef enum
    {
        RCC_KCLK_PCLK = 1, /* PCLK1, or PCLK2 for USART1 */
        RCC_KCLK_SYSCLK,
        RCC_KCLK_HSI16,
        RCC_KCLK_LSE,
        RCC_KCLK_LSI,
        RCC_KCLK_HSE,
        RCC_KCLK_PLLQ,
        RCC_KCLK_PLLP,
        RCC_KCLK_HSI48,
        RCC_KCLK_I2S_CKIN, /* external pin, frequency unknown to the library */
        RCC_KCLK_NONE      /* ADC12/ADC345 "no clock" */
    } RCC_KernelSrcTypeDef;

    /* MCO source, RCC_CFGR.MCOSEL encoding (RM0440 7.4.3). */
    typedef enum
    {
        RCC_MCO_OFF = 0,
        RCC_MCO_SYSCLK = 1,
        RCC_MCO_HSI16 = 3,
        RCC_MCO_HSE = 4,
        RCC_MCO_PLL = 5,
        RCC_MCO_LSI = 6,
        RCC_MCO_LSE = 7,
        RCC_MCO_HSI48 = 8
    } RCC_McoSourceTypeDef;

/* Reset-cause flags returned by RCC_GetResetFlags (RCC_CSR bits 31:25). */
#define RCC_RESET_FLAGS                                                                            \
    (RCC_CSR_LPWRRSTF | RCC_CSR_WWDGRSTF | RCC_CSR_IWDGRSTF | RCC_CSR_SFTRSTF | RCC_CSR_BORRSTF |  \
     RCC_CSR_PINRSTF | RCC_CSR_OBLRSTF)

    /* --- Oscillators (non-blocking: start, then poll RCC_GetOscReady) --- */
    /* Sets the ON bit; idempotent. */
    /* Seviye: PARAM[r] */
    SPL_Status RCC_StartOsc(RCC_OscTypeDef osc);
    /* Clears the ON bit. HSI16/HSE: STATE if selected (SW or SWS) as SYSCLK or the
     * PLL source while the PLL is on (PLLON or PLLRDY). LSI: OK means LSION=0 only;
     * IWDG, RTC or LSE CSS keep LSI running (LSIRDY stays 1, RM0440 7.4.28). */
    /* Seviye: PARAM[r] STATE[r] */
    SPL_Status RCC_StopOsc(RCC_OscTypeDef osc);
    /* *ready = RDY bit (always written). STATE when the oscillator is off and not
     * ready (polling will not help); OK otherwise. */
    /* Seviye: PARAM[S,r] STATE[r] */
    SPL_Status RCC_GetOscReady(RCC_OscTypeDef osc, uint8_t *ready);
    /* CR.HSEBYP = bypass (0 crystal, 1 external clock). Only while HSE is off:
     * HSEON=1 with a different value -> STATE; HSEON=0 but HSERDY=1 -> BUSY.
     * ERRATA ES0431 2.2.9 / ES0430 2.2.9 / ES0523 2.2.7: bypass HSE as SYSCLK blocks Standby entry:
     * select HSI16 as SYSCLK before entering Standby (caller). */
    /* Seviye: PARAM[S] STATE[r] BUSY[r] */
    SPL_Status RCC_SetHseBypass(uint8_t bypass);

    /* --- PLL --- */
    /* Validates and writes the whole PLLCFGR while the PLL is off. hse_hz is used
     * only for the HSE source. PLLON=1 -> STATE; PLLON=0, PLLRDY=1 -> BUSY. */
    /* Seviye: PARAM[S,r] STATE[r] BUSY[r] */
    SPL_Status RCC_ConfigPll(const RCC_PllConfigTypeDef *config, uint32_t hse_hz);
    /* Sets PLLON; source oscillator must be ready (on, not ready -> BUSY; off -> STATE). */
    /* Seviye: STATE[r] BUSY[r] */
    SPL_Status RCC_EnablePll(void);
    /* Clears PLLON; STATE while the PLL is SYSCLK (SW or SWS). */
    /* Seviye: STATE[r] */
    SPL_Status RCC_DisablePll(void);
    /* *ready = PLLRDY (always written). STATE when PLLON=0 and PLLRDY=0. */
    /* Seviye: PARAM[S] STATE[r] */
    SPL_Status RCC_GetPllReady(uint8_t *ready);

    /* --- SYSCLK and bus prescalers (sequence for > 80 MHz: contract section 3) --- */
    /* Writes CFGR.SW. Target must be ready (PLL: PLLRDY and PLLREN). A previous
     * switch still pending (SW != SWS): its target on -> BUSY, off (PLL: PLLON=0
     * or PLLREN=0) -> STATE (it never completes; start that target). Confirm
     * with RCC_GetSysclkSource. */
    /* Seviye: PARAM[r] STATE[r] BUSY[r] */
    SPL_Status RCC_SetSysclkSource(RCC_SysclkTypeDef source);
    /* *source = CFGR.SWS. Reserved SWS=00 -> UNSUPPORTED. */
    /* Seviye: PARAM[S] UNSUPPORTED[r] */
    SPL_Status RCC_GetSysclkSource(RCC_SysclkTypeDef *source);
    /* HPRE: divider 1, 2, 4, 8, 16, 64, 128, 256 or 512; CFGR read back. */
    /* Seviye: PARAM[r] */
    SPL_Status RCC_SetAhbPrescaler(uint32_t divider);
    /* PPRE1 / PPRE2: divider 1, 2, 4, 8 or 16 each; CFGR read back. */
    /* Seviye: PARAM[r] */
    SPL_Status RCC_SetApbPrescalers(uint32_t apb1_divider, uint32_t apb2_divider);

    /* --- Peripheral bus clocks and resets --- */
    /* Sets the ENR bit and reads the register back (clock propagation). */
    /* Seviye: PARAM[r] UNSUPPORTED[r] */
    SPL_Status RCC_EnablePeriphClock(RCC_PeriphTypeDef periph);
    /* Clears the ENR bit. RCC_PERIPH_FLASH -> PARAM (RM: flash power-down only). */
    /* Seviye: PARAM[r] UNSUPPORTED[r] */
    SPL_Status RCC_DisablePeriphClock(RCC_PeriphTypeDef periph);
    /* *enabled = ENR bit. */
    /* Seviye: PARAM[S,r] UNSUPPORTED[r] */
    SPL_Status RCC_GetPeriphClock(RCC_PeriphTypeDef periph, uint8_t *enabled);
    /* Sets / clears the RSTR bit. No reset bit (RTCAPB, WWDG) -> UNSUPPORTED;
     * RCC_PERIPH_FLASH -> PARAM. */
    /* Seviye: PARAM[r] UNSUPPORTED[r] */
    SPL_Status RCC_AssertPeriphReset(RCC_PeriphTypeDef periph);
    /* Seviye: PARAM[r] UNSUPPORTED[r] */
    SPL_Status RCC_ReleasePeriphReset(RCC_PeriphTypeDef periph);

    /* --- Kernel clocks --- */
    /* Selector absent on this SKU -> UNSUPPORTED; source not offered -> PARAM. */
    /* Seviye: PARAM[r] UNSUPPORTED[r] */
    SPL_Status RCC_SetKernelClock(RCC_KernelTypeDef kernel, RCC_KernelSrcTypeDef source);
    /* Reserved encoding read back -> UNSUPPORTED. */
    /* Seviye: PARAM[S,r] UNSUPPORTED[r] */
    SPL_Status RCC_GetKernelSource(RCC_KernelTypeDef kernel, RCC_KernelSrcTypeDef *source);
    /* Configuration-derived frequency (oscillator readiness is not checked).
     * NONE or a disabled PLL output -> STATE; I2S_CKIN -> UNSUPPORTED. */
    /* Seviye: PARAM[S,r] STATE[r] UNSUPPORTED[r] */
    SPL_Status RCC_GetKernelClock(RCC_KernelTypeDef kernel, uint32_t hse_hz, uint32_t *hz);

    /* --- MCO, CSS, reset cause --- */
    /* MCOSEL / MCOPRE (divider 1, 2, 4, 8, 16). Pin AF setup is the caller's duty. */
    /* Seviye: PARAM[r] */
    SPL_Status RCC_ConfigMco(RCC_McoSourceTypeDef source, uint32_t divider);
    /* Sets CR.CSSON (set-only; cleared by reset only). CSS failure raises NMI. */
    /* Seviye: - */
    SPL_Status RCC_EnableCss(void);
    /* *set = CIFR.CSSF. */
    /* Seviye: PARAM[S] */
    SPL_Status RCC_GetCssFlag(uint8_t *set);
    /* Writes CICR.CSSC only (write-1-to-clear register). */
    /* Seviye: - */
    SPL_Status RCC_ClearCssFlag(void);
    /* *flags = CSR & RCC_RESET_FLAGS (test with RCC_CSR_xxxRSTF).
     * ERRATA ES0431 2.2.5 / ES0430 2.2.5 / ES0523 2.2.3, ES0431 2.2.3 / ES0430 2.2.4 (LSI as
     * RTCCLK; not in ES0523): BORRSTF selects the boot-time backup-domain erase
     * (RTC_BackupDomainFullReset), then RCC_ClearResetFlags; sequence in
     * Docs/v0.10_RTC_CONTRACT.md. */
    /* Seviye: PARAM[S] */
    SPL_Status RCC_GetResetFlags(uint32_t *flags);
    /* Sets CSR.RMVF (LSION preserved). */
    /* Seviye: - */
    SPL_Status RCC_ClearResetFlags(void);

#endif /* RCC_CR_HSION */

#ifdef __cplusplus
}
#endif
#endif
