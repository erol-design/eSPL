#include "stm32g4_fdcan_layout.h"

static uint32_t FDCAN_InstanceCount(FDCAN_SkuTypeDef sku)
{
    uint32_t count;

    switch (sku)
    {
    case FDCAN_SKU_G411XB:
    case FDCAN_SKU_G414:
    case FDCAN_SKU_G431:
        count = 1U;
        break;
    case FDCAN_SKU_G491:
    case FDCAN_SKU_G4A1:
        count = 2U;
        break;
    case FDCAN_SKU_G474:
        count = 3U;
        break;
    default:
        count = 0U;
        break;
    }

    return count;
}

/* Per-instance section start words and element sizes (RM0440 44.3.6, Figure 670). The same
 * values are FDCAN_RAM_*_WORD in stm32g4_fdcan.c; both files pin them to the derivation below. */
#define FDCAN_LAYOUT_EXT_FILTER_WORD 28U
#define FDCAN_LAYOUT_RX_FIFO0_WORD 44U
#define FDCAN_LAYOUT_RX_FIFO1_WORD 98U
#define FDCAN_LAYOUT_TX_EVENT_WORD 152U
#define FDCAN_LAYOUT_TX_FIFO_WORD 158U
#define FDCAN_LAYOUT_ELEMENT_WORDS 18U
#define FDCAN_LAYOUT_EVENT_WORDS 2U

/* Compile-time check (CCL-2 YAP-03): each section starts where the previous one ends when
 * sized from the public FDCAN_*_MAX limits, and the Tx FIFO ends exactly at
 * FDCAN_MESSAGE_RAM_WORDS_PER_INSTANCE. */
typedef char Fdcan_LayoutSectionCheck
    [((FDCAN_LAYOUT_EXT_FILTER_WORD == (FDCAN_STANDARD_FILTER_MAX * 1U)) &&
      (FDCAN_LAYOUT_RX_FIFO0_WORD ==
       (FDCAN_LAYOUT_EXT_FILTER_WORD + (FDCAN_EXTENDED_FILTER_MAX * 2U))) &&
      (FDCAN_LAYOUT_RX_FIFO1_WORD ==
       (FDCAN_LAYOUT_RX_FIFO0_WORD + (FDCAN_RX_FIFO_ELEMENT_MAX * FDCAN_LAYOUT_ELEMENT_WORDS))) &&
      (FDCAN_LAYOUT_TX_EVENT_WORD ==
       (FDCAN_LAYOUT_RX_FIFO1_WORD + (FDCAN_RX_FIFO_ELEMENT_MAX * FDCAN_LAYOUT_ELEMENT_WORDS))) &&
      (FDCAN_LAYOUT_TX_FIFO_WORD ==
       (FDCAN_LAYOUT_TX_EVENT_WORD + (FDCAN_TX_EVENT_ELEMENT_MAX * FDCAN_LAYOUT_EVENT_WORDS))) &&
      (FDCAN_MESSAGE_RAM_WORDS_PER_INSTANCE ==
       (FDCAN_LAYOUT_TX_FIFO_WORD + (FDCAN_TX_FIFO_ELEMENT_MAX * FDCAN_LAYOUT_ELEMENT_WORDS))))
         ? 1
         : -1];

/* RM0440 Rev 9, section 44.3.6, Figure 670: fixed physical reservations.
 * Active filter counts never move the following sections. */
static FDCAN_MessageRamSectionTypeDef FDCAN_Section(uint32_t start, uint32_t count,
                                                    uint32_t element_words)
{
    FDCAN_MessageRamSectionTypeDef section;
    section.start_word = start;
    section.element_count = count;
    section.element_words = element_words;
    section.word_count = count * element_words;
    return section;
}

SPL_Status FDCAN_LayoutCalculate(FDCAN_SkuTypeDef sku, uint32_t instance,
                                 const FDCAN_MessageRamConfigTypeDef *config,
                                 FDCAN_MessageRamLayoutTypeDef *layout)
{
    FDCAN_MessageRamLayoutTypeDef candidate = {0};
    uint32_t count;
    uint32_t base;
    if ((config == (const FDCAN_MessageRamConfigTypeDef *)0) ||
        (layout == (FDCAN_MessageRamLayoutTypeDef *)0))
    {
        return SPL_ERR_PARAM;
    }
    count = FDCAN_InstanceCount(sku);
    if (count == 0U)
    {
        return SPL_ERR_UNSUPPORTED;
    }
    if ((instance == 0U) || (instance > count) ||
        (config->standard_filter_count > FDCAN_STANDARD_FILTER_MAX) ||
        (config->extended_filter_count > FDCAN_EXTENDED_FILTER_MAX) ||
        (config->rx_fifo0_count != FDCAN_RX_FIFO_ELEMENT_MAX) ||
        (config->rx_fifo1_count != FDCAN_RX_FIFO_ELEMENT_MAX) ||
        (config->tx_event_count != FDCAN_TX_EVENT_ELEMENT_MAX) ||
        (config->tx_fifo_count != FDCAN_TX_FIFO_ELEMENT_MAX) ||
        (config->rx_fifo0_payload_bytes != 64U) || (config->rx_fifo1_payload_bytes != 64U) ||
        (config->tx_payload_bytes != 64U))
    {
        return SPL_ERR_PARAM;
    }
    /* Bounds above prove instance <= 3, filters <= 28/8 and FIFO counts = 3.
     * All arithmetic below is bounded by 636 words; no unchecked multiplication. */
    base = (instance - 1U) * FDCAN_MESSAGE_RAM_WORDS_PER_INSTANCE;
    candidate.sku = sku;
    candidate.instance = instance;
    candidate.instance_start_word = base;
    candidate.instance_end_word = base + FDCAN_MESSAGE_RAM_WORDS_PER_INSTANCE;
    candidate.standard_filters = FDCAN_Section(base, config->standard_filter_count, 1U);
    candidate.extended_filters =
        FDCAN_Section(base + FDCAN_LAYOUT_EXT_FILTER_WORD, config->extended_filter_count, 2U);
    candidate.rx_fifo0 = FDCAN_Section(base + FDCAN_LAYOUT_RX_FIFO0_WORD,
                                       FDCAN_RX_FIFO_ELEMENT_MAX, FDCAN_LAYOUT_ELEMENT_WORDS);
    candidate.rx_fifo1 = FDCAN_Section(base + FDCAN_LAYOUT_RX_FIFO1_WORD,
                                       FDCAN_RX_FIFO_ELEMENT_MAX, FDCAN_LAYOUT_ELEMENT_WORDS);
    candidate.tx_events = FDCAN_Section(base + FDCAN_LAYOUT_TX_EVENT_WORD,
                                        FDCAN_TX_EVENT_ELEMENT_MAX, FDCAN_LAYOUT_EVENT_WORDS);
    candidate.tx_fifo = FDCAN_Section(base + FDCAN_LAYOUT_TX_FIFO_WORD, FDCAN_TX_FIFO_ELEMENT_MAX,
                                      FDCAN_LAYOUT_ELEMENT_WORDS);
    candidate.used_words = FDCAN_MESSAGE_RAM_WORDS_PER_INSTANCE;
    candidate.free_words = 0U;
    *layout = candidate;
    return SPL_OK;
}

static uint8_t FDCAN_SectionEqual(const FDCAN_MessageRamSectionTypeDef *a,
                                  const FDCAN_MessageRamSectionTypeDef *b)
{
    return (uint8_t)(((a->start_word == b->start_word) && (a->element_count == b->element_count) &&
                      (a->element_words == b->element_words) && (a->word_count == b->word_count))
                         ? 1U
                         : 0U);
}

static uint8_t FDCAN_LayoutValid(const FDCAN_MessageRamLayoutTypeDef *layout)
{
    FDCAN_MessageRamConfigTypeDef config = {0U, 0U, 3U, 3U, 3U, 3U, 64U, 64U, 64U};
    FDCAN_MessageRamLayoutTypeDef expected;
    if (layout == (const FDCAN_MessageRamLayoutTypeDef *)0)
    {
        return 0U;
    }
    config.standard_filter_count = layout->standard_filters.element_count;
    config.extended_filter_count = layout->extended_filters.element_count;
    if (FDCAN_LayoutCalculate(layout->sku, layout->instance, &config, &expected) != SPL_OK)
    {
        return 0U;
    }
    /* Compare fields, not struct bytes/padding. Forged bases or sizes cannot
     * pass merely because the supplied word counts are arithmetically consistent. */
    return (uint8_t)(((layout->instance_start_word == expected.instance_start_word) &&
                      (layout->instance_end_word == expected.instance_end_word) &&
                      (layout->used_words == expected.used_words) &&
                      (layout->free_words == expected.free_words) &&
                      (FDCAN_SectionEqual(&layout->standard_filters, &expected.standard_filters) !=
                       0U) &&
                      (FDCAN_SectionEqual(&layout->extended_filters, &expected.extended_filters) !=
                       0U) &&
                      (FDCAN_SectionEqual(&layout->rx_fifo0, &expected.rx_fifo0) != 0U) &&
                      (FDCAN_SectionEqual(&layout->rx_fifo1, &expected.rx_fifo1) != 0U) &&
                      (FDCAN_SectionEqual(&layout->tx_events, &expected.tx_events) != 0U) &&
                      (FDCAN_SectionEqual(&layout->tx_fifo, &expected.tx_fifo) != 0U))
                         ? 1U
                         : 0U);
}

SPL_Status FDCAN_LayoutValidatePair(const FDCAN_MessageRamLayoutTypeDef *first,
                                    const FDCAN_MessageRamLayoutTypeDef *second)
{
    if ((FDCAN_LayoutValid(first) == 0U) || (FDCAN_LayoutValid(second) == 0U) ||
        (first->sku != second->sku) || (first->instance == second->instance))
    {
        return SPL_ERR_PARAM;
    }
    /* Canonical fixed reservations of distinct instances cannot overlap. */
    return SPL_OK;
}
