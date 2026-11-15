#ifndef STM32G4_OPAMP_H
#define STM32G4_OPAMP_H
#include <stdint.h>
#include "stm32g4xx.h"
#include "stm32g4_status.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* Operational amplifiers OPAMP1..OPAMP6 as present in CMSIS (RM0440 section 25).
     * G431: OPAMP1..3; G411xB: OPAMP1; G491/G4A1: OPAMP1..3 and OPAMP6; G474: OPAMP1..6;
     * G414: no OPAMP (this API is not declared). Contract: Docs/v0.11_OPAMP_CONTRACT.md.
     *
     * Caller duties (not done by the driver):
     * - Clock: SYSCFG clock in RCC (OPAMP has no own enable bit, RM0440 25.3.2).
     * - GPIO VINP/VINM/VOUT pins in analog mode (RM0440 25.3.3).
     * - Start-up / settling time from the datasheet after OPAMP_Enable.
     * - Calibration timing: wait tOFFTRIM (max 2 ms, datasheet) after every trim write before
     *   reading CALOUT (RM0440 25.3.7). The driver never waits.
     * LOCK: once CSR.LOCK is set the CSR is read-only until system reset (RM0440 25.5.1);
     * every API that would write CSR then returns SPL_ERR_STATE and writes nothing.
     * OPAMPx_TCMR (timer-controlled mux) is not handled by this driver.
     * Not reentrant per instance: serialize calls (ISR included). */

#if defined(OPAMP1)

/* Operating mode -> CSR.VM_SEL (RM0440 25.3.5, 25.5.1 bits 6:5). */
#define OPAMP_MODE_STANDALONE 0U /* VM_SEL = vm_sel (00 VINM0 / 01 VINM1) */
#define OPAMP_MODE_FOLLOWER 1U   /* VM_SEL = 11 */
#define OPAMP_MODE_PGA 2U        /* VM_SEL = 10 */

/* VP_SEL raw 0..3 (RM0440 25.5.1..25.5.6 bits 3:2): 0..2 = VINP0..VINP2 pins;
 * 3 = OPAMP1 DAC3_CH1, OPAMP2 VINP3 pin, OPAMP3 DAC3_CH2, OPAMP4 DAC4_CH1,
 *     OPAMP5 DAC4_CH2, OPAMP6 DAC3_CH1. Pin names per instance: RM0440 Table 204. */
#define OPAMP_VP_SEL_MAX 3U
/* Standalone VM_SEL: 0 = VINM0 pin, 1 = VINM1 pin. */
#define OPAMP_VM_SEL_MAX 1U

/* PGA gain index -> PGA_GAIN[2:0]: non-inverting 2/4/8/16/32/64 (inverting -1..-63). */
#define OPAMP_PGA_GAIN_2 0U  /* inverting topologies: -1 */
#define OPAMP_PGA_GAIN_4 1U  /* -3 */
#define OPAMP_PGA_GAIN_8 2U  /* -7 */
#define OPAMP_PGA_GAIN_16 3U /* -15 */
#define OPAMP_PGA_GAIN_32 4U /* -31 */
#define OPAMP_PGA_GAIN_64 5U /* -63 */
/* PGA topology -> PGA_GAIN[4:3] (RM0440 25.5.1 bits 18:14, codes x110/x111 not used):
 * 0 = internal gain, VINM not used            (00000..00101)
 * 1 = VINM0 pin for input or bias              (01000..01101)
 * 2 = non-inverting, filtering on VINM0 pin    (10000..10101)
 * 3 = VINM0 input/bias, VINM1 pin for filtering (11000..11101) */
#define OPAMP_PGA_INTERNAL 0U
#define OPAMP_PGA_VINM0_BIAS 1U
#define OPAMP_PGA_VINM0_FILTER 2U
#define OPAMP_PGA_VINM0_BIAS_VINM1_FILTER 3U

/* Calibration differential pair -> CSR.CALSEL (RM0440 25.3.7, 25.5.1 bits 13:12). */
#define OPAMP_CAL_PAIR_P 0U /* CALSEL = 01, 0.1 x VDDA, trims TRIMOFFSETP */
#define OPAMP_CAL_PAIR_N 1U /* CALSEL = 11, 0.9 x VDDA, trims TRIMOFFSETN */

#define OPAMP_TRIM_MAX 31U

    typedef struct
    {
        uint8_t mode;            /* OPAMP_MODE_* */
        uint8_t vp_sel;          /* 0..OPAMP_VP_SEL_MAX */
        uint8_t vm_sel;          /* standalone: 0..1; other modes must be 0 */
        uint8_t pga_gain;        /* PGA: OPAMP_PGA_GAIN_*; other modes must be 0 */
        uint8_t pga_topology;    /* PGA: OPAMP_PGA_*; other modes must be 0 */
        uint8_t internal_output; /* OPAINTOEN: 1 = output to ADC channel, VOUT pin free */
        uint8_t high_speed;      /* OPAHSM: 1 = high-speed mode */
    } OPAMP_ConfigTypeDef;

    /* LOCK=1: STATE; OPAEN=1: STATE. Writes VP_SEL, VM_SEL, PGA_GAIN, OPAINTOEN, OPAHSM;
     * FORCE_VP=0, CALON=0, CALSEL=00, OPAEN=0; USERTRIM and trim values preserved. */
    /* ERRATA ES0431 2.7.1 / ES0430 2.10.1 / ES0523 2.7.1: in non-inverting PGA mode fast edges on
     * the VINM0 pin are amplified at the output; keep VINM0 static/slow (pins: contract). */
    /* Seviye: PARAM[S] STATE[r] */
    SPL_Status OPAMP_Config(OPAMP_TypeDef *opamp, const OPAMP_ConfigTypeDef *config);
    /* LOCK=1: STATE. Sets OPAEN. */
    /* Seviye: PARAM[S] STATE[r] */
    SPL_Status OPAMP_Enable(OPAMP_TypeDef *opamp);
    /* LOCK=1: STATE; CALON=1: STATE (exit calibration first). Clears OPAEN. */
    /* Seviye: PARAM[S] STATE[r] */
    SPL_Status OPAMP_Disable(OPAMP_TypeDef *opamp);

    /* User trimming. LOCK=1: STATE. Sets USERTRIM (separate write if it was 0), then writes
     * TRIMOFFSETP/TRIMOFFSETN (RM0440 25.5.1: modifiable only with USERTRIM=1). */
    /* Seviye: PARAM[S] STATE[r] */
    SPL_Status OPAMP_SetUserTrim(OPAMP_TypeDef *opamp, uint32_t trim_p, uint32_t trim_n);
    /* Reads TRIMOFFSETP/N (factory values after reset while USERTRIM=0). */
    /* Seviye: PARAM[S] */
    SPL_Status OPAMP_GetTrim(const OPAMP_TypeDef *opamp, uint32_t *trim_p, uint32_t *trim_n);
    /* LOCK=1: STATE; CALON=1: STATE. Clears USERTRIM: factory trim used again. */
    /* Seviye: PARAM[S] STATE[r] */
    SPL_Status OPAMP_UseFactoryTrim(OPAMP_TypeDef *opamp);

    /* Calibration steps (RM0440 25.3.8), see contract for the full sequence.
     * Enter: OPAEN=1 required (STATE). Sets USERTRIM, CALSEL for pair, then CALON=1.
     * CALOUT is a valid indicator only with OPAINTOEN=0 (output on the VOUT pin, RM 25.3.8);
     * with OPAINTOEN=1 use the ADC reading of the internal output instead (contract). */
    /* Seviye: PARAM[S] STATE[r] */
    SPL_Status OPAMP_CalibrationEnter(OPAMP_TypeDef *opamp, uint32_t pair);
    /* CALON=1 required (STATE). Changes CALSEL to the other pair. */
    /* Seviye: PARAM[S] STATE[r] */
    SPL_Status OPAMP_CalibrationSelect(OPAMP_TypeDef *opamp, uint32_t pair);
    /* CALON=1 required (STATE). Reads CALOUT; valid only tOFFTRIM after the last trim write. */
    /* Seviye: PARAM[S] STATE[r] */
    SPL_Status OPAMP_CalibrationOutput(const OPAMP_TypeDef *opamp, uint8_t *calout);
    /* CALON=1 required (STATE). Clears CALON and CALSEL; USERTRIM and trims kept. */
    /* Seviye: PARAM[S] STATE[r] */
    SPL_Status OPAMP_CalibrationExit(OPAMP_TypeDef *opamp);

    /* Sets LOCK (irreversible until system reset). Already locked: STATE. CALON=1: STATE. */
    /* Seviye: PARAM[S] STATE[r] */
    SPL_Status OPAMP_Lock(OPAMP_TypeDef *opamp);
    /* Seviye: PARAM[S] */
    SPL_Status OPAMP_GetLockState(const OPAMP_TypeDef *opamp, uint8_t *locked);

#endif /* OPAMP1 */

#ifdef __cplusplus
}
#endif
#endif
