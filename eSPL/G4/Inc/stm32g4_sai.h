#ifndef STM32G4_SAI_H
#define STM32G4_SAI_H
#include <stdint.h>
#include "stm32g4xx.h"
#include "stm32g4_status.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* SAI1 (blocks A and B) and its PDM interface, RM0440 Rev 9 chapter 43.
     * Contract: Docs/v0.12_SAI_CONTRACT.md.
     * Caller duties: RCC bus clock (RCC_PERIPH_SAI1) and kernel clock source
     * (RCC_KERNEL_SAI1 / CCIPR.SAI1SEL), GPIO alternate functions, DMA channel
     * and DMAMUX request, NVIC. G4 has no SAI_GCR (no external synchronization,
     * RM0440 43.6 starts at offset 0x04), so there is no SYNCIN/SYNCOUT API.
     * SAI1 is absent on STM32G411xx/G414xx: only SAI_CheckSupport exists there. */

    /* SPL_OK when this SKU has SAI1, SPL_ERR_UNSUPPORTED otherwise. */
    /* Seviye: UNSUPPORTED[r] */
    SPL_Status SAI_CheckSupport(void);

#if defined(SAI1)

/* MODE[1:0] (SAI_xCR1). Bit 1 = slave, bit 0 = receiver. */
#define SAI_MODE_MASTER_TX 0U
#define SAI_MODE_MASTER_RX 1U
#define SAI_MODE_SLAVE_TX 2U
#define SAI_MODE_SLAVE_RX 3U

/* PRTCFG[1:0]. 3 is reserved. */
#define SAI_PROTOCOL_FREE 0U
#define SAI_PROTOCOL_SPDIF 1U
#define SAI_PROTOCOL_AC97 2U

/* DS[2:0]. 0 and 1 are reserved. */
#define SAI_DS_8 2U
#define SAI_DS_10 3U
#define SAI_DS_16 4U
#define SAI_DS_20 5U
#define SAI_DS_24 6U
#define SAI_DS_32 7U

/* SYNCEN[1:0]. 2 and 3 are reserved on G4 (no external synchronization). */
#define SAI_SYNC_ASYNC 0U
#define SAI_SYNC_INTERNAL 1U /* Synchronous with the other block; slave only. */

/* FTH[2:0]. 5..7 are reserved. */
#define SAI_FTH_EMPTY 0U
#define SAI_FTH_QUARTER 1U
#define SAI_FTH_HALF 2U
#define SAI_FTH_3QUARTER 3U
#define SAI_FTH_FULL 4U

/* COMP[1:0]. 1 is reserved. */
#define SAI_COMP_NONE 0U
#define SAI_COMP_ULAW 2U
#define SAI_COMP_ALAW 3U

/* SLOTSZ[1:0]. 3 is reserved. */
#define SAI_SLOTSZ_DATASIZE 0U
#define SAI_SLOTSZ_16 1U
#define SAI_SLOTSZ_32 2U

/* FLVL[2:0] codes returned by SAI_GetFifoLevel (RM0440 43.6.6). */
#define SAI_FLVL_EMPTY 0U
#define SAI_FLVL_FULL 5U

/* SAI_xSR flags reported by SAI_GetFlags. FREQ is a level status cleared by
 * hardware; the others are cleared through SAI_xCLRFR. */
#define SAI_FLAG_OVRUDR SAI_xSR_OVRUDR
#define SAI_FLAG_MUTEDET SAI_xSR_MUTEDET
#define SAI_FLAG_WCKCFG SAI_xSR_WCKCFG
#define SAI_FLAG_FREQ SAI_xSR_FREQ
#define SAI_FLAG_CNRDY SAI_xSR_CNRDY
#define SAI_FLAG_AFSDET SAI_xSR_AFSDET
#define SAI_FLAG_LFSDET SAI_xSR_LFSDET
#define SAI_FLAGS_CLEARABLE                                                                        \
    (SAI_FLAG_OVRUDR | SAI_FLAG_MUTEDET | SAI_FLAG_WCKCFG | SAI_FLAG_CNRDY | SAI_FLAG_AFSDET |     \
     SAI_FLAG_LFSDET)
#define SAI_FLAGS_ALL (SAI_FLAGS_CLEARABLE | SAI_FLAG_FREQ)

/* SAI_xIM interrupt enables accepted by SAI_InterruptConfig. */
#define SAI_IT_OVRUDR SAI_xIMR_OVRUDRIE
#define SAI_IT_MUTEDET SAI_xIMR_MUTEDETIE
#define SAI_IT_WCKCFG SAI_xIMR_WCKCFGIE
#define SAI_IT_FREQ SAI_xIMR_FREQIE
#define SAI_IT_CNRDY SAI_xIMR_CNRDYIE
#define SAI_IT_AFSDET SAI_xIMR_AFSDETIE
#define SAI_IT_LFSDET SAI_xIMR_LFSDETIE
#define SAI_IT_ALL                                                                                 \
    (SAI_IT_OVRUDR | SAI_IT_MUTEDET | SAI_IT_WCKCFG | SAI_IT_FREQ | SAI_IT_CNRDY | SAI_IT_AFSDET | \
     SAI_IT_LFSDET)

    /* Block configuration. Raw register field values (RM0440 43.6.1-43.6.4);
     * flags are 0/1. frl, fsall and nbslot use the register encoding:
     * frame bits = frl + 1, FS active bits = fsall + 1, slots = nbslot + 1. */
    typedef struct
    {
        /* SAI_xCR1 */
        uint32_t mode;         /* SAI_MODE_* */
        uint32_t protocol;     /* SAI_PROTOCOL_* */
        uint32_t data_size;    /* SAI_DS_* */
        uint32_t lsb_first;    /* LSBFIRST */
        uint32_t clock_strobe; /* CKSTR */
        uint32_t sync;         /* SAI_SYNC_* */
        uint32_t mono;         /* MONO */
        uint32_t out_drive;    /* OUTDRIV, written last (RM0440 43.6.1) */
        uint32_t no_div;       /* NODIV */
        uint32_t mck_div;      /* MCKDIV 0..63 (0 acts as 1) */
        uint32_t osr;          /* OSR: MCLK = 256 (0) or 512 (1) x FS, NODIV=0 */
        uint32_t mck_en;       /* MCKEN */
        /* SAI_xCR2 */
        uint32_t fifo_threshold; /* SAI_FTH_* */
        uint32_t tris;           /* TRIS */
        uint32_t mute;           /* MUTE */
        uint32_t mute_value;     /* MUTEVAL */
        uint32_t mute_count;     /* MUTECNT 0..63 */
        uint32_t complement;     /* CPL */
        uint32_t companding;     /* SAI_COMP_* */
        /* SAI_xFRCR */
        uint32_t frl;           /* FRL 0..255 */
        uint32_t fsall;         /* FSALL 0..127 */
        uint32_t fs_definition; /* FSDEF */
        uint32_t fs_polarity;   /* FSPOL */
        uint32_t fs_offset;     /* FSOFF */
        /* SAI_xSLOTR */
        uint32_t first_bit_offset; /* FBOFF 0..31 */
        uint32_t slot_size;        /* SAI_SLOTSZ_* */
        uint32_t nbslot;           /* NBSLOT 0..15 */
        uint32_t slot_enable;      /* SLOTEN 16-bit mask */
    } SAI_BlockInitTypeDef;

    typedef struct
    {
        uint32_t mclk_hz; /* Master clock (NODIV=1: equals SCK) */
        uint32_t sck_hz;  /* Bit clock */
        uint32_t fs_hz;   /* Frame (sampling) rate */
    } SAI_ClocksTypeDef;

    /* Writes CR1 (DMAEN=0, SAIEN=0), CR2 (FFLUSH=0), FRCR and SLOTR. IMR and
     * flags are untouched. PARAM: invalid/inconsistent field (contract table).
     * STATE: SAIEN=1 (also while a disable is still completing), or MCKEN=1
     * and mck_div differs from the running divider (clear MCKEN first). */
    /* Seviye: PARAM[S] STATE[r] */
    SPL_Status SAI_BlockInit(SAI_Block_TypeDef *block, const SAI_BlockInitTypeDef *cfg);

    /* Pure: clock rates for a free-protocol master configuration and a
     * sai_x_ker_ck of ker_hz (RM0440 43.4.8, truncated integer division). */
    /* Seviye: PARAM[S] */
    SPL_Status SAI_CalcClocks(uint32_t ker_hz, const SAI_BlockInitTypeDef *cfg,
                              SAI_ClocksTypeDef *clocks);

    /* Sets SAIEN. STATE: SAIEN already 1 (enabled or disable pending), or a
     * slave transmitter with an empty FIFO (preload it first, RM0440 43.4.4).
     * Master: the kernel clock must be running (caller duty). */
    /* Seviye: PARAM[S] STATE[r] */
    SPL_Status SAI_Enable(SAI_Block_TypeDef *block);

    /* Non-blocking disable request: clears SAIEN. Hardware keeps SAIEN=1 until
     * the current audio frame ends (RM0440 43.4.15); poll SAI_IsDisabled with
     * a bounded budget. A slave whose SCK/FS stopped never completes the
     * frame: recover with the RCC SAI1 reset. OK without a write when already
     * disabled. If one block is synchronous with the other, disable the
     * asynchronous (clock-providing) block first (caller duty). */
    /* Seviye: PARAM[S] */
    SPL_Status SAI_Disable(SAI_Block_TypeDef *block);
    /* Seviye: PARAM[S] */
    SPL_Status SAI_IsDisabled(SAI_Block_TypeDef *block, uint8_t *disabled);

    /* FFLUSH. STATE when SAIEN=1 (RM0440 43.6.2). Disable the DMA channel and
     * the FREQ interrupt beforehand (caller duty). */
    /* Seviye: PARAM[S] STATE[r] */
    SPL_Status SAI_FlushFifo(SAI_Block_TypeDef *block);

    /* One FIFO word. STATE: wrong direction (TryWrite on a receiver, TryRead
     * on a transmitter), or FIFO full (write) / empty (read) with SAIEN=0.
     * WOULD_BLOCK: FIFO full/empty while SAIEN=1. Error flags are not checked
     * (an underrun is normal right after a master TX enable); read them with
     * SAI_GetFlags. A slave without SCK/FS stays WOULD_BLOCK: bound retries. */
    /* Seviye: PARAM[S] STATE[r] WOULD_BLOCK[r] */
    SPL_Status SAI_TryWrite(SAI_Block_TypeDef *block, uint32_t data);
    /* Seviye: PARAM[S] STATE[r] WOULD_BLOCK[r] */
    SPL_Status SAI_TryRead(SAI_Block_TypeDef *block, uint32_t *data);

    /* FLVL code 0..5 (SAI_FLVL_*). */
    /* Seviye: PARAM[S] */
    SPL_Status SAI_GetFifoLevel(SAI_Block_TypeDef *block, uint32_t *level);
    /* SR & SAI_FLAGS_ALL, no side effects. */
    /* Seviye: PARAM[S] */
    SPL_Status SAI_GetFlags(SAI_Block_TypeDef *block, uint32_t *flags);
    /* Writes exactly the CLRFR bits of the given flags (non-zero subset of
     * SAI_FLAGS_CLEARABLE). CLRFR is write-only; nothing is read back. */
    /* Seviye: PARAM[S] */
    SPL_Status SAI_ClearFlags(SAI_Block_TypeDef *block, uint32_t flags);

    /* Sets (enable=1) or clears (enable=0) the given IMR bits (non-zero subset
     * of SAI_IT_ALL). Allowed in any state. */
    /* Seviye: PARAM[S] */
    SPL_Status SAI_InterruptConfig(SAI_Block_TypeDef *block, uint32_t interrupts, uint8_t enable);

    /* DMAEN. STATE when SAIEN=1: a CR1 read-modify-write would copy SAIEN=1
     * back over a pending disable request. Order: BlockInit, DMA channel,
     * SAI_DmaCmd(1), SAI_Enable. */
    /* Seviye: PARAM[S] STATE[r] */
    SPL_Status SAI_DmaCmd(SAI_Block_TypeDef *block, uint8_t enable);

#if defined(SAI_PDMCR_PDMEN)
/* PDM interface: G4 provides SAI_D1..D3 and SAI_CK1..2 only (RM0440 43.3). */
#define SAI_PDM_MIC_2 0U
#define SAI_PDM_MIC_4 1U
#define SAI_PDM_MIC_6 2U
#define SAI_PDM_CK1 SAI_PDMCR_CKEN1
#define SAI_PDM_CK2 SAI_PDMCR_CKEN2

    typedef struct
    {
        uint32_t microphones; /* SAI_PDM_MIC_* (MICNBR) */
        uint32_t clocks;      /* SAI_PDM_CK1 | SAI_PDM_CK2 */
        uint32_t enable;      /* PDMEN */
    } SAI_PdmInitTypeDef;

    /* Writes PDMCR: MICNBR/CKEN with PDMEN=0, then PDMEN when enable=1
     * (RM0440 43.4.10 sequence). STATE: block A SAIEN=1 (the PDM interface
     * must be configured before SAI_A is enabled), or enable=1 while block A
     * is not a free-protocol master receiver (RM0440 Table 400). */
    /* Seviye: PARAM[S] STATE[r] */
    SPL_Status SAI_PdmInit(SAI_TypeDef *sai, const SAI_PdmInitTypeDef *cfg);
#endif

#endif /* SAI1 */

#ifdef __cplusplus
}
#endif
#endif
