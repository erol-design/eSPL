/* Internal DMA/DMAMUX device mapping. Not a public API. */
#ifndef STM32G4_DMA_PRIV_H
#define STM32G4_DMA_PRIV_H

#include <stdint.h>
#include "stm32g4xx.h"

#if defined(DMA1_Channel8)
#define STM32G4_DMA_CHANNELS_PER_CTRL 8U
#define STM32G4_DMAMUX_VALID_MASK     0xFFFFU
#define STM32G4_DMAMUX_DMA2_FIRST    DMAMUX1_Channel8_BASE
#else
#define STM32G4_DMA_CHANNELS_PER_CTRL 6U
#define STM32G4_DMAMUX_VALID_MASK     0x0FFFU
#define STM32G4_DMAMUX_DMA2_FIRST    DMAMUX1_Channel6_BASE
#endif

/* The peripheral map is sparse on some G4 SKUs: do not index DMA
 * channels using sizeof(DMA_Channel_TypeDef). Channel stride is 0x14. */
static inline uint8_t STM32G4_DmaChannelLocation(
    const DMA_Channel_TypeDef *channel,
    DMA_TypeDef **controller,
    uint32_t *local_index,
    uint32_t *global_index)
{
    uintptr_t address;
    uintptr_t base;
    uintptr_t offset;
    uint32_t controller_offset;

    if ((channel == (const DMA_Channel_TypeDef *)0) ||
        (controller == (DMA_TypeDef **)0) ||
        (local_index == (uint32_t *)0) ||
        (global_index == (uint32_t *)0))
    {
        return 0U;
    }

    address = (uintptr_t)channel;
    if ((address >= (uintptr_t)DMA1_Channel1) &&
        (address <= ((uintptr_t)DMA1_Channel1 +
                    (STM32G4_DMA_CHANNELS_PER_CTRL - 1U) * 0x14U)))
    {
        base = (uintptr_t)DMA1_Channel1;
        *controller = DMA1;
        controller_offset = 0U;
    }
    else if ((address >= (uintptr_t)DMA2_Channel1) &&
             (address <= ((uintptr_t)DMA2_Channel1 +
                          (STM32G4_DMA_CHANNELS_PER_CTRL - 1U) * 0x14U)))
    {
        base = (uintptr_t)DMA2_Channel1;
        *controller = DMA2;
        controller_offset = STM32G4_DMA_CHANNELS_PER_CTRL;
    }
    else
    {
        return 0U;
    }

    offset = address - base;
    if ((offset % 0x14U) != 0U)
    {
        return 0U;
    }

    *local_index = (uint32_t)(offset / 0x14U);
    *global_index = controller_offset + *local_index;
    return 1U;
}

#endif /* STM32G4_DMA_PRIV_H */
