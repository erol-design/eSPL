/* SPDX-License-Identifier: MIT */
#ifndef STM32G0_PWR_H
#define STM32G0_PWR_H
#include <stdint.h>
#include "stm32g0xx.h"
#include "stm32g0_status.h"

/* Power control (RM0444 Rev 6 section 4): voltage scaling, PVD, wake-up pins, low-power mode
 * selection, RTC domain access and SRAM retention.
 * Usage: independent calls; voltage scaling is PWR_StartVoltageScaling, then poll
 * PWR_GetVoltageScalingStatus.
 * No static state, no waiting. Output parameters are written only on OK unless the function
 * says otherwise.
 * Contract: Docs/v0.2_PWR_CONTRACT.md.
 * Not in v0.2: low-power run (LPR), flash power-down bits, PVM, pull-up/down in Standby (APC,
 * PUCRx/PDCRx), VBAT charging, ultra-low-power mode.
 * ERRATA ES0548 2.2.12 (silicon A, Z): leaving low-power run, Stop 1 or Standby within 400 us can
 *   leave the internal voltage reference wrong until the next low-power entry: stay >= 400 us,
 *   or enter through 400 us of low-power run. Timing is the caller's (no timer in this driver).
 * ERRATA ES0548 2.2.13 (silicon A, Z): first low-power entry > 20 ms after POR draws a few uA
 *   extra for some 100 ms: within 20 ms of POR do 400 us of low-power run (caller).
 * Caller duties (all APIs): PWR bus clock enabled first (RCC_PERIPH_PWR); serialize calls
 * (CR1..CR4 updates are read-modify-write). */

#ifdef __cplusplus
extern "C"
{
#endif

typedef enum
{
    PWR_RANGE1 = 1, /* PWR_CR1.VOS = 01, HCLK up to 64 MHz */
    PWR_RANGE2 = 2  /* PWR_CR1.VOS = 10, HCLK up to 16 MHz */
} PWR_RangeTypeDef;

typedef enum
{
    PWR_LPM_STOP0 = 0,
    PWR_LPM_STOP1 = 1,
    PWR_LPM_STANDBY = 3,
    PWR_LPM_SHUTDOWN = 4
} PWR_LowPowerModeTypeDef;

/* Wake-up pins of this SKU (bit n-1 = WKUPn), from the CMSIS PWR_CR3_EWUPx definitions:
 * WKUP1/2/4/6 on every G0, WKUP3 only on G0B0/G0B1/G0C1, WKUP5 only on G070/G071/G081/G0B0/G0B1/
 * G0C1 (2026-11-14 capability audit: the mask was 0x3F everywhere and armed reserved bits). */
#if defined(PWR_CR3_EWUP3)
#define PWR_WAKEUP_PIN3_BIT 0x04UL
#else
#define PWR_WAKEUP_PIN3_BIT 0UL
#endif
#if defined(PWR_CR3_EWUP5)
#define PWR_WAKEUP_PIN5_BIT 0x10UL
#else
#define PWR_WAKEUP_PIN5_BIT 0UL
#endif
#define PWR_WAKEUP_PINS_ALL (0x2BUL | PWR_WAKEUP_PIN3_BIT | PWR_WAKEUP_PIN5_BIT)
/* Shutdown (LPMS = 1xx) exists only on the G0x1 devices; on the value line (G030/G050/G070/G0B0,
 * RM0454 PWR_CR1) LPMS 1xx is reserved and PWR_CR1_LPMS_2 is not defined. */
#if defined(PWR_CR1_LPMS_2)
#define PWR_SHUTDOWN_SUPPORTED 1U
#else
#define PWR_SHUTDOWN_SUPPORTED 0U
#endif
#define PWR_PVD_RISING_PVD_IN 7U /* PVDRT = 111: PVD_IN pin against VREFINT */

/* PWR_CR1.DBP: RTC domain write access (RTC, TAMP backup registers, RCC_BDCR). */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status PWR_SetBackupAccess(uint8_t enable);
/* Requests a voltage range; poll PWR_GetVoltageScalingStatus. To range 2: current HCLK must be
 * <= 16 MHz (RCC_GetClocks with hse_hz) and the PLL off (PLLON = 0; configure it after the change,
 * RCC_ConfigPll checks the range-2 limits) -> else STATE. Range 1 has no precondition. */
/* Caller: pass the real HSE frequency (hse_hz); poll PWR_GetVoltageScalingStatus
 * (bounded) until ready = 1 before raising HCLK or turning the PLL on. */
/* Level: PARAM[F] STATE[F] */
SPL_Status PWR_StartVoltageScaling(PWR_RangeTypeDef range, uint32_t hse_hz);
/* ready = 1 when VOSF = 0 (regulator at the selected level). */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status PWR_GetVoltageScalingStatus(uint8_t *ready);
/* PVD thresholds and enable: falling 0..6 (PVDFT), rising 0..7 (PVDRT, 7 = PVD_IN pin; then
 * falling is ignored by hardware but still range-checked). SYSCFG_CFGR2.PVDL (PVD lock) set ->
 * STATE (the bits are write-protected until reset). */
/* Caller: PVD interrupt / wake-up through EXTI line 16 (EXTI driver) and the
 * PVD_VDDIO2_IRQn handler. */
/* Level: PARAM[F] STATE[F] UNSUPPORTED[r] */
SPL_Status PWR_ConfigPVD(uint32_t falling, uint32_t rising, uint8_t enable);
/* below = PVDO (1: VDD below the selected threshold). UNSUPPORTED (no PVD): *below = 0. */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] UNSUPPORTED[r] */
SPL_Status PWR_GetPVDOutput(uint8_t *below);
/* Enables (enable = 1, polarity first) or disables the WKUPx pins in pins (subset of
 * PWR_WAKEUP_PINS_ALL). falling = 1: low level / falling edge.
 * ERRATA ES0548 2.2.2: configuring a wake-up pin may set its WUFx spuriously; after enabling, the
 * driver clears WUFx of exactly the configured pins (RM workaround, part of the configuration). */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status PWR_ConfigWakeupPins(uint32_t pins, uint8_t enable, uint8_t falling);
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status PWR_GetWakeupFlags(uint32_t *flags);
/* SCR write-1-to-clear of the requested CWUFx only. */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status PWR_ClearWakeupFlags(uint32_t pins);
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status PWR_GetStandbyFlag(uint8_t *set);
/* Caller: none beyond the file-header duties. */
/* Level: - */
SPL_Status PWR_ClearStandbyFlag(void);
/* PWR_CR1.LPMS for the next deep sleep; does not set SCB_SCR.SLEEPDEEP and does not execute
 * WFI/WFE (caller).
 * ERRATA ES0548 2.2.4: with RCC_CR.HSIDIV != 0 the device does not enter Stop when SYSCLK = HSE,
 * and peripherals cannot wake it from Stop: Stop 0 / Stop 1 with HSIDIV != 0 -> SPL_ERR_STATE
 * (set HSIDIV = 1 first, RCC_SetHsiDiv). Changing HSIDIV after this call is not detected.
 * PWR_LPM_SHUTDOWN on a SKU without Shutdown (PWR_SHUTDOWN_SUPPORTED = 0): SPL_ERR_UNSUPPORTED. */
/* Caller: set SCB_SCR.SLEEPDEEP and execute WFI/WFE; keep HSIDIV = 1 until Stop entry
 * (a later change is not detected); ES0548 2.2.12 / 2.2.13 timing (file header). */
/* Level: PARAM[F] STATE[F] UNSUPPORTED[F] */
SPL_Status PWR_SetLowPowerMode(PWR_LowPowerModeTypeDef mode);
/* PWR_CR3.RRS: SRAM retained in Standby.
 * ERRATA ES0548 2.2.7 / ES0549 2.2.7 (G0B1 / G0C1 only, v0.12): on silicon A (REV_ID 0x1000)
 * Standby corrupts SRAM: enable = 1 -> SPL_ERR_UNSUPPORTED. On these SKUs a REV_ID other than
 * 0x1000 / 0x1001 / 0x1003 (for example 0 when the DBG clock is off, HW-006) -> SPL_ERR_STATE.
 * Other SKUs with RRS (G031..G081) have no such item and no REV_ID check (Docs/ERRATA_FAMILY.md).
 */
/* Caller: on G0B1 / G0C1 enable the DBG clock first so REV_ID can be read. */
/* Level: PARAM[F] STATE[r] UNSUPPORTED[r] */
SPL_Status PWR_SetSramRetention(uint8_t enable);

#ifdef __cplusplus
}
#endif
#endif /* STM32G0_PWR_H */
