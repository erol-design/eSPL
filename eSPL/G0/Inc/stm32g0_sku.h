#ifndef STM32G0_SKU_H
#define STM32G0_SKU_H
#include "stm32g0xx.h"

/* STM32G0 SKU capability profile (v0.8, Docs/v0.8_SKU_PROFILES.md). One place where the driver
 * learns what the selected device (CMSIS define STM32G0xxxx) has. Sources, in this order:
 *  1. presence of the CMSIS instance / register definitions (cmsis_device_g0 v1.4.4);
 *  2. RM0444 Rev 6 implementation tables (G0x1): Table 49 DMA, Table 54 DMAMUX, Table 165 I2C,
 *     Table 183 USART/LPUART classes;
 *  3. value line G030/G050/G070/G0B0: RM0454 Rev 5 Table 33 (DMA), Table 38/39 (DMAMUX),
 *     Table 102 (I2C), Table 120 (USART classes), Table 49 (EXTI), 4.4.2/4.4.3 (no PVD, no RRS):
 *     the same facts as the G0x1 group tables (checked 2026-11-12, v0.9).
 * Only macros, no code. */

/* ---- SKU group ----------------------------------------------------------------------------- */
#if defined(STM32G030xx) || defined(STM32G031xx) || defined(STM32G041xx)
#define SPL_G0_GROUP_03X 1
#elif defined(STM32G050xx) || defined(STM32G051xx) || defined(STM32G061xx)
#define SPL_G0_GROUP_05X 1
#elif defined(STM32G070xx) || defined(STM32G071xx) || defined(STM32G081xx)
#define SPL_G0_GROUP_07X 1
#elif defined(STM32G0B0xx) || defined(STM32G0B1xx) || defined(STM32G0C1xx)
#define SPL_G0_GROUP_0BX 1
#else
#error "stm32g0_sku.h: unknown STM32G0 device (define one CMSIS STM32G0xxxx macro)"
#endif

#if defined(STM32G030xx) || defined(STM32G050xx) || defined(STM32G070xx) || defined(STM32G0B0xx)
#define SPL_G0_VALUE_LINE 1
#endif

/* ---- errata applicability (v0.12, Docs/v0.12_FAMILY_AUDIT.md) ------------------------------
 * A silicon limitation is enforced only on the SKUs whose errata sheet lists it (12 sheets,
 * ES0544 for G050 since v0.12.1). */
#if defined(SPL_G0_GROUP_03X) || defined(SPL_G0_GROUP_07X)
/* ES0486 / ES0487 / ES0488 / ES0468 / ES0418 / ES0412 2.2.3: under RDP level 1, booting from main
 * flash selected through the PA14-BOOT0 pin fails; 2.2.4: DMAMUX EXTI sync / trigger inputs dead.
 */
#define SPL_G0_ERRATA_RDP1_BOOT0_PIN 1
#define SPL_G0_ERRATA_DMAMUX_EXTI 1
#else
#define SPL_G0_ERRATA_RDP1_BOOT0_PIN 0
#define SPL_G0_ERRATA_DMAMUX_EXTI 0
#endif
#if defined(STM32G050xx) || defined(STM32G051xx) || defined(STM32G061xx)
/* ES0544 2.2.6 / ES0545 2.2.7 / ES0546 2.2.7: DBG_APB_FZ1 reads back wrong TIM6 / TIM7 /
 * RTC bits. */
#define SPL_G0_ERRATA_DBG_FZ1_READ 1
#else
#define SPL_G0_ERRATA_DBG_FZ1_READ 0
#endif
#if defined(STM32G051xx) || defined(STM32G061xx)
/* ES0545 2.8.6 / ES0546 2.9.6: TIM2_CHx unstable with OCREF_CLR on TIM2_ETRF (G050: no TIM2,
 * no item in ES0544). */
#define SPL_G0_ERRATA_TIM2_ETRF_OCCLR 1
#else
#define SPL_G0_ERRATA_TIM2_ETRF_OCCLR 0
#endif
#if defined(SPL_G0_GROUP_0BX)
/* ES0548 / ES0549 2.2.7: Rev A (REV_ID 0x1000) SRAM corrupted in Standby with RRS; the REV_ID
 * values A / Z / Y of ES0548 Table 1 hold only for this group (G07x uses 0x2000..0x2003). */
#define SPL_G0_ERRATA_RRS_REV_A 1
#else
#define SPL_G0_ERRATA_RRS_REV_A 0
#endif

/* ---- channel counts (CMSIS, RM0444 Tables 49 / 54) ----------------------------------------- */
#if defined(DMA1_Channel7_BASE)
#define SPL_G0_DMA1_CHANNELS 7U
#else
#define SPL_G0_DMA1_CHANNELS 5U
#endif
#if defined(DMA2)
#define SPL_G0_DMA2_CHANNELS 5U
#else
#define SPL_G0_DMA2_CHANNELS 0U
#endif
#define SPL_G0_DMAMUX_CHANNELS (SPL_G0_DMA1_CHANNELS + SPL_G0_DMA2_CHANNELS)

/* ---- USART / LPUART classes (RM0444 Table 183) --------------------------------------------
 * FULL: FIFO, prescaler, smartcard, IrDA, LIN, RTO, auto baud, wake-up. BASIC: none of these.
 * group       USART1 USART2 USART3 USART4 USART5 USART6
 * 03X / 05X   FULL   BASIC  -      -      -      -
 * 07X         FULL   FULL   BASIC  BASIC  -      -
 * 0BX         FULL   FULL   FULL   BASIC  BASIC  BASIC                                         */
#if defined(SPL_G0_GROUP_03X) || defined(SPL_G0_GROUP_05X)
#define SPL_G0_USART2_FULL 0
#define SPL_G0_USART3_FULL 0
#elif defined(SPL_G0_GROUP_07X)
#define SPL_G0_USART2_FULL 1
#define SPL_G0_USART3_FULL 0
#else
#define SPL_G0_USART2_FULL 1
#define SPL_G0_USART3_FULL 1
#endif

/* ---- I2C (RM0444 Table 165): SMBus/PMBus, independent clock and wake-up from Stop on I2C1
 * always, on I2C2 only on STM32G0B1 / STM32G0C1 (group 0BX), never on I2C3. -------------------- */
#if defined(SPL_G0_GROUP_0BX)
#define SPL_G0_I2C2_SMBUS 1
#else
#define SPL_G0_I2C2_SMBUS 0
#endif

/* ---- SPI2 / I2S2 (RM0444 Table 205, RM0454 Table 129): I2S on SPI2 only in group 0BX. -------- */
#if defined(SPL_G0_GROUP_0BX)
#define SPL_G0_SPI2_I2S 1
#else
#define SPL_G0_SPI2_I2S 0
#endif

/* ---- whole peripherals (CMSIS instance present) ------------------------------------------- */
#if defined(LPTIM1)
#define SPL_G0_HAS_LPTIM 1
#else
#define SPL_G0_HAS_LPTIM 0
#endif
#if defined(DAC1)
#define SPL_G0_HAS_DAC 1
#else
#define SPL_G0_HAS_DAC 0
#endif
#if defined(COMP1)
#define SPL_G0_HAS_COMP 1
#else
#define SPL_G0_HAS_COMP 0
#endif
#if defined(VREFBUF)
#define SPL_G0_HAS_VREFBUF 1
#else
#define SPL_G0_HAS_VREFBUF 0
#endif
#if defined(CRS)
#define SPL_G0_HAS_CRS 1
#else
#define SPL_G0_HAS_CRS 0
#endif
#if defined(FDCAN1)
#define SPL_G0_HAS_FDCAN 1
#else
#define SPL_G0_HAS_FDCAN 0
#endif
/* v0.9: AES and RNG on G041 / G061 / G081 / G0C1; CEC and UCPD1/2 on G071 / G081 / G0B1 / G0C1. */
#if defined(AES)
#define SPL_G0_HAS_AES 1
#else
#define SPL_G0_HAS_AES 0
#endif
#if defined(RNG)
#define SPL_G0_HAS_RNG 1
#else
#define SPL_G0_HAS_RNG 0
#endif
#if defined(CEC)
#define SPL_G0_HAS_CEC 1
#else
#define SPL_G0_HAS_CEC 0
#endif
#if defined(UCPD1)
#define SPL_G0_HAS_UCPD 1
#else
#define SPL_G0_HAS_UCPD 0
#endif
/* FLASH dual bank (ECC2R, BSY2, DUAL_BANK option): G0B0 / G0B1 / G0C1. */
#if defined(FLASH_SR_BSY2)
#define SPL_G0_HAS_FLASH_DUAL_BANK 1
#else
#define SPL_G0_HAS_FLASH_DUAL_BANK 0
#endif
/* PVD (PWR_CR2): not on the value line. */
#if defined(PWR_CR2_PVDE)
#define SPL_G0_HAS_PVD 1
#else
#define SPL_G0_HAS_PVD 0
#endif
/* EXTI bank 2 lines differ per SKU (G07x/G08x: 32..33, G0B0: 36 only, G0B1/G0C1: 32..36): the
 * driver uses the generated SPL_G0_MASK_EXTI_IMR2 (Private/stm32g0_sku_masks.h), not a probe. */

#endif /* STM32G0_SKU_H */
