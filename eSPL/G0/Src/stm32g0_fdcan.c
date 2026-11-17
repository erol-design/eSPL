/* SPDX-License-Identifier: MIT */
/* FDCAN driver implementation for STM32G0B1 (RM0444 Rev 6 section 36, ES0548 Rev 4 section 2.15),
 * ported from the G4 SPL v0.13.5 FDCAN driver (Autometry_Mcu_Spl 13c21ce): same IP and message
 * RAM. Compiled only when SPL_G0_HAS_FDCAN; public contract in stm32g0_fdcan.h. */
/* Library unit: this unit also emits the spl_check.h mismatch-guard reference; the only unit
 * that does not is stm32g0_check.c (SPL_CHECK_LEVEL_DEFINER). BULGU-02, CHECK_LEVEL.md 3.2. */
#define SPL_LIBRARY_BUILD 1
#include "stm32g0_fdcan.h"
#include "stm32g0_fdcan_layout.h"

#if SPL_G0_HAS_FDCAN

/* RM0444 Rev 6 36.3.6, Figure 399: fixed per-instance word offsets. They are
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

/* Element bit fields (RM0444 36.3.8, 36.3.9, 36.3.11, 36.3.12). */
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

/* Check level (A4 wave 4a, Docs/CHECK_LEVEL.md 5.4 FDCAN table): SPL_REQUIRE = checked at FULL only
 * (not compiled at FAST). INIT = 1 (bus-off included), FIFO full / empty, FIFO index, incoming FD
 * frame result: plain if at every level. Helpers are static inline (pure, Tools/pure_checks.txt).
 */

/* Zero-based instance number, or FDCAN_INSTANCE_NONE. Message RAM of
 * instance n starts at SRAMCAN_BASE + n * 0x350 (RM0444 36.3.6). */
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
    return index;
}

static inline uint8_t Fdcan_Valid(const FDCAN_GlobalTypeDef *can)
{
    return (uint8_t)((Fdcan_Index(can) != FDCAN_INSTANCE_NONE) ? 1U : 0U);
}

/* Message RAM is accessed only through 32-bit words. */
static volatile uint32_t *Fdcan_Ram(const FDCAN_GlobalTypeDef *can, uint32_t word)
{
    uint32_t offset = (Fdcan_Index(can) * FDCAN_MESSAGE_RAM_WORDS_PER_INSTANCE) + word;
    return (volatile uint32_t *)(SRAMCAN_BASE + ((uintptr_t)offset * 4U));
}

static inline uint8_t Fdcan_InitSet(const FDCAN_GlobalTypeDef *can)
{
    return (uint8_t)(((can->CCCR & FDCAN_CCCR_INIT) != 0U) ? 1U : 0U);
}

static inline uint8_t Fdcan_CceSet(const FDCAN_GlobalTypeDef *can)
{
    return (uint8_t)(((can->CCCR & FDCAN_CCCR_CCE) != 0U) ? 1U : 0U);
}

static inline uint8_t Fdcan_IsBool(uint32_t value)
{
    return (uint8_t)((value <= 1U) ? 1U : 0U);
}

SPL_Status FDCAN_RequestInit(FDCAN_GlobalTypeDef *can)
{
    SPL_REQUIRE(Fdcan_Valid(can) != 0U, SPL_ERR_PARAM); /* arbitrary SRAM address */

    /* CCL-2 CAN-09b: leaving normal operation with a pending transmission would abort it, and
     * the following Configure (CCE) resets TXBRP. Pending requests -> BUSY: the caller waits for
     * them (FDCAN_TxStatus) or cancels them (FDCAN_TxCancel) first. Already in INIT (bus-off,
     * repeated call): no check. */
    if (((can->CCCR & FDCAN_CCCR_INIT) == 0U) && ((can->TXBRP & FDCAN_TX_BUFFER_MASK) != 0U))
    {
        return SPL_BUSY;
    }
    can->CCCR |= FDCAN_CCCR_INIT;
    return SPL_OK;
}

SPL_Status FDCAN_InitReady(FDCAN_GlobalTypeDef *can, uint8_t *ready)
{
    SPL_REQUIRE(Fdcan_Valid(can) != 0U, SPL_ERR_PARAM); /* arbitrary SRAM address */
    SPL_REQUIRE(ready != (uint8_t *)0, SPL_ERR_PARAM);

    *ready = (uint8_t)(((can->CCCR & FDCAN_CCCR_INIT) != 0U) ? 1U : 0U);
    return SPL_OK;
}

SPL_Status FDCAN_SetNominalTiming(FDCAN_GlobalTypeDef *can, const FDCAN_NominalTimingTypeDef *cfg)
{
    SPL_REQUIRE(Fdcan_Valid(can) != 0U, SPL_ERR_PARAM); /* arbitrary SRAM address */
    SPL_REQUIRE(cfg != (const FDCAN_NominalTimingTypeDef *)0, SPL_ERR_PARAM);
    /* 0 - 1 fills all fields / NBTP overflow / out-of-specification timing. */
    SPL_REQUIRE((cfg->prescaler != 0U) && (cfg->prescaler <= 512U) && (cfg->seg1 != 0U) &&
                    (cfg->seg1 <= 256U) && (cfg->seg2 != 0U) && (cfg->seg2 <= 128U) &&
                    (cfg->sjw != 0U) && (cfg->sjw <= 128U) && (cfg->sjw <= cfg->seg2),
                SPL_ERR_PARAM);
    SPL_REQUIRE(Fdcan_InitSet(can) != 0U, SPL_ERR_STATE); /* protected write is ignored */

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
    SPL_REQUIRE(Fdcan_Valid(can) != 0U, SPL_ERR_PARAM);              /* arbitrary SRAM address */
    SPL_REQUIRE((can->CCCR & FDCAN_CCCR_INIT) != 0U, SPL_ERR_STATE); /* no effect */

    can->CCCR &= (uint32_t)~FDCAN_CCCR_INIT;
    return SPL_OK;
}

SPL_Status FDCAN_Status(FDCAN_GlobalTypeDef *can, uint32_t *psr, uint32_t *ecr)
{
    SPL_REQUIRE(Fdcan_Valid(can) != 0U, SPL_ERR_PARAM); /* arbitrary SRAM address */
    SPL_REQUIRE((psr != (uint32_t *)0) && (ecr != (uint32_t *)0), SPL_ERR_PARAM);

    *psr = can->PSR;
    *ecr = can->ECR;
    return SPL_OK;
}

SPL_Status FDCAN_ClearIrq(FDCAN_GlobalTypeDef *can, uint32_t mask)
{
    SPL_REQUIRE(Fdcan_Valid(can) != 0U, SPL_ERR_PARAM); /* arbitrary SRAM address */
    /* IR 31:24 reserved (same bit positions as IE, RM0444 36.4.x); 2026-11-14 audit. */
    /* (EDR-003): write to a reserved bit. */
    SPL_REQUIRE((mask & ~FDCAN_IE_ALL) == 0U, SPL_ERR_PARAM);

    /* IR is a write-one-to-clear register. */
    can->IR = mask;
    /* Read back: the clear reaches the peripheral before return (METHOD 1.7, issue #23). */
    (void)can->IR;
    return SPL_OK;
}

static inline uint8_t Fdcan_NonMatchValid(FDCAN_NonMatchTypeDef mode)
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

    SPL_REQUIRE(Fdcan_Valid(can) != 0U, SPL_ERR_PARAM); /* arbitrary SRAM address */
    SPL_REQUIRE(cfg != (const FDCAN_ConfigTypeDef *)0, SPL_ERR_PARAM);
    /* RXGFC / TXBC field overflow (reject_remote_* / tx_queue shifted), filters spill into
     * another section. */
    SPL_REQUIRE((cfg->std_filter_count <= FDCAN_STANDARD_FILTER_MAX) &&
                    (cfg->ext_filter_count <= FDCAN_EXTENDED_FILTER_MAX) &&
                    (Fdcan_NonMatchValid(cfg->nonmatch_std) != 0U) &&
                    (Fdcan_NonMatchValid(cfg->nonmatch_ext) != 0U) &&
                    (Fdcan_IsBool(cfg->reject_remote_std) != 0U) &&
                    (Fdcan_IsBool(cfg->reject_remote_ext) != 0U) &&
                    (Fdcan_IsBool(cfg->tx_queue) != 0U),
                SPL_ERR_PARAM);
    /* fd_enable / brs_enable act as 1. */
    SPL_REQUIRE((Fdcan_IsBool(cfg->fd_enable) != 0U) && (Fdcan_IsBool(cfg->brs_enable) != 0U),
                SPL_ERR_PARAM);
    SPL_REQUIRE((cfg->brs_enable == 0U) || (cfg->fd_enable != 0U), SPL_ERR_PARAM);
    /* the Message RAM of a running core would be zeroed. */
    SPL_REQUIRE(Fdcan_InitSet(can) != 0U, SPL_ERR_STATE);

    /* CCE resets RXFxS/TXFQS/TXBRP and holds the Rx/Tx handlers idle
     * (RM0444 36.3.4 Software initialization). Then clear the instance RAM:
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

    cccr = can->CCCR & (uint32_t)~(FDCAN_CCCR_FDOE | FDCAN_CCCR_BRSE);
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

    SPL_REQUIRE(Fdcan_Valid(can) != 0U, SPL_ERR_PARAM); /* arbitrary SRAM address */
    SPL_REQUIRE(cfg != (const FDCAN_DataTimingTypeDef *)0, SPL_ERR_PARAM);
    /* DBTP / TDCR field overflow, out-of-specification timing. */
    SPL_REQUIRE((cfg->prescaler != 0U) && (cfg->prescaler <= 32U) && (cfg->seg1 != 0U) &&
                    (cfg->seg1 <= 32U) && (cfg->seg2 != 0U) && (cfg->seg2 <= 16U) &&
                    (cfg->sjw != 0U) && (cfg->sjw <= cfg->seg2) && (cfg->tdc_offset <= 127U) &&
                    (cfg->tdc_filter <= 127U),
                SPL_ERR_PARAM);
    SPL_REQUIRE(Fdcan_IsBool(cfg->tdc) != 0U, SPL_ERR_PARAM);
    SPL_REQUIRE(Fdcan_InitSet(can) != 0U, SPL_ERR_STATE); /* protected write is ignored */

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

    SPL_REQUIRE(Fdcan_Valid(can) != 0U, SPL_ERR_PARAM); /* arbitrary SRAM address */
    /* an invalid mode silently becomes normal mode. */
    SPL_REQUIRE((mode == FDCAN_MODE_NORMAL) || (mode == FDCAN_MODE_EXT_LOOPBACK) ||
                    (mode == FDCAN_MODE_INT_LOOPBACK) || (mode == FDCAN_MODE_BUS_MONITOR),
                SPL_ERR_PARAM);
    SPL_REQUIRE(Fdcan_InitSet(can) != 0U, SPL_ERR_STATE); /* protected write is ignored */

    /* MON may be set only with INIT and CCE (RM0444 36.4.6). Clearing
     * CCCR.TEST resets FDCAN_TEST in hardware (36.4.4). */
    cccr = (can->CCCR | FDCAN_CCCR_CCE) & (uint32_t)~(FDCAN_CCCR_TEST | FDCAN_CCCR_MON);
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

static inline uint8_t Fdcan_FilterValid(const FDCAN_FilterTypeDef *filter, uint32_t id_max)
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

    SPL_REQUIRE(Fdcan_Valid(can) != 0U, SPL_ERR_PARAM); /* arbitrary SRAM address */
    SPL_REQUIRE(filter != (const FDCAN_FilterTypeDef *)0, SPL_ERR_PARAM);
    /* an invalid type / action / identifier writes a wrong filter to Message RAM. */
    SPL_REQUIRE(Fdcan_FilterValid(filter, FDCAN_STD_ID_MAX) != 0U, SPL_ERR_PARAM);
    count = (can->RXGFC & FDCAN_RXGFC_LSS) >> FDCAN_RXGFC_LSS_Pos;
    if (count > FDCAN_STANDARD_FILTER_MAX)
    {
        count = FDCAN_STANDARD_FILTER_MAX;
    }
    SPL_REQUIRE(index < count, SPL_ERR_PARAM);            /* other filter / section overwritten */
    SPL_REQUIRE(Fdcan_InitSet(can) != 0U, SPL_ERR_STATE); /* protected write is ignored */

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

    SPL_REQUIRE(Fdcan_Valid(can) != 0U, SPL_ERR_PARAM); /* arbitrary SRAM address */
    SPL_REQUIRE(filter != (const FDCAN_FilterTypeDef *)0, SPL_ERR_PARAM);
    /* an invalid type / action / identifier writes a wrong filter to Message RAM. */
    SPL_REQUIRE(Fdcan_FilterValid(filter, FDCAN_EXT_ID_MAX) != 0U, SPL_ERR_PARAM);
    count = (can->RXGFC & FDCAN_RXGFC_LSE) >> FDCAN_RXGFC_LSE_Pos;
    if (count > FDCAN_EXTENDED_FILTER_MAX)
    {
        count = FDCAN_EXTENDED_FILTER_MAX;
    }
    SPL_REQUIRE(index < count, SPL_ERR_PARAM);            /* other filter / section overwritten */
    SPL_REQUIRE(Fdcan_InitSet(can) != 0U, SPL_ERR_STATE); /* protected write is ignored */

    /* EFT 00 for range: XIDAM is applied, and Configure sets it to all ones.
     * F1 first, F0 (holding EFEC enable) last. */
    element = Fdcan_Ram(can, FDCAN_RAM_EXT_FILTER_WORD + (2U * index));
    element[1] = ((uint32_t)filter->type << FDCAN_EFT_POS) | filter->id2;
    element[0] = ((uint32_t)filter->action << FDCAN_EFEC_POS) | filter->id1;
    return SPL_OK;
}

SPL_Status FDCAN_DlcToLength(uint32_t dlc, uint8_t fd, uint8_t *length)
{
    SPL_REQUIRE(dlc <= 15U, SPL_ERR_PARAM); /* read outside the table */
    SPL_REQUIRE(length != (uint8_t *)0, SPL_ERR_PARAM);
    SPL_REQUIRE(fd <= 1U, SPL_ERR_PARAM);

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

    SPL_REQUIRE(dlc != (uint8_t *)0, SPL_ERR_PARAM);
    SPL_REQUIRE(fd <= 1U, SPL_ERR_PARAM);
    for (code = 0U; code <= limit; ++code)
    {
        if (Fdcan_DlcBytes[code] == length)
        {
            *dlc = (uint8_t)code;
            return SPL_OK;
        }
    }
    return SPL_ERR_PARAM; /* result: length not in the DLC table, at every level */
}

/* A4: pure field validator (5.3: a frame of another type on the line). Length / DLC is
 * checked separately in FDCAN_TryWrite (it produces a value). */
static inline uint8_t Fdcan_FrameFieldsValid(const FDCAN_FrameTypeDef *frame)
{
    uint32_t id_max;

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
    return (uint8_t)((frame->id <= id_max) ? 1U : 0U);
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

    SPL_REQUIRE(Fdcan_Valid(can) != 0U, SPL_ERR_PARAM); /* arbitrary SRAM address */
    SPL_REQUIRE(frame != (const FDCAN_FrameTypeDef *)0, SPL_ERR_PARAM);
    SPL_REQUIRE(Fdcan_FrameFieldsValid(frame) != 0U, SPL_ERR_PARAM);
#if SPL_CHECK_LEVEL == SPL_CHECK_FULL
    if (FDCAN_LengthToDlc(frame->length, frame->fdf, &dlc) != SPL_OK)
    {
        return SPL_ERR_PARAM; /* the length overflows the Message RAM element (5.3) */
    }
#else
    (void)FDCAN_LengthToDlc(frame->length, frame->fdf, &dlc);
#endif
    /* INIT = 1 (software init or bus-off, which sets INIT by hardware,
     * RM0444 36.3.4): transfers are stopped, so a queued frame would not be
     * sent and the FIFO would only fill up. INIT = 1 also covers CCE = 1
     * (TXBAR not writable). With FDOE/BRSE clear the core silently ignores
     * FDF/BRS (Table 410): reject instead. */
    if (Fdcan_InitSet(can) != 0U)
    {
        return SPL_ERR_STATE; /* result: INIT = 1 (bus-off included), at every level */
    }
    /* with FDOE / BRSE clear the core silently ignores FDF / BRS (Table 410). */
    SPL_REQUIRE(((frame->fdf == 0U) || ((can->CCCR & FDCAN_CCCR_FDOE) != 0U)) &&
                    ((frame->brs == 0U) || ((can->CCCR & FDCAN_CCCR_BRSE) != 0U)),
                SPL_ERR_STATE);
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
    SPL_REQUIRE(Fdcan_Valid(can) != 0U, SPL_ERR_PARAM); /* arbitrary SRAM address */
    SPL_REQUIRE(mask != 0U, SPL_ERR_PARAM);             /* no effect */
    /* (EDR-003): write to a reserved bit. */
    SPL_REQUIRE((mask & ~FDCAN_TX_BUFFER_MASK) == 0U, SPL_ERR_PARAM);
    /* TXBCR is not writable while CCE = 1, the cancel silently does not happen. */
    SPL_REQUIRE(Fdcan_CceSet(can) == 0U, SPL_ERR_STATE);

    /* TXBCR: write 1 sets, write 0 has no effect. */
    can->TXBCR = mask;
    return SPL_OK;
}

SPL_Status FDCAN_TxStatus(FDCAN_GlobalTypeDef *can, uint32_t *pending, uint32_t *occurred,
                          uint32_t *cancelled)
{
    SPL_REQUIRE(Fdcan_Valid(can) != 0U, SPL_ERR_PARAM); /* arbitrary SRAM address */
    SPL_REQUIRE((pending != (uint32_t *)0) && (occurred != (uint32_t *)0) &&
                    (cancelled != (uint32_t *)0),
                SPL_ERR_PARAM);

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

    SPL_REQUIRE(Fdcan_Valid(can) != 0U, SPL_ERR_PARAM); /* arbitrary SRAM address */
    SPL_REQUIRE(frame != (FDCAN_FrameTypeDef *)0, SPL_ERR_PARAM);
    SPL_REQUIRE(fifo <= 1U, SPL_ERR_PARAM);
    /* CCE resets RXFxS and holds the Rx handler idle: data cannot arrive. CCE requires
     * INIT, so an empty FIFO still returns STATE through INIT. */
    SPL_REQUIRE(Fdcan_CceSet(can) == 0U, SPL_ERR_STATE);
    status = (fifo == 0U) ? can->RXF0S : can->RXF1S;
    /* RXF0S and RXF1S share the same field layout. */
    if ((status & FDCAN_RXF0S_F0FL) == 0U)
    {
        /* INIT = 1 (e.g. set by hardware on bus-off, RM0444 36.3.4): no new
         * frame can arrive, polling will not help. Frames already in the
         * FIFO stay readable (checked above). */
        if (Fdcan_InitSet(can) != 0U)
        {
            return SPL_ERR_STATE; /* result: bus-off included, at every level */
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

    SPL_REQUIRE(Fdcan_Valid(can) != 0U, SPL_ERR_PARAM); /* arbitrary SRAM address */
    SPL_REQUIRE((fill != (uint32_t *)0) && (lost != (uint8_t *)0), SPL_ERR_PARAM);
    SPL_REQUIRE(fifo <= 1U, SPL_ERR_PARAM);

    status = (fifo == 0U) ? can->RXF0S : can->RXF1S;
    *fill = status & FDCAN_RXF0S_F0FL;
    *lost = (uint8_t)(((status & FDCAN_RXF0S_RF0L) != 0U) ? 1U : 0U);
    return SPL_OK;
}

SPL_Status FDCAN_ClearRxLost(FDCAN_GlobalTypeDef *can, uint32_t fifo)
{
    SPL_REQUIRE(Fdcan_Valid(can) != 0U, SPL_ERR_PARAM); /* arbitrary SRAM address */
    SPL_REQUIRE(fifo <= 1U, SPL_ERR_PARAM);             /* acts as FIFO 1 */

    /* IR is W1C: write only the one flag, never read-modify-write. */
    can->IR = (fifo == 0U) ? FDCAN_IR_RF0L : FDCAN_IR_RF1L;
    /* Read back: the clear reaches the peripheral before return (METHOD 1.7, issue #23). */
    (void)can->IR;
    return SPL_OK;
}

SPL_Status FDCAN_IrqEnable(FDCAN_GlobalTypeDef *can, uint32_t ie_mask)
{
    SPL_REQUIRE(Fdcan_Valid(can) != 0U, SPL_ERR_PARAM); /* arbitrary SRAM address */
    /* (EDR-003): write to a reserved bit. */
    SPL_REQUIRE((ie_mask & ~FDCAN_IE_ALL) == 0U, SPL_ERR_PARAM);

    can->IE = ie_mask;
    return SPL_OK;
}

SPL_Status FDCAN_IrqLineSelect(FDCAN_GlobalTypeDef *can, uint32_t ils_mask)
{
    SPL_REQUIRE(Fdcan_Valid(can) != 0U, SPL_ERR_PARAM); /* arbitrary SRAM address */
    /* (EDR-003): write to a reserved bit. */
    SPL_REQUIRE((ils_mask & ~FDCAN_ILS_ALL) == 0U, SPL_ERR_PARAM);

    can->ILS = ils_mask;
    return SPL_OK;
}

SPL_Status FDCAN_IrqLineEnable(FDCAN_GlobalTypeDef *can, uint32_t ile_mask)
{
    SPL_REQUIRE(Fdcan_Valid(can) != 0U, SPL_ERR_PARAM); /* arbitrary SRAM address */
    /* (EDR-003): write to a reserved bit. */
    SPL_REQUIRE((ile_mask & ~FDCAN_ILE_ALL) == 0U, SPL_ERR_PARAM);

    can->ILE = ile_mask;
    return SPL_OK;
}

/* ---- Classic CAN path (Docs/FAST_PATH.md section 8) ---- */

/* Word mask for n bytes (0..4): the bytes beyond n become 0. */
static uint32_t Fdcan_ByteMask(uint32_t n)
{
    return (n >= 4U) ? 0xFFFFFFFFUL : (((uint32_t)1U << (8U * n)) - 1U);
}

/* 4 bytes -> little-endian word (Message RAM layout, RM0444 36.3.12). */
static uint32_t Fdcan_Pack4(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) | ((uint32_t)p[2] << 16U) |
           ((uint32_t)p[3] << 24U);
}

static void Fdcan_Unpack4(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8U);
    p[2] = (uint8_t)(v >> 16U);
    p[3] = (uint8_t)(v >> 24U);
}

SPL_Status FDCAN_TryWriteClassic(FDCAN_GlobalTypeDef *can, const FDCAN_ClassicFrameTypeDef *frame,
                                 uint32_t *buffer)
{
    volatile uint32_t *element;
    uint32_t txfqs;
    uint32_t put;
    uint32_t t0;

    SPL_REQUIRE(Fdcan_Valid(can) != 0U, SPL_ERR_PARAM); /* arbitrary SRAM address */
    SPL_REQUIRE(frame != (const FDCAN_ClassicFrameTypeDef *)0, SPL_ERR_PARAM);
    /* (5.3): a frame of another type on the line; length > 8 overflows the Message RAM
     * element. */
    SPL_REQUIRE((frame->ide <= 1U) && (frame->rtr <= 1U) && (frame->length <= 8U) &&
                    (frame->id <= ((frame->ide != 0U) ? FDCAN_EXT_ID_MAX : FDCAN_STD_ID_MAX)),
                SPL_ERR_PARAM);
    /* INIT = 1 (software or bus-off, RM0444 36.3.4): transfers stop, the queue only fills up.
     * CCE = 1 also requires INIT = 1 (TXBAR not writable). result: at every level. */
    if (Fdcan_InitSet(can) != 0U)
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
    element = Fdcan_Ram(can, FDCAN_RAM_TX_BUFFER_WORD + (put * FDCAN_RAM_ELEMENT_WORDS));
    element[0] = t0;
    /* FDF = BRS = 0, MM = 0, EFC = 0; in classic CAN DLC = length (0..8). */
    element[1] = (uint32_t)frame->length << FDCAN_ELEM_DLC_POS;
    /* Same as FDCAN_TryWrite: ceil(length / 4) words are written, bytes beyond length are 0. */
    if ((frame->rtr == 0U) && (frame->length != 0U))
    {
        element[FDCAN_RAM_HEADER_WORDS] =
            Fdcan_Pack4(&frame->data[0]) & Fdcan_ByteMask(frame->length);
        if (frame->length > 4U)
        {
            element[FDCAN_RAM_HEADER_WORDS + 1U] =
                Fdcan_Pack4(&frame->data[4]) & Fdcan_ByteMask((uint32_t)frame->length - 4U);
        }
    }
    can->TXBAR = (uint32_t)1U << put;
    if (buffer != (uint32_t *)0)
    {
        *buffer = put;
    }
    return SPL_OK;
}

SPL_Status FDCAN_TryReadClassic(FDCAN_GlobalTypeDef *can, uint32_t fifo,
                                FDCAN_ClassicFrameTypeDef *frame)
{
    const volatile uint32_t *element;
    uint32_t status;
    uint32_t get;
    uint32_t r0;
    uint32_t r1;
    uint32_t d0 = 0U;
    uint32_t d1 = 0U;

    SPL_REQUIRE(Fdcan_Valid(can) != 0U, SPL_ERR_PARAM); /* arbitrary SRAM address */
    SPL_REQUIRE(frame != (FDCAN_ClassicFrameTypeDef *)0, SPL_ERR_PARAM);
    SPL_REQUIRE(fifo <= 1U, SPL_ERR_PARAM);
    /* CCE resets RXFxS and holds the Rx handler idle: data cannot arrive (INIT on an
     * empty FIFO). */
    SPL_REQUIRE(Fdcan_CceSet(can) == 0U, SPL_ERR_STATE);
    status = (fifo == 0U) ? can->RXF0S : can->RXF1S;
    if ((status & FDCAN_RXF0S_F0FL) == 0U)
    {
        /* INIT = 1 (bus-off included): no new frame arrives, waiting does not help. */
        if (Fdcan_InitSet(can) != 0U)
        {
            return SPL_ERR_STATE; /* result: bus-off included, at every level */
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
    if ((r1 & FDCAN_ELEM_FDF) != 0U)
    {
        return SPL_ERR_UNSUPPORTED; /* result (incoming data): not acknowledged, read it with
                                     * FDCAN_TryRead */
    }
    frame->ide = (uint8_t)(((r0 & FDCAN_ELEM_XTD) != 0U) ? 1U : 0U);
    frame->rtr = (uint8_t)(((r0 & FDCAN_ELEM_RTR) != 0U) ? 1U : 0U);
    frame->id = (frame->ide != 0U) ? (r0 & FDCAN_EXT_ID_MAX)
                                   : ((r0 >> FDCAN_ELEM_STD_ID_POS) & FDCAN_STD_ID_MAX);
    frame->dlc = (uint8_t)((r1 >> FDCAN_ELEM_DLC_POS) & 0xFU);
    frame->length = (uint8_t)((frame->dlc > 8U) ? 8U : frame->dlc);
    frame->timestamp = r1 & 0xFFFFU;
    frame->filter_index = ((r1 & FDCAN_ELEM_ANMF) != 0U)
                              ? (uint8_t)FDCAN_FILTER_INDEX_NONE
                              : (uint8_t)((r1 >> FDCAN_ELEM_FIDX_POS) & 0x7FU);
    frame->reserved[0] = 0U;
    frame->reserved[1] = 0U;
    frame->reserved[2] = 0U;
    if (frame->rtr == 0U)
    {
        d0 = element[FDCAN_RAM_HEADER_WORDS] & Fdcan_ByteMask(frame->length);
        d1 = (frame->length > 4U) ? (element[FDCAN_RAM_HEADER_WORDS + 1U] &
                                     Fdcan_ByteMask((uint32_t)frame->length - 4U))
                                  : 0U;
    }
    /* Bytes beyond length are 0 (stale Message RAM content is not copied). */
    Fdcan_Unpack4(&frame->data[0], d0);
    Fdcan_Unpack4(&frame->data[4], d1);

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

#else
/* ISO C forbids an empty translation unit. */
typedef int stm32g0_fdcan_absent;
#endif /* SPL_G0_HAS_FDCAN */
