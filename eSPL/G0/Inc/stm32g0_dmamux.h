#ifndef STM32G0_DMAMUX_H
#define STM32G0_DMAMUX_H
#include <stdint.h>
#include "stm32g0xx.h"
#include "stm32g0_status.h"

/* DMA request multiplexer driver (RM0444 Rev 6 section 11).
 * Scope: 12 request channels on G0B1 (DMAMUX channel n serves DMA1 channel n+1 for n = 0..6 and
 * DMA2 channel n-6 for n = 7..11; v0.8: 5 / 7 / 12 channels per SKU). v0.11: synchronization
 * (DMAMUX_SetSync) and the four request generators (DMAMUX_RequestGen*; request ids 1..4 =
 * generator 0..3).
 * Usage: with the DMA channel disabled, DMAMUX_SetRequest (or DMAMUX_SetSync), then DMA_Enable.
 * Generator: DMAMUX_RequestGenConfig, route request id 1..4, DMAMUX_RequestGenEnable(gen, 1).
 * Common rules: no function waits; no static state.
 * ERRATA ES0548 2.5.4: CxCR is written as a whole; SE = 1 only together with SPOL != 00 in the same
 * write (enable with SPOL = 00 -> PARAM); SPOL = 00 whenever SE = 0.
 * ERRATA ES0548 2.5.1 / 2.5.3: an overrun of another channel / generator during the CFR / RGCFR
 * write is not flagged; no workaround (avoid two synchronized channels, or two generators, that can
 * both overrun). ERRATA ES0548 2.5.2: with more than two requests per trigger, keep the trigger
 * period longer than the time to serve them (caller's duty).
 * Caller duties (all APIs):
 *  - Clock: RCC_PERIPH_DMA1 (DMAMUX is clocked with DMA1).
 *  - Trigger sources: the EXTI line or peripheral that drives a SYNC_ID / SIG_ID input is
 *    configured by its own driver.
 *  - ERRATA ES0548 2.5.2 above (trigger period) and 2.5.1 / 2.5.3 (unflagged overrun).
 *  - Interrupts: NVIC enable and handler for SOIE / OIE; the handler clears the overrun flags.
 *  - Concurrency: serialize calls (ISR included); CFR / RGCFR are shared by all channels.
 * Contract: Docs/v0.3_DMA_CONTRACT.md. */

#ifdef __cplusplus
extern "C"
{
#endif

/* Routes request request_id (0 = none, 1..77 on G0B0 / G0B1 / G0C1, 1..63 on the other SKUs where
 * DMAREQ_ID is 6 bits; RM0444 Table 55) to the DMAMUX channel of dma_channel. The DMA channel
 * must be disabled (CCR.EN = 0) -> else STATE. */
/* Caller: request_id must match the peripheral named in RM0444 Table 55 for
 * the SKU (the id is range checked only). */
/* Level: PARAM[F] STATE[F] */
SPL_Status DMAMUX_SetRequest(DMA_Channel_TypeDef *dma_channel, uint8_t request_id);
/* Write-1 clear of synchronization overrun flags; mask bit n = DMAMUX channel n (0..11). */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status DMAMUX_ClearOverrun(uint16_t mask);

/* ---- v0.11: synchronization and request generator (RM0444 11.4.4 / 11.4.5, 11.6) ---- */
typedef struct
{
    uint8_t enable;      /* SE */
    uint8_t polarity;    /* SPOL 1 rising, 2 falling, 3 both; 0 only with enable = 0 */
    uint8_t sync_id;     /* SYNC_ID 0..22 (RM0444 Table 57); 0..15 EXTI: UNSUPPORTED on 03X/07X */
    uint8_t requests;    /* requests forwarded per event, 1..32 (NBREQ + 1) */
    uint8_t event_out;   /* EGE: event output after the requests */
    uint8_t overrun_irq; /* SOIE */
} DMAMUX_SyncTypeDef;
/* Routes request_id like DMAMUX_SetRequest and configures synchronization / event generation. DMA
 * channel enabled -> STATE. NBREQ is written with SE = EGE = 0 first (RM0444 11.6.1), then SE /
 * SPOL / EGE in a second write. */
/* Caller: configure the sync source (sync_id) before enabling the DMA channel;
 * ES0548 2.5.1: avoid a second synchronized channel that can also overrun. */
/* Level: PARAM[F] STATE[F] UNSUPPORTED[F] */
SPL_Status DMAMUX_SetSync(DMA_Channel_TypeDef *dma_channel, uint8_t request_id,
                          const DMAMUX_SyncTypeDef *sync);
/* *mask = CSR.SOF (bit n = DMAMUX channel n). Clear with DMAMUX_ClearOverrun. */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status DMAMUX_GetSyncOverrun(uint16_t *mask);

typedef struct
{
    uint8_t signal_id;   /* SIG_ID 0..22 (RM0444 Table 56); 0..15 EXTI: UNSUPPORTED on 03X/07X */
    uint8_t polarity;    /* GPOL 0 none, 1 rising, 2 falling, 3 both */
    uint8_t requests;    /* requests generated per trigger, 1..32 (GNBREQ + 1) */
    uint8_t overrun_irq; /* OIE */
} DMAMUX_RequestGenTypeDef;
/* Generator gen 0..3: RGxCR (GE kept 0). GE = 1 -> STATE (GNBREQ needs GE = 0, RM0444 11.6.4). */
/* Caller: configure the trigger source (signal_id) and route the generator
 * request (ids 1..4) to a DMA channel; ES0548 2.5.3: avoid two
 * generators that can both overrun. */
/* Level: PARAM[F] STATE[F] UNSUPPORTED[F] */
SPL_Status DMAMUX_RequestGenConfig(uint8_t gen, const DMAMUX_RequestGenTypeDef *cfg);
/* RGxCR.GE = enable. */
/* Caller: enable=1 only after DMAMUX_RequestGenConfig and the DMA channel
 * that serves the generator request are set up. */
/* Level: PARAM[F] */
SPL_Status DMAMUX_RequestGenEnable(uint8_t gen, uint8_t enable);
/* *mask = RGSR.OF (bit n = generator n). */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status DMAMUX_GetGenOverrun(uint8_t *mask);
/* RGCFR = mask (1..0xF, write-1 clear), then RGSR read back. */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status DMAMUX_ClearGenOverrun(uint8_t mask);

#ifdef __cplusplus
}
#endif
#endif /* STM32G0_DMAMUX_H */
