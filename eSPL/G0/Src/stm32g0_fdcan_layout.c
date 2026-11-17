/* FDCAN message RAM layout (FD-01) implementation for STM32G0B1, ported from the G4 SPL v0.13.5;
 * public contract in stm32g0_fdcan_layout.h. */
/* Library unit: this unit also emits the spl_check.h mismatch-guard reference; the only unit
 * that does not is stm32g0_check.c (SPL_CHECK_LEVEL_DEFINER). BULGU-02, CHECK_LEVEL.md 3.2. */
#define SPL_LIBRARY_BUILD 1
#include "stm32g0_fdcan_layout.h"

static uint32_t FDCAN_InstanceCount(FDCAN_SkuTypeDef sku)
{
    uint32_t count;

    switch (sku)
    {
    case FDCAN_SKU_G0B1:
        count = 2U;
        break;
    default:
        count = 0U;
        break;
    }

    return count;
}

/* RM0444 Rev 6, section 36.3.6, Figure 399: fixed physical reservations.
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
    /* Check level (A4 wave 4a): NULL is checked; configuration / pair validation is this
     * validator's answer (result), at every level. */
    SPL_REQUIRE((config != (const FDCAN_MessageRamConfigTypeDef *)0) &&
                    (layout != (FDCAN_MessageRamLayoutTypeDef *)0),
                SPL_ERR_PARAM);
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
    /* Bounds above prove instance <= 2, filters <= 28/8 and FIFO counts = 3.
     * All arithmetic below is bounded by 424 words; no unchecked multiplication. */
    base = (instance - 1U) * FDCAN_MESSAGE_RAM_WORDS_PER_INSTANCE;
    candidate.sku = sku;
    candidate.instance = instance;
    candidate.instance_start_word = base;
    candidate.instance_end_word = base + FDCAN_MESSAGE_RAM_WORDS_PER_INSTANCE;
    candidate.standard_filters = FDCAN_Section(base, config->standard_filter_count, 1U);
    candidate.extended_filters = FDCAN_Section(base + 28U, config->extended_filter_count, 2U);
    candidate.rx_fifo0 = FDCAN_Section(base + 44U, 3U, 18U);
    candidate.rx_fifo1 = FDCAN_Section(base + 98U, 3U, 18U);
    candidate.tx_events = FDCAN_Section(base + 152U, 3U, 2U);
    candidate.tx_fifo = FDCAN_Section(base + 158U, 3U, 18U);
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
