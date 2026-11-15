#define SPL_LIBRARY_BUILD 1
#include "stm32g4_fdcan.h"
#include "stm32g4_fdcan_layout.h"

/* RM0440 Rev 9 44.3.6, Figure 670: fixed per-instance word offsets. They are
 * the FD-01 canonical layout (FDCAN_LayoutCalculate); the host test asserts
 * equality so the two cannot drift apart. */
#define FDCAN_RAM_STD_FILTER_WORD 0U
#define FDCAN_RAM_EXT_FILTER_WORD 28U
#define FDCAN_RAM_RX_FIFO0_WORD 44U
#define FDCAN_RAM_RX_FIFO1_WORD 98U
#define FDCAN_RAM_TX_BUFFER_WORD 158U
#define FDCAN_RAM_ELEMENT_WORDS 18U
#define FDCAN_RAM_HEADER_WORDS 2U
#define FDCAN_INSTANCE_NONE 0xFFU

/* Element bit fields (RM0440 44.3.8, 44.3.9, 44.3.11, 44.3.12). */
#define FDCAN_ELEM_ESI (1UL << 31U)
#define FDCAN_ELEM_XTD (1UL << 30U)
#define FDCAN_ELEM_RTR (1UL << 29U)
#define FDCAN_ELEM_STD_ID_POS 18U
#define FDCAN_ELEM_ANMF (1UL << 31U)
#define FDCAN_ELEM_FIDX_POS 24U
#define FDCAN_ELEM_FDF (1UL << 21U)
#define FDCAN_ELEM_BRS (1UL << 20U)
#define FDCAN_ELEM_DLC_POS 16U
#define FDCAN_SFT_POS 30U
#define FDCAN_SFEC_POS 27U
#define FDCAN_SFID1_POS 16U
#define FDCAN_EFEC_POS 29U
#define FDCAN_EFT_POS 30U

#define FDCAN_STD_ID_MAX 0x7FFUL
#define FDCAN_EXT_ID_MAX 0x1FFFFFFFUL
#define FDCAN_TX_BUFFER_MASK 0x7UL

#define FDCAN_IE_ALL                                                                               \
    (FDCAN_IE_RF0NE | FDCAN_IE_RF0FE | FDCAN_IE_RF0LE | FDCAN_IE_RF1NE | FDCAN_IE_RF1FE |          \
     FDCAN_IE_RF1LE | FDCAN_IE_HPME | FDCAN_IE_TCE | FDCAN_IE_TCFE | FDCAN_IE_TFEE |               \
     FDCAN_IE_TEFNE | FDCAN_IE_TEFFE | FDCAN_IE_TEFLE | FDCAN_IE_TSWE | FDCAN_IE_MRAFE |           \
     FDCAN_IE_TOOE | FDCAN_IE_ELOE | FDCAN_IE_EPE | FDCAN_IE_EWE | FDCAN_IE_BOE | FDCAN_IE_WDIE |  \
     FDCAN_IE_PEAE | FDCAN_IE_PEDE | FDCAN_IE_ARAE)
#define FDCAN_ILS_ALL                                                                              \
    (FDCAN_ILS_RXFIFO0 | FDCAN_ILS_RXFIFO1 | FDCAN_ILS_SMSG | FDCAN_ILS_TFERR | FDCAN_ILS_MISC |   \
     FDCAN_ILS_BERR | FDCAN_ILS_PERR)
#define FDCAN_ILE_ALL (FDCAN_ILE_EINT0 | FDCAN_ILE_EINT1)

/* CAN FD DLC code => payload bytes (ISO 11898-1). */
#define FDCAN_MAX_PAYLOAD 64U
static const uint8_t Fdcan_DlcBytes[16] = {0U, 1U,  2U,  3U,  4U,  5U,  6U,  7U,
                                           8U, 12U, 16U, 20U, 24U, 32U, 48U, FDCAN_MAX_PAYLOAD};

/* Compile-time check (CCL2 YAP-03): the largest payload fits the frame buffer
 * and the data words of one Message RAM element (18 words - 2 header words). */
typedef char Fdcan_PayloadCheck[((sizeof(((FDCAN_FrameTypeDef *)0)->data) >= FDCAN_MAX_PAYLOAD) &&
                                 (((FDCAN_RAM_ELEMENT_WORDS - FDCAN_RAM_HEADER_WORDS) * 4U) ==
                                  FDCAN_MAX_PAYLOAD))
                                    ? 1
                                    : -1];

/* Zero-based instance number, or FDCAN_INSTANCE_NONE. Message RAM of
 * instance n starts at SRAMCAN_BASE + n * 0x350 (RM0440 44.3.6). */
static uint32_t Fdcan_Index(const FDCAN_GlobalTypeDef *can)
{
    uint32_t index = FDCAN_INSTANCE_NONE;

    if (can == FDCAN1)
    {
        index = 0U;
    }
#if defined(FDCAN2)
    if (can == FDCAN2)
    {
        index = 1U;
    }
#endif
#if defined(FDCAN3)
    if (can == FDCAN3)
    {
        index = 2U;
    }
#endif
    return index;
}

static uint8_t Fdcan_Valid(const FDCAN_GlobalTypeDef *can)
{
    return (uint8_t)((Fdcan_Index(can) != FDCAN_INSTANCE_NONE) ? 1U : 0U);
}

/* Message RAM is accessed only through 32-bit words. */
static volatile uint32_t *Fdcan_Ram(const FDCAN_GlobalTypeDef *can, uint32_t word)
{
    uint32_t offset = (Fdcan_Index(can) * FDCAN_MESSAGE_RAM_WORDS_PER_INSTANCE) + word;
    return (volatile uint32_t *)(SRAMCAN_BASE + ((uintptr_t)offset * 4U));
}

static uint8_t Fdcan_InitSet(const FDCAN_GlobalTypeDef *can)
{
    return (uint8_t)(((can->CCCR & FDCAN_CCCR_INIT) != 0U) ? 1U : 0U);
}

static uint8_t Fdcan_CceSet(const FDCAN_GlobalTypeDef *can)
{
    return (uint8_t)(((can->CCCR & FDCAN_CCCR_CCE) != 0U) ? 1U : 0U);
}

static uint8_t Fdcan_IsBool(uint32_t value)
{
    return (uint8_t)((value <= 1U) ? 1U : 0U);
}

SPL_Status FDCAN_RequestInit(FDCAN_GlobalTypeDef *can)
{
    /* SAFE: invalid input may cause an incorrect MMIO access or silent data corruption. */
    SPL_REQUIRE_SAFE(!(Fdcan_Valid(can) == 0U), SPL_ERR_PARAM);

    can->CCCR |= FDCAN_CCCR_INIT;
    return SPL_OK;
}

SPL_Status FDCAN_InitReady(FDCAN_GlobalTypeDef *can, uint8_t *ready)
{
    /* SAFE: invalid input may cause an incorrect MMIO access or silent data corruption. */
    SPL_REQUIRE_SAFE(!(Fdcan_Valid(can) == 0U), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(!(ready == (uint8_t *)0), SPL_ERR_PARAM);

    *ready = (uint8_t)(((can->CCCR & FDCAN_CCCR_INIT) != 0U) ? 1U : 0U);
    return SPL_OK;
}

SPL_Status FDCAN_SetNominalTiming(FDCAN_GlobalTypeDef *can, const FDCAN_NominalTimingTypeDef *cfg)
{
    /* SAFE: invalid input may cause an incorrect MMIO access or silent data corruption. */
    SPL_REQUIRE_SAFE(!(Fdcan_Valid(can) == 0U), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(!(cfg == (const FDCAN_NominalTimingTypeDef *)0), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(!(cfg->prescaler == 0U), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(!(cfg->prescaler > 512U), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(!(cfg->seg1 == 0U), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(!(cfg->seg1 > 256U), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(!(cfg->seg2 == 0U), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(!(cfg->seg2 > 128U), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(!(cfg->sjw == 0U), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(!(cfg->sjw > 128U), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(!(cfg->sjw > cfg->seg2), SPL_ERR_PARAM);
    if ((can->CCCR & FDCAN_CCCR_INIT) == 0U)
    {
        return SPL_ERR_STATE;
    }

    /* Timing registers may be updated only after INIT has acknowledged.
     * Enable protected configuration writes through CCE. */
    can->CCCR |= FDCAN_CCCR_CCE;
    can->NBTP = ((uint32_t)(cfg->prescaler - 1U) << FDCAN_NBTP_NBRP_Pos) |
                ((uint32_t)(cfg->seg1 - 1U) << FDCAN_NBTP_NTSEG1_Pos) |
                ((uint32_t)(cfg->seg2 - 1U) << FDCAN_NBTP_NTSEG2_Pos) |
                ((uint32_t)(cfg->sjw - 1U) << FDCAN_NBTP_NSJW_Pos);
    return SPL_OK;
}

SPL_Status FDCAN_LeaveInit(FDCAN_GlobalTypeDef *can)
{
    /* SAFE: invalid input may cause an incorrect MMIO access or silent data corruption. */
    SPL_REQUIRE_SAFE(!(Fdcan_Valid(can) == 0U), SPL_ERR_PARAM);
    if ((can->CCCR & FDCAN_CCCR_INIT) == 0U)
    {
        return SPL_ERR_STATE;
    }

    can->CCCR &= ~FDCAN_CCCR_INIT;
    return SPL_OK;
}

SPL_Status FDCAN_Status(FDCAN_GlobalTypeDef *can, uint32_t *psr, uint32_t *ecr)
{
    /* SAFE: invalid input may cause an incorrect MMIO access or silent data corruption. */
    SPL_REQUIRE_SAFE(!(Fdcan_Valid(can) == 0U), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(!(psr == (uint32_t *)0), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(!(ecr == (uint32_t *)0), SPL_ERR_PARAM);

    *psr = can->PSR;
    *ecr = can->ECR;
    return SPL_OK;
}

SPL_Status FDCAN_ClearIrq(FDCAN_GlobalTypeDef *can, uint32_t mask)
{
    /* SAFE: an invalid pointer cannot identify an FDCAN peripheral. */
    SPL_REQUIRE_SAFE(Fdcan_Valid(can) != 0U, SPL_ERR_PARAM);
    /* SAFE: reserved W1C bits must be written as zero. */
    SPL_REQUIRE_SAFE((mask & ~FDCAN_IE_ALL) == 0U, SPL_ERR_PARAM);

    /* IR is a write-one-to-clear register. */
    can->IR = mask;
    /* Read back: the clear reaches the peripheral before return (METHOD 1.7, issue #23). */
    (void)can->IR;
    return SPL_OK;
}

static uint8_t Fdcan_NonMatchValid(FDCAN_NonMatchTypeDef mode)
{
    return (uint8_t)((((mode == FDCAN_NONMATCH_FIFO0) || (mode == FDCAN_NONMATCH_FIFO1) ||
                       (mode == FDCAN_NONMATCH_REJECT))
                          ? 1U
                          : 0U)
                         ? 1U
                         : 0U);
}

SPL_Status FDCAN_Configure(FDCAN_GlobalTypeDef *can, const FDCAN_ConfigTypeDef *cfg)
{
    volatile uint32_t *ram;
    uint32_t word;
    uint32_t cccr;

    /* SAFE: invalid input may cause an incorrect MMIO access or silent data corruption. */
    SPL_REQUIRE_SAFE(!(Fdcan_Valid(can) == 0U), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(!(cfg == (const FDCAN_ConfigTypeDef *)0), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(!(cfg->std_filter_count > FDCAN_STANDARD_FILTER_MAX), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(!(cfg->ext_filter_count > FDCAN_EXTENDED_FILTER_MAX), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(!(Fdcan_NonMatchValid(cfg->nonmatch_std) == 0U), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(!(Fdcan_NonMatchValid(cfg->nonmatch_ext) == 0U), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(!(Fdcan_IsBool(cfg->reject_remote_std) == 0U), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(!(Fdcan_IsBool(cfg->reject_remote_ext) == 0U), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(!(Fdcan_IsBool(cfg->tx_queue) == 0U), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(!(Fdcan_IsBool(cfg->fd_enable) == 0U), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(!(Fdcan_IsBool(cfg->brs_enable) == 0U), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(!((cfg->brs_enable != 0U) && (cfg->fd_enable == 0U)), SPL_ERR_PARAM);
    if (Fdcan_InitSet(can) == 0U)
    {
        return SPL_ERR_STATE;
    }

    /* CCE resets RXFxS/TXFQS/TXBRP and holds the Rx/Tx handlers idle
     * (RM0440 44.3.4 Software initialization). Then clear the instance RAM:
     * zero SFEC/EFEC disables every filter element. */
    can->CCCR |= FDCAN_CCCR_CCE;
    ram = Fdcan_Ram(can, 0U);
    for (word = 0U; word < FDCAN_MESSAGE_RAM_WORDS_PER_INSTANCE; ++word)
    {
        ram[word] = 0U;
    }

    /* F0OM = F1OM = 0: blocking FIFOs. */
    can->RXGFC = (cfg->ext_filter_count << FDCAN_RXGFC_LSE_Pos) |
                 (cfg->std_filter_count << FDCAN_RXGFC_LSS_Pos) |
                 ((uint32_t)cfg->nonmatch_std << FDCAN_RXGFC_ANFS_Pos) |
                 ((uint32_t)cfg->nonmatch_ext << FDCAN_RXGFC_ANFE_Pos) |
                 (cfg->reject_remote_std << FDCAN_RXGFC_RRFS_Pos) |
                 (cfg->reject_remote_ext << FDCAN_RXGFC_RRFE_Pos);
    can->XIDAM = FDCAN_XIDAM_EIDM;
    can->TXBC = cfg->tx_queue << FDCAN_TXBC_TFQM_Pos;

    cccr = can->CCCR & ~(FDCAN_CCCR_FDOE | FDCAN_CCCR_BRSE);
    if (cfg->fd_enable != 0U)
    {
        cccr |= FDCAN_CCCR_FDOE;
    }
    if (cfg->brs_enable != 0U)
    {
        cccr |= FDCAN_CCCR_BRSE;
    }
    can->CCCR = cccr;
    return SPL_OK;
}

SPL_Status FDCAN_SetDataTiming(FDCAN_GlobalTypeDef *can, const FDCAN_DataTimingTypeDef *cfg)
{
    uint32_t dbtp;

    /* SAFE: invalid input may cause an incorrect MMIO access or silent data corruption. */
    SPL_REQUIRE_SAFE(!(Fdcan_Valid(can) == 0U), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(!(cfg == (const FDCAN_DataTimingTypeDef *)0), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(!(cfg->prescaler == 0U), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(!(cfg->prescaler > 32U), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(!(cfg->seg1 == 0U), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(!(cfg->seg1 > 32U), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(!(cfg->seg2 == 0U), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(!(cfg->seg2 > 16U), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(!(cfg->sjw == 0U), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(!(cfg->sjw > cfg->seg2), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(!(Fdcan_IsBool(cfg->tdc) == 0U), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(!(cfg->tdc_offset > 127U), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(!(cfg->tdc_filter > 127U), SPL_ERR_PARAM);
    if (Fdcan_InitSet(can) == 0U)
    {
        return SPL_ERR_STATE;
    }

    dbtp = ((cfg->prescaler - 1U) << FDCAN_DBTP_DBRP_Pos) |
           ((cfg->seg1 - 1U) << FDCAN_DBTP_DTSEG1_Pos) |
           ((cfg->seg2 - 1U) << FDCAN_DBTP_DTSEG2_Pos) | ((cfg->sjw - 1U) << FDCAN_DBTP_DSJW_Pos);
    if (cfg->tdc != 0U)
    {
        dbtp |= FDCAN_DBTP_TDC;
    }
    can->CCCR |= FDCAN_CCCR_CCE;
    can->DBTP = dbtp;
    can->TDCR = (cfg->tdc_offset << FDCAN_TDCR_TDCO_Pos) | (cfg->tdc_filter << FDCAN_TDCR_TDCF_Pos);
    return SPL_OK;
}

SPL_Status FDCAN_SetTestMode(FDCAN_GlobalTypeDef *can, FDCAN_TestModeTypeDef mode)
{
    uint32_t cccr;

    /* SAFE: invalid input may cause an incorrect MMIO access or silent data corruption. */
    SPL_REQUIRE_SAFE(!(Fdcan_Valid(can) == 0U), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(!((mode != FDCAN_MODE_NORMAL) && (mode != FDCAN_MODE_EXT_LOOPBACK) &&
         (mode != FDCAN_MODE_INT_LOOPBACK) && (mode != FDCAN_MODE_BUS_MONITOR)), SPL_ERR_PARAM);
    if (Fdcan_InitSet(can) == 0U)
    {
        return SPL_ERR_STATE;
    }

    /* MON may be set only with INIT and CCE (RM0440 44.4.6). Clearing
     * CCCR.TEST resets FDCAN_TEST in hardware (44.4.4). */
    cccr = (can->CCCR | FDCAN_CCCR_CCE) & ~(FDCAN_CCCR_TEST | FDCAN_CCCR_MON);
    if ((mode == FDCAN_MODE_EXT_LOOPBACK) || (mode == FDCAN_MODE_INT_LOOPBACK))
    {
        cccr |= FDCAN_CCCR_TEST;
    }
    if ((mode == FDCAN_MODE_INT_LOOPBACK) || (mode == FDCAN_MODE_BUS_MONITOR))
    {
        cccr |= FDCAN_CCCR_MON;
    }
    can->CCCR = cccr;
    if ((cccr & FDCAN_CCCR_TEST) != 0U)
    {
        /* TX[1:0] = 00: pin controlled by the core. */
        can->TEST = FDCAN_TEST_LBCK;
    }
    return SPL_OK;
}

static uint8_t Fdcan_FilterValid(const FDCAN_FilterTypeDef *filter, uint32_t id_max)
{
    if (filter == (const FDCAN_FilterTypeDef *)0)
    {
        return 0U;
    }
    if ((filter->type != FDCAN_FILTER_RANGE) && (filter->type != FDCAN_FILTER_DUAL) &&
        (filter->type != FDCAN_FILTER_MASK))
    {
        return 0U;
    }
    if ((filter->action != FDCAN_FILTER_DISABLE) && (filter->action != FDCAN_FILTER_TO_FIFO0) &&
        (filter->action != FDCAN_FILTER_TO_FIFO1) && (filter->action != FDCAN_FILTER_REJECT))
    {
        return 0U;
    }
    if ((filter->id1 > id_max) || (filter->id2 > id_max))
    {
        return 0U;
    }
    if ((filter->type == FDCAN_FILTER_RANGE) && (filter->id1 > filter->id2))
    {
        return 0U;
    }
    return 1U;
}

SPL_Status FDCAN_SetStdFilter(FDCAN_GlobalTypeDef *can, uint32_t index,
                              const FDCAN_FilterTypeDef *filter)
{
    uint32_t count;

    /* SAFE: invalid input may cause an incorrect MMIO access or silent data corruption. */
    SPL_REQUIRE_SAFE(!(Fdcan_Valid(can) == 0U), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(!(Fdcan_FilterValid(filter, FDCAN_STD_ID_MAX) == 0U), SPL_ERR_PARAM);
    count = (can->RXGFC & FDCAN_RXGFC_LSS) >> FDCAN_RXGFC_LSS_Pos;
    if (count > FDCAN_STANDARD_FILTER_MAX)
    {
        count = FDCAN_STANDARD_FILTER_MAX;
    }
    /* SAFE: invalid input may cause an incorrect MMIO access or silent data corruption. */
    SPL_REQUIRE_SAFE(!(index >= count), SPL_ERR_PARAM);
    if (Fdcan_InitSet(can) == 0U)
    {
        return SPL_ERR_STATE;
    }

    *Fdcan_Ram(can, FDCAN_RAM_STD_FILTER_WORD + index) =
        ((uint32_t)filter->type << FDCAN_SFT_POS) | ((uint32_t)filter->action << FDCAN_SFEC_POS) |
        (filter->id1 << FDCAN_SFID1_POS) | filter->id2;
    return SPL_OK;
}

SPL_Status FDCAN_SetExtFilter(FDCAN_GlobalTypeDef *can, uint32_t index,
                              const FDCAN_FilterTypeDef *filter)
{
    volatile uint32_t *element;
    uint32_t count;

    /* SAFE: invalid input may cause an incorrect MMIO access or silent data corruption. */
    SPL_REQUIRE_SAFE(!(Fdcan_Valid(can) == 0U), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(!(Fdcan_FilterValid(filter, FDCAN_EXT_ID_MAX) == 0U), SPL_ERR_PARAM);
    count = (can->RXGFC & FDCAN_RXGFC_LSE) >> FDCAN_RXGFC_LSE_Pos;
    if (count > FDCAN_EXTENDED_FILTER_MAX)
    {
        count = FDCAN_EXTENDED_FILTER_MAX;
    }
    /* SAFE: invalid input may cause an incorrect MMIO access or silent data corruption. */
    SPL_REQUIRE_SAFE(!(index >= count), SPL_ERR_PARAM);
    if (Fdcan_InitSet(can) == 0U)
    {
        return SPL_ERR_STATE;
    }

    /* EFT 00 for range: XIDAM is applied, and Configure sets it to all ones.
     * F1 first, F0 (holding EFEC enable) last. */
    element = Fdcan_Ram(can, FDCAN_RAM_EXT_FILTER_WORD + (2U * index));
    element[1] = ((uint32_t)filter->type << FDCAN_EFT_POS) | filter->id2;
    element[0] = ((uint32_t)filter->action << FDCAN_EFEC_POS) | filter->id1;
    return SPL_OK;
}

SPL_Status FDCAN_DlcToLength(uint32_t dlc, uint8_t fd, uint8_t *length)
{
    /* SAFE: invalid input may cause an incorrect MMIO access or silent data corruption. */
    SPL_REQUIRE_SAFE(!(dlc > 15U), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(!(fd > 1U), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(!(length == (uint8_t *)0), SPL_ERR_PARAM);

    if ((fd == 0U) && (dlc > 8U))
    {
        *length = 8U;
    }
    else
    {
        *length = Fdcan_DlcBytes[dlc];
    }
    return SPL_OK;
}

SPL_Status FDCAN_LengthToDlc(uint32_t length, uint8_t fd, uint8_t *dlc)
{
    uint32_t code;
    uint32_t limit = (fd != 0U) ? 15U : 8U;

    /* SAFE: invalid input may cause an incorrect MMIO access or silent data corruption. */
    SPL_REQUIRE_SAFE(!(fd > 1U), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(!(dlc == (uint8_t *)0), SPL_ERR_PARAM);
    for (code = 0U; code <= limit; ++code)
    {
        if (Fdcan_DlcBytes[code] == length)
        {
            *dlc = (uint8_t)code;
            return SPL_OK;
        }
    }
    return SPL_ERR_PARAM;
}

static uint8_t Fdcan_FrameValid(const FDCAN_FrameTypeDef *frame, uint8_t *dlc)
{
    uint32_t id_max;

    if (frame == (const FDCAN_FrameTypeDef *)0)
    {
        return 0U;
    }
    if ((frame->ide > 1U) || (frame->rtr > 1U) || (frame->fdf > 1U) || (frame->brs > 1U) ||
        (frame->esi > 1U))
    {
        return 0U;
    }
    /* Remote frames do not exist in CAN FD; BRS/ESI exist only in CAN FD. */
    if (((frame->rtr != 0U) && (frame->fdf != 0U)) ||
        ((frame->fdf == 0U) && ((frame->brs != 0U) || (frame->esi != 0U))))
    {
        return 0U;
    }
    id_max = (frame->ide != 0U) ? FDCAN_EXT_ID_MAX : FDCAN_STD_ID_MAX;
    if (frame->id > id_max)
    {
        return 0U;
    }
    return (uint8_t)((FDCAN_LengthToDlc(frame->length, frame->fdf, dlc) == SPL_OK) ? 1U : 0U);
}

SPL_Status FDCAN_TryWrite(FDCAN_GlobalTypeDef *can, const FDCAN_FrameTypeDef *frame,
                          uint32_t *buffer)
{
    volatile uint32_t *element;
    uint32_t txfqs;
    uint32_t put;
    uint32_t t0;
    uint32_t words;
    uint32_t w;
    uint32_t b;
    uint8_t dlc = 0U;

    /* SAFE: validate the peripheral before using its MMIO registers. */
    SPL_REQUIRE_SAFE(Fdcan_Valid(can) != 0U, SPL_ERR_PARAM);
    /* The frame validation also computes dlc: required even in FAST. */
    if (Fdcan_FrameValid(frame, &dlc) == 0U)
    {
        return SPL_ERR_PARAM;
    }
    /* INIT = 1 (software init or bus-off, which sets INIT by hardware,
     * RM0440 44.3.4): transfers are stopped, so a queued frame would not be
     * sent and the FIFO would only fill up. INIT = 1 also covers CCE = 1
     * (TXBAR not writable). With FDOE/BRSE clear the core silently ignores
     * FDF/BRS (Table 410): reject instead. */
    if ((Fdcan_InitSet(can) != 0U) ||
        ((frame->fdf != 0U) && ((can->CCCR & FDCAN_CCCR_FDOE) == 0U)) ||
        ((frame->brs != 0U) && ((can->CCCR & FDCAN_CCCR_BRSE) == 0U)))
    {
        return SPL_ERR_STATE;
    }
    txfqs = can->TXFQS;
    if ((txfqs & FDCAN_TXFQS_TFQF) != 0U)
    {
        return SPL_WOULD_BLOCK;
    }
    put = (txfqs & FDCAN_TXFQS_TFQPI) >> FDCAN_TXFQS_TFQPI_Pos;
    if (put >= FDCAN_TX_FIFO_ELEMENT_MAX)
    {
        return SPL_ERR_HW;
    }

    t0 = (frame->ide != 0U) ? (FDCAN_ELEM_XTD | frame->id) : (frame->id << FDCAN_ELEM_STD_ID_POS);
    if (frame->rtr != 0U)
    {
        t0 |= FDCAN_ELEM_RTR;
    }
    if (frame->esi != 0U)
    {
        t0 |= FDCAN_ELEM_ESI;
    }
    element = Fdcan_Ram(can, FDCAN_RAM_TX_BUFFER_WORD + (put * FDCAN_RAM_ELEMENT_WORDS));
    element[0] = t0;
    /* MM = 0, EFC = 0: no Tx event stored. */
    element[1] = ((frame->fdf != 0U) ? FDCAN_ELEM_FDF : 0U) |
                 ((frame->brs != 0U) ? FDCAN_ELEM_BRS : 0U) | ((uint32_t)dlc << FDCAN_ELEM_DLC_POS);
    words = (frame->rtr != 0U) ? 0U : (((uint32_t)frame->length + 3U) / 4U);
    for (w = 0U; w < words; ++w)
    {
        uint32_t value = 0U;
        for (b = 0U; b < 4U; ++b)
        {
            uint32_t i = (w * 4U) + b;
            if (i < frame->length)
            {
                value |= (uint32_t)frame->data[i] << (8U * b);
            }
        }
        element[FDCAN_RAM_HEADER_WORDS + w] = value;
    }
    can->TXBAR = 1U << put;
    if (buffer != (uint32_t *)0)
    {
        *buffer = put;
    }
    return SPL_OK;
}

SPL_Status FDCAN_TxCancel(FDCAN_GlobalTypeDef *can, uint32_t mask)
{
    /* SAFE: invalid input may cause an incorrect MMIO access or silent data corruption. */
    SPL_REQUIRE_SAFE(!(Fdcan_Valid(can) == 0U), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(!(mask == 0U), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(!((mask & ~FDCAN_TX_BUFFER_MASK) != 0U), SPL_ERR_PARAM);
    if (Fdcan_CceSet(can) != 0U)
    {
        return SPL_ERR_STATE;
    }

    /* TXBCR: write 1 sets, write 0 has no effect. */
    can->TXBCR = mask;
    return SPL_OK;
}

SPL_Status FDCAN_TxStatus(FDCAN_GlobalTypeDef *can, uint32_t *pending, uint32_t *occurred,
                          uint32_t *cancelled)
{
    /* SAFE: invalid input may cause an incorrect MMIO access or silent data corruption. */
    SPL_REQUIRE_SAFE(!(Fdcan_Valid(can) == 0U), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(!(pending == (uint32_t *)0), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(!(occurred == (uint32_t *)0), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(!(cancelled == (uint32_t *)0), SPL_ERR_PARAM);

    *pending = can->TXBRP & FDCAN_TX_BUFFER_MASK;
    *occurred = can->TXBTO & FDCAN_TX_BUFFER_MASK;
    *cancelled = can->TXBCF & FDCAN_TX_BUFFER_MASK;
    return SPL_OK;
}

SPL_Status FDCAN_TryRead(FDCAN_GlobalTypeDef *can, uint32_t fifo, FDCAN_FrameTypeDef *frame)
{
    FDCAN_FrameTypeDef out = {0};
    const volatile uint32_t *element;
    uint32_t status;
    uint32_t get;
    uint32_t r0;
    uint32_t r1;
    uint32_t words;
    uint32_t w;
    uint32_t b;

    /* SAFE: invalid input may cause an incorrect MMIO access or silent data corruption. */
    SPL_REQUIRE_SAFE(!(Fdcan_Valid(can) == 0U), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(!(fifo > 1U), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(!(frame == (FDCAN_FrameTypeDef *)0), SPL_ERR_PARAM);
    /* CCE resets RXFxS and holds the Rx handler idle: data cannot arrive. */
    if (Fdcan_CceSet(can) != 0U)
    {
        return SPL_ERR_STATE;
    }
    status = (fifo == 0U) ? can->RXF0S : can->RXF1S;
    /* RXF0S and RXF1S share the same field layout. */
    if ((status & FDCAN_RXF0S_F0FL) == 0U)
    {
        /* INIT = 1 (e.g. set by hardware on bus-off, RM0440 44.3.4): no new
         * frame can arrive, polling will not help. Frames already in the
         * FIFO stay readable (checked above). */
        if (Fdcan_InitSet(can) != 0U)
        {
            return SPL_ERR_STATE;
        }
        return SPL_WOULD_BLOCK;
    }
    get = (status & FDCAN_RXF0S_F0GI) >> FDCAN_RXF0S_F0GI_Pos;
    if (get >= FDCAN_RX_FIFO_ELEMENT_MAX)
    {
        return SPL_ERR_HW;
    }

    element = Fdcan_Ram(can, ((fifo == 0U) ? FDCAN_RAM_RX_FIFO0_WORD : FDCAN_RAM_RX_FIFO1_WORD) +
                                 (get * FDCAN_RAM_ELEMENT_WORDS));
    r0 = element[0];
    r1 = element[1];
    out.ide = (uint8_t)(((r0 & FDCAN_ELEM_XTD) != 0U) ? 1U : 0U);
    out.rtr = (uint8_t)(((r0 & FDCAN_ELEM_RTR) != 0U) ? 1U : 0U);
    out.esi = (uint8_t)(((r0 & FDCAN_ELEM_ESI) != 0U) ? 1U : 0U);
    out.id = (out.ide != 0U) ? (r0 & FDCAN_EXT_ID_MAX)
                             : ((r0 >> FDCAN_ELEM_STD_ID_POS) & FDCAN_STD_ID_MAX);
    out.fdf = (uint8_t)(((r1 & FDCAN_ELEM_FDF) != 0U) ? 1U : 0U);
    out.brs = (uint8_t)(((r1 & FDCAN_ELEM_BRS) != 0U) ? 1U : 0U);
    out.dlc = (uint8_t)((r1 >> FDCAN_ELEM_DLC_POS) & 0xFU);
    out.timestamp = r1 & 0xFFFFU;
    out.filter_index = ((r1 & FDCAN_ELEM_ANMF) != 0U)
                           ? (uint8_t)FDCAN_FILTER_INDEX_NONE
                           : (uint8_t)((r1 >> FDCAN_ELEM_FIDX_POS) & 0x7FU);
    /* Cannot fail: dlc is masked to 0..15, fdf is 0/1 and &out.length is non-NULL. */
    (void)FDCAN_DlcToLength(out.dlc, out.fdf, &out.length);
    words = (out.rtr != 0U) ? 0U : (((uint32_t)out.length + 3U) / 4U);
    for (w = 0U; w < words; ++w)
    {
        uint32_t value = element[FDCAN_RAM_HEADER_WORDS + w];
        for (b = 0U; b < 4U; ++b)
        {
            uint32_t i = (w * 4U) + b;
            /* Bytes >= length stay zero (stale Message RAM content). */
            if (i < out.length)
            {
                out.data[i] = (uint8_t)(value >> (8U * b));
            }
        }
    }
    *frame = out;

    if (fifo == 0U)
    {
        can->RXF0A = get;
    }
    else
    {
        can->RXF1A = get;
    }
    return SPL_OK;
}

SPL_Status FDCAN_RxFifoStatus(FDCAN_GlobalTypeDef *can, uint32_t fifo, uint32_t *fill,
                              uint8_t *lost)
{
    uint32_t status;

    /* SAFE: invalid input may cause an incorrect MMIO access or silent data corruption. */
    SPL_REQUIRE_SAFE(!(Fdcan_Valid(can) == 0U), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(!(fifo > 1U), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(!(fill == (uint32_t *)0), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(!(lost == (uint8_t *)0), SPL_ERR_PARAM);

    status = (fifo == 0U) ? can->RXF0S : can->RXF1S;
    *fill = status & FDCAN_RXF0S_F0FL;
    *lost = (uint8_t)(((status & FDCAN_RXF0S_RF0L) != 0U) ? 1U : 0U);
    return SPL_OK;
}

SPL_Status FDCAN_ClearRxLost(FDCAN_GlobalTypeDef *can, uint32_t fifo)
{
    /* SAFE: invalid input may cause an incorrect MMIO access or silent data corruption. */
    SPL_REQUIRE_SAFE(!(Fdcan_Valid(can) == 0U), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(!(fifo > 1U), SPL_ERR_PARAM);

    /* IR is W1C: write only the one flag, never read-modify-write. */
    can->IR = (fifo == 0U) ? FDCAN_IR_RF0L : FDCAN_IR_RF1L;
    /* Read back: the clear reaches the peripheral before return (METHOD 1.7, issue #23). */
    (void)can->IR;
    return SPL_OK;
}

SPL_Status FDCAN_IrqEnable(FDCAN_GlobalTypeDef *can, uint32_t ie_mask)
{
    /* SAFE: invalid input may cause an incorrect MMIO access or silent data corruption. */
    SPL_REQUIRE_SAFE(!(Fdcan_Valid(can) == 0U), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(!((ie_mask & ~FDCAN_IE_ALL) != 0U), SPL_ERR_PARAM);

    can->IE = ie_mask;
    return SPL_OK;
}

SPL_Status FDCAN_IrqLineSelect(FDCAN_GlobalTypeDef *can, uint32_t ils_mask)
{
    /* SAFE: invalid input may cause an incorrect MMIO access or silent data corruption. */
    SPL_REQUIRE_SAFE(!(Fdcan_Valid(can) == 0U), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(!((ils_mask & ~FDCAN_ILS_ALL) != 0U), SPL_ERR_PARAM);

    can->ILS = ils_mask;
    return SPL_OK;
}

SPL_Status FDCAN_IrqLineEnable(FDCAN_GlobalTypeDef *can, uint32_t ile_mask)
{
    /* SAFE: invalid input may cause an incorrect MMIO access or silent data corruption. */
    SPL_REQUIRE_SAFE(!(Fdcan_Valid(can) == 0U), SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(!((ile_mask & ~FDCAN_ILE_ALL) != 0U), SPL_ERR_PARAM);

    can->ILE = ile_mask;
    return SPL_OK;
}
