/* SPDX-License-Identifier: MIT */
/* DMAMUX1 request routing driver (RM0440 Rev 9 section 13). The 6/8-channel SKU difference
 * (mux line count, DMA2 address hole) comes from Private/stm32g4_dma_priv.h.
 * Public contract: stm32g4_dmamux.h. */
#include "stm32g4_dmamux.h"
#include "stm32g4_dma_priv.h"

SPL_Status DMAMUX_SetRequest(DMA_Channel_TypeDef *dma_channel, uint8_t request_id)
{
    DMA_TypeDef *controller;
    uint32_t local_index;
    uint32_t global_index;
    uintptr_t mux_address;

    /* DMAREQ_ID in CxCR is only 7 bits; an overflow would change other fields. */
    SPL_REQUIRE(request_id <= 127U, SPL_ERR_PARAM);
    /* Structural address resolution stays at every level: the location is used below. */
    if (STM32G4_DmaChannelLocation(dma_channel, &controller, &local_index, &global_index) == 0U)
    {
        return SPL_ERR_PARAM;
    }

    (void)controller;
    SPL_REQUIRE((dma_channel->CCR & DMA_CCR_EN) == 0U, SPL_ERR_STATE);
    /* The request selection of an enabled DMA channel must not be changed. */

    /* G431 leaves a reserved address hole before DMAMUX channel 6.
     * Use the CMSIS base of the first DMA2 request channel, not a
     * continuous offset from DMAMUX1_Channel0_BASE. */
    if (global_index < STM32G4_DMA_CHANNELS_PER_CTRL)
    {
        mux_address = (uintptr_t)DMAMUX1_Channel0_BASE + (4U * local_index);
    }
    else
    {
        mux_address = (uintptr_t)STM32G4_DMAMUX_DMA2_FIRST + (4U * local_index);
    }

    ((DMAMUX_Channel_TypeDef *)mux_address)->CCR = (uint32_t)request_id;
    return SPL_OK;
}

SPL_Status DMAMUX_ClearOverrun(uint16_t mask)
{
    /* Each DMAMUX request channel has one synchronization overrun bit.
     * G431/G411: 12 channels; G474/G491 and related SKUs: 16. */
    /* A W1C bit overflow could touch unsupported flags. */
    SPL_REQUIRE(((uint32_t)mask & ~STM32G4_DMAMUX_VALID_MASK) == 0U, SPL_ERR_PARAM);

    /* CFR is write-one-to-clear: no read-modify-write. */
    DMAMUX1_ChannelStatus->CFR = (uint32_t)mask;
    /* Read back: the clear reaches the peripheral before return (METHOD 1.7, issue #23). */
    (void)DMAMUX1_ChannelStatus->CSR;
    return SPL_OK;
}
