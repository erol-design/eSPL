#ifndef STM32G4_DBGMCU_H
#define STM32G4_DBGMCU_H
#include <stdint.h>
#include "stm32g4xx.h"
#include "stm32g4_status.h"

/* v0.12 DBGMCU (RM0440 47.16): peripheral freeze while the core is halted,
 * low-power debug modes, device ID. Contract: Docs/v0.12_DBGMCU_CONTRACT.md.
 *
 * DBGMCU registers are on the PPB (0xE0042000), need no RCC clock, are reset by
 * power-on reset only (system reset does not clear them) and accept 32-bit
 * accesses only. The debugger may also write them (Keil dbgconf), so the
 * application and the debug script share them: these APIs change only the
 * requested bit(s) (read-modify-write, single context, typically start-up).
 * Freeze has an effect only while a debugger halts the core. */

#ifdef __cplusplus
extern "C"
{
#endif

    /* Freeze sources. Every ID exists on every SKU; an ID whose CMSIS bit macro
     * is not defined for the build SKU returns SPL_ERR_UNSUPPORTED
     * (of the six checked SKUs: TIM5/I2C4 on G474 only, TIM20 on G474/G491/G4A1,
     * HRTIM1 on G474/G414, I2C3 not on G411xB). There are no FDCAN,
     * USART or SPI freeze bits on STM32G4 (RM0440 47.16.4..47.16.6). */
    typedef enum
    {
        DBGMCU_FZ_TIM2 = 0, /* APB1FZR1 */
        DBGMCU_FZ_TIM3,
        DBGMCU_FZ_TIM4,
        DBGMCU_FZ_TIM5,
        DBGMCU_FZ_TIM6,
        DBGMCU_FZ_TIM7,
        DBGMCU_FZ_RTC,
        DBGMCU_FZ_WWDG,
        DBGMCU_FZ_IWDG,
        DBGMCU_FZ_I2C1, /* SMBus timeout counter only */
        DBGMCU_FZ_I2C2,
        DBGMCU_FZ_I2C3,
        DBGMCU_FZ_LPTIM1,
        DBGMCU_FZ_I2C4, /* APB1FZR2 */
        DBGMCU_FZ_TIM1, /* APB2FZ (RM name DBGMCU_APB2FZR) */
        DBGMCU_FZ_TIM8,
        DBGMCU_FZ_TIM15,
        DBGMCU_FZ_TIM16,
        DBGMCU_FZ_TIM17,
        DBGMCU_FZ_TIM20,
        DBGMCU_FZ_HRTIM1,
        DBGMCU_FZ_COUNT
    } DBGMCU_FreezeTypeDef;

/* DBGMCU_CR low-power debug bits accepted by DBGMCU_SetLowPowerDebug. */
#define DBGMCU_LP_SLEEP DBGMCU_CR_DBG_SLEEP
#define DBGMCU_LP_STOP DBGMCU_CR_DBG_STOP
#define DBGMCU_LP_STANDBY DBGMCU_CR_DBG_STANDBY
#define DBGMCU_LP_ALL (DBGMCU_LP_SLEEP | DBGMCU_LP_STOP | DBGMCU_LP_STANDBY)

    /* freeze=1: counter stopped while the core is halted; 0: keeps counting.
     * Only the selected bit changes. id >= COUNT or freeze > 1 -> PARAM;
     * bit not on this SKU -> UNSUPPORTED; no write in both cases. */
    /* Seviye: PARAM[n,r] UNSUPPORTED[r] */
    SPL_Status DBGMCU_SetFreeze(DBGMCU_FreezeTypeDef id, uint8_t freeze);
    /* Seviye: PARAM[S,r] UNSUPPORTED[r] */
    SPL_Status DBGMCU_GetFreeze(DBGMCU_FreezeTypeDef id, uint8_t *frozen);

    /* Sets DBG_SLEEP/DBG_STOP/DBG_STANDBY to exactly 'modes' (subset of
     * DBGMCU_LP_ALL); TRACE_IOEN/TRACE_MODE are kept. Other bits -> PARAM.
     * Effect (RM0440 47.16.1): clocks (and for STANDBY the regulators) stay on in
     * that low-power mode so the debugger keeps the connection. Leaves current
     * consumption high: clear for power measurements.
     * ERRATA ES0431 2.2.7 / ES0430 2.2.7 / ES0523 2.2.5: with DBG_SLEEP or DBG_STOP set, wrong
     * flash fetches can follow a Sleep/Stop wakeup: caller puts an ISB right after WFI/WFE (Sleep
     * only alternative: keep RCC_AHB1SMENR.FLASHSMEN = 1). Debug-only issue.
     * ERRATA ES0431 2.2.1 / ES0430 2.2.1 / ES0523 2.2.1: 4-wire JTAG needs PB4 = NJTRST; use SWD.
     */
    /* Seviye: PARAM[S] */
    SPL_Status DBGMCU_SetLowPowerDebug(uint32_t modes);
    /* Seviye: PARAM[S] */
    SPL_Status DBGMCU_GetLowPowerDebug(uint32_t *modes);

    /* IDCODE (RM0440 47.6.1): devId = DEV_ID[11:0] (0x468 category 2 = G431/G441,
     * 0x469 category 3 = G471/G473/G474/G483/G484, 0x479 category 4 = G491/G4A1;
     * G411/G414 are not listed in RM0440 Rev 9), revId = REV_ID[15:0]. Raw values,
     * no interpretation. Either pointer may be NULL, not both.
     * Errata applicability depends on REV_ID: 0x2001 Z, 0x2002 Y, 0x2003 X (ES0430 / ES0431
     * Rev 10, DEV_ID 0x468/0x469); 0x1000 A, 0x1001 Z (ES0523 Rev 7, DEV_ID 0x479). No errata
     * sheet exists here for G411/G414. */
    /* Seviye: PARAM[n] */
    SPL_Status DBGMCU_GetIdCode(uint16_t *devId, uint16_t *revId);

#ifdef __cplusplus
}
#endif
#endif
