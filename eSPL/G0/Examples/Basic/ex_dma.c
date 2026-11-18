/* SPDX-License-Identifier: MIT */
/* DMA: copy a 64-word table from flash to RAM with DMA1 channel 1 (memory-to-memory), wait for
 * the transfer-complete flag, then check every word and print the result.
 * Shows: DMA_Init with the channel off, memory-to-memory direction (source = peripheral_address,
 * no DMAMUX request needed), start with DMA_Enable, completion by polling DMA_Flags, the flags
 * cleared by the application (the hardware does not), DMA_DeInit to release the channel. */
#include "ex_board.h"

#define WORDS 64U
#define DMA_FLAG_TC 0x2U /* channel flag nibble: GIF 1, TCIF 2, HTIF 4, TEIF 8 */
#define DMA_FLAG_TE 0x8U

static const uint32_t g_source[WORDS] = {
    0x00000000UL, 0x11111111UL, 0x22222222UL, 0x33333333UL, 0x44444444UL,
    0x55555555UL, 0x66666666UL, 0x77777777UL, 0x88888888UL, 0x99999999UL,
    0xAAAAAAAAUL, 0xBBBBBBBBUL, 0xCCCCCCCCUL, 0xDDDDDDDDUL, 0xEEEEEEEEUL,
    0xFFFFFFFFUL, 0x01234567UL, 0x89ABCDEFUL, 0xDEADBEEFUL, 0xCAFEF00DUL}; /* the rest is 0 */
static uint32_t g_dest[WORDS];

int main(void)
{
    DMA_InitTypeDef cfg;
    uint8_t flags = 0U;
    uint32_t t0 = 0U;
    uint32_t errors = 0U;
    uint32_t i;

    Ex_Init();
    Ex_Print("\r\nex_dma: memory-to-memory copy\r\n");
    for (i = 0U; i < WORDS; i++)
    {
        g_dest[i] = 0x5A5A5A5AUL; /* differs from every source word */
    }

    EX_CHECK(RCC_EnablePeriphClock(RCC_PERIPH_DMA1));
    cfg.peripheral_address = (uint32_t)&g_source[0]; /* memory-to-memory: the source */
    cfg.memory_address = (uint32_t)&g_dest[0];
    cfg.direction = 2U; /* memory -> memory */
    cfg.count = (uint16_t)WORDS;
    cfg.peripheral_width = 2U; /* 32 bit */
    cfg.memory_width = 2U;
    cfg.peripheral_increment = 1U;
    cfg.memory_increment = 1U;
    cfg.circular = 0U; /* not allowed with memory-to-memory */
    cfg.priority = 1U;
    EX_CHECK(DMA_Init(DMA1_Channel1, &cfg));
    EX_CHECK(DMA_Enable(DMA1_Channel1, 1U)); /* memory-to-memory starts at once */

    t0 = Ex_Millis();
    do
    {
        EX_CHECK(DMA_Flags(DMA1_Channel1, &flags));
        if ((flags & DMA_FLAG_TE) != 0U) /* bus error, e.g. a bad address */
        {
            Ex_Fatal(SPL_ERR_HW, (uint32_t)__LINE__);
        }
        if (Ex_Expired(t0, 10U) != 0U)
        {
            Ex_Fatal(SPL_ERR_TIMEOUT, (uint32_t)__LINE__);
        }
    } while ((flags & DMA_FLAG_TC) == 0U);

    EX_CHECK(DMA_Enable(DMA1_Channel1, 0U));
    EX_CHECK(DMA_ClearFlags(DMA1_Channel1, 0xFU)); /* GIF may be cleared now: channel is off */
    EX_WAIT(DMA_DeInit(DMA1_Channel1), 1U);

    for (i = 0U; i < WORDS; i++)
    {
        if (g_dest[i] != g_source[i])
        {
            errors++;
        }
    }
    Ex_Print((errors == 0U) ? "copy OK, " : "copy FAILED, ");
    Ex_PrintU32(WORDS - errors);
    Ex_Print(" of ");
    Ex_PrintU32(WORDS);
    Ex_Print(" words equal\r\n");

    for (;;)
    {
        Ex_LedToggle(); /* slow blink = OK, fast blink = failed */
        Ex_DelayMs((errors == 0U) ? 1000U : 100U);
    }
}
