#ifndef STM32G0_FDCAN_LAYOUT_H
#define STM32G0_FDCAN_LAYOUT_H

#include <stdint.h>
#include "stm32g0_status.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* FDCAN message RAM layout calculator and validator (FD-01) for STM32G0B1 (RM0444 Rev 6 section
 * 36.3.6), ported from the G4 SPL v0.13.5.
 * Scope: computes and checks the fixed per-instance message RAM reservation of the
 * FDCAN_SKU_G0B1 profile (FDCAN1 + FDCAN2); stm32g0_fdcan.c uses it for its offsets.
 * Usage: FDCAN_LayoutCalculate per instance, then FDCAN_LayoutValidatePair for two instances.
 * Common rules: no MMIO access, no static state, no waits; the validation results are the
 * function's answer at every check level (Docs/CHECK_LEVEL.md 5.4 FDCAN table).
 * Caller duties: none.
 * Contract: Docs/v0.6_FDCAN_CONTRACT.md. */

#define FDCAN_MESSAGE_RAM_WORDS_PER_INSTANCE 212U
#define FDCAN_STANDARD_FILTER_MAX 28U
#define FDCAN_EXTENDED_FILTER_MAX 8U
#define FDCAN_RX_FIFO_ELEMENT_MAX 3U
#define FDCAN_TX_EVENT_ELEMENT_MAX 3U
#define FDCAN_TX_FIFO_ELEMENT_MAX 3U

/* These identifiers are the supported CMSIS device-header profiles. They are
 * not a claim that every package or silicon revision is hardware-verified. */
typedef enum
{
    FDCAN_SKU_INVALID = 0,
    FDCAN_SKU_G0B1 = 1 /* FDCAN1 + FDCAN2 (G0C1 has the same layout; not a separate profile) */
} FDCAN_SkuTypeDef;

/* Active filter counts: 0..28/0..8. All FIFO counts must be 3;
 * all payload capacities must be 64, including for classic CAN frames.
 * G0B1 physical slots and section offsets cannot be resized (RM0444 36.3.6). */
typedef struct
{
    uint32_t standard_filter_count;
    uint32_t extended_filter_count;
    uint32_t rx_fifo0_count;
    uint32_t rx_fifo1_count;
    uint32_t tx_event_count;
    uint32_t tx_fifo_count;
    uint32_t rx_fifo0_payload_bytes;
    uint32_t rx_fifo1_payload_bytes;
    uint32_t tx_payload_bytes;
} FDCAN_MessageRamConfigTypeDef;

typedef struct
{
    uint32_t start_word;
    uint32_t element_count;
    uint32_t element_words;
    uint32_t word_count;
} FDCAN_MessageRamSectionTypeDef;

typedef struct
{
    FDCAN_SkuTypeDef sku;
    uint32_t instance;
    uint32_t instance_start_word;
    uint32_t instance_end_word;
    FDCAN_MessageRamSectionTypeDef standard_filters;
    FDCAN_MessageRamSectionTypeDef extended_filters;
    FDCAN_MessageRamSectionTypeDef rx_fifo0;
    FDCAN_MessageRamSectionTypeDef rx_fifo1;
    FDCAN_MessageRamSectionTypeDef tx_events;
    FDCAN_MessageRamSectionTypeDef tx_fifo;
    /* Physical reservation: always 212 used, 0 reclaimable words. */
    uint32_t used_words;
    uint32_t free_words;
} FDCAN_MessageRamLayoutTypeDef;

/* Caller: none. */
/* Level: PARAM[F,r] UNSUPPORTED[r] */
SPL_Status FDCAN_LayoutCalculate(FDCAN_SkuTypeDef sku, uint32_t instance,
                                 const FDCAN_MessageRamConfigTypeDef *config,
                                 FDCAN_MessageRamLayoutTypeDef *layout);

/* Validates canonical fixed sections, bases and distinct instance
 * reservations. Both layouts must describe the same SKU profile. */
/* Caller: none. */
/* Level: PARAM[r] */
SPL_Status FDCAN_LayoutValidatePair(const FDCAN_MessageRamLayoutTypeDef *first,
                                    const FDCAN_MessageRamLayoutTypeDef *second);

#ifdef __cplusplus
}
#endif

#endif /* STM32G0_FDCAN_LAYOUT_H */
