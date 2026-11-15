#ifndef STM32G4_FDCAN_LAYOUT_H
#define STM32G4_FDCAN_LAYOUT_H

#include <stdint.h>
#include "stm32g4_status.h"

#ifdef __cplusplus
extern "C"
{
#endif

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
        FDCAN_SKU_G411XB = 1,
        FDCAN_SKU_G414 = 2,
        FDCAN_SKU_G431 = 3,
        FDCAN_SKU_G474 = 4,
        FDCAN_SKU_G491 = 5,
        FDCAN_SKU_G4A1 = 6
    } FDCAN_SkuTypeDef;

    /* Active filter counts: 0..28/0..8. All FIFO counts must be 3;
     * all payload capacities must be 64, including for classic CAN frames.
     * G4 physical slots and section offsets cannot be resized. */
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

    SPL_Status FDCAN_LayoutCalculate(FDCAN_SkuTypeDef sku, uint32_t instance,
                                     const FDCAN_MessageRamConfigTypeDef *config,
                                     FDCAN_MessageRamLayoutTypeDef *layout);

    /* Validates canonical fixed sections, bases and distinct instance
     * reservations. Both layouts must describe the same SKU profile. */
    SPL_Status FDCAN_LayoutValidatePair(const FDCAN_MessageRamLayoutTypeDef *first,
                                        const FDCAN_MessageRamLayoutTypeDef *second);

#ifdef __cplusplus
}
#endif

#endif /* STM32G4_FDCAN_LAYOUT_H */
