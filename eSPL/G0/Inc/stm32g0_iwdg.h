#ifndef STM32G0_IWDG_H
#define STM32G0_IWDG_H
#include <stdint.h>
#include "stm32g0xx.h"
#include "stm32g0_status.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* Independent watchdog (RM0444 Rev 6 section 28). Single IWDG instance,
 * no static state. Once started the IWDG cannot be stopped except by a
 * reset (RM0444 28.3.2). The hardware-watchdog option bit may start it at
 * power-on without any software action. */

/* IWDG_KR key values (RM0444 28.4.1). Not provided by CMSIS. */
#define IWDG_KEY_RELOAD 0x0000AAAAUL
#define IWDG_KEY_ACCESS 0x00005555UL
#define IWDG_KEY_START 0x0000CCCCUL

/* Nominal LSI frequency (RM0444 5.2: "about 32 kHz"). The real tolerance is in
 * the device datasheet; derate timeouts accordingly. */
#define IWDG_LSI_NOMINAL_HZ 32000UL

/* KR=0xCCCC. Irreversible. Also starts the LSI if needed. */
/* Seviye: - */
SPL_Status IWDG_Start(void);
/* KR=0x5555, then PR (0..7) and RLR (1..0xFFF). BUSY if PVU/RVU/WVU set. */
/* Seviye: PARAM[S] BUSY[r] */
SPL_Status IWDG_SetPrescalerReload(uint32_t prescaler, uint32_t reload);
/* KR=0x5555, then WINR (1..0xFFF; 0xFFF disables the window). BUSY unless
 * IWDG_SR == 0. Writing WINR also reloads the counter (RM0444 28.3.2). */
/* Seviye: PARAM[S] BUSY[r] */
SPL_Status IWDG_SetWindow(uint32_t window);
/* KR=0xAAAA. Outside the window this resets the MCU. */
/* Seviye: - */
SPL_Status IWDG_Refresh(void);
/* BUSY while any of PVU/RVU/WVU is set, else OK. These bits clear only
 * with the LSI running; LSIRDY = 0 is not reported as STATE because it is
 * also the normal state during LSI start-up right after IWDG_Start. The
 * update takes up to five (LSI) cycles once the LSI runs (RM0444 28.4.4);
 * the caller bounds its retries. */
/* Seviye: BUSY[r] */
SPL_Status IWDG_UpdateStatus(void);
/* Nominal timeout in microseconds at IWDG_LSI_NOMINAL_HZ (pure calculation). */
/* Seviye: PARAM[S] */
SPL_Status IWDG_CalcTimeoutUs(uint32_t prescaler, uint32_t reload, uint32_t *timeout_us);

#ifdef __cplusplus
}
#endif
#endif
