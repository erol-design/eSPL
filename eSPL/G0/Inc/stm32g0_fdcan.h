#ifndef STM32G0_FDCAN_H
#define STM32G0_FDCAN_H
#include <stdint.h>
#include "stm32g0xx.h"
#include "stm32g0_status.h"
#include "stm32g0_sku.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* FDCAN controller driver for FDCAN1 and FDCAN2 of STM32G0B1 (RM0444 Rev 6 section 36), ported
 * from the G4 SPL v0.13.5 FDCAN driver (same M_CAN IP and message RAM layout).
 * Scope: v0.8 SKU profile: this API exists only where the device has the peripheral
 * (SPL_G0_HAS_FDCAN); absent on every SKU except G0B1 / G0C1. Both instances share SRAMCAN
 * (FDCAN1 words 0..211, FDCAN2 212..423) and the FDCAN_CONFIG clock divider (CKDIV).
 * Usage (typical bring-up): RequestInit -> InitReady==1 -> SetNominalTiming ->
 * Configure -> [SetDataTiming] -> filters -> [SetTestMode] -> LeaveInit
 * -> InitReady==0 -> TryWrite/TryRead. INIT crosses clock domains and
 * reads back late (RM0444 36.4.6 CCCR note): until InitReady reports 0
 * after LeaveInit, TryWrite/TryRead return STATE for the stale INIT=1.
 * That STATE is transient, not bus-off. Bound this poll; if INIT stays
 * 1, check FDCAN_Status (PSR.BO).
 * Common rules: no function waits (every wait is the caller's bounded poll).
 * Caller duties (all APIs):
 *  - Clock: RCC_PERIPH_FDCAN and RCC_KERNEL_FDCAN; FDCAN_CONFIG.CKDIV (shared by both instances,
 *    not touched by the driver) before the first RequestInit. Timing values assume that clock.
 *  - Pins (AF) and the transceiver.
 *  - Interrupts: NVIC and handlers of the vectors shared with TIM16 (FDCAN_IT0) and TIM17
 *    (FDCAN_IT1); the handler clears the IR bits it handled (FDCAN_ClearIrq).
 *  - Concurrency: serialize all calls per instance; no call is safe against a concurrent call
 *    on the same instance (ISR included) unless stated otherwise.
 * Contract: Docs/v0.6_FDCAN_CONTRACT.md. */
#if SPL_G0_HAS_FDCAN

typedef struct
{
    uint32_t prescaler; /* 1..512 */
    uint32_t seg1;      /* 1..256 */
    uint32_t seg2;      /* 1..128 */
    uint32_t sjw;       /* 1..128, <=seg2 */
} FDCAN_NominalTimingTypeDef;

/* CAN FD data phase (DBTP/TDCR, RM0444 36.4.3/36.4.14). */
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

/* Mode hazards on the bus (RM0444 36.3.4):
 * FDCAN_MODE_EXT_LOOPBACK drives FDCAN_TX: frames reach a connected bus and ACK errors are
 * ignored; never select it on a live bus. FDCAN_MODE_INT_LOOPBACK keeps FDCAN_TX recessive.
 * FDCAN_MODE_BUS_MONITOR sends no ACK and no error frame. */
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

/* Sets CCCR.INIT. From normal operation (INIT=0) with pending transmit requests (TXBRP != 0)
 * -> BUSY, nothing written: wait for them (FDCAN_TxStatus) or cancel them (FDCAN_TxCancel)
 * first, so a planned reconfiguration does not drop a queued frame. INIT=1 already: no check. */
/* Caller: on BUSY wait for (FDCAN_TxStatus) or cancel (FDCAN_TxCancel) the
 * pending frames, then call again; after OK poll FDCAN_InitReady until 1. */
/* Level: PARAM[F] BUSY[r] */
SPL_Status FDCAN_RequestInit(FDCAN_GlobalTypeDef *can);
/* Caller: bound the poll; INIT stuck at 1 after LeaveInit: check FDCAN_Status
 * (PSR.BO). */
/* Level: PARAM[F] */
SPL_Status FDCAN_InitReady(FDCAN_GlobalTypeDef *can, uint8_t *ready);
/* Caller: compute the values from the FDCAN kernel clock after CKDIV
 * (bit rate and sample point are not checked). */
/* Level: PARAM[F] STATE[F] */
SPL_Status FDCAN_SetNominalTiming(FDCAN_GlobalTypeDef *can, const FDCAN_NominalTimingTypeDef *cfg);
/* Clears CCCR.INIT. The read-back follows only after clock-domain sync:
 * poll FDCAN_InitReady until *ready == 0 before TryWrite/TryRead. */
/* Caller: complete the configuration first; poll FDCAN_InitReady until 0
 * (bounded) before TryWrite / TryRead. */
/* Level: PARAM[F] STATE[F] */
SPL_Status FDCAN_LeaveInit(FDCAN_GlobalTypeDef *can);
/* DESTRUCTIVE STATUS READ: Reading PSR may reset LEC/DLEC to 7,
 * and reading ECR resets CEL on M_CAN. Capture or log the returned
 * snapshot; do not call this API merely to check that an instance exists.
 * Callers coordinating multiple diagnostics must serialize this read. */
/* Caller: keep the returned snapshot (the read clears LEC / DLEC / CEL). */
/* Level: PARAM[F] */
SPL_Status FDCAN_Status(FDCAN_GlobalTypeDef *can, uint32_t *psr, uint32_t *ecr);
/* IR = mask (write-one-to-clear); bits outside IE / IR 23:0 -> PARAM. */
/* Caller: clear only the IR bits the handler has handled. */
/* Level: PARAM[F] */
SPL_Status FDCAN_ClearIrq(FDCAN_GlobalTypeDef *can, uint32_t mask);

/* INIT=1 required; sets CCE. Zeroes this instance's 212 Message RAM words
 * (all filters disabled, FIFOs blocking mode, XIDAM = all ones). */
/* ERRATA ES0548 2.15.1: CCCR.EFBI never set (kept by RMW); keep it 0.
 * ERRATA ES0548 2.15.2: not reachable, the G0B1 FDCAN has no dedicated
 * Tx buffers (3 FIFO/queue elements, RM0444 36.3.9). */
/* Caller: follow the bring-up order above; call before the filters (it zeroes
 * them); never set CCCR.EFBI directly. */
/* Level: PARAM[F] STATE[F] */
SPL_Status FDCAN_Configure(FDCAN_GlobalTypeDef *can, const FDCAN_ConfigTypeDef *cfg);
/* INIT=1 required; sets CCE. Writes DBTP and TDCR. */
/* Caller: compute the values from the FDCAN kernel clock after CKDIV and the
 * transceiver delay (not checked). */
/* Level: PARAM[F] STATE[F] */
SPL_Status FDCAN_SetDataTiming(FDCAN_GlobalTypeDef *can, const FDCAN_DataTimingTypeDef *cfg);
/* INIT=1 required; sets CCE. Bench/self-test only. */
/* Caller: mode hazards at FDCAN_TestModeTypeDef (EXT_LOOPBACK drives the bus). */
/* Level: PARAM[F] STATE[F] */
SPL_Status FDCAN_SetTestMode(FDCAN_GlobalTypeDef *can, FDCAN_TestModeTypeDef mode);
/* INIT=1 required; index < configured LSS / LSE (RXGFC). */
/* Caller: FDCAN_Configure first (it sets LSS and zeroes the filters). */
/* Level: PARAM[F] STATE[F] */
SPL_Status FDCAN_SetStdFilter(FDCAN_GlobalTypeDef *can, uint32_t index,
                              const FDCAN_FilterTypeDef *filter);
/* Caller: FDCAN_Configure first (it sets LSE and zeroes the filters). */
/* Level: PARAM[F] STATE[F] */
SPL_Status FDCAN_SetExtFilter(FDCAN_GlobalTypeDef *can, uint32_t index,
                              const FDCAN_FilterTypeDef *filter);

/* Adds one frame at TXFQS.TFQPI and requests it (TXBAR). INIT must be 0:
 * INIT=1 (software init, or set by hardware on bus-off) => STATE; recover
 * via FDCAN_Status (PSR.BO) and FDCAN_LeaveInit.
 * WOULD_BLOCK when the FIFO/queue is full. buffer (may be NULL) returns
 * the Tx buffer index 0..2 used, for TxCancel/TxStatus. */
/* Caller: on STATE check FDCAN_Status (PSR.BO) before FDCAN_LeaveInit; track the
 * returned buffer with FDCAN_TxStatus. */
/* Level: PARAM[F,r] STATE[F,r] HW[r] WOULD_BLOCK[r] */
SPL_Status FDCAN_TryWrite(FDCAN_GlobalTypeDef *can, const FDCAN_FrameTypeDef *frame,
                          uint32_t *buffer);
/* mask: Tx buffer bits 1..7. CCE must be 0. Intended for queue mode. */
/* Caller: completion is not reported here: poll FDCAN_TxStatus (cancelled). */
/* Level: PARAM[F] STATE[F] */
SPL_Status FDCAN_TxCancel(FDCAN_GlobalTypeDef *can, uint32_t mask);
/* Plain reads of TXBRP / TXBTO / TXBCF (bits 2:0). */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status FDCAN_TxStatus(FDCAN_GlobalTypeDef *can, uint32_t *pending, uint32_t *occurred,
                          uint32_t *cancelled);

/* Reads the element at the get index of RX FIFO 0/1, then acknowledges it.
 * CCE must be 0. Empty: WOULD_BLOCK, or STATE when INIT=1 (bus-off sets
 * INIT; nothing can arrive). Frames already queued stay readable with
 * INIT=1. Bytes >= length are zero. Does not report message lost. */
/* Caller: poll FDCAN_RxFifoStatus for message lost (not reported here). */
/* Level: PARAM[F] STATE[F,r] HW[r] WOULD_BLOCK[r] */
SPL_Status FDCAN_TryRead(FDCAN_GlobalTypeDef *can, uint32_t fifo, FDCAN_FrameTypeDef *frame);
/* Plain read of RXFxS: fill level 0..3 and message-lost flag (RFxL). */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status FDCAN_RxFifoStatus(FDCAN_GlobalTypeDef *can, uint32_t fifo, uint32_t *fill,
                              uint8_t *lost);
/* Clears IR.RFxL (W1C); RXFxS.RFxL follows. Capture it first. */
/* Caller: capture the lost flag (FDCAN_RxFifoStatus) before clearing it. */
/* Level: PARAM[F] */
SPL_Status FDCAN_ClearRxLost(FDCAN_GlobalTypeDef *can, uint32_t fifo);

/* IE / ILS / ILE full-register writes; unknown bits => SPL_ERR_PARAM. */
/* Caller: clear stale IR bits (FDCAN_ClearIrq) before enabling them. */
/* Level: PARAM[F] */
SPL_Status FDCAN_IrqEnable(FDCAN_GlobalTypeDef *can, uint32_t ie_mask);
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status FDCAN_IrqLineSelect(FDCAN_GlobalTypeDef *can, uint32_t ils_mask);
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status FDCAN_IrqLineEnable(FDCAN_GlobalTypeDef *can, uint32_t ile_mask);

/* Pure helpers. fd=0: DLC 9..15 => 8 bytes; length 0..8 only. */
/* Caller: none (pure helper, no register access). */
/* Level: PARAM[F] */
SPL_Status FDCAN_DlcToLength(uint32_t dlc, uint8_t fd, uint8_t *length);
/* Caller: none (pure helper, no register access). */
/* Level: PARAM[F,r] */
SPL_Status FDCAN_LengthToDlc(uint32_t length, uint8_t fd, uint8_t *dlc);

/* ---- Classic CAN path (Docs/FAST_PATH.md section 8) ----
 * Classic (non-FD) frame: 8 data bytes. Same state and value checks as FDCAN_TryRead /
 * FDCAN_TryWrite; the difference is 8 bytes and a single copy instead of the 64-byte FD frame and
 * a double copy.
 * RX: dlc is the raw DLC (0..15); length = min(dlc, 8) (in classic CAN DLC 9..15 = 8 bytes,
 * ISO 11898-1).
 * The struct has no padding for -Wpadded: the reserved[] fields are not used. */
typedef struct
{
    uint32_t id;          /* 11-bit (ide = 0) or 29-bit (ide = 1) */
    uint32_t timestamp;   /* RX: RXTS; TX: ignored */
    uint8_t ide;          /* 0 standard, 1 extended */
    uint8_t rtr;          /* remote frame */
    uint8_t dlc;          /* RX: raw DLC; TX: ignored (from length) */
    uint8_t length;       /* bytes: 0..8 */
    uint8_t filter_index; /* RX: FIDX or FDCAN_FILTER_INDEX_NONE; TX: ignored */
    uint8_t reserved[3];
    uint8_t data[8];
} FDCAN_ClassicFrameTypeDef;

/* Same rules as TryWrite: invalid instance, frame NULL, ide / rtr > 1, length > 8, identifier out
 * of range -> PARAM (nothing written); INIT = 1 (software or bus-off) -> STATE; queue full ->
 * WOULD_BLOCK; put index >= 3 -> ERR_HW. FDF / BRS / ESI always 0. buffer may be NULL. */
/* Caller: as FDCAN_TryWrite. */
/* Level: PARAM[F] STATE[r] HW[r] WOULD_BLOCK[r] */
SPL_Status FDCAN_TryWriteClassic(FDCAN_GlobalTypeDef *can, const FDCAN_ClassicFrameTypeDef *frame,
                                 uint32_t *buffer);
/* Same rules as TryRead: invalid instance, fifo > 1, frame NULL -> PARAM; CCE = 1 -> STATE;
 * FIFO empty -> STATE if INIT = 1, else WOULD_BLOCK; get index >= 3 -> ERR_HW.
 * FD frame (FDF = 1) -> SPL_ERR_UNSUPPORTED: the element is NOT acknowledged and frame is not
 * written (not silently truncated to 8 bytes); read it with FDCAN_TryRead. The frame is written
 * on success only. */
/* Caller: on UNSUPPORTED read the element with FDCAN_TryRead; message lost
 * as FDCAN_TryRead. */
/* Level: PARAM[F] STATE[F,r] UNSUPPORTED[r] HW[r] WOULD_BLOCK[r] */
SPL_Status FDCAN_TryReadClassic(FDCAN_GlobalTypeDef *can, uint32_t fifo,
                                FDCAN_ClassicFrameTypeDef *frame);

#endif /* SPL_G0_HAS_FDCAN */

#ifdef __cplusplus
}
#endif
#endif
