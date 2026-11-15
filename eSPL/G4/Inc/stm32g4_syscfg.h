#ifndef STM32G4_SYSCFG_H
#define STM32G4_SYSCFG_H
#include <stdint.h>
#include "stm32g4xx.h"
#include "stm32g4_status.h"

/* v0.12 SYSCFG extras (RM0440 Rev 9 chapter 10): memory remap, flash bank swap,
 * CCM SRAM erase / write protection, FPU interrupt enables, analog switch
 * supply, Fast-mode Plus, CFGR2 break-input locks and the SRAM parity flag.
 * Contract: Docs/v0.12_SYSCFG_CONTRACT.md.
 * ERRATA ES0431 2.2.6 / ES0430 2.2.6 / ES0523 2.2.4: the first SRAM access after a system
 * reset may be a lost write: startup code dummy-reads each SRAM instance used (G431:
 * 0x20000000, 0x20004000, 0x10000000; G47x/48x and G491/G4A1: 0x20000000, 0x20008000,
 * 0x20010000, 0x20014000, 0x10000000); with parity enabled use a variable initialisation
 * instead (caller).
 * EXTICR (GPIO port -> EXTI line) is NOT here: use EXTI_SetGpioSource.
 *
 * Common rules:
 *  - SYSCFG clock (RCC_APB2ENR.SYSCFGEN) off -> SPL_ERR_STATE, no write
 *    (the bus ignores writes and reads return 0 with the clock off).
 *  - Invalid argument -> SPL_ERR_PARAM, feature missing on the SKU ->
 *    SPL_ERR_UNSUPPORTED; both before any register write.
 *  - CFGR1/MEMRMP read-modify-write: one context (start-up code).
 *  - Write-once bits (SWPR PxWP, CFGR2 CLL/SPL/PVDL/ECCL; "rs", cleared only by
 *    a system reset) have no unlock API. */

#ifdef __cplusplus
extern "C"
{
#endif

/* CCM SRAM write-protection pages (1 KB each, RM0440 2.4.2 Table 4): 10 pages on
 * category 2 (G431/G441), 16 on category 4 (G491/G4A1), 32 on category 3
 * (G47x/G48x); derived from the CMSIS SYSCFG_SWPR_PAGEx macros of the SKU. */
#if defined(SYSCFG_SWPR_PAGE31_Msk)
#define SYSCFG_CCM_PAGES_ALL 0xFFFFFFFFU
#elif defined(SYSCFG_SWPR_PAGE15_Msk)
#define SYSCFG_CCM_PAGES_ALL 0x0000FFFFU
#elif defined(SYSCFG_SWPR_PAGE9_Msk)
#define SYSCFG_CCM_PAGES_ALL 0x000003FFU
#else
#error "SYSCFG_SWPR page macros missing in the CMSIS device header"
#endif

/* CFGR1 FPU_IE[5:0] (bits 31:26, RM0440 10.2.2). Reset value 0x7C000000:
 * FPU_IE[4:0] are SET after reset, FPU_IE[5] (inexact) is clear. */
#define SYSCFG_FPU_IE_INVALID SYSCFG_CFGR1_FPU_IE_0
#define SYSCFG_FPU_IE_DIVZERO SYSCFG_CFGR1_FPU_IE_1
#define SYSCFG_FPU_IE_UNDERFLOW SYSCFG_CFGR1_FPU_IE_2
#define SYSCFG_FPU_IE_OVERFLOW SYSCFG_CFGR1_FPU_IE_3
#define SYSCFG_FPU_IE_DENORMAL SYSCFG_CFGR1_FPU_IE_4
#define SYSCFG_FPU_IE_INEXACT SYSCFG_CFGR1_FPU_IE_5
#define SYSCFG_FPU_IE_ALL                                                                          \
    (SYSCFG_CFGR1_FPU_IE_0 | SYSCFG_CFGR1_FPU_IE_1 | SYSCFG_CFGR1_FPU_IE_2 |                       \
     SYSCFG_CFGR1_FPU_IE_3 | SYSCFG_CFGR1_FPU_IE_4 | SYSCFG_CFGR1_FPU_IE_5)

/* CFGR1 Fast-mode Plus bits accepted by SYSCFG_SetFastModePlus (per SKU). */
#if defined(SYSCFG_CFGR1_I2C3_FMP_Msk)
#define SYSCFG_FMP_I2C3_BIT SYSCFG_CFGR1_I2C3_FMP_Msk
#else
#define SYSCFG_FMP_I2C3_BIT 0U
#endif
#if defined(SYSCFG_CFGR1_I2C4_FMP_Msk)
#define SYSCFG_FMP_I2C4_BIT SYSCFG_CFGR1_I2C4_FMP_Msk
#else
#define SYSCFG_FMP_I2C4_BIT 0U
#endif
#define SYSCFG_FMP_ALL                                                                             \
    (SYSCFG_CFGR1_I2C_PB6_FMP_Msk | SYSCFG_CFGR1_I2C_PB7_FMP_Msk | SYSCFG_CFGR1_I2C_PB8_FMP_Msk |  \
     SYSCFG_CFGR1_I2C_PB9_FMP_Msk | SYSCFG_CFGR1_I2C1_FMP_Msk | SYSCFG_CFGR1_I2C2_FMP_Msk |        \
     SYSCFG_FMP_I2C3_BIT | SYSCFG_FMP_I2C4_BIT)

/* CFGR2 lock bits (rs, cleared only by system reset) for SYSCFG_Lock. Each
 * connects a fault source to the TIM1/8/15/16/17/20 break inputs and HRTIM1
 * hrtim_sys_flt. PVDL additionally makes PWR_CR2 PVDE/PLS read-only, so
 * PWR_ConfigPVD returns SPL_ERR_STATE afterwards. */
#define SYSCFG_LOCK_LOCKUP SYSCFG_CFGR2_CLL_Msk /* Cortex-M4 LOCKUP (HardFault) */
#define SYSCFG_LOCK_PARITY SYSCFG_CFGR2_SPL_Msk /* SRAM1/CCM SRAM parity error */
#define SYSCFG_LOCK_PVD SYSCFG_CFGR2_PVDL_Msk   /* PVD + PWR_CR2 PVDE/PLS lock */
#define SYSCFG_LOCK_ECC SYSCFG_CFGR2_ECCL_Msk   /* Flash ECC error */
#define SYSCFG_LOCK_ALL                                                                            \
    (SYSCFG_LOCK_LOCKUP | SYSCFG_LOCK_PARITY | SYSCFG_LOCK_PVD | SYSCFG_LOCK_ECC)

    /* MEMRMP.MEM_MODE values (RM0440 10.2.1). FMC needs an FMC (G47x/G48x),
     * QUADSPI a QUADSPI (G47x/G48x/G491/G4A1); otherwise UNSUPPORTED. */
    typedef enum
    {
        SYSCFG_MEM_FLASH = 0,
        SYSCFG_MEM_SYSTEM = 1,
        SYSCFG_MEM_FMC = 2,
        SYSCFG_MEM_SRAM1 = 3,
        SYSCFG_MEM_QUADSPI = 4
    } SYSCFG_MemoryModeTypeDef;

    /* CFGR1 BOOSTEN/ANASWVDD per RM0440 Table 60 (see contract open item). */
    typedef enum
    {
        SYSCFG_ANASW_VDDA = 0,   /* BOOSTEN=0 ANASWVDD=0: VDDA > 2.4 V */
        SYSCFG_ANASW_VDD = 1,    /* BOOSTEN=0 ANASWVDD=1: VDDA < 2.4 V, VDD > 2.4 V */
        SYSCFG_ANASW_BOOSTER = 2 /* BOOSTEN=1 ANASWVDD=0: VDDA < 2.4 V, VDD < 2.4 V */
    } SYSCFG_AnalogSwitchTypeDef;

    /* Changes what is aliased at 0x00000000 (also the vector table when VTOR=0).
     * MEM_MODE only; FB_MODE kept. */
    /* Seviye: PARAM[S] STATE[r] UNSUPPORTED[r] */
    SPL_Status SYSCFG_SetMemoryMode(SYSCFG_MemoryModeTypeDef mode);
    /* Raw MEM_MODE (0..7); after reset it reflects BOOT0/nBOOT0/BOOT1. */
    /* Seviye: PARAM[S] STATE[r] */
    SPL_Status SYSCFG_GetMemoryMode(uint32_t *mode);

    /* MEMRMP.FB_MODE (dual-bank SKUs only, FLASH_OPTR_DBANK in CMSIS; else
     * UNSUPPORTED). DANGER: swaps the banks under the running code and changes
     * which bank the WRP/PCROP registers protect (RM0440 3.5.2/3.5.3); call only from code
     * running in RAM, or when both banks hold matching images. swap 0/1. */
    /* Seviye: PARAM[n] STATE[r] UNSUPPORTED[r] */
    SPL_Status SYSCFG_SetFlashBankSwap(uint8_t swap);
    /* Seviye: PARAM[S] STATE[r] UNSUPPORTED[r] */
    SPL_Status SYSCFG_GetFlashBankSwap(uint8_t *swapped);

    /* Starts a hardware CCM SRAM erase: SKR=0xCA, SKR=0x53, SCSR=CCMER
     * (RM0440 10.2.7/10.2.10). CCMBSY or CCMER set -> BUSY, no write. Destroys
     * all CCM SRAM content (and the SRAM2-end alias): never while stack, data
     * or code live there. Non-blocking: poll SYSCFG_CcmEraseStatus. */
    /* Seviye: STATE[r] BUSY[r] */
    SPL_Status SYSCFG_CcmEraseStart(void);
    /* BUSY while CCMBSY or CCMER is set, OK when no erase is running. */
    /* Seviye: STATE[r] BUSY[r] */
    SPL_Status SYSCFG_CcmEraseStatus(void);

    /* Sets PxWP for each page in 'pages' (subset of SYSCFG_CCM_PAGES_ALL, non-zero).
     * Write-once: protection is removed only by a system reset. Pages already
     * protected are accepted; if all are, nothing is written. */
    /* Seviye: PARAM[S,n] STATE[r] */
    SPL_Status SYSCFG_CcmWriteProtect(uint32_t pages);
    /* Seviye: PARAM[S] STATE[r] */
    SPL_Status SYSCFG_GetCcmWriteProtect(uint32_t *pages);

    /* Sets CFGR1.FPU_IE[5:0] to exactly 'mask' (subset of SYSCFG_FPU_IE_ALL).
     * The FPU interrupt also needs NVIC_Enable(FPU_IRQn). */
    /* Seviye: PARAM[S] STATE[r] */
    SPL_Status SYSCFG_SetFpuInterrupts(uint32_t mask);
    /* Seviye: PARAM[S] STATE[r] */
    SPL_Status SYSCFG_GetFpuInterrupts(uint32_t *mask);

    /* Writes BOOSTEN/ANASWVDD for the I/O analog switch supply. */
    /* Seviye: PARAM[r] STATE[r] */
    SPL_Status SYSCFG_SetAnalogSwitchSupply(SYSCFG_AnalogSwitchTypeDef supply);

    /* Sets (enable=1) or clears (enable=0) the given Fm+ bits (subset of
     * SYSCFG_FMP_ALL, non-zero). Program after the GPIO AF selection; with Fm+
     * the pin OSPEEDR setting is ignored (RM0440 10.2.2). */
    /* Seviye: PARAM[S,n] STATE[r] */
    SPL_Status SYSCFG_SetFastModePlus(uint32_t mask, uint8_t enable);

    /* Sets the given CFGR2 lock bits (subset of SYSCFG_LOCK_ALL, non-zero).
     * Write-once until system reset. Never writes SPF=1. If all requested
     * locks are already set, nothing is written. */
    /* Seviye: PARAM[S,n] STATE[r] */
    SPL_Status SYSCFG_Lock(uint32_t locks);
    /* Seviye: PARAM[S] STATE[r] */
    SPL_Status SYSCFG_GetLocks(uint32_t *locks);

    /* SRAM1/CCM SRAM parity error flag SPF (rc_w1). Not cleared by Get. */
    /* Seviye: PARAM[S] STATE[r] */
    SPL_Status SYSCFG_GetParityError(uint8_t *flag);
    /* Writes SPF=1 (clear); lock bits are written back as read (no effect,
     * rs bits), so no lock is set or lost. */
    /* Seviye: STATE[r] */
    SPL_Status SYSCFG_ClearParityError(void);

#ifdef __cplusplus
}
#endif
#endif
