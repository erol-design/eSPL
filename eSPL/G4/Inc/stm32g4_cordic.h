#ifndef STM32G4_CORDIC_H
#define STM32G4_CORDIC_H
#include <stdint.h>
#include "stm32g4xx.h"
#include "stm32g4_status.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* CORDIC coprocessor (RM0440 Rev 9 section 17). Single instance on every
 * supported SKU, no static state, no waits, integer only.
 * Caller duties: AHB clock RCC_AHB1ENR.CORDICEN; NVIC/DMA setup for those
 * modes; one caller-owned CORDIC_ContextTypeDef shared by all calls;
 * serialize callers (not reentrant). Arguments are written pre-scaled and
 * results are returned raw (RM0440 17.3.4). The driver never reads
 * CORDIC_RDATA before RRDY=1, so the AHB bus is never stalled (17.3.7).
 * Contract: Docs/v0.13_CORDIC_CONTRACT.md. */

/* CORDIC_CSR.FUNC values (RM0440 17.4.1). */
#define CORDIC_FUNC_COSINE 0U
#define CORDIC_FUNC_SINE 1U
#define CORDIC_FUNC_PHASE 2U
#define CORDIC_FUNC_MODULUS 3U
#define CORDIC_FUNC_ARCTANGENT 4U
#define CORDIC_FUNC_HCOSINE 5U
#define CORDIC_FUNC_HSINE 6U
#define CORDIC_FUNC_HARCTANGENT 7U
#define CORDIC_FUNC_NATURALLOG 8U
#define CORDIC_FUNC_SQUAREROOT 9U

/* CORDIC_CSR.ARGSIZE / RESSIZE values: q1.31 (32-bit) or q1.15 (16-bit). */
#define CORDIC_SIZE_32BIT 0U
#define CORDIC_SIZE_16BIT 1U

    typedef struct
    {
        uint32_t function;   /* CORDIC_FUNC_x. */
        uint32_t precision;  /* PRECISION = iterations / 4, 1..15 (Table 118). Best accuracy at 6
                              * (sqrt: 3); higher values were less accurate on silicon (contract). */
        uint32_t scale;      /* SCALE n; allowed range depends on function. */
        uint32_t arg_size;   /* CORDIC_SIZE_x. */
        uint32_t res_size;   /* CORDIC_SIZE_x. */
        uint32_t arg_writes; /* WDATA writes per calculation: 1 or 2 (NARGS+1). */
        uint32_t res_reads;  /* RDATA reads per result: 1 or 2 (NRES+1). */
    } CORDIC_ConfigTypeDef;

    /* Caller-owned state. outstanding = argument sets written whose results
     * are not read yet (0..2, RM0440 17.3.7: one in progress + one pending). */
    typedef struct
    {
        uint32_t outstanding;
    } CORDIC_ContextTypeDef;

    /* outstanding = 0. No register access. */
    /* Level: PARAM[F] */
    SPL_Status CORDIC_ContextInit(CORDIC_ContextTypeDef *ctx);
    /* One CSR write (IEN kept, DMA bits 0). PARAM: NULL or field rule broken
     * (see contract). STATE: DMA enabled, RRDY=1, or outstanding != 0. */
    /* Level: PARAM[F] STATE[r] */
    SPL_Status CORDIC_Config(const CORDIC_ContextTypeDef *ctx, const CORDIC_ConfigTypeDef *config);
    /* IEN = enable (interrupt while RRDY=1). Other fields kept. */
    /* Level: PARAM[F] */
    SPL_Status CORDIC_InterruptConfig(uint8_t enable);
    /* DMAREN/DMAWEN. Entering DMA mode needs outstanding=0 and RRDY=0 (STATE).
     * Configure the DMA channels first: DMAWEN=1 requests data at once. */
    /* Level: PARAM[F] STATE[r] */
    SPL_Status CORDIC_DmaConfig(const CORDIC_ContextTypeDef *ctx, uint8_t read_enable,
                                uint8_t write_enable);
    /* Writes the arguments of one calculation (starts it or queues it).
     * 32-bit: wdata1 = ARG1, wdata2 = ARG2 (written only when NARGS=1).
     * 16-bit: wdata1 = ARG1 [15:0] | ARG2 [31:16], wdata2 ignored.
     * PARAM: NULL or argument outside the RM range for FUNC/SCALE.
     * STATE: DMA enabled, two operations outstanding (read first), or CSR
     * not a valid configuration. */
    /* Level: PARAM[F] STATE[r] */
    SPL_Status CORDIC_TryWrite(CORDIC_ContextTypeDef *ctx, uint32_t wdata1, uint32_t wdata2);
    /* Reads the result(s) of the oldest calculation when RRDY=1: rdata1 = first
     * RDATA word, rdata2 = second word (NRES=1 only; may be NULL otherwise).
     * WOULD_BLOCK: RRDY=0 with a calculation outstanding (RDATA not touched).
     * STATE: DMA enabled, or RRDY=0 and nothing outstanding. */
    /* Level: PARAM[F] STATE[r] WOULD_BLOCK[r] */
    SPL_Status CORDIC_TryRead(CORDIC_ContextTypeDef *ctx, uint32_t *rdata1, uint32_t *rdata2);
    /* q1.15 packing helpers (pure integer, no register access). */
    /* Level: PARAM[F] */
    SPL_Status CORDIC_PackQ15(int16_t arg1, int16_t arg2, uint32_t *wdata);
    /* Level: PARAM[F] */
    SPL_Status CORDIC_UnpackQ15(uint32_t rdata, int16_t *res1, int16_t *res2);

#ifdef __cplusplus
}
#endif
#endif
