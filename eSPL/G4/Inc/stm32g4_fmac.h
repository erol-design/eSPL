/* SPDX-License-Identifier: MIT */
#ifndef STM32G4_FMAC_H
#define STM32G4_FMAC_H
#include <stdint.h>
#include "stm32g4xx.h"
#include "stm32g4_status.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* Filter math accelerator driver for STM32G4 (FMAC, RM0440 Rev 9 section 18).
 * Scope: single instance, present on every STM32G4 SKU. FIR and IIR (direct form 1) filters
 * with Q1.15 data; buffer layout, preload, start / stop and the polling data path.
 * Usage: FMAC_ConfigBuffers -> FMAC_ControlConfig(0 or error IEs) ->
 * FMAC_LoadX2 (coefficients) [-> FMAC_LoadX1 / FMAC_LoadY] ->
 * FMAC_ControlConfig(data IEs / DMA) -> FMAC_FilterStart ->
 * FMAC_TryWrite / FMAC_TryRead ... -> FMAC_FilterStop.
 * Common rules: no float, no dynamic memory, no static state, no function waits (BUSY /
 * WOULD_BLOCK are returned instead); output parameters are written only on OK.
 * Error flags (OVFL, UNFL, SAT) are cleared ONLY by FMAC_Reset (RM0440
 * 18.4.6), which also stops the function and resets the buffer pointers.
 * Caller duties (all APIs; not done by the driver):
 *  - Clock: AHB clock RCC_AHB1ENR.FMACEN (RCC module).
 *  - Interrupts / DMA: NVIC (FMAC_IRQn) and DMA/DMAMUX channels when RIEN/WIEN or
 *    DMAREN/DMAWEN are used.
 *  - Serialize callers: no two APIs may run concurrently (for example a TryRead in an ISR and
 *    FilterStop in the main loop).
 * Contract: Docs/v0.13_FMAC_CONTRACT.md. */

/* FMAC_PARAM.FUNC values (RM0440 18.4.4). */
#define FMAC_FUNC_LOAD_X1 1U
#define FMAC_FUNC_LOAD_X2 2U
#define FMAC_FUNC_LOAD_Y 3U
#define FMAC_FUNC_FIR 8U
#define FMAC_FUNC_IIR 9U

/* Local memory size in 16-bit words (RM0440 18.3.2). */
#define FMAC_MEMORY_WORDS 256U

/* FMAC_CR bits accepted by FMAC_ControlConfig (RESET is not one of them). */
#define FMAC_CR_CONFIG_ALL                                                                         \
    (FMAC_CR_RIEN | FMAC_CR_WIEN | FMAC_CR_OVFLIEN | FMAC_CR_UNFLIEN | FMAC_CR_SATIEN |            \
     FMAC_CR_DMAREN | FMAC_CR_DMAWEN | FMAC_CR_CLIPEN)

/* FMAC_SR flags reported by FMAC_GetFlags; error subset. */
#define FMAC_FLAGS_ALL (FMAC_SR_YEMPTY | FMAC_SR_X1FULL | FMAC_SR_OVFL | FMAC_SR_UNFL | FMAC_SR_SAT)
#define FMAC_ERRORS_ALL (FMAC_SR_OVFL | FMAC_SR_UNFL | FMAC_SR_SAT)

/* FMAC_GetState results. */
#define FMAC_STATE_IDLE 0U    /* START = 0, no reset pending. */
#define FMAC_STATE_RESET 1U   /* CR.RESET still set (self-clearing). */
#define FMAC_STATE_LOAD 2U    /* START = 1, FUNC = load X1/X2/Y. */
#define FMAC_STATE_FILTER 3U  /* START = 1, FUNC = FIR or IIR. */
#define FMAC_STATE_UNKNOWN 4U /* START = 1 with a reserved FUNC (not set by this driver). */

    /* Buffer layout in 16-bit word addresses of the local memory. */
    typedef struct
    {
        uint8_t x1Base;   /* X1 (input samples, circular). */
        uint8_t x1Size;   /* 1..255 */
        uint8_t x1FullWm; /* FULL_WM 0..3: threshold 1, 2, 4, 8 free spaces. */
        uint8_t x2Base;   /* X2 (coefficients, fixed). */
        uint8_t x2Size;   /* 1..255 */
        uint8_t yBase;    /* Y (output samples, circular). */
        uint8_t ySize;    /* 1..255 */
        uint8_t yEmptyWm; /* EMPTY_WM 0..3: threshold 1, 2, 4, 8 unread values. */
    } FMAC_LayoutTypeDef;

    /* Pure check, no register access. PARAM if NULL, a size is 0, a
     * watermark > 3, base + size >= 256 (RM0440 18.3.2), X2 overlaps X1 or
     * Y, or X1 and Y overlap without coinciding exactly (same base and
     * size: the RM0440 18.3.8 memory-saving FIR layout). OK otherwise. */
    /* Caller: none. */
    /* Level: PARAM[r] */
    SPL_Status FMAC_LayoutCheck(const FMAC_LayoutTypeDef *layout);
    /* Writes X1BUFCFG, X2BUFCFG, YBUFCFG (reserved bits 0) after
     * FMAC_LayoutCheck. Requires START = 0 (RM0440 18.4.1-18.4.3):
     * filter running => STATE; load in progress or reset pending => BUSY. */
    /* Caller: bound the BUSY retries (reset / load completes only while the FMAC is clocked). */
    /* Level: PARAM[F] STATE[r] BUSY[r] */
    SPL_Status FMAC_ConfigBuffers(const FMAC_LayoutTypeDef *layout);
    /* CR = flags (subset of FMAC_CR_CONFIG_ALL), RESET written 0. PARAM for
     * other bits, or WIEN with DMAWEN, or RIEN with DMAREN (RM0440 Table 120:
     * one method per direction). DMAREN/DMAWEN can change only with
     * START = 0 (RM0440 18.4.5): a change while START = 1 => STATE.
     * Reset pending => BUSY. Never touches PARAM. */
    /* Caller: set up the NVIC line or the DMA channels before enabling their requests. */
    /* Level: PARAM[F] STATE[r] BUSY[r] */
    SPL_Status FMAC_ControlConfig(uint32_t flags);
    /* CR.RESET = 1, CR configuration bits kept. Resets pointers, SR (clears
     * OVFL/UNFL/SAT) and PARAM incl. START (RM0440 18.4.5). Self-clearing:
     * poll FMAC_GetState until it is not FMAC_STATE_RESET. Always OK. */
    /* Caller: poll FMAC_GetState (bounded) until it is not FMAC_STATE_RESET before the next call;
     * buffers, coefficients and the filter function must be loaded again. */
    /* Level: - */
    SPL_Status FMAC_Reset(void);

    /* Preload sequences (RM0440 18.3.5): one PARAM write (FUNC, P[, Q],
     * START = 1), then exactly n (+ m) WDATA writes, in order. Hardware
     * clears START after the last write; no waiting here (no flow control
     * during initialization). data: Q1.15 values.
     * PARAM: NULL data (a may be NULL when m = 0), n = 0.
     * STATE: filter running; RIEN, WIEN, DMAREN or DMAWEN set (RM0440 18.3.5
     * recommends them off during initialization); n (+ m) larger than the
     * configured buffer size. BUSY: reset pending or a load in progress. */
    /* Caller: none beyond the file-header duties. */
    /* Level: PARAM[F] STATE[r] BUSY[r] */
    SPL_Status FMAC_LoadX1(const int16_t *data, uint8_t n);
    /* X2 = b[0..n-1] followed by a[0..m-1] (IIR feedback; FIR: m = 0). */
    /* Caller: coefficients in Q1.15 scaled for the gain r used at FMAC_FilterStart (overflow is
     * reported only as SAT / clipped output). */
    /* Level: PARAM[F] STATE[r] BUSY[r] */
    SPL_Status FMAC_LoadX2(const int16_t *b, uint8_t n, const int16_t *a, uint8_t m);
    /* Caller: none beyond the file-header duties. */
    /* Level: PARAM[F] STATE[r] BUSY[r] */
    SPL_Status FMAC_LoadY(const int16_t *data, uint8_t n);

    /* PARAM = START | FUNC | R | Q | P in one write (RM0440 18.3.8/18.3.9).
     * FIR: p (taps) 2..127, q = 0, r (gain 2^r) 0..7.
     * IIR: p 2..64, q (feedback taps) 1..63 and q < p, r 0..7. Else PARAM.
     * STATE: filter running (FMAC_FilterStop first); configured buffers do
     * not fit (X2 < p + q, X1 < p + 2^FULL_WM - 1, Y < q + 2^EMPTY_WM),
     * layout invalid, IIR with X1/Y overlapping, DMAWEN with FULL_WM != 0 or
     * DMAREN with EMPTY_WM != 0 (RM0440 18.4.1/18.4.3).
     * BUSY: reset pending or a load in progress. */
    /* Caller: DMA channels ready before this call when DMAREN / DMAWEN are set; check SAT when the
     * output must not saturate (FMAC_GetFlags). */
    /* Level: PARAM[F] STATE[r] BUSY[r] */
    SPL_Status FMAC_FilterStart(uint32_t func, uint8_t p, uint8_t q, uint8_t r);
    /* START = 0 by software; FUNC/P/Q/R kept (RM0440 18.4.4). Stops any
     * function. No write when START is already 0. Always OK. */
    /* Caller: stop the DMA channels first; read the remaining output with FMAC_TryRead. */
    /* Level: - */
    SPL_Status FMAC_FilterStop(void);

    /* Polling data path, never stalls. Reads PARAM before SR.
     * TryWrite: BUSY (reset/load), STATE (no filter running), ERR_HW (OVFL
     * set), WOULD_BLOCK (X1FULL = 1), else one WDATA write. */
    /* Caller: bound the WOULD_BLOCK retries (an unread Y buffer stalls the filter); after ERR_HW
     * FMAC_Reset and reload. */
    /* Level: STATE[r] HW[r] BUSY[r] WOULD_BLOCK[r] */
    SPL_Status FMAC_TryWrite(int16_t sample);
    /* TryRead: PARAM (NULL), BUSY (reset/load), ERR_HW (UNFL set), OK with
     * one RDATA read when YEMPTY = 0 (also after FilterStop), WOULD_BLOCK
     * when YEMPTY = 1 and a filter runs, STATE when YEMPTY = 1 and no filter
     * runs. *sample written on OK only. */
    /* Caller: bound the WOULD_BLOCK retries; after ERR_HW FMAC_Reset and reload. */
    /* Level: PARAM[F] STATE[r] HW[r] BUSY[r] WOULD_BLOCK[r] */
    SPL_Status FMAC_TryRead(int16_t *sample);

    /* *flags = SR & FMAC_FLAGS_ALL. No side effects. */
    /* Caller: none beyond the file-header duties. */
    /* Level: PARAM[F] */
    SPL_Status FMAC_GetFlags(uint32_t *flags);
    /* *state = one of FMAC_STATE_*. No side effects. */
    /* Caller: none beyond the file-header duties. */
    /* Level: PARAM[F] */
    SPL_Status FMAC_GetState(uint32_t *state);

#ifdef __cplusplus
}
#endif
#endif
