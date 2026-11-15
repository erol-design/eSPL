#ifndef STM32G0_VREFBUF_H
#define STM32G0_VREFBUF_H
#include <stdint.h>
#include "stm32g0xx.h"
#include "stm32g0_status.h"
#include "stm32g0_sku.h"
#ifdef __cplusplus
extern "C"
{
#endif

/* v0.8 SKU profile: this API exists only where the device has the peripheral (SPL_G0_HAS_VREFBUF);
 * absent on G030 / G050 / G070 / G0B0. */
#if SPL_G0_HAS_VREFBUF
/* Voltage reference buffer (VREFBUF, RM0444 Rev 6 section 17; Docs/v0.5_VREFBUF_CONTRACT.md).
 * Ported from the G4 SPL v0.13.5 driver. G0 difference: two scales only (VRS is one bit).
 * Single instance, no static state, no waits.
 * Caller duties:
 *  - Clock: SYSCFG clock in RCC (SYSCFG + COMP + VREFBUF).
 *  - Packages where VREF+ is double-bonded with VDDA: VREFBUF must stay disabled (RM0444
 *    17.1); the driver cannot detect the package.
 *  - VREFBUF_MODE_OFF_PULLDOWN pulls VREF+ to VSSA: never select it while an external reference
 *    drives VREF+.
 *  - Minimum VDDA per scale: device datasheet (RM0444 17.2 note a). */
/* VREFBUF_CSR.VRS values (RM0444 17.3.1). */
#define VREFBUF_SCALE_2V048 0U
#define VREFBUF_SCALE_2V5 1U
/* ENVR/HIZ combinations (RM0444 Table 91). The zero code is the harmless reset mode (EXTERNAL):
 * a zero-initialised variable never pulls VREF+ down. Pull-down needs its explicit code. */
#define VREFBUF_MODE_EXTERNAL 0U     /* ENVR=0 HIZ=1: VREF+ input (reset). */
#define VREFBUF_MODE_INTERNAL 1U     /* ENVR=1 HIZ=0: buffer drives VREF+. */
#define VREFBUF_MODE_HOLD 2U         /* ENVR=1 HIZ=1: hold mode, VRR frozen. */
#define VREFBUF_MODE_OFF_PULLDOWN 3U /* ENVR=0 HIZ=0: buffer off, VREF+ to VSSA. */
/* VRS write. PARAM: scale > 1. STATE: ENVR=1. Deliberate, conservative rule kept from G4
 * (RM0440 23.4.1): RM0444 17.2 / 17.3.1 state no ENVR condition for VRS (2026-11-14 capability
 * audit: reviewed, kept); set the scale first, then VREFBUF_SetMode(INTERNAL). */
/* Seviye: PARAM[S] STATE[S] */
SPL_Status VREFBUF_SetScale(uint32_t scale);
/* Writes ENVR and HIZ in one CSR write, VRS kept. After INTERNAL poll VREFBUF_ReadyStatus. */
/* Seviye: PARAM[r] */
SPL_Status VREFBUF_SetMode(uint32_t mode);
/* OK: VRR=1 in INTERNAL mode. BUSY: INTERNAL mode, VRR=0 (poll again).
 * STATE: ENVR=0 (never ready) or HOLD mode (VRR detection disabled). */
/* Seviye: STATE[S] BUSY[r] */
SPL_Status VREFBUF_ReadyStatus(void);
/* *trim = VREFBUF_CCR.TRIM[5:0] (read only; the driver never writes TRIM). */
/* Seviye: PARAM[S] */
SPL_Status VREFBUF_GetTrim(uint32_t *trim);
#endif /* SPL_G0_HAS_VREFBUF */

#ifdef __cplusplus
}
#endif
#endif /* STM32G0_VREFBUF_H */
