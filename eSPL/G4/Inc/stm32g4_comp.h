/* SPDX-License-Identifier: MIT */
#ifndef STM32G4_COMP_H
#define STM32G4_COMP_H
#include <stdint.h>
#include "stm32g4xx.h"
#include "stm32g4_status.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* Analog comparator driver for STM32G4 (RM0440 Rev 9 section 24): COMP1..COMP7 as present in
 * CMSIS (G431: COMP1..4). One register per instance (COMP_CxCSR).
 * Scope: input selection, hysteresis, polarity, blanking, enable/disable, output read, LOCK.
 * No interrupt or EXTI configuration.
 * Usage: COMP_Config (comparator disabled), COMP_Enable, wait the start-up time, then
 * COMP_GetOutput; COMP_Disable before reconfiguring; optional COMP_Lock.
 * Common rules: no static state, no function waits; output parameters are unchanged unless
 * SPL_OK (METHOD 1.8). LOCK: once CSR.LOCK is set the register is read-only until system reset
 * (RM0440 24.3.4); every API that would write CSR then returns SPL_ERR_STATE and writes nothing.
 * Caller duties (all APIs; not done by the driver):
 *  - Clock: SYSCFG clock in RCC (COMP has no own enable bit, RM0440 24.3.3).
 *  - Pins: GPIO inputs in analog mode, output pin in alternate function (RM0440 24.3.2).
 *  - Start-up: after COMP_Enable wait the comparator start-up time and, when a VREFINT input is
 *    used, the VREFINT scaler start-up time from the datasheet before trusting the output.
 *  - Interrupt / wake-up: EXTI line of the comparator (EXTI driver, RM0440 24.5).
 *  - Serialize calls per instance (ISR included).
 * Contract: Docs/v0.11_COMP_CONTRACT.md. */

/* INPSEL: raw selector 0..1, meaning per instance in RM0440 Table 199
 * (e.g. COMP1: 0 = PA1, 1 = PB1). */
#define COMP_INP_SEL_MAX 1U

/* INMSEL: raw selector 0..7, RM0440 Table 200 (common rows 0..3, per-instance rows 4..7):
 * 0 = 1/4 VREFINT, 1 = 1/2 VREFINT, 2 = 3/4 VREFINT, 3 = VREFINT (all instances)
 * 4: COMP1 DAC3_CH1, COMP2 DAC3_CH2, COMP3 DAC3_CH1, COMP4 DAC3_CH2,
 *    COMP5 DAC4_CH1, COMP6 DAC4_CH2, COMP7 DAC4_CH1
 * 5: COMP1 DAC1_CH1, COMP2 DAC1_CH2, COMP3 DAC1_CH1, COMP4 DAC1_CH1,
 *    COMP5 DAC1_CH2, COMP6 DAC2_CH1, COMP7 DAC2_CH1
 * 6: COMP1 PA4, COMP2 PA5, COMP3 PF1, COMP4 PE8, COMP5 PB10, COMP6 PD10, COMP7 PD15
 * 7: COMP1 PA0, COMP2 PA2, COMP3 PC0, COMP4 PB2, COMP5 PD13, COMP6 PB15, COMP7 PB12
 * The driver sets SCALEN=1 for selectors 0..3 and BRGEN=1 for selectors 0..2 (RM0440
 * 24.6.1 bits 23/22), and clears both for selectors 4..7 in this instance only. */
#define COMP_INM_1_4_VREFINT 0U
#define COMP_INM_1_2_VREFINT 1U
#define COMP_INM_3_4_VREFINT 2U
#define COMP_INM_VREFINT 3U
#define COMP_INM_SEL_MAX 7U

/* HYST: 0 = none, n = n * 10 mV (n = 1..7), RM0440 24.6.1. */
#define COMP_HYST_MAX 7U

/* POL. */
#define COMP_POL_NON_INVERTED 0U
#define COMP_POL_INVERTED 1U

/* BLANKSEL: raw 0..7, 0 = no blanking, 1..7 per instance in RM0440 Table 201.
 * Value 5 (TIM20_OC5 for all instances) => SPL_ERR_UNSUPPORTED on SKUs without TIM20. */
#define COMP_BLANK_NONE 0U
#define COMP_BLANK_TIM20_OC5 5U
#define COMP_BLANK_MAX 7U

    typedef struct
    {
        uint8_t inp_sel;    /* 0..COMP_INP_SEL_MAX */
        uint8_t inm_sel;    /* 0..COMP_INM_SEL_MAX */
        uint8_t hysteresis; /* 0..COMP_HYST_MAX */
        uint8_t polarity;   /* COMP_POL_* */
        uint8_t blanking;   /* 0..COMP_BLANK_MAX */
    } COMP_ConfigTypeDef;

    /* LOCK=1: STATE. EN=1: STATE. Writes the whole CSR with EN=0, LOCK=0. */
    /* ERRATA ES0431 2.6.1 / ES0430 2.9.1 (Rev Z, Y; not Rev X; not in ES0523, G491/G4A1 free):
     * Voffset -20..+3 mV (typ -15/+2) replaces the datasheet value; no workaround, budget it in
     * thresholds.
     * Caller: disable first (COMP_Disable); on Rev Z/Y budget the errata Voffset in the INMSEL /
     * DAC threshold and hysteresis. */
    /* Level: PARAM[F] STATE[r] UNSUPPORTED[r] */
    SPL_Status COMP_Config(COMP_TypeDef *comp, const COMP_ConfigTypeDef *config);
    /* LOCK=1: STATE. Sets EN; other bits unchanged. Does not wait for start-up.
     * Caller: COMP_Config first; wait the start-up time(s) (file header) before reading the
     * output. */
    /* Level: PARAM[F] STATE[r] */
    SPL_Status COMP_Enable(COMP_TypeDef *comp);
    /* LOCK=1: STATE. Clears EN; other bits unchanged.
     * Caller: none beyond the file-header duties. */
    /* Level: PARAM[F] STATE[r] */
    SPL_Status COMP_Disable(COMP_TypeDef *comp);
    /* VALUE: comparator output before polarity and blanking (RM0440 24.6.1). EN=0: STATE.
     * Caller: the start-up time after COMP_Enable has elapsed (not checked). */
    /* Level: PARAM[F] STATE[r] */
    SPL_Status COMP_GetOutput(const COMP_TypeDef *comp, uint8_t *level);
    /* Irreversible: sets LOCK, CSR read-only until system reset. Already locked: STATE.
     * Caller: final configuration (COMP_Config, COMP_Enable) done first. */
    /* Level: PARAM[F] STATE[r] */
    SPL_Status COMP_Lock(COMP_TypeDef *comp);
    /* Reads CSR.LOCK into *locked (1 = locked). PARAM: invalid instance, NULL.
     * Caller: none beyond the file-header duties. */
    /* Level: PARAM[F] */
    SPL_Status COMP_GetLockState(const COMP_TypeDef *comp, uint8_t *locked);

#ifdef __cplusplus
}
#endif
#endif
