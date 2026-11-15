#ifndef STM32G0_DBGMCU_H
#define STM32G0_DBGMCU_H
#include <stdint.h>
#include "stm32g0xx.h"
#include "stm32g0_status.h"

/* Debug support MCU registers (RM0444 Rev 6 section 40.10, Docs/v0.2_DBGMCU_CONTRACT.md).
 * G0 difference: DBG has its own APB clock (RCC_APBENR1.DBGEN). Writes to DBG_CR / DBG_APB_FZx with
 * DBGEN = 0 would be lost silently: they return SPL_ERR_STATE. DBGMCU_GetIdCode does not check
 * DBGEN: whether IDCODE reads without the clock is open (HW-006, G6).
 * ERRATA ES0548 2.2.8 (documentation): after a debugger connection, until power-down, the boot
 *   source after reset / low-power wake-up is the PA14-BOOT0 level latched before the connection.
 */

#ifdef __cplusplus
extern "C"
{
#endif

/* Freeze identifier: register (1 = APB_FZ1, 2 = APB_FZ2) in bits 7:5, bit position in 4:0. */
#define DBGMCU_FREEZE_ID(reg, bit) ((((uint32_t)(reg)) << 5U) | ((uint32_t)(bit)))
typedef enum
{
    DBGMCU_FREEZE_TIM2 = DBGMCU_FREEZE_ID(1, 0),
    DBGMCU_FREEZE_TIM3 = DBGMCU_FREEZE_ID(1, 1),
    DBGMCU_FREEZE_TIM4 = DBGMCU_FREEZE_ID(1, 2),
    DBGMCU_FREEZE_TIM6 = DBGMCU_FREEZE_ID(1, 4),
    DBGMCU_FREEZE_TIM7 = DBGMCU_FREEZE_ID(1, 5),
    DBGMCU_FREEZE_RTC = DBGMCU_FREEZE_ID(1, 10),
    DBGMCU_FREEZE_WWDG = DBGMCU_FREEZE_ID(1, 11),
    DBGMCU_FREEZE_IWDG = DBGMCU_FREEZE_ID(1, 12),
    DBGMCU_FREEZE_I2C1_SMBUS = DBGMCU_FREEZE_ID(1, 21),
    DBGMCU_FREEZE_I2C2_SMBUS = DBGMCU_FREEZE_ID(1, 22),
    DBGMCU_FREEZE_LPTIM2 = DBGMCU_FREEZE_ID(1, 30),
    DBGMCU_FREEZE_LPTIM1 = DBGMCU_FREEZE_ID(1, 31),
    DBGMCU_FREEZE_TIM1 = DBGMCU_FREEZE_ID(2, 11),
    DBGMCU_FREEZE_TIM14 = DBGMCU_FREEZE_ID(2, 15),
    DBGMCU_FREEZE_TIM15 = DBGMCU_FREEZE_ID(2, 16),
    DBGMCU_FREEZE_TIM16 = DBGMCU_FREEZE_ID(2, 17),
    DBGMCU_FREEZE_TIM17 = DBGMCU_FREEZE_ID(2, 18)
} DBGMCU_FreezeTypeDef;

/* DBG_CR low-power debug modes. */
#define DBGMCU_LP_STOP DBG_CR_DBG_STOP
#define DBGMCU_LP_STANDBY DBG_CR_DBG_STANDBY
#define DBGMCU_LP_ALL (DBGMCU_LP_STOP | DBGMCU_LP_STANDBY)

/* DEV_ID (0x467 for G0B1/G0C1) and REV_ID (0x1000 A, 0x1001 Z, 0x1003 Y per ES0548). */
/* Seviye: PARAM[S] */
SPL_Status DBGMCU_GetIdCode(uint16_t *dev_id, uint16_t *rev_id);
/* DBG_CR = modes (subset of DBGMCU_LP_ALL, 0 allowed). Not reset by a system reset. */
/* Seviye: PARAM[S] STATE[S] */
SPL_Status DBGMCU_SetLowPowerDebug(uint32_t modes);
/* Seviye: PARAM[S] STATE[S] */
SPL_Status DBGMCU_GetLowPowerDebug(uint32_t *modes);
/* Seviye: PARAM[S,n] STATE[S] UNSUPPORTED[S] */
SPL_Status DBGMCU_SetFreeze(DBGMCU_FreezeTypeDef id, uint8_t freeze);
/* Seviye: PARAM[S] STATE[S] UNSUPPORTED[S] */
SPL_Status DBGMCU_GetFreeze(DBGMCU_FreezeTypeDef id, uint8_t *frozen);
/* v0.12: DBG_APB_FZ1 = mask (whole register, no read; DBG_APB_FZ1_* bits of this SKU, else PARAM).
 * ERRATA ES0544 2.2.6 / ES0545 2.2.7 / ES0546 2.2.7 (G050 / G051 / G061): APB_FZ1 reads back
 * wrong TIM6 / TIM7 / RTC
 * bits. There DBGMCU_SetFreeze on an APB_FZ1 id -> UNSUPPORTED (its read-modify-write would store
 * the wrong bits) and DBGMCU_GetFreeze on TIM6 / TIM7 / RTC -> UNSUPPORTED; this call works. */
/* Seviye: PARAM[S] STATE[S] */
SPL_Status DBGMCU_WriteFreeze1(uint32_t mask);

#ifdef __cplusplus
}
#endif
#endif /* STM32G0_DBGMCU_H */
