#define SPL_LIBRARY_BUILD 1
#include "stm32g4_sai.h"

/* RM0440 Rev 9 chapter 43 (SAI). Contract: Docs/v0.12_SAI_CONTRACT.md. */

#if defined(SAI1)

SPL_Status SAI_CheckSupport(void)
{
    return SPL_OK;
}

static uint8_t Sai_BlockValid(const SAI_Block_TypeDef *block)
{
    return (uint8_t)(((block == SAI1_Block_A) || (block == SAI1_Block_B)) ? 1U : 0U);
}

static uint8_t Sai_Enabled(const SAI_Block_TypeDef *block)
{
    return (uint8_t)(((block->CR1 & SAI_xCR1_SAIEN) != 0U) ? 1U : 0U);
}

static uint32_t Sai_FifoLevel(const SAI_Block_TypeDef *block)
{
    return (block->SR & SAI_xSR_FLVL_Msk) >> SAI_xSR_FLVL_Pos;
}

/* DS code -> data bits (RM0440 43.6.1). Codes 0/1 are rejected earlier. */
static uint32_t Sai_DataBits(uint32_t ds)
{
    static const uint8_t bits[8] = {0U, 0U, 8U, 10U, 16U, 20U, 24U, 32U};

    return bits[ds & 7U];
}

static uint8_t Sai_Flag(uint32_t value)
{
    return (uint8_t)((value <= 1U) ? 1U : 0U);
}

static uint8_t Sai_RangesValid(const SAI_BlockInitTypeDef *cfg)
{
    uint8_t ok = 1U;

    if ((cfg->mode > SAI_MODE_SLAVE_RX) || (cfg->protocol > SAI_PROTOCOL_AC97) ||
        (cfg->data_size < SAI_DS_8) || (cfg->data_size > SAI_DS_32) ||
        (cfg->sync > SAI_SYNC_INTERNAL) || (cfg->mck_div > 63U))
    {
        ok = 0U;
    }
    if ((cfg->fifo_threshold > SAI_FTH_FULL) || (cfg->mute_count > 63U) ||
        (cfg->companding == 1U) || (cfg->companding > SAI_COMP_ALAW))
    {
        ok = 0U;
    }
    if ((cfg->frl > 255U) || (cfg->fsall > 127U) || (cfg->first_bit_offset > 31U) ||
        (cfg->slot_size > SAI_SLOTSZ_32) || (cfg->nbslot > 15U) || (cfg->slot_enable > 0xFFFFU))
    {
        ok = 0U;
    }
    if ((Sai_Flag(cfg->lsb_first) == 0U) || (Sai_Flag(cfg->clock_strobe) == 0U) ||
        (Sai_Flag(cfg->mono) == 0U) || (Sai_Flag(cfg->out_drive) == 0U) ||
        (Sai_Flag(cfg->no_div) == 0U) || (Sai_Flag(cfg->osr) == 0U) ||
        (Sai_Flag(cfg->mck_en) == 0U))
    {
        ok = 0U;
    }
    if ((Sai_Flag(cfg->tris) == 0U) || (Sai_Flag(cfg->mute) == 0U) ||
        (Sai_Flag(cfg->mute_value) == 0U) || (Sai_Flag(cfg->complement) == 0U) ||
        (Sai_Flag(cfg->fs_definition) == 0U) || (Sai_Flag(cfg->fs_polarity) == 0U) ||
        (Sai_Flag(cfg->fs_offset) == 0U))
    {
        ok = 0U;
    }
    return ok;
}

/* Protocol rules: RM0440 43.6.1 (SYNCEN, MODE note), 43.6.2 (COMP, MUTE),
 * 43.4.11 (AC'97 data size), 43.4.12 (SPDIF). */
static uint8_t Sai_ProtocolValid(const SAI_BlockInitTypeDef *cfg)
{
    uint8_t ok = 1U;

    if ((cfg->sync == SAI_SYNC_INTERNAL) && ((cfg->mode & 2U) == 0U))
    {
        ok = 0U;
    }
    if ((cfg->protocol != SAI_PROTOCOL_FREE) && (cfg->companding != SAI_COMP_NONE))
    {
        ok = 0U;
    }
    if ((cfg->protocol == SAI_PROTOCOL_SPDIF) &&
        ((cfg->mode != SAI_MODE_MASTER_TX) || (cfg->sync != SAI_SYNC_ASYNC) || (cfg->mute != 0U) ||
         (cfg->mute_value != 0U)))
    {
        ok = 0U;
    }
    if ((cfg->protocol == SAI_PROTOCOL_AC97) && (cfg->data_size != SAI_DS_16) &&
        (cfg->data_size != SAI_DS_20))
    {
        ok = 0U;
    }
    return ok;
}

/* Free-protocol frame/slot consistency. RM rules: FRL >= 7 (43.6.3),
 * power-of-2 frame for a master with NODIV=0 (43.4.8), FSDEF needs an even
 * slot count (43.6.3), slot size >= data size (43.6.4). Driver rules derived
 * from 43.6.4 / 43.4.6 / Figure 643: data field inside the slot, slots inside
 * the frame, FS active level inside the frame, enabled slots exist. */
static uint8_t Sai_FrameValid(const SAI_BlockInitTypeDef *cfg)
{
    uint32_t frame_bits = cfg->frl + 1U;
    uint32_t data_bits = Sai_DataBits(cfg->data_size);
    uint32_t slots = cfg->nbslot + 1U;
    uint32_t slot_bits;
    uint8_t ok = 1U;

    if (cfg->slot_size == SAI_SLOTSZ_16)
    {
        slot_bits = 16U;
    }
    else if (cfg->slot_size == SAI_SLOTSZ_32)
    {
        slot_bits = 32U;
    }
    else
    {
        slot_bits = data_bits;
    }

    if ((cfg->frl < 7U) || (cfg->fsall > cfg->frl))
    {
        ok = 0U;
    }
    if (((cfg->mode & 2U) == 0U) && (cfg->no_div == 0U) && ((frame_bits & (frame_bits - 1U)) != 0U))
    {
        ok = 0U;
    }
    if ((cfg->fs_definition != 0U) && ((slots & 1U) != 0U))
    {
        ok = 0U;
    }
    if ((data_bits > slot_bits) || ((cfg->first_bit_offset + data_bits) > slot_bits) ||
        ((slots * slot_bits) > frame_bits))
    {
        ok = 0U;
    }
    if ((cfg->slot_enable == 0U) || ((cfg->slot_enable >> slots) != 0U))
    {
        ok = 0U;
    }
    return ok;
}

static uint8_t Sai_ConfigValid(const SAI_BlockInitTypeDef *cfg)
{
    uint8_t ok = 0U;

    if ((Sai_RangesValid(cfg) != 0U) && (Sai_ProtocolValid(cfg) != 0U))
    {
        ok = 1U;
        if ((cfg->protocol == SAI_PROTOCOL_FREE) && (Sai_FrameValid(cfg) == 0U))
        {
            ok = 0U;
        }
    }
    return ok;
}

/* CR1 without MCKEN, OUTDRIV, DMAEN and SAIEN. */
static uint32_t Sai_Cr1Base(const SAI_BlockInitTypeDef *cfg)
{
    return (cfg->mode << SAI_xCR1_MODE_Pos) | (cfg->protocol << SAI_xCR1_PRTCFG_Pos) |
           (cfg->data_size << SAI_xCR1_DS_Pos) | (cfg->lsb_first << SAI_xCR1_LSBFIRST_Pos) |
           (cfg->clock_strobe << SAI_xCR1_CKSTR_Pos) | (cfg->sync << SAI_xCR1_SYNCEN_Pos) |
           (cfg->mono << SAI_xCR1_MONO_Pos) | (cfg->no_div << SAI_xCR1_NODIV_Pos) |
           (cfg->mck_div << SAI_xCR1_MCKDIV_Pos) | (cfg->osr << SAI_xCR1_OSR_Pos);
}

SPL_Status SAI_BlockInit(SAI_Block_TypeDef *block, const SAI_BlockInitTypeDef *cfg)
{
    uint32_t cr1;
    uint32_t base;
    uint32_t keep_mck;

    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!(Sai_BlockValid(block) == 0U), SPL_ERR_PARAM);
    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!(cfg == (const SAI_BlockInitTypeDef *)0), SPL_ERR_PARAM);
    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!(Sai_ConfigValid(cfg) == 0U), SPL_ERR_PARAM);
    cr1 = block->CR1;
    if ((cr1 & SAI_xCR1_SAIEN) != 0U)
    {
        return SPL_ERR_STATE;
    }
    /* RM0440 43.4.8: changing MCKDIV while MCKEN=1 disturbs MCLK. */
    if (((cr1 & SAI_xCR1_MCKEN) != 0U) &&
        (((cr1 & SAI_xCR1_MCKDIV_Msk) >> SAI_xCR1_MCKDIV_Pos) != cfg->mck_div))
    {
        return SPL_ERR_STATE;
    }

    /* MCLK sequence (RM0440 43.4.8): divider first, MCKEN afterwards. A
     * running MCLK that stays enabled is not switched off in between. */
    base = Sai_Cr1Base(cfg);
    keep_mck = ((cfg->mck_en != 0U) && ((cr1 & SAI_xCR1_MCKEN) != 0U)) ? SAI_xCR1_MCKEN : 0U;
    block->CR1 = base | keep_mck;
    block->CR2 = (cfg->fifo_threshold << SAI_xCR2_FTH_Pos) | (cfg->tris << SAI_xCR2_TRIS_Pos) |
                 (cfg->mute << SAI_xCR2_MUTE_Pos) | (cfg->mute_value << SAI_xCR2_MUTEVAL_Pos) |
                 (cfg->mute_count << SAI_xCR2_MUTECNT_Pos) | (cfg->complement << SAI_xCR2_CPL_Pos) |
                 (cfg->companding << SAI_xCR2_COMP_Pos);
    block->FRCR = (cfg->frl << SAI_xFRCR_FRL_Pos) | (cfg->fsall << SAI_xFRCR_FSALL_Pos) |
                  (cfg->fs_definition << SAI_xFRCR_FSDEF_Pos) |
                  (cfg->fs_polarity << SAI_xFRCR_FSPOL_Pos) |
                  (cfg->fs_offset << SAI_xFRCR_FSOFF_Pos);
    block->SLOTR = (cfg->first_bit_offset << SAI_xSLOTR_FBOFF_Pos) |
                   (cfg->slot_size << SAI_xSLOTR_SLOTSZ_Pos) |
                   (cfg->nbslot << SAI_xSLOTR_NBSLOT_Pos) |
                   (cfg->slot_enable << SAI_xSLOTR_SLOTEN_Pos);
    /* RM0440 43.6.1: OUTDRIV is set after the block configuration. */
    block->CR1 =
        base | (cfg->mck_en << SAI_xCR1_MCKEN_Pos) | (cfg->out_drive << SAI_xCR1_OUTDRIV_Pos);
    return SPL_OK;
}

SPL_Status SAI_CalcClocks(uint32_t ker_hz, const SAI_BlockInitTypeDef *cfg,
                          SAI_ClocksTypeDef *clocks)
{
    uint64_t div;
    uint64_t frame_bits;

    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!(ker_hz == 0U), SPL_ERR_PARAM);
    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!(cfg == (const SAI_BlockInitTypeDef *)0), SPL_ERR_PARAM);
    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!(clocks == (SAI_ClocksTypeDef *)0), SPL_ERR_PARAM);
    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!(Sai_ConfigValid(cfg) == 0U), SPL_ERR_PARAM);
    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!(cfg->protocol != SAI_PROTOCOL_FREE), SPL_ERR_PARAM);
    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!((cfg->mode & 2U) != 0U), SPL_ERR_PARAM);

    /* RM0440 43.4.8: MCKDIV = 0 acts as 1. */
    div = (cfg->mck_div == 0U) ? 1U : (uint64_t)cfg->mck_div;
    frame_bits = (uint64_t)cfg->frl + 1U;
    if (cfg->no_div == 0U)
    {
        uint64_t mclk_per_fs = ((uint64_t)cfg->osr + 1U) * 256U;

        clocks->mclk_hz = (uint32_t)((uint64_t)ker_hz / div);
        clocks->fs_hz = (uint32_t)((uint64_t)ker_hz / (div * mclk_per_fs));
        clocks->sck_hz = (uint32_t)(((uint64_t)ker_hz * frame_bits) / (div * mclk_per_fs));
    }
    else
    {
        clocks->mclk_hz = (uint32_t)((uint64_t)ker_hz / div);
        clocks->sck_hz = clocks->mclk_hz;
        clocks->fs_hz = (uint32_t)((uint64_t)ker_hz / (div * frame_bits));
    }
    return SPL_OK;
}

SPL_Status SAI_Enable(SAI_Block_TypeDef *block)
{
    uint32_t cr1;

    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!(Sai_BlockValid(block) == 0U), SPL_ERR_PARAM);
    cr1 = block->CR1;
    /* RM0440 43.6.1: setting SAIEN while it reads 1 is not taken into account. */
    if ((cr1 & SAI_xCR1_SAIEN) != 0U)
    {
        return SPL_ERR_STATE;
    }
    /* RM0440 43.4.4: a slave transmitter needs data in the FIFO first. */
    if ((((cr1 & SAI_xCR1_MODE_Msk) >> SAI_xCR1_MODE_Pos) == SAI_MODE_SLAVE_TX) &&
        (Sai_FifoLevel(block) == SAI_FLVL_EMPTY))
    {
        return SPL_ERR_STATE;
    }

    block->CR1 = cr1 | SAI_xCR1_SAIEN;
    return SPL_OK;
}

SPL_Status SAI_Disable(SAI_Block_TypeDef *block)
{
    uint32_t cr1;

    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!(Sai_BlockValid(block) == 0U), SPL_ERR_PARAM);
    cr1 = block->CR1;
    if ((cr1 & SAI_xCR1_SAIEN) == 0U)
    {
        return SPL_OK;
    }
    /* RM0440 43.4.15: SAIEN stays 1 until the current frame ends. */
    block->CR1 = cr1 & ~SAI_xCR1_SAIEN;
    return SPL_OK;
}

SPL_Status SAI_IsDisabled(SAI_Block_TypeDef *block, uint8_t *disabled)
{
    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!(Sai_BlockValid(block) == 0U), SPL_ERR_PARAM);
    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!(disabled == (uint8_t *)0), SPL_ERR_PARAM);
    *disabled = (uint8_t)((Sai_Enabled(block) == 0U) ? 1U : 0U);
    return SPL_OK;
}

SPL_Status SAI_FlushFifo(SAI_Block_TypeDef *block)
{
    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!(Sai_BlockValid(block) == 0U), SPL_ERR_PARAM);
    /* RM0440 43.6.2: FFLUSH must be configured when the SAI is disabled. */
    if (Sai_Enabled(block) != 0U)
    {
        return SPL_ERR_STATE;
    }
    /* FFLUSH always reads 0; other CR2 bits are plain configuration. */
    block->CR2 = (block->CR2 & ~SAI_xCR2_FFLUSH) | SAI_xCR2_FFLUSH;
    return SPL_OK;
}

/* Direction and enable are sampled before SR: once SAIEN reads 0 the last
 * frame has completed (RM0440 43.4.15), so an empty RX FIFO stays empty and a
 * full TX FIFO stays full. */
SPL_Status SAI_TryWrite(SAI_Block_TypeDef *block, uint32_t data)
{
    uint32_t cr1;

    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!(Sai_BlockValid(block) == 0U), SPL_ERR_PARAM);
    cr1 = block->CR1;
    if ((cr1 & SAI_xCR1_MODE_0) != 0U)
    {
        return SPL_ERR_STATE;
    }
    if (Sai_FifoLevel(block) == SAI_FLVL_FULL)
    {
        return ((cr1 & SAI_xCR1_SAIEN) != 0U) ? SPL_WOULD_BLOCK : SPL_ERR_STATE;
    }
    block->DR = data;
    return SPL_OK;
}

SPL_Status SAI_TryRead(SAI_Block_TypeDef *block, uint32_t *data)
{
    uint32_t cr1;

    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!(Sai_BlockValid(block) == 0U), SPL_ERR_PARAM);
    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!(data == (uint32_t *)0), SPL_ERR_PARAM);
    cr1 = block->CR1;
    if ((cr1 & SAI_xCR1_MODE_0) == 0U)
    {
        return SPL_ERR_STATE;
    }
    if (Sai_FifoLevel(block) == SAI_FLVL_EMPTY)
    {
        return ((cr1 & SAI_xCR1_SAIEN) != 0U) ? SPL_WOULD_BLOCK : SPL_ERR_STATE;
    }
    *data = block->DR;
    return SPL_OK;
}

SPL_Status SAI_GetFifoLevel(SAI_Block_TypeDef *block, uint32_t *level)
{
    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!(Sai_BlockValid(block) == 0U), SPL_ERR_PARAM);
    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!(level == (uint32_t *)0), SPL_ERR_PARAM);
    *level = Sai_FifoLevel(block);
    return SPL_OK;
}

SPL_Status SAI_GetFlags(SAI_Block_TypeDef *block, uint32_t *flags)
{
    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!(Sai_BlockValid(block) == 0U), SPL_ERR_PARAM);
    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!(flags == (uint32_t *)0), SPL_ERR_PARAM);
    *flags = block->SR & SAI_FLAGS_ALL;
    return SPL_OK;
}

SPL_Status SAI_ClearFlags(SAI_Block_TypeDef *block, uint32_t flags)
{
    uint32_t clr = 0U;

    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!(Sai_BlockValid(block) == 0U), SPL_ERR_PARAM);
    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!(flags == 0U), SPL_ERR_PARAM);
    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!((flags & ~SAI_FLAGS_CLEARABLE) != 0U), SPL_ERR_PARAM);
    /* RM0440 43.6.7: CLRFR is write-only; write the matching bits only. */
    if ((flags & SAI_FLAG_OVRUDR) != 0U)
    {
        clr |= SAI_xCLRFR_COVRUDR;
    }
    if ((flags & SAI_FLAG_MUTEDET) != 0U)
    {
        clr |= SAI_xCLRFR_CMUTEDET;
    }
    if ((flags & SAI_FLAG_WCKCFG) != 0U)
    {
        clr |= SAI_xCLRFR_CWCKCFG;
    }
    if ((flags & SAI_FLAG_CNRDY) != 0U)
    {
        clr |= SAI_xCLRFR_CCNRDY;
    }
    if ((flags & SAI_FLAG_AFSDET) != 0U)
    {
        clr |= SAI_xCLRFR_CAFSDET;
    }
    if ((flags & SAI_FLAG_LFSDET) != 0U)
    {
        clr |= SAI_xCLRFR_CLFSDET;
    }
    block->CLRFR = clr;
    /* Read back: the clear reaches the peripheral before return (METHOD 1.7, issue #23). */
    (void)block->SR;
    return SPL_OK;
}

SPL_Status SAI_InterruptConfig(SAI_Block_TypeDef *block, uint32_t interrupts, uint8_t enable)
{
    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!(Sai_BlockValid(block) == 0U), SPL_ERR_PARAM);
    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!(interrupts == 0U), SPL_ERR_PARAM);
    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!((interrupts & ~SAI_IT_ALL) != 0U), SPL_ERR_PARAM);
    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!(enable > 1U), SPL_ERR_PARAM);
    if (enable != 0U)
    {
        block->IMR = block->IMR | interrupts;
    }
    else
    {
        block->IMR = block->IMR & ~interrupts;
    }
    return SPL_OK;
}

SPL_Status SAI_DmaCmd(SAI_Block_TypeDef *block, uint8_t enable)
{
    uint32_t cr1;

    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!(Sai_BlockValid(block) == 0U), SPL_ERR_PARAM);
    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!(enable > 1U), SPL_ERR_PARAM);
    cr1 = block->CR1;
    if ((cr1 & SAI_xCR1_SAIEN) != 0U)
    {
        return SPL_ERR_STATE;
    }
    if (enable != 0U)
    {
        block->CR1 = cr1 | SAI_xCR1_DMAEN;
    }
    else
    {
        block->CR1 = cr1 & ~SAI_xCR1_DMAEN;
    }
    return SPL_OK;
}

#if defined(SAI_PDMCR_PDMEN)
SPL_Status SAI_PdmInit(SAI_TypeDef *sai, const SAI_PdmInitTypeDef *cfg)
{
    uint32_t cr1;
    uint32_t pdmcr;

    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!(sai != SAI1), SPL_ERR_PARAM);
    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!(cfg == (const SAI_PdmInitTypeDef *)0), SPL_ERR_PARAM);
    /* RM0440 43.3: D[3:1] and CK[2:1] only (max 6 microphones). */
    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!(cfg->microphones > SAI_PDM_MIC_6), SPL_ERR_PARAM);
    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!((cfg->clocks & ~(SAI_PDM_CK1 | SAI_PDM_CK2)) != 0U), SPL_ERR_PARAM);
    /* SAFE: invalid parameter can violate peripheral configuration or memory contract. */
    SPL_REQUIRE_SAFE(!(cfg->enable > 1U), SPL_ERR_PARAM);
    /* RM0440 43.4.10: the PDM interface is enabled before SAI_A. */
    cr1 = SAI1_Block_A->CR1;
    if ((cr1 & SAI_xCR1_SAIEN) != 0U)
    {
        return SPL_ERR_STATE;
    }
    /* RM0440 Table 400: SAI_A must be a free-protocol master receiver. */
    if ((cfg->enable != 0U) &&
        ((((cr1 & SAI_xCR1_MODE_Msk) >> SAI_xCR1_MODE_Pos) != SAI_MODE_MASTER_RX) ||
         ((cr1 & SAI_xCR1_PRTCFG_Msk) != 0U)))
    {
        return SPL_ERR_STATE;
    }

    /* RM0440 43.4.10 sequence: MICNBR and CKEN first, then PDMEN. */
    pdmcr = (cfg->microphones << SAI_PDMCR_MICNBR_Pos) | cfg->clocks;
    sai->PDMCR = pdmcr;
    if (cfg->enable != 0U)
    {
        sai->PDMCR = pdmcr | SAI_PDMCR_PDMEN;
    }
    return SPL_OK;
}
#endif

#else /* SAI1 */

SPL_Status SAI_CheckSupport(void)
{
    return SPL_ERR_UNSUPPORTED;
}

#endif /* SAI1 */
