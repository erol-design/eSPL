#ifndef STM32G4_FDCAN_H
#define STM32G4_FDCAN_H
#include <stdint.h>
#include "stm32g4xx.h"
#include "stm32g4_status.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* Instances: FDCAN1, plus FDCAN2/FDCAN3 only where CMSIS defines them.
     * Callers manage RCC, kernel clock, pins and transceiver.
     * Serialize all calls per instance; no call is safe against a concurrent
     * call on the same instance (ISR included) unless stated otherwise.
     * Typical bring-up: RequestInit -> InitReady==1 -> SetNominalTiming ->
     * Configure -> [SetDataTiming] -> filters -> [SetTestMode] -> LeaveInit
     * -> InitReady==0 -> TryWrite/TryRead. INIT crosses clock domains and
     * reads back late (RM0440 44.4.6 CCCR note): until InitReady reports 0
     * after LeaveInit, TryWrite/TryRead return STATE for the stale INIT=1.
     * That STATE is transient, not bus-off. Bound this poll; if INIT stays
     * 1, check FDCAN_Status (PSR.BO). */

    typedef struct
    {
        uint32_t prescaler; /* 1..512 */
        uint32_t seg1;      /* 1..256 */
        uint32_t seg2;      /* 1..128 */
        uint32_t sjw;       /* 1..128, <=seg2 */
    } FDCAN_NominalTimingTypeDef;

    /* CAN FD data phase (DBTP/TDCR, RM0440 44.4.3/44.4.14). */
    typedef struct
    {
        uint32_t prescaler;  /* 1..32 */
        uint32_t seg1;       /* 1..32 */
        uint32_t seg2;       /* 1..16 */
        uint32_t sjw;        /* 1..16, <=seg2 */
        uint32_t tdc;        /* 0/1: transceiver delay compensation */
        uint32_t tdc_offset; /* TDCO 0..127 mtq */
        uint32_t tdc_filter; /* TDCF 0..127 mtq */
    } FDCAN_DataTimingTypeDef;

    /* RXGFC ANFS/ANFE encoding. */
    typedef enum
    {
        FDCAN_NONMATCH_FIFO0 = 0,
        FDCAN_NONMATCH_FIFO1 = 1,
        FDCAN_NONMATCH_REJECT = 2
    } FDCAN_NonMatchTypeDef;

    typedef struct
    {
        uint32_t std_filter_count; /* LSS 0..28 */
        uint32_t ext_filter_count; /* LSE 0..8 */
        FDCAN_NonMatchTypeDef nonmatch_std;
        FDCAN_NonMatchTypeDef nonmatch_ext;
        uint32_t reject_remote_std; /* RRFS 0/1 */
        uint32_t reject_remote_ext; /* RRFE 0/1 */
        uint32_t tx_queue;          /* TXBC.TFQM: 0 FIFO, 1 queue */
        uint32_t fd_enable;         /* CCCR.FDOE 0/1 */
        uint32_t brs_enable;        /* CCCR.BRSE 0/1, requires fd_enable */
    } FDCAN_ConfigTypeDef;

    /* SFT/EFT encoding (priority/HPM variants are not exposed). */
    typedef enum
    {
        FDCAN_FILTER_RANGE = 0, /* id1..id2, id1 <= id2 */
        FDCAN_FILTER_DUAL = 1,  /* id1 or id2 */
        FDCAN_FILTER_MASK = 2   /* id1 = filter, id2 = mask */
    } FDCAN_FilterTypeTypeDef;

    /* SFEC/EFEC encoding. */
    typedef enum
    {
        FDCAN_FILTER_DISABLE = 0,
        FDCAN_FILTER_TO_FIFO0 = 1,
        FDCAN_FILTER_TO_FIFO1 = 2,
        FDCAN_FILTER_REJECT = 3
    } FDCAN_FilterActionTypeDef;

    typedef struct
    {
        FDCAN_FilterTypeTypeDef type;
        FDCAN_FilterActionTypeDef action;
        uint32_t id1;
        uint32_t id2;
    } FDCAN_FilterTypeDef;

    typedef enum
    {
        FDCAN_MODE_NORMAL = 0,
        FDCAN_MODE_EXT_LOOPBACK = 1, /* CCCR.TEST + TEST.LBCK */
        FDCAN_MODE_INT_LOOPBACK = 2, /* CCCR.TEST + TEST.LBCK + CCCR.MON */
        FDCAN_MODE_BUS_MONITOR = 3   /* CCCR.MON */
    } FDCAN_TestModeTypeDef;

#define FDCAN_FILTER_INDEX_NONE 0xFFU /* RX: ANMF=1, no filter matched */

    /* TX input fields: id, ide, rtr, fdf, brs, esi, length, data.
     * RX fills every field; dlc is the raw received DLC. */
    typedef struct
    {
        uint32_t id;          /* 11-bit (ide=0) or 29-bit (ide=1) */
        uint32_t timestamp;   /* RX: RXTS; TX: ignored */
        uint8_t ide;          /* 0 standard, 1 extended */
        uint8_t rtr;          /* remote frame; classic only */
        uint8_t fdf;          /* CAN FD format */
        uint8_t brs;          /* bit rate switch; requires fdf */
        uint8_t esi;          /* TX: force ESI recessive; requires fdf */
        uint8_t dlc;          /* RX: raw DLC; TX: ignored (from length) */
        uint8_t length;       /* bytes: classic 0..8, FD 0..8,12,16,20,24,32,48,64 */
        uint8_t filter_index; /* RX: FIDX or FDCAN_FILTER_INDEX_NONE; TX: ignored */
        uint8_t data[64];
    } FDCAN_FrameTypeDef;

    /* Seviye: PARAM[S] */
    SPL_Status FDCAN_RequestInit(FDCAN_GlobalTypeDef *can);
    /* Seviye: PARAM[S] */
    SPL_Status FDCAN_InitReady(FDCAN_GlobalTypeDef *can, uint8_t *ready);
    /* Seviye: PARAM[S] STATE[r] */
    SPL_Status FDCAN_SetNominalTiming(FDCAN_GlobalTypeDef *can,
                                      const FDCAN_NominalTimingTypeDef *cfg);
    /* Clears CCCR.INIT. The read-back follows only after clock-domain sync:
     * poll FDCAN_InitReady until *ready == 0 before TryWrite/TryRead. */
    /* Seviye: PARAM[S] STATE[r] */
    SPL_Status FDCAN_LeaveInit(FDCAN_GlobalTypeDef *can);
    /* DESTRUCTIVE STATUS READ: Reading PSR may reset LEC/DLEC to 7,
     * and reading ECR resets CEL on M_CAN. Capture or log the returned
     * snapshot; do not call this API merely to check that an instance exists.
     * Callers coordinating multiple diagnostics must serialize this read. */
    /* Seviye: PARAM[S] */
    SPL_Status FDCAN_Status(FDCAN_GlobalTypeDef *can, uint32_t *psr, uint32_t *ecr);
    /* Seviye: PARAM[S] */
    SPL_Status FDCAN_ClearIrq(FDCAN_GlobalTypeDef *can, uint32_t mask);

    /* INIT=1 required; sets CCE. Zeroes this instance's 212 Message RAM words
     * (all filters disabled, FIFOs blocking mode, XIDAM = all ones). */
    /* ERRATA ES0431 2.15.1 / ES0430 2.19.1 / ES0523 2.15.1: CCCR.EFBI never set (kept by RMW);
     * keep it 0.
     * ERRATA ES0431 2.15.2 / ES0430 2.19.2 / ES0523 2.15.2: not reachable, G4 has no dedicated
     * Tx buffers (3 FIFO/queue elements, RM0440 44.3.9). */
    /* Seviye: PARAM[S] STATE[r] */
    SPL_Status FDCAN_Configure(FDCAN_GlobalTypeDef *can, const FDCAN_ConfigTypeDef *cfg);
    /* INIT=1 required; sets CCE. Writes DBTP and TDCR. */
    /* Seviye: PARAM[S] STATE[r] */
    SPL_Status FDCAN_SetDataTiming(FDCAN_GlobalTypeDef *can, const FDCAN_DataTimingTypeDef *cfg);
    /* INIT=1 required; sets CCE. Bench/self-test only. */
    /* Seviye: PARAM[S] STATE[r] */
    SPL_Status FDCAN_SetTestMode(FDCAN_GlobalTypeDef *can, FDCAN_TestModeTypeDef mode);
    /* INIT=1 required; index < configured LSS / LSE (RXGFC). */
    /* Seviye: PARAM[S] STATE[r] */
    SPL_Status FDCAN_SetStdFilter(FDCAN_GlobalTypeDef *can, uint32_t index,
                                  const FDCAN_FilterTypeDef *filter);
    /* Seviye: PARAM[S] STATE[r] */
    SPL_Status FDCAN_SetExtFilter(FDCAN_GlobalTypeDef *can, uint32_t index,
                                  const FDCAN_FilterTypeDef *filter);

    /* Adds one frame at TXFQS.TFQPI and requests it (TXBAR). INIT must be 0:
     * INIT=1 (software init, or set by hardware on bus-off) => STATE; recover
     * via FDCAN_Status (PSR.BO) and FDCAN_LeaveInit.
     * WOULD_BLOCK when the FIFO/queue is full. buffer (may be NULL) returns
     * the Tx buffer index 0..2 used, for TxCancel/TxStatus. */
    /* Seviye: PARAM[S,r] STATE[r] HW[r] WOULD_BLOCK[r] */
    SPL_Status FDCAN_TryWrite(FDCAN_GlobalTypeDef *can, const FDCAN_FrameTypeDef *frame,
                              uint32_t *buffer);
    /* mask: Tx buffer bits 1..7. CCE must be 0. Intended for queue mode. */
    /* Seviye: PARAM[S] STATE[r] */
    SPL_Status FDCAN_TxCancel(FDCAN_GlobalTypeDef *can, uint32_t mask);
    /* Plain reads of TXBRP / TXBTO / TXBCF (bits 2:0). */
    /* Seviye: PARAM[S] */
    SPL_Status FDCAN_TxStatus(FDCAN_GlobalTypeDef *can, uint32_t *pending, uint32_t *occurred,
                              uint32_t *cancelled);

    /* Reads the element at the get index of RX FIFO 0/1, then acknowledges it.
     * CCE must be 0. Empty: WOULD_BLOCK, or STATE when INIT=1 (bus-off sets
     * INIT; nothing can arrive). Frames already queued stay readable with
     * INIT=1. Bytes >= length are zero. Does not report message lost. */
    /* Seviye: PARAM[S] STATE[r] HW[r] WOULD_BLOCK[r] */
    SPL_Status FDCAN_TryRead(FDCAN_GlobalTypeDef *can, uint32_t fifo, FDCAN_FrameTypeDef *frame);
    /* Plain read of RXFxS: fill level 0..3 and message-lost flag (RFxL). */
    /* Seviye: PARAM[S] */
    SPL_Status FDCAN_RxFifoStatus(FDCAN_GlobalTypeDef *can, uint32_t fifo, uint32_t *fill,
                                  uint8_t *lost);
    /* Clears IR.RFxL (W1C); RXFxS.RFxL follows. Capture it first. */
    /* Seviye: PARAM[S] */
    SPL_Status FDCAN_ClearRxLost(FDCAN_GlobalTypeDef *can, uint32_t fifo);

    /* IE / ILS / ILE full-register writes; unknown bits => SPL_ERR_PARAM. */
    /* Seviye: PARAM[S] */
    SPL_Status FDCAN_IrqEnable(FDCAN_GlobalTypeDef *can, uint32_t ie_mask);
    /* Seviye: PARAM[S] */
    SPL_Status FDCAN_IrqLineSelect(FDCAN_GlobalTypeDef *can, uint32_t ils_mask);
    /* Seviye: PARAM[S] */
    SPL_Status FDCAN_IrqLineEnable(FDCAN_GlobalTypeDef *can, uint32_t ile_mask);

    /* Pure helpers. fd=0: DLC 9..15 => 8 bytes; length 0..8 only. */
    /* Seviye: PARAM[S] */
    SPL_Status FDCAN_DlcToLength(uint32_t dlc, uint8_t fd, uint8_t *length);
    /* Seviye: PARAM[S,r] */
    SPL_Status FDCAN_LengthToDlc(uint32_t length, uint8_t fd, uint8_t *dlc);

#ifdef __cplusplus
}
#endif
#endif
