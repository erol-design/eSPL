#ifndef STM32G4_VREFBUF_H
#define STM32G4_VREFBUF_H
#include <stdint.h>
#include "stm32g4xx.h"
#include "stm32g4_status.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* Voltage reference buffer (VREFBUF, RM0440 Rev 9 section 23). Single
 * instance, no static state, no waits. CMSIS defines VREFBUF on G431, G474,
 * G491 and G4A1 but not on G411xB or G414: there every function returns
 * SPL_ERR_UNSUPPORTED without any access.
 * Caller duties:
 *  - APB2 clock: RCC_APB2ENR.SYSCFGEN (SYSCFG + COMP + VREFBUF + OPAMP).
 *  - VREF+ pin conflicts, per mode that acts on the pin (CCL-2 ANL-09; the
 *    driver cannot detect the package or the board):
 *    INTERNAL (buffer drives VREF+) and OFF_PULLDOWN (VREF+ pulled to VSSA):
 *    never on packages where VREF+ is double-bonded with VDDA (RM0440 23.1:
 *    the buffer would fight VDDA, the pull-down would load it), and never
 *    while an external reference or any other source drives VREF+ (two
 *    sources on one pin / the pull-down loads that reference). Allowed only
 *    with VREF+ connected to its decoupling capacitor alone.
 *    EXTERNAL (reset) and HOLD: VREFBUF does not drive VREF+.
 *  - Minimum VDDA per scale: device datasheet (RM0440 23.2 note a).
 * Contract: Docs/v0.11_VREFBUF_CONTRACT.md. */

/* VREFBUF_CSR.VRS values (RM0440 23.4.1); 3 is reserved. */
#define VREFBUF_SCALE_2V048 0U
#define VREFBUF_SCALE_2V5 1U
#define VREFBUF_SCALE_2V9 2U

/* ENVR/HIZ combinations (RM0440 Table 197). The zero code is the harmless
 * reset mode (EXTERNAL): a zero-initialised variable never pulls VREF+ down.
 * Pull-down needs its explicit non-zero code. */
#define VREFBUF_MODE_EXTERNAL 0U     /* ENVR=0 HIZ=1: VREF+ input (reset). */
#define VREFBUF_MODE_INTERNAL 1U     /* ENVR=1 HIZ=0: buffer drives VREF+. */
#define VREFBUF_MODE_HOLD 2U         /* ENVR=1 HIZ=1: hold mode, VRR frozen. */
#define VREFBUF_MODE_OFF_PULLDOWN 3U /* ENVR=0 HIZ=0: buffer off, VREF+ to VSSA. */

    /* VRS write. PARAM: scale > 2. STATE: ENVR=1 (RM0440 23.4.1 note).
     * Hardware reloads the factory TRIM for the new scale (RM0440 23.3). */
    /* Seviye: PARAM[S] STATE[r] UNSUPPORTED[r] */
    SPL_Status VREFBUF_SetScale(uint32_t scale);
    /* Writes ENVR and HIZ in one CSR write, VRS kept. Enable = INTERNAL,
     * disable = EXTERNAL. After INTERNAL poll VREFBUF_ReadyStatus. */
    /* ERRATA ES0430 2.8.1 (G47x/G48x only; removed for G491/G4A1 in ES0523 Rev 2): GPIO toggling on
     * PB10/PB11/PB13/PB14/PD14 (Rev Z/Y) or PB10/PB13 (Rev X) disturbs VREF+ in INTERNAL mode; keep
     * them low. */
    /* Seviye: PARAM[r] UNSUPPORTED[r] */
    SPL_Status VREFBUF_SetMode(uint32_t mode);
    /* OK: VRR=1 in INTERNAL mode. BUSY: INTERNAL mode, VRR=0 (poll again).
     * STATE: ENVR=0 (never ready) or HOLD mode (VRR detection disabled). */
    /* Seviye: STATE[r] UNSUPPORTED[r] BUSY[r] */
    SPL_Status VREFBUF_ReadyStatus(void);
    /* *trim = VREFBUF_CCR.TRIM (read only; the driver never writes TRIM). */
    /* Seviye: PARAM[S] UNSUPPORTED[r] */
    SPL_Status VREFBUF_GetTrim(uint32_t *trim);

#ifdef __cplusplus
}
#endif
#endif
