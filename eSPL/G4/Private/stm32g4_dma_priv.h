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

/* Compile-time check (CCL-2 YAP-03): channel stride 0x14 and DMAMUX stride 4 match CMSIS,
 * the ISR/IFCR nibble of the last channel (shift (n - 1) * 4) fits in 32 bits, the DMA1 channel
 * block ends before DMA2, and STM32G4_DMAMUX_VALID_MASK has one bit per DMAMUX channel. */
typedef char Stm32g4_DmaStrideCheck
    [(((DMA1_Channel2_BASE - DMA1_Channel1_BASE) == 0x14U) &&
      ((DMA2_Channel2_BASE - DMA2_Channel1_BASE) == 0x14U) &&
      ((DMAMUX1_Channel1_BASE - DMAMUX1_Channel0_BASE) == 4U) &&
      (((STM32G4_DMA_CHANNELS_PER_CTRL - 1U) * 4U) <= 28U) &&
      ((DMA1_Channel1_BASE + (STM32G4_DMA_CHANNELS_PER_CTRL * 0x14U)) <= DMA2_BASE) &&
      (STM32G4_DMAMUX_VALID_MASK == ((1UL << (2U * STM32G4_DMA_CHANNELS_PER_CTRL)) - 1UL)))
         ? 1
         : -1];

/* Compile-time check (CCL-2 YAP-03): the last channel reached by
 * base + (STM32G4_DMA_CHANNELS_PER_CTRL - 1) * stride is the last CMSIS channel of each DMA
 * controller and of each DMAMUX half (DMA1: channel 0.., DMA2: STM32G4_DMAMUX_DMA2_FIRST..). */
#if defined(DMA1_Channel8)
typedef char Stm32g4_DmaRegionCheck
    [(((DMA1_Channel1_BASE + ((STM32G4_DMA_CHANNELS_PER_CTRL - 1U) * 0x14U)) ==
       DMA1_Channel8_BASE) &&
      ((DMA2_Channel1_BASE + ((STM32G4_DMA_CHANNELS_PER_CTRL - 1U) * 0x14U)) ==
       DMA2_Channel8_BASE) &&
      ((DMAMUX1_Channel0_BASE + ((STM32G4_DMA_CHANNELS_PER_CTRL - 1U) * 4U)) ==
       DMAMUX1_Channel7_BASE) &&
      ((STM32G4_DMAMUX_DMA2_FIRST + ((STM32G4_DMA_CHANNELS_PER_CTRL - 1U) * 4U)) ==
       DMAMUX1_Channel15_BASE))
         ? 1
         : -1];
#else
typedef char Stm32g4_DmaRegionCheck
    [(((DMA1_Channel1_BASE + ((STM32G4_DMA_CHANNELS_PER_CTRL - 1U) * 0x14U)) ==
       DMA1_Channel6_BASE) &&
      ((DMA2_Channel1_BASE + ((STM32G4_DMA_CHANNELS_PER_CTRL - 1U) * 0x14U)) ==
       DMA2_Channel6_BASE) &&
      ((DMAMUX1_Channel0_BASE + ((STM32G4_DMA_CHANNELS_PER_CTRL - 1U) * 4U)) ==
       DMAMUX1_Channel5_BASE) &&
      ((STM32G4_DMAMUX_DMA2_FIRST + ((STM32G4_DMA_CHANNELS_PER_CTRL - 1U) * 4U)) ==
       DMAMUX1_Channel11_BASE))
         ? 1
         : -1];
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
