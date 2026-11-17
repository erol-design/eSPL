#ifndef STM32G0_COMP_H
#define STM32G0_COMP_H
#include <stdint.h>
#include "stm32g0xx.h"
#include "stm32g0_status.h"
#include "stm32g0_sku.h"
#ifdef __cplusplus
extern "C"
{
#endif

/* v0.8 SKU profile: this API exists only where the device has the peripheral (SPL_G0_HAS_COMP);
 * absent on G030 / G031 / G041 / G050 / G070 / G0B0. */
#if SPL_G0_HAS_COMP
/* Analog comparators COMP1..COMP3 of STM32G0B1 (RM0444 Rev 6 section 18).
 * Usage: COMP_Config (comparator disabled), COMP_Enable, start-up wait, COMP_GetOutput;
 * optionally COMP_Lock last.
 * Ported from the G4 SPL v0.13.5 COMP driver. G0 differences: no SCALEN/BRGEN bits (the VREFINT
 * scaler is internal), 4-entry INPSEL, 9-entry INMSEL, 2-bit hysteresis, PWRMODE, window mode
 * (WINMODE/WINOUT), 5-bit blanking mask, and VALUE reflects the output after polarity and
 * blanking (G4: before).
 * No static state; no function waits (the start-up wait is the caller's). Output parameters are
 * written only on OK.
 * Contract: Docs/v0.5_COMP_CONTRACT.md.
 *
 * Caller duties (all APIs, not done by the driver):
 * - Clock: SYSCFG clock in RCC (COMP has no own enable bit, RM0444 18.3.3).
 * - GPIO inputs in analog mode, output pin alternate function (RM0444 18.3.2).
 * - Wait the comparator start-up time and, when a VREFINT input (1/4 .. 1 VREFINT) is selected,
 *   the VREFINT scaler start-up time (datasheet) after COMP_Enable before trusting the output.
 * - Interrupt/wake-up: EXTI line 17 (COMP1), 18 (COMP2), 20 (COMP3) with the EXTI driver.
 * LOCK: once CSR.LOCK is set the register is read-only until system reset (RM0444 18.3.4);
 * every API that would write CSR then returns SPL_ERR_STATE and writes nothing.
 * Not reentrant per instance: serialize calls (ISR included). */
/* INPSEL (RM0444 Tables 93/95/97): 0..2 = pins (COMP1: PC5, PB2, PA1; COMP2: PB4, PB6, PA3;
 * COMP3: PB0, PC1, PE7), 3 = open. */
#define COMP_INP_SEL_MAX 3U
/* INMSEL (RM0444 Tables 94/96/98): 0 = 1/4 VREFINT, 1 = 1/2, 2 = 3/4, 3 = VREFINT,
 * 4 = DAC channel 1, 5 = DAC channel 2, 6..8 = pins (COMP1: PB1, PC4, PA0; COMP2: PB3, PB7, PA2;
 * COMP3: PB2, PC0, PE8). Codes > 8 alias 1/4 VREFINT in hardware and are refused. */
#define COMP_INM_1_4_VREFINT 0U
#define COMP_INM_1_2_VREFINT 1U
#define COMP_INM_3_4_VREFINT 2U
#define COMP_INM_VREFINT 3U
#define COMP_INM_DAC_CH1 4U
#define COMP_INM_DAC_CH2 5U
#define COMP_INM_SEL_MAX 8U
/* HYST: 0 none, 1 low, 2 medium, 3 high. */
#define COMP_HYST_MAX 3U
/* PWRMODE: 0 high speed, 1 medium speed (others reserved). */
#define COMP_POWER_HIGH_SPEED 0U
#define COMP_POWER_MEDIUM_SPEED 1U
/* POLARITY. */
#define COMP_POL_NON_INVERTED 0U
#define COMP_POL_INVERTED 1U
/* BLANKSEL bit mask (RM0444 18.6.x): bit 0 TIM1 OC4, bit 1 TIM1 OC5, bit 2 TIM2 OC3,
 * bit 3 TIM3 OC3, bit 4 TIM15 OC2; 0 = no blanking. */
#define COMP_BLANK_NONE 0U
#define COMP_BLANK_MAX 0x1FU
typedef struct
{
    uint8_t inp_sel;     /* 0..COMP_INP_SEL_MAX */
    uint8_t inm_sel;     /* 0..COMP_INM_SEL_MAX */
    uint8_t hysteresis;  /* 0..COMP_HYST_MAX */
    uint8_t polarity;    /* COMP_POL_* */
    uint8_t blanking;    /* 0..COMP_BLANK_MAX */
    uint8_t power_mode;  /* COMP_POWER_* */
    uint8_t window_mode; /* WINMODE: plus input from the paired comparator (COMP1 <- COMP2_INP,
                          * COMP2 <- COMP1_INP, COMP3 <- COMP2_INP) */
    uint8_t window_out;  /* WINOUT: output = own VALUE XOR paired VALUE */
} COMP_ConfigTypeDef;
/* LOCK=1: STATE. EN=1: STATE. Writes the whole CSR with EN=0, LOCK=0. */
/* Caller: configure the selected DAC channel, the blanking timer output and, with
 * window_mode, the paired comparator input pin (DAC / TIM / GPIO drivers). */
/* Level: PARAM[F] STATE[F] */
SPL_Status COMP_Config(COMP_TypeDef *comp, const COMP_ConfigTypeDef *config);
/* LOCK=1: STATE. Sets EN; other bits unchanged. */
/* Caller: wait the start-up time(s) in the file header before using the output. */
/* Level: PARAM[F] STATE[F] */
SPL_Status COMP_Enable(COMP_TypeDef *comp);
/* LOCK=1: STATE. Clears EN; other bits unchanged. */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] STATE[F] */
SPL_Status COMP_Disable(COMP_TypeDef *comp);
/* VALUE: comparator output after polarity and blanking (RM0444 18.6.1). EN=0: STATE. */
/* Caller: trust the level only after the start-up wait that follows COMP_Enable. */
/* Level: PARAM[F] STATE[F] */
SPL_Status COMP_GetOutput(const COMP_TypeDef *comp, uint8_t *level);
/* Sets LOCK (irreversible until system reset). Already locked: STATE. */
/* Caller: Irreversible until system reset: finish COMP_Config and COMP_Enable first; after
 * this the comparator cannot be reconfigured or disabled. */
/* Level: PARAM[F] STATE[F] */
SPL_Status COMP_Lock(COMP_TypeDef *comp);
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status COMP_GetLockState(const COMP_TypeDef *comp, uint8_t *locked);
#endif /* SPL_G0_HAS_COMP */

#ifdef __cplusplus
}
#endif
#endif /* STM32G0_COMP_H */
