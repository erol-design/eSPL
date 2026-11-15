#ifndef STM32G0_SYSCFG_H
#define STM32G0_SYSCFG_H
#include <stdint.h>
#include "stm32g0xx.h"
#include "stm32g0_status.h"

/* System configuration controller (RM0444 Rev 6 section 8, Docs/v0.2_SYSCFG_CONTRACT.md).
 * The SYSCFG bus clock (RCC_PERIPH_SYSCFG) must be enabled by the caller.
 * G4 difference: EXTI line routing is in EXTI (EXTICR), not in SYSCFG; G0 adds the interrupt-line
 * status registers (ITLINE0..31). Not in v0.2: IR modulation (IRTIM), UCPD strobes (UCPD out of
 * scope), clamping diodes (CDEN). */

#ifdef __cplusplus
extern "C"
{
#endif

typedef enum
{
    SYSCFG_MEM_FLASH = 0,  /* MEM_MODE = x0 */
    SYSCFG_MEM_SYSTEM = 1, /* MEM_MODE = 01 */
    SYSCFG_MEM_SRAM = 3    /* MEM_MODE = 11 */
} SYSCFG_MemoryModeTypeDef;

/* CFGR1 FM+ enable bits accepted by SYSCFG_SetFastModePlus (v0.8: I2C3_FMP only where the SKU
 * has I2C3, group 0BX). */
#if defined(SYSCFG_CFGR1_I2C3_FMP)
#define SYSCFG_FMP_I2C3 SYSCFG_CFGR1_I2C3_FMP
#else
#define SYSCFG_FMP_I2C3 0U
#endif
#define SYSCFG_FMP_ALL                                                                             \
    (SYSCFG_CFGR1_I2C_PB6_FMP | SYSCFG_CFGR1_I2C_PB7_FMP | SYSCFG_CFGR1_I2C_PB8_FMP |              \
     SYSCFG_CFGR1_I2C_PB9_FMP | SYSCFG_CFGR1_I2C1_FMP | SYSCFG_CFGR1_I2C2_FMP |                    \
     SYSCFG_CFGR1_I2C_PA9_FMP | SYSCFG_CFGR1_I2C_PA10_FMP | SYSCFG_FMP_I2C3)
/* CFGR1 pin remap bits: PA11 -> PA9, PA12 -> PA10 (the original PA9/PA10 is forced analog). */
#define SYSCFG_REMAP_ALL (SYSCFG_CFGR1_PA11_RMP | SYSCFG_CFGR1_PA12_RMP)
/* CFGR2 set-only locks: Cortex LOCKUP to TIM break, SRAM parity, PVD (not on the value line,
 * v0.8), flash ECC error. */
#if defined(SYSCFG_CFGR2_PVDL)
#define SYSCFG_LOCK_PVD SYSCFG_CFGR2_PVDL
#else
#define SYSCFG_LOCK_PVD 0U
#endif
#define SYSCFG_LOCK_ALL (SYSCFG_CFGR2_CLL | SYSCFG_CFGR2_SPL | SYSCFG_LOCK_PVD | SYSCFG_CFGR2_ECCL)

/* MEM_MODE remap of address 0. The caller relocates VTOR / code consistently. */
/* Seviye: PARAM[S] */
SPL_Status SYSCFG_SetMemoryMode(SYSCFG_MemoryModeTypeDef mode);
/* Raw MEM_MODE (0..3; 2 also means main flash). */
/* Seviye: PARAM[S] */
SPL_Status SYSCFG_GetMemoryMode(uint32_t *mode);
/* Sets (enable = 1) or clears the FM+ bits in mask (non-empty subset of SYSCFG_FMP_ALL). */
/* Seviye: PARAM[S,n] */
SPL_Status SYSCFG_SetFastModePlus(uint32_t mask, uint8_t enable);
/* Sets or clears PA11_RMP / PA12_RMP (mask: non-empty subset of SYSCFG_REMAP_ALL). */
/* Seviye: PARAM[S,n] */
SPL_Status SYSCFG_SetPinRemap(uint32_t mask, uint8_t enable);
/* BOOSTEN: I/O analog switch booster (recommended for VDD < 2.4 V, RM0444 8.1.1). */
/* Seviye: PARAM[S,n] */
SPL_Status SYSCFG_SetAnalogBooster(uint8_t enable);
/* Sets the locks in mask (non-empty subset of SYSCFG_LOCK_ALL); cleared only by a system reset.
 * The SPF parity flag (write-1-to-clear) is never written back by this read-modify-write. */
/* Seviye: PARAM[S,n] */
SPL_Status SYSCFG_Lock(uint32_t locks);
/* Seviye: PARAM[S] */
SPL_Status SYSCFG_GetLocks(uint32_t *locks);
/* SRAM parity error flag SPF and its write-1 clear. */
/* Seviye: PARAM[S] */
SPL_Status SYSCFG_GetParityError(uint8_t *flag);
/* Seviye: - */
SPL_Status SYSCFG_ClearParityError(void);
/* Raw ITLINEx (line 0..31): pending sources of the shared interrupt vector x. */
/* Seviye: PARAM[S] */
SPL_Status SYSCFG_GetInterruptLineStatus(uint32_t line, uint32_t *status);

#ifdef __cplusplus
}
#endif
#endif /* STM32G0_SYSCFG_H */
