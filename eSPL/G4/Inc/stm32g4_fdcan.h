/* SPDX-License-Identifier: MIT */
#ifndef STM32G4_FDCAN_H
#define STM32G4_FDCAN_H
#include <stdint.h>
#include "stm32g4xx.h"
#include "stm32g4_status.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* FDCAN driver (RM0440 Rev 9 section 44): Message RAM configuration, acceptance filters,
     * RX FIFO read / acknowledge, TX FIFO / queue add / cancel, CAN FD / BRS and data timing,
     * IRQ routing. Instances: FDCAN1, plus FDCAN2/FDCAN3 only where CMSIS defines them; any
     * other pointer -> SPL_ERR_PARAM. Message RAM layout fixed per instance
     * (stm32g4_fdcan_layout.h, FD-01).
     * Common rules: no function waits, no static mutable state; status order PARAM -> STATE ->
     * WOULD_BLOCK / ERR_HW, and any non-OK result writes no register, Message RAM or output.
     * Protected configuration (Configure, SetDataTiming, SetTestMode, SetNominalTiming) needs
     * INIT = 1 and sets CCE itself; CCE resets RXFxS, TXFQS, TXBRP, TXBTO, TXBCF.
     * Usage: RequestInit -> InitReady==1 -> SetNominalTiming -> Configure -> [SetDataTiming]
     * -> filters -> [SetTestMode] -> LeaveInit -> InitReady==0 -> TryWrite/TryRead. INIT
     * crosses clock domains and reads back late (RM0440 44.4.6 CCCR note): until InitReady
     * reports 0 after LeaveInit, TryWrite/TryRead return STATE for the stale INIT=1. That
     * STATE is transient, not bus-off. Bound this poll; if INIT stays 1, check FDCAN_Status
     * (PSR.BO).
     * Caller duties (all APIs): FDCAN bus clock and kernel clock (FDCANSEL) on, timing values
     * computed from the real kernel clock; GPIO AF of the TX / RX pins and the transceiver
     * (standby / enable pins, termination); NVIC enable, priority and handlers (each enabled
     * interrupt source routed to a serviced line); serialize all calls per instance (task vs
     * ISR included; a single writer owns the TX put index); bound every poll (InitReady,
     * WOULD_BLOCK retries); bus-off recovery (FDCAN_Status, then FDCAN_LeaveInit) is the
     * caller's decision; do not set CCCR.EFBI directly (ERRATA at FDCAN_Configure).
     * Contract: Docs/v0.10_FDCAN_CONTRACT.md. */

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

    /* Caller duty (CCL-2 CAN-03d): choose FDCAN_NONMATCH_REJECT for nonmatch_std/ext, or,
     * when non-matching frames go to a FIFO, check id (filter_index ==
     * FDCAN_FILTER_INDEX_NONE) after FDCAN_TryRead and discard unknown IDs. */
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

    /* Sets CCCR.INIT. With INIT = 0 and a pending transmission request (TXBRP != 0) -> BUSY, no
     * write: entering init stops transfers and CCE (set by the configuration APIs) resets
     * TXBRP. Retry when the requests complete, or cancel them first with FDCAN_TxCancel (a
     * request that never completes, e.g. no ACK, needs the cancel). INIT = 1 already (software
     * or bus-off) is accepted.
     * Caller: poll FDCAN_InitReady until *ready == 1 (bounded) before the configuration
     * APIs. */
    /* Level: PARAM[F] BUSY[r] */
    SPL_Status FDCAN_RequestInit(FDCAN_GlobalTypeDef *can);
    /* *ready = CCCR.INIT as read back (1 = in init). */
    /* Caller: none beyond the file-header duties. */
    /* Level: PARAM[F] */
    SPL_Status FDCAN_InitReady(FDCAN_GlobalTypeDef *can, uint8_t *ready);
    /* INIT=1 required (else STATE); sets CCE, writes NBTP from prescaler / seg1 / seg2 / sjw.
     * Caller: compute the values from the real FDCAN kernel clock so that every node on the bus
     * uses the same bit rate and a compatible sample point. */
    /* Level: PARAM[F] STATE[r] */
    SPL_Status FDCAN_SetNominalTiming(FDCAN_GlobalTypeDef *can,
                                      const FDCAN_NominalTimingTypeDef *cfg);
    /* Clears CCCR.INIT. The read-back follows only after clock-domain sync:
     * poll FDCAN_InitReady until *ready == 0 before TryWrite/TryRead. INIT=0 already -> STATE.
     * Caller: finish the configuration first (CCE is cleared by hardware with INIT); bound the
     * InitReady poll. After bus-off the core rejoins only after 129 x 11 recessive bits. */
    /* Level: PARAM[F] STATE[r] */
    SPL_Status FDCAN_LeaveInit(FDCAN_GlobalTypeDef *can);
    /* DESTRUCTIVE STATUS READ: Reading PSR may reset LEC/DLEC to 7,
     * and reading ECR resets CEL on M_CAN. Capture or log the returned
     * snapshot; do not call this API merely to check that an instance exists.
     * Caller: callers coordinating multiple diagnostics must serialize this read.
     * Caller duty (CCL-2 CAN-10b): detect error passive, either by enabling FDCAN_IE_EPE
     * and handling it, or by calling this API periodically and evaluating PSR.EP / ECR. */
    /* Level: PARAM[F] */
    SPL_Status FDCAN_Status(FDCAN_GlobalTypeDef *can, uint32_t *psr, uint32_t *ecr);
    /* IR = mask (W1C, no read-modify-write), then IR read back. Unknown bits -> PARAM.
     * Caller: capture the flags (and their data, e.g. RFxL) before clearing them. */
    /* Level: PARAM[F] */
    SPL_Status FDCAN_ClearIrq(FDCAN_GlobalTypeDef *can, uint32_t mask);

    /* INIT=1 required; sets CCE. Zeroes this instance's 212 Message RAM words
     * (all filters disabled, FIFOs blocking mode, XIDAM = all ones). */
    /* ERRATA ES0431 2.15.1 / ES0430 2.19.1 / ES0523 2.15.1: CCCR.EFBI never set (kept by RMW);
     * keep it 0.
     * ERRATA ES0431 2.15.2 / ES0430 2.19.2 / ES0523 2.15.2: not reachable, G4 has no dedicated
     * Tx buffers (3 FIFO/queue elements, RM0440 44.3.9).
     * Caller: keep CCCR.EFBI 0 (ERRATA above); call the filter APIs after this one (it disables
     * all filters); data bit rate >= nominal bit rate (FD) is not checked. */
    /* Level: PARAM[F] STATE[r] */
    SPL_Status FDCAN_Configure(FDCAN_GlobalTypeDef *can, const FDCAN_ConfigTypeDef *cfg);
    /* INIT=1 required; sets CCE. Writes DBTP and TDCR.
     * Caller: data bit rate >= nominal and the TDC settings (DBRP with TDC, TDCO from the
     * transceiver loop delay) are not checked: derive them from the kernel clock and the
     * transceiver datasheet. */
    /* Level: PARAM[F] STATE[r] */
    SPL_Status FDCAN_SetDataTiming(FDCAN_GlobalTypeDef *can, const FDCAN_DataTimingTypeDef *cfg);
    /* INIT=1 required; sets CCE. Bench/self-test only. FDCAN_MODE_NORMAL clears TEST / MON.
     * Caller: FDCAN_MODE_EXT_LOOPBACK transmits on the real bus through the TX pin while the
     * node ignores missing ACKs: use it only on an isolated bench bus, never on a vehicle /
     * production bus. FDCAN_MODE_INT_LOOPBACK and FDCAN_MODE_BUS_MONITOR keep the TX pin
     * recessive (no transmission possible; TryWrite returns STATE in bus monitoring). Return to
     * FDCAN_MODE_NORMAL before normal operation. */
    /* Level: PARAM[F] STATE[r] */
    SPL_Status FDCAN_SetTestMode(FDCAN_GlobalTypeDef *can, FDCAN_TestModeTypeDef mode);
    /* INIT=1 required; index < configured LSS / LSE (RXGFC). Standard ID filter element.
     * Caller: FDCAN_Configure first (it zeroes the filters); choose the filter set and the
     * nonmatch policy so that unknown IDs are rejected or discarded (CCL-2 CAN-03d). */
    /* Level: PARAM[F] STATE[r] */
    SPL_Status FDCAN_SetStdFilter(FDCAN_GlobalTypeDef *can, uint32_t index,
                                  const FDCAN_FilterTypeDef *filter);
    /* Extended ID filter element; same rules (range filters use XIDAM = all ones).
     * Caller: as FDCAN_SetStdFilter. */
    /* Level: PARAM[F] STATE[r] */
    SPL_Status FDCAN_SetExtFilter(FDCAN_GlobalTypeDef *can, uint32_t index,
                                  const FDCAN_FilterTypeDef *filter);

    /* Adds one frame at TXFQS.TFQPI and requests it (TXBAR). INIT must be 0:
     * INIT=1 (software init, or set by hardware on bus-off) => STATE; recover
     * via FDCAN_Status (PSR.BO) and FDCAN_LeaveInit. Bus monitoring mode (MON = 1, TEST = 0,
     * FDCAN_SetTestMode) => STATE: the core cannot transmit there.
     * WOULD_BLOCK when the FIFO/queue is full. buffer (may be NULL) returns
     * the Tx buffer index 0..2 used, for TxCancel/TxStatus.
     * Caller: one writer per instance (the put index is not re-read after TXBAR); a frame that
     * never gets an ACK stays pending: supervise it with FDCAN_TxStatus / FDCAN_TxCancel. */
    /* Level: PARAM[F,r] STATE[r] HW[r] WOULD_BLOCK[r] */
    SPL_Status FDCAN_TryWrite(FDCAN_GlobalTypeDef *can, const FDCAN_FrameTypeDef *frame,
                              uint32_t *buffer);
    /* mask: Tx buffer bits 1..7. CCE must be 0. Intended for queue mode.
     * Caller: cancellation completes asynchronously: confirm with FDCAN_TxStatus (cancelled /
     * occurred) before reusing the buffer. */
    /* Level: PARAM[F] STATE[r] */
    SPL_Status FDCAN_TxCancel(FDCAN_GlobalTypeDef *can, uint32_t mask);
    /* Plain reads of TXBRP / TXBTO / TXBCF (bits 2:0). */
    /* Caller: none beyond the file-header duties. */
    /* Level: PARAM[F] */
    SPL_Status FDCAN_TxStatus(FDCAN_GlobalTypeDef *can, uint32_t *pending, uint32_t *occurred,
                              uint32_t *cancelled);

    /* Reads the element at the get index of RX FIFO 0/1, then acknowledges it.
     * CCE must be 0. Empty: WOULD_BLOCK, or STATE when INIT=1 (bus-off sets
     * INIT; nothing can arrive). Frames already queued stay readable with
     * INIT=1. Bytes >= length are zero. Does not report message lost.
     * Caller duty (CCL-2 CAN-06a): one call reads one frame and RFxN is a per-message event
     * flag, so the RX interrupt handler must call this until WOULD_BLOCK (FIFO empty). */
    /* Level: PARAM[F] STATE[r] HW[r] WOULD_BLOCK[r] */
    SPL_Status FDCAN_TryRead(FDCAN_GlobalTypeDef *can, uint32_t fifo, FDCAN_FrameTypeDef *frame);
    /* Plain read of RXFxS: fill level 0..3 and message-lost flag (RFxL). */
    /* Caller: poll it (or enable RFxLE) to detect message lost; TryRead does not report it. */
    /* Level: PARAM[F] */
    SPL_Status FDCAN_RxFifoStatus(FDCAN_GlobalTypeDef *can, uint32_t fifo, uint32_t *fill,
                                  uint8_t *lost);
    /* Clears IR.RFxL (W1C); RXFxS.RFxL follows.
     * Caller: capture the lost flag (FDCAN_RxFifoStatus) before clearing it. */
    /* Level: PARAM[F] */
    SPL_Status FDCAN_ClearRxLost(FDCAN_GlobalTypeDef *can, uint32_t fifo);

    /* IE / ILS / ILE full-register writes; unknown bits => SPL_ERR_PARAM.
     * Caller duty (CCL-2 CAN-11a): route each enabled source group (ILS) to a line (IT0/IT1)
     * enabled in ILE whose NVIC handler is defined and services that source's IR flags. */
    /* Level: PARAM[F] */
    SPL_Status FDCAN_IrqEnable(FDCAN_GlobalTypeDef *can, uint32_t ie_mask);
    /* ILS full-register write: interrupt group -> line IT0 / IT1.
     * Caller: as FDCAN_IrqEnable (CCL-2 CAN-11a). */
    /* Level: PARAM[F] */
    SPL_Status FDCAN_IrqLineSelect(FDCAN_GlobalTypeDef *can, uint32_t ils_mask);
    /* ILE full-register write: enables lines IT0 / IT1.
     * Caller: as FDCAN_IrqEnable (CCL-2 CAN-11a); clear stale IR flags (FDCAN_ClearIrq) before
     * enabling a line, else the IRQ fires at once. */
    /* Level: PARAM[F] */
    SPL_Status FDCAN_IrqLineEnable(FDCAN_GlobalTypeDef *can, uint32_t ile_mask);

    /* Pure helpers. fd=0: DLC 9..15 => 8 bytes; length 0..8 only. */
    /* Caller: none. */
    /* Level: PARAM[F] */
    SPL_Status FDCAN_DlcToLength(uint32_t dlc, uint8_t fd, uint8_t *length);
    /* Length -> DLC; a length not representable in the format -> PARAM.
     * Caller: none. */
    /* Level: PARAM[F,r] */
    SPL_Status FDCAN_LengthToDlc(uint32_t length, uint8_t fd, uint8_t *dlc);

#ifdef __cplusplus
}
#endif
#endif
