#ifndef STM32G4_PWR_H
#define STM32G4_PWR_H
#include <stdint.h>
#include "stm32g4xx.h"
#include "stm32g4_status.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* PWR (RM0440 section 6). PWR clock (RCC APB1ENR1 PWREN) is the caller's duty.
     * Rejected calls write nothing. RMW APIs must be serialised by the caller.
     * See Docs/v0.10_PWR_CONTRACT.md for the voltage-scaling sequence. */

    typedef enum
    {
        PWR_RANGE1_NORMAL = 0, /* VOS=01, R1MODE=1, SYSCLK <= 150 MHz */
        PWR_RANGE1_BOOST = 1,  /* VOS=01, R1MODE=0, SYSCLK <= 170 MHz */
        PWR_RANGE2 = 2         /* VOS=10, SYSCLK <= 26 MHz */
    } PWR_RangeTypeDef;

    typedef enum
    {
        PWR_PVD_LEVEL0 = 0,
        PWR_PVD_LEVEL1 = 1,
        PWR_PVD_LEVEL2 = 2,
        PWR_PVD_LEVEL3 = 3,
        PWR_PVD_LEVEL4 = 4,
        PWR_PVD_LEVEL5 = 5,
        PWR_PVD_LEVEL6 = 6,
        PWR_PVD_EXT_INPUT = 7 /* PVD_IN compared with VREFINT */
    } PWR_PvdLevelTypeDef;

    typedef enum
    {
        PWR_LPM_STOP0 = 0,
        PWR_LPM_STOP1 = 1,
        PWR_LPM_STANDBY = 3,
        PWR_LPM_SHUTDOWN = 4
    } PWR_LowPowerModeTypeDef;

#define PWR_WAKEUP_PIN1 PWR_CR3_EWUP1
#define PWR_WAKEUP_PIN2 PWR_CR3_EWUP2
#define PWR_WAKEUP_PIN3 PWR_CR3_EWUP3
#define PWR_WAKEUP_PIN4 PWR_CR3_EWUP4
#define PWR_WAKEUP_PIN5 PWR_CR3_EWUP5
#define PWR_WAKEUP_PIN_ALL PWR_CR3_EWUP

    /* CR1.DBP = enable (0/1) with read-back; mismatch (PWR clock off) -> STATE. */
    /* Seviye: PARAM[S] STATE[r] */
    SPL_Status PWR_SetBackupAccess(uint8_t enable);
    /* Starts a range change (CR5.R1MODE, then CR1.VOS) and returns. Caller handles
     * FLASH latency, SYSCLK and AHB prescaler per RM0440 6.1.5. VOSF/REGLPF set ->
     * BUSY; LPR set -> STATE. */
    /* Seviye: PARAM[S] STATE[r] BUSY[r] */
    SPL_Status PWR_StartVoltageScaling(PWR_RangeTypeDef range);
    /* *ready = 1 when SR2.VOSF = 0. */
    /* Seviye: PARAM[S] */
    SPL_Status PWR_GetVoltageScalingStatus(uint8_t *ready);
    /* CR2.PLS/PVDE with read-back; SYSCFG_CFGR2.PVDL lock -> STATE. */
    /* Seviye: PARAM[S] STATE[r] */
    SPL_Status PWR_ConfigPVD(PWR_PvdLevelTypeDef level, uint8_t enable);
    /* *below = SR2.PVDO (1: VDD below the PVD threshold). */
    /* Seviye: PARAM[S] */
    SPL_Status PWR_GetPVDOutput(uint8_t *below);
    /* enable=1: CR4.WPx = falling, then CR3.EWUPx = 1. enable=0: CR3.EWUPx = 0. */
    /* Seviye: PARAM[S] */
    SPL_Status PWR_ConfigWakeupPins(uint32_t pins, uint8_t enable, uint8_t falling);
    /* *flags = SR1.WUF1..5 in PWR_WAKEUP_PINx layout. */
    /* Seviye: PARAM[S] */
    SPL_Status PWR_GetWakeupFlags(uint32_t *flags);
    /* SCR = pins (write-1-to-clear, only the requested CWUFx). ISR-safe. */
    /* Seviye: PARAM[S] */
    SPL_Status PWR_ClearWakeupFlags(uint32_t pins);
    /* CR1.LPMS for the next deep sleep. Does not set SLEEPDEEP or execute WFI/WFE.
     * ERRATA ES0431 2.2.9 / ES0430 2.2.9 / ES0523 2.2.7: Standby cannot be entered with HSE
     * bypass as SYSCLK: caller switches SYSCLK to HSI16 before WFI.
     * ERRATA ES0431 2.2.7 / ES0430 2.2.7 / ES0523 2.2.5: with DBG_SLEEP/DBG_STOP set, put an ISB
     * right after the caller's WFI/WFE. */
    /* Seviye: PARAM[S] */
    SPL_Status PWR_SetLowPowerMode(PWR_LowPowerModeTypeDef mode);

#ifdef __cplusplus
}
#endif
#endif
