/* SPDX-License-Identifier: MIT */
/**
 * @file    stm32g4_usart.h
 * @brief   STM32G4 USART, UART and LPUART configuration and nonblocking I/O.
 *
 * Usage: the legacy path USART_Init() sets up asynchronous 8N1 and sets UE. Extended path:
 * USART_InitEx(), then with UE = 0 the optional FIFO / DE / communication mode settings, then
 * USART_Enable(). USART_Ready() when no data may move before TEACK/REACK are seen.
 *
 * Scope       : USART1..3, UART4/5 and LPUART1 as defined by the selected CMSIS device.
 *               Some APIs support a narrower set and return SPL_ERR_UNSUPPORTED.
 * Ownership   : RCC clocks, kernel clock selection, GPIO alternate functions, pins, NVIC,
 *               DMA channels and external transceivers belong to the caller.
 * Memory      : No dynamic memory, no stored user pointer, no mutable software state; the
 *               state lives in the peripheral registers.
 * Blocking    : No API waits. Try, status and request services do one step;
 *               SPL_WOULD_BLOCK or SPL_BUSY means "retry later".
 * Concurrency : Calls on the same instance are not serialized. The caller serializes task,
 *               ISR and DMA access (in particular CR1/CR3 updates and the TDR/RDR data path).
 *               Different instances are independent.
 * ISR         : Every API may be called from an ISR (no lock, no wait) | Bound: no loop;
 *               USART_InitEx uses a 64-bit division | WCET: not measured.
 * Error exit  : A rejected call writes no register and leaves the output parameters unchanged.
 * Recovery    : ERR_STATE (UE set) -> USART_Disable or USART_Abort, then configure.
 *               ERR_HW -> read the flags with USART_ErrorFlags, clear only those with
 *               USART_ClearErrorFlags. BUSY / WOULD_BLOCK -> retry later.
 *
 * Caller duties (all APIs; not done by the driver):
 *  - Clock: RCC bus clock and kernel clock source of the instance; kernel_hz passed to the
 *    init APIs must be the real kernel clock frequency (the driver only computes BRR from it).
 *  - Pins: TX / RX (CK, CTS, RTS / DE when used) in their GPIO alternate function.
 *  - Interrupts / DMA: NVIC line, priority and handler; DMA channel / DMAMUX setup and buffers.
 *  - Serialize all calls per instance (ISR and DMA included); CR1 / CR3 are
 *    read-modify-written.
 *
 * @see ../Docs/v0.11_USART_CONTRACT.md
 * @see ../Docs/v0.16_USART_MODES_CONTRACT.md
 */

#ifndef STM32G4_USART_H
#define STM32G4_USART_H

#include <stdint.h>
#include "stm32g4xx.h"
#include "stm32g4_status.h"

#ifdef __cplusplus
extern "C"
{
#endif
    /**
     * @brief Legacy asynchronous 8N1 configuration (USART_Init).
     *
     * Read during the call, not stored.
     */
    typedef struct
    {
        uint32_t kernel_hz; /**< Real kernel clock before the prescaler, Hz (after RCC mux). */
        uint32_t baudrate;  /**< Requested line rate, bit/s; > 0. */
    } USART_InitTypeDef;
    /**
     * @brief Sets up and enables the legacy asynchronous 8N1 mode.
     *
     * USART1/2/3, UART4/5 (when CMSIS defines them). CR1, CR2, CR3, PRESC and BRR are
     * rewritten: 8N1, x16 oversampling, no FIFO / parity / flow control / interrupts; the
     * receive error flags are cleared, TE, RE and UE are set. LPUART1 is not accepted
     * (different BRR formula; use USART_InitEx).
     *
     * Caller: none beyond the file-header duties (this API sets UE = 1 itself).
     *
     * @param[in] instance USART instance.
     * @param[in] cfg      Configuration; read during the call.
     * @retval SPL_OK        Configuration applied, UE set.
     * @retval SPL_ERR_PARAM Invalid instance (LPUART1 included), NULL cfg, zero clock or rate,
     *                       divider outside 16..65535.
     * @retval SPL_ERR_STATE UE already set.
     */
    /* Level: PARAM[F] STATE[F] */
    SPL_Status USART_Init(USART_TypeDef *instance, const USART_InitTypeDef *cfg);

    /**
     * @brief Writes one 8-bit value if the transmitter can accept it.
     *
     * A 9-bit data field without parity is rejected so that bit 8 is not lost. TDR is not
     * written unless OK.
     *
     * Caller: none beyond the file-header duties.
     *
     * @param[in] instance USART instance.
     * @param[in] byte     Value to send.
     * @retval SPL_OK          Value written to TDR (does not mean it has left the line).
     * @retval SPL_WOULD_BLOCK Transmitter not ready (TXE/TXFNF = 0).
     * @retval SPL_ERR_STATE   UE or TE cleared, or data field wider than 8 bits.
     * @retval SPL_ERR_PARAM   Invalid instance.
     */
    /* Level: PARAM[F] STATE[F] WOULD_BLOCK[r] */
    SPL_Status USART_TryWrite(USART_TypeDef *instance, uint8_t byte);

    /**
     * @brief Reads one 8-bit value if receive data is available.
     *
     * DESTRUCTIVE: on OK RDR is read and one element is popped from the receive FIFO. While
     * PE, FE, NE or ORE is set RDR is not read and the flags are kept; capture them with
     * USART_ErrorFlags before clearing.
     *
     * Caller: after ERR_HW capture USART_ErrorFlags, then clear (USART_ClearErrorFlags); the
     * received frame is lost.
     *
     * @param[in]  instance USART instance.
     * @param[out] byte     Value read.
     * @retval SPL_OK          RDR read, byte written.
     * @retval SPL_WOULD_BLOCK No receive data (RXNE/RXFNE = 0).
     * @retval SPL_ERR_HW      Receive error flag set; RDR not read.
     * @retval SPL_ERR_STATE   UE or RE cleared, or data field wider than 8 bits.
     * @retval SPL_ERR_PARAM   Invalid instance or NULL output.
     * @note Data only: with parity (PCE=1) or a 7-bit frame the parity bit is masked off (CCL-2
     *       YAP-09). USART_TryWrite: bits above the frame's data width are not sent.
     */
    /* Level: PARAM[F] STATE[F] HW[r] WOULD_BLOCK[r] */
    SPL_Status USART_TryRead(USART_TypeDef *instance, uint8_t *byte);

    /**
     * @brief Clears all of the PE, FE, NE and ORE receive error flags.
     *
     * When errors can arrive concurrently prefer USART_ErrorFlags + USART_ClearErrorFlags;
     * this convenience API clears the whole set present at that moment (an error arriving
     * after the snapshot is lost as well).
     *
     * Caller: capture USART_ErrorFlags first; an error arriving after the capture is lost
     * (use USART_ClearErrorFlags to keep it).
     *
     * @param[in] instance USART instance.
     * @retval SPL_OK        ICR written and ISR read back.
     * @retval SPL_ERR_PARAM Invalid instance.
     */
    /* Level: PARAM[F] */
    SPL_Status USART_ClearErrors(USART_TypeDef *instance);

    /**
     * @brief Gives a snapshot of the transmitter and receiver enable acknowledge; does not wait.
     *
     * Caller: poll (bounded) until the needed output is 1 before relying on TE / RE.
     *
     * @param[in]  instance USART instance.
     * @param[out] tx_ready 1 if TE is set and TEACK = 1, else 0.
     * @param[out] rx_ready 1 if RE is set and REACK = 1, else 0.
     * @retval SPL_OK        Both outputs written.
     * @retval SPL_ERR_STATE UE cleared.
     * @retval SPL_ERR_PARAM Invalid instance or NULL output.
     */
    /* Level: PARAM[F] STATE[F] */
    SPL_Status USART_Ready(USART_TypeDef *instance, uint8_t *tx_ready, uint8_t *rx_ready);

    /**
     * @brief Gives a snapshot of the raw PE, FE, NE and ORE error flags.
     *
     * Caller: none beyond the file-header duties.
     *
     * @param[in]  instance USART instance.
     * @param[out] errors   ISR & USART_ERRORS_ALL.
     * @retval SPL_OK        Snapshot written.
     * @retval SPL_ERR_PARAM Invalid instance or NULL output.
     */
    /* Level: PARAM[F] */
    SPL_Status USART_ErrorFlags(USART_TypeDef *instance, uint32_t *errors);

    /**
     * @brief Reports whether the last frame has completely left the line.
     *
     * Caller: TC stays set until USART_ClearTxComplete; clear it after use so the next check
     * sees only the new transmission.
     *
     * @param[in]  instance USART instance.
     * @param[out] complete 1 if TC = 1, else 0.
     * @retval SPL_OK        Status written.
     * @retval SPL_ERR_PARAM Invalid instance or NULL output.
     */
    /* Level: PARAM[F] */
    SPL_Status USART_TxComplete(USART_TypeDef *instance, uint8_t *complete);

/* ---- v0.11: extended configuration, LPUART1, FIFO, RS485, IRQ/DMA ---- */
/* @see ../Docs/v0.11_USART_CONTRACT.md. The configuration APIs below touch "UE = 0 only"
 * fields (RM0440 40.8 / 41.7); with UE = 1 they return SPL_ERR_STATE.
 * ERRATA ES0431 2.12.2 / ES0430 2.16.2 / ES0523 2.12.2: a glitch in the second half of the
 * stop bit (shorter than half a bit) can corrupt RX data; keep the RX line clean or filter it
 * (board duty).
 * ES0431 2.12.4 / ES0430 2.16.4 / ES0523 2.12.4: not applicable (ONEBIT is not in this API).
 * For 2.12.1 (synchronous slave) and 2.12.3 (ABREN) see USART_SyncConfig and
 * USART_AutoBaudConfig. */

/* USART_InitExTypeDef.word_length: data bits including the parity bit. */
#define USART_WORDLENGTH_7B 7U
#define USART_WORDLENGTH_8B 8U
#define USART_WORDLENGTH_9B 9U
/* USART_InitExTypeDef.parity */
#define USART_PARITY_NONE 0U
#define USART_PARITY_EVEN 1U
#define USART_PARITY_ODD 2U
/* USART_InitExTypeDef.stop_bits (value = CR2.STOP encoding). 0.5 and 1.5
 * are smartcard-oriented (RM0440 40.3/40.5.5): accepted on USART1/2/3 only;
 * UART4/5 and LPUART1 => SPL_ERR_UNSUPPORTED. */
#define USART_STOPBITS_1 0U
#define USART_STOPBITS_0_5 1U
#define USART_STOPBITS_2 2U
#define USART_STOPBITS_1_5 3U
/* USART_InitExTypeDef.oversampling. LPUART1 has no OVER8 => x8 UNSUPPORTED. */
#define USART_OVERSAMPLING_16 0U
#define USART_OVERSAMPLING_8 1U
/* USART_InitExTypeDef.prescaler: PRESC.PRESCALER code (RM0440 40.8.14). */
#define USART_PRESC_DIV1 0U
#define USART_PRESC_DIV2 1U
#define USART_PRESC_DIV4 2U
#define USART_PRESC_DIV6 3U
#define USART_PRESC_DIV8 4U
#define USART_PRESC_DIV10 5U
#define USART_PRESC_DIV12 6U
#define USART_PRESC_DIV16 7U
#define USART_PRESC_DIV32 8U
#define USART_PRESC_DIV64 9U
#define USART_PRESC_DIV128 10U
#define USART_PRESC_DIV256 11U
/* USART_InitExTypeDef.direction (bit mask, at least one bit). */
#define USART_DIR_TX 0x01U
#define USART_DIR_RX 0x02U
/* USART_InitExTypeDef.flow_control */
#define USART_FLOW_NONE 0U
#define USART_FLOW_RTS 1U
#define USART_FLOW_CTS 2U
#define USART_FLOW_RTS_CTS 3U
/* USART_InitExTypeDef.options (bit mask) */
#define USART_OPT_MSBFIRST 0x01U    /* CR2.MSBFIRST */
#define USART_OPT_SWAP 0x02U        /* CR2.SWAP: TX/RX pin functions swapped */
#define USART_OPT_TXINV 0x04U       /* CR2.TXINV: TX pin level inverted */
#define USART_OPT_RXINV 0x08U       /* CR2.RXINV: RX pin level inverted */
#define USART_OPT_DATAINV 0x10U     /* CR2.DATAINV: data (and parity) inverted */
#define USART_OPT_HALF_DUPLEX 0x20U /* CR3.HDSEL: single wire on TX pin */
/* Caller duty (CCL-2 UART-04): in half-duplex (HDSEL) or RS485 use the library does not
 * filter received data; discard own echoed bytes (or keep the transceiver receiver off while
 * transmitting), and release a GPIO direction pin only after USART_TxComplete reports 1. */

    /**
     * @brief Extended asynchronous frame configuration (USART_InitEx).
     *
     * Copied during USART_InitEx, its address is not stored. 16 bytes without padding on the
     * supported targets.
     */
    typedef struct
    {
        uint32_t kernel_hz;   /* usart_ker_ck / lpuart_ker_ck before PRESC, Hz */
        uint32_t baudrate;    /* bit/s, > 0 */
        uint8_t word_length;  /* USART_WORDLENGTH_* */
        uint8_t parity;       /* USART_PARITY_* */
        uint8_t stop_bits;    /* USART_STOPBITS_* */
        uint8_t oversampling; /* USART_OVERSAMPLING_* */
        uint8_t prescaler;    /* USART_PRESC_* */
        uint8_t direction;    /* USART_DIR_* mask */
        uint8_t flow_control; /* USART_FLOW_* */
        uint8_t options;      /* USART_OPT_* mask */
    } USART_InitExTypeDef;

    /**
     * @brief Sets up the extended asynchronous frame and leaves the peripheral disabled.
     *
     * UE must be 0. CR1 (M, PCE, PS, OVER8, TE, RE; the rest, FIFOEN, DEAT/DEDT and interrupt
     * enables included, 0), CR2 (STOP + options; the rest 0), CR3 (RTSE/CTSE/HDSEL; the rest
     * 0), PRESC and BRR are written; mode settings made earlier are erased. UE stays 0: if
     * needed USART_FifoConfig / USART_DriverEnableConfig / mode settings, then USART_Enable.
     * LPUART1 is accepted (BRR = round(256*f/baud), 0x300..0xFFFFF).
     * Bound: the BRR computation uses a 64-bit division (a library call on Cortex-M4).
     *
     * ERRATA ES0431 2.13.1 / ES0430 2.17.1 / ES0523 2.13.1: on LPUART1, BRR 0x301..0x3FF with
     * BRR & 0xFF != 0 (kernel/rate ratio between 3 and 4 and not an integer, e.g.
     * 32768 Hz / 9600 Bd) is rejected; raise the kernel clock or lower the rate.
     *
     * Caller: none beyond the file-header duties (kernel_hz is the kernel clock before PRESC).
     *
     * @param[in] instance USART, UART or LPUART instance.
     * @param[in] cfg      Configuration; read during the call.
     * @retval SPL_OK              Configuration applied; UE cleared.
     * @retval SPL_ERR_PARAM       Invalid argument or unreachable rate (BRR range,
     *                             errata 2.13.1).
     * @retval SPL_ERR_UNSUPPORTED Feature not on the instance (0.5/1.5 stop bits, LPUART x8).
     * @retval SPL_ERR_STATE       UE set.
     */
    /* Level: PARAM[F,r] STATE[F] UNSUPPORTED[r] */
    SPL_Status USART_InitEx(USART_TypeDef *instance, const USART_InitExTypeDef *cfg);

    /**
     * @brief Enables the configured peripheral (CR1.UE = 1).
     *
     * An instance that is already enabled returns OK without a write. BRR must meet the
     * instance minimum: USART_InitEx first; USART_Ready afterwards if TEACK/REACK
     * acknowledge is needed.
     *
     * Caller: poll USART_Ready for TEACK / REACK before the first transfer.
     *
     * @param[in] instance USART instance.
     * @retval SPL_OK        UE set, or already set.
     * @retval SPL_ERR_STATE BRR is not a valid initialized value (USART < 0x10,
     *                       LPUART < 0x300); fix with USART_InitEx.
     * @retval SPL_ERR_PARAM Invalid instance.
     */
    /* Level: PARAM[F] STATE[r] */
    SPL_Status USART_Enable(USART_TypeDef *instance);

    /**
     * @brief Disables the peripheral after the pending transmission has finished.
     *
     * With UE=1, TE=1 and TC=0 returns SPL_BUSY without touching UE (RM0440 40.5.5 step 8).
     * Clearing UE also resets the DMA requests; disable the DMA channel before this call.
     *
     * Caller: disable the DMA channel first; bound the BUSY poll (a transmitter held by CTS
     * never completes: USART_Abort).
     *
     * @param[in] instance USART instance.
     * @retval SPL_OK        UE cleared, or already cleared.
     * @retval SPL_BUSY      The last frame has not completely left the line; retry later
     *                       (USART_Abort if CTS holds the transmitter).
     * @retval SPL_ERR_PARAM Invalid instance.
     */
    /* Level: PARAM[F] BUSY[r] */
    SPL_Status USART_Disable(USART_TypeDef *instance);

    /**
     * @brief Clears UE at once.
     *
     * DESTRUCTIVE: the frame in progress is dropped; sent or received data can be lost. TC is
     * not awaited. Meant for forced recovery (transmitter held by CTS included).
     *
     * Caller: disable the DMA channel first (UE = 0 resets the DMA requests).
     *
     * @param[in] instance USART instance.
     * @retval SPL_OK        UE cleared.
     * @retval SPL_ERR_PARAM Invalid instance.
     */
    /* Level: PARAM[F] */
    SPL_Status USART_Abort(USART_TypeDef *instance);

/* FIFO thresholds (CR3 TXFTCFG/RXFTCFG). EMPTY_FULL = TXFIFO empty / RXFIFO full. */
#define USART_FIFO_THRESHOLD_1_8 0U
#define USART_FIFO_THRESHOLD_1_4 1U
#define USART_FIFO_THRESHOLD_1_2 2U
#define USART_FIFO_THRESHOLD_3_4 3U
#define USART_FIFO_THRESHOLD_7_8 4U
#define USART_FIFO_THRESHOLD_EMPTY_FULL 5U
    /**
     * @brief Sets the FIFO mode and the transmit/receive thresholds.
     *
     * UE must be 0. The three values are applied as one configuration.
     *
     * Caller: none beyond the file-header duties.
     *
     * @param[in] instance     USART instance.
     * @param[in] enable       1: FIFOEN = 1, 0: disabled.
     * @param[in] tx_threshold USART_FIFO_THRESHOLD_*.
     * @param[in] rx_threshold USART_FIFO_THRESHOLD_*.
     * @retval SPL_OK        FIFO configuration applied.
     * @retval SPL_ERR_STATE UE set.
     * @retval SPL_ERR_PARAM Invalid instance, enable or threshold.
     */
    /* Level: PARAM[F] STATE[F] */
    SPL_Status USART_FifoConfig(USART_TypeDef *instance, uint8_t enable, uint8_t tx_threshold,
                                uint8_t rx_threshold);

    /**
     * @brief Sets the RS485 driver enable signal on the RTS/DE pin.
     *
     * UE must be 0. Enabling DE while RTS flow control uses the same pin is rejected.
     *
     * Caller: RTS / DE pin in its alternate function; active_low and the DEAT/DEDT times for
     * the transceiver (datasheet of the board part).
     *
     * @param[in] instance      USART instance.
     * @param[in] enable        1: CR3.DEM = 1.
     * @param[in] active_low    1: CR3.DEP = 1 (DE active low).
     * @param[in] assert_time   DEAT, 0..31 sample times (bit/16 at x16, bit/8 at x8).
     * @param[in] deassert_time DEDT, 0..31, same unit.
     * @retval SPL_OK        DE configuration applied.
     * @retval SPL_ERR_STATE UE set, or RTS flow control conflicts with DE.
     * @retval SPL_ERR_PARAM Invalid instance, boolean value or time.
     */
    /* Level: PARAM[F] STATE[F] */
    SPL_Status USART_DriverEnableConfig(USART_TypeDef *instance, uint8_t enable, uint8_t active_low,
                                        uint8_t assert_time, uint8_t deassert_time);

    /**
     * @brief Writes one value of up to 9 bits through the wide TDR path if the transmitter is
     * ready.
     *
     * TDR is not written unless OK.
     *
     * Caller: none beyond the file-header duties.
     *
     * @param[in] instance USART instance.
     * @param[in] data     Must fit the configured data field (9/8/7 bits, one bit less with
     *                     parity).
     * @retval SPL_OK          Value written.
     * @retval SPL_WOULD_BLOCK Transmitter not ready.
     * @retval SPL_ERR_STATE   UE or TE cleared.
     * @retval SPL_ERR_PARAM   Invalid instance or data wider than the data field.
     */
    /* Level: PARAM[F] STATE[F] WOULD_BLOCK[r] */
    SPL_Status USART_TryWrite9(USART_TypeDef *instance, uint16_t data);

    /**
     * @brief Reads one raw value of up to 9 bits through the wide RDR path if data is
     * available.
     *
     * DESTRUCTIVE: on OK RDR is read and one element is popped from the receive FIFO. On a
     * receive error RDR is not read.
     *
     * Caller: after ERR_HW capture USART_ErrorFlags, then clear (USART_ClearErrorFlags); the
     * received frame is lost.
     *
     * @param[in]  instance USART instance.
     * @param[out] data     RDR[8:0]; the parity bit included when parity is enabled.
     * @retval SPL_OK          Raw value read.
     * @retval SPL_WOULD_BLOCK No receive data.
     * @retval SPL_ERR_HW      PE, FE, NE or ORE set; RDR not read.
     * @retval SPL_ERR_STATE   UE or RE cleared.
     * @retval SPL_ERR_PARAM   Invalid instance or NULL output.
     */
    /* Level: PARAM[F] STATE[F] HW[r] WOULD_BLOCK[r] */
    SPL_Status USART_TryRead9(USART_TypeDef *instance, uint16_t *data);

/* Interrupt sources for USART_ITConfig. */
#define USART_IT_RXNE 0x01U /* CR1.RXNEIE/RXFNEIE (also ORE) */
#define USART_IT_TXE 0x02U  /* CR1.TXEIE/TXFNFIE */
#define USART_IT_TC 0x04U   /* CR1.TCIE */
#define USART_IT_IDLE 0x08U /* CR1.IDLEIE */
#define USART_IT_PE 0x10U   /* CR1.PEIE */
#define USART_IT_ERR 0x20U  /* CR3.EIE (FE/ORE/NE) */
#define USART_IT_ALL 0x3FU
    /**
     * @brief Enables or disables the selected USART interrupt sources.
     *
     * Only the selected CR1/CR3 enable bits change, the others are kept. Allowed with UE set.
     * Read-modify-write: serialize with the other CR1/CR3 writers of the same instance.
     *
     * Caller: clear stale flags (errors, IDLE, TC) before enabling their interrupt; the
     * handler clears ORE too (USART_IT_RXNE also enables the ORE interrupt), else the
     * interrupt repeats.
     *
     * @param[in] instance USART instance.
     * @param[in] it_mask  USART_IT_* mask, not empty.
     * @param[in] enable   1: enable, 0: disable.
     * @retval SPL_OK        Selected enables updated.
     * @retval SPL_ERR_PARAM Invalid instance, mask or enable.
     */
    /* Level: PARAM[F] */
    SPL_Status USART_ITConfig(USART_TypeDef *instance, uint32_t it_mask, uint8_t enable);

/* DMA request flags for USART_DmaConfig. */
#define USART_DMA_TX 0x01U   /* CR3.DMAT */
#define USART_DMA_RX 0x02U   /* CR3.DMAR */
#define USART_DMA_DDRE 0x04U /* CR3.DDRE: DMA disabled on reception error */
    /**
     * @brief Sets the transmit, receive and receive-error DMA request policy.
     *
     * DMAT and DMAR may change with UE set; a DDRE change with UE set is rejected.
     *
     * Caller: DMA buffers stay valid until the DMA channel is disabled; disable the channel
     * before USART_Disable / USART_Abort.
     *
     * @param[in] instance  USART instance.
     * @param[in] dma_flags The complete wanted USART_DMA_* set (missing bits are cleared).
     * @retval SPL_OK        DMA request bits updated.
     * @retval SPL_ERR_STATE DDRE would change with UE set.
     * @retval SPL_ERR_PARAM Invalid instance or mask.
     */
    /* Level: PARAM[F] STATE[r] */
    SPL_Status USART_DmaConfig(USART_TypeDef *instance, uint32_t dma_flags);

    /**
     * @brief Gives a snapshot of the IDLE line detection flag.
     *
     * Caller: none beyond the file-header duties.
     *
     * @param[in]  instance USART instance.
     * @param[out] idle     1 if ISR.IDLE = 1, else 0.
     * @retval SPL_OK        Status written.
     * @retval SPL_ERR_PARAM Invalid instance or NULL output.
     */
    /* Level: PARAM[F] */
    SPL_Status USART_IdleDetected(USART_TypeDef *instance, uint8_t *idle);

    /**
     * @brief Clears the IDLE flag with a direct W1C write.
     *
     * Only ICR.IDLECF is written; ISR is read back before return.
     *
     * Caller: none beyond the file-header duties.
     *
     * @param[in] instance USART instance.
     * @retval SPL_OK        Clear written and read back.
     * @retval SPL_ERR_PARAM Invalid instance.
     */
    /* Level: PARAM[F] */
    SPL_Status USART_ClearIdle(USART_TypeDef *instance);

    /**
     * @brief Clears the transmission complete (TC) flag with a direct W1C write.
     *
     * Only ICR.TCCF is written; ISR is read back before return.
     *
     * Caller: none beyond the file-header duties.
     *
     * @param[in] instance USART instance.
     * @retval SPL_OK        Clear written and read back.
     * @retval SPL_ERR_PARAM Invalid instance.
     */
    /* Level: PARAM[F] */
    SPL_Status USART_ClearTxComplete(USART_TypeDef *instance);
/* Error flags (ISR bit positions) reported by USART_ErrorFlags. */
#define USART_ERRORS_ALL 0x0FU /* PE | FE | NE | ORE */
    /**
     * @brief Clears a captured subset of the receive error flags.
     *
     * A direct ICR write clears only the requested flags; an error arriving after the
     * snapshot is not lost.
     *
     * Caller: none beyond the file-header duties.
     *
     * @param[in] instance USART instance.
     * @param[in] errors   Non-empty subset of USART_ERRORS_ALL; normally the value given by
     *                     USART_ErrorFlags.
     * @retval SPL_OK        Selected flags cleared, ISR read back.
     * @retval SPL_ERR_PARAM Invalid instance, empty mask or unsupported bit.
     */
    /* Level: PARAM[F] */
    SPL_Status USART_ClearErrorFlags(USART_TypeDef *instance, uint32_t errors);

    /* ---- v0.16 (G4): communication modes; @see ../Docs/v0.16_USART_MODES_CONTRACT.md ----
     * Call after USART_InitEx (which rewrites CR1..CR3) and before USART_Enable: every mode setting
     * needs UE = 0 (else SPL_ERR_STATE). Instance support follows RM0440 Table 370: synchronous and
     * smartcard on USART1..3; IrDA, LIN, auto baud and receiver timeout on USART1..3/UART4/5;
     * wake-up and multiprocessor (mute) on all instances. Unsupported instance/feature combinations
     * return SPL_ERR_UNSUPPORTED. Mutually exclusive modes (RM0440 40.5.13 .. 40.5.18 "must be kept
     * cleared" lists) are checked against the current CR1..CR3: a conflict -> SPL_ERR_STATE. */

    typedef struct
    {
        uint8_t clock_out;      /* CLKEN: synchronous master, CK output */
        uint8_t cpol;           /* CPOL */
        uint8_t cpha;           /* CPHA */
        uint8_t last_bit_clock; /* LBCL: clock pulse for the last data bit */
        uint8_t slave;          /* SLVEN: synchronous slave (CK input); not with clock_out */
        uint8_t ignore_nss;     /* DIS_NSS: slave select pin ignored */
    } USART_SyncTypeDef;
    /**
     * @brief Sets synchronous clock operation while UE = 0.
     *
     * All mode fields 0 selects asynchronous operation. USART1..3 only. Synchronous operation
     * conflicts with LIN, smartcard, half-duplex and IrDA. Fields are 0/1.
     *
     * ERRATA ES0431 2.12.1 / ES0430 2.16.1 / ES0523 2.12.1: in synchronous slave mode at low
     * rate TC can be set before the last bit is shifted out; after TC wait until the CK line
     * has been idle for longer than half a clock period before disabling the USART.
     *
     * Caller: CK pin in its alternate function (NSS pin too when ignore_nss = 0); synchronous
     * slave: after TC wait for an idle CK line longer than half a clock period before
     * disabling (errata 2.12.1 above).
     *
     * @param[in] instance USART instance.
     * @param[in] cfg      Synchronous settings; read during the call.
     * @retval SPL_OK              Synchronous fields updated.
     * @retval SPL_ERR_PARAM       Invalid instance, NULL cfg, field greater than 1, or
     *                             clock_out together with slave.
     * @retval SPL_ERR_STATE       UE set or another mode conflicts.
     * @retval SPL_ERR_UNSUPPORTED No synchronous mode on the instance.
     */
    /* Level: PARAM[F] STATE[F] UNSUPPORTED[F] */
    SPL_Status USART_SyncConfig(USART_TypeDef *instance, const USART_SyncTypeDef *cfg);

    typedef struct
    {
        uint8_t enable;     /* SCEN */
        uint8_t guard_time; /* GT: guard time in baud clocks */
        uint8_t prescaler;  /* PSC 1..31: smartcard clock = kernel clock / (2 x PSC) */
        uint8_t nack;       /* NACK on parity error */
        uint8_t retries;    /* SCARCNT 0..7 */
        uint8_t clock_out;  /* CLKEN: clock to the card */
    } USART_SmartcardTypeDef;
    /**
     * @brief Sets the ISO 7816 smartcard mode while UE = 0.
     *
     * USART1/2/3 only. Enabling needs LIN, half-duplex and IrDA disabled; 8 data bits +
     * parity, x16 oversampling and 0.5 or 1.5 stop bits. Disabling clears SCEN, NACK and the
     * retry count; GTPR and CLKEN are kept.
     *
     * Caller: TX (card I/O) pin in alternate function open drain and CK pin when
     * clock_out = 1; the card protocol (ATR, retry policy) is the caller's.
     *
     * @param[in] instance USART instance.
     * @param[in] cfg      Smartcard settings; read during the call.
     * @retval SPL_OK              Smartcard settings updated.
     * @retval SPL_ERR_PARAM       Invalid instance, NULL cfg or field value (prescaler 0
     *                             while enabling included).
     * @retval SPL_ERR_STATE       UE set or frame/mode preconditions not met.
     * @retval SPL_ERR_UNSUPPORTED No smartcard mode on the instance.
     */
    /* Level: PARAM[F] STATE[F] UNSUPPORTED[F] */
    SPL_Status USART_SmartcardConfig(USART_TypeDef *instance, const USART_SmartcardTypeDef *cfg);

    /**
     * @brief Sets the IrDA normal or low-power mode while UE = 0.
     *
     * USART/UART instances only. GTPR.GT is kept. Enabling needs one stop bit, x16
     * oversampling; LIN, synchronous, smartcard and half-duplex disabled.
     *
     * Caller: low_power: prescaler chosen for the IrDA low-power frequency from the kernel
     * clock; IR transceiver on the board.
     *
     * @param[in] instance  USART instance.
     * @param[in] enable    1: IREN = 1.
     * @param[in] low_power 1: IRLP = 1.
     * @param[in] prescaler GTPR.PSC, 1..255 while enabling; not used when disabling.
     * @retval SPL_OK              IrDA settings updated.
     * @retval SPL_ERR_PARAM       Invalid argument or prescaler 0 while enabling.
     * @retval SPL_ERR_STATE       UE set or another mode / frame setting conflicts.
     * @retval SPL_ERR_UNSUPPORTED No IrDA mode on the instance.
     */
    /* Level: PARAM[F] STATE[F] UNSUPPORTED[F] */
    SPL_Status USART_IrdaConfig(USART_TypeDef *instance, uint8_t enable, uint8_t low_power,
                                uint8_t prescaler);

    /**
     * @brief Sets the LIN mode and break detection while UE = 0.
     *
     * USART/UART instances only. Enabling needs one stop bit, 8-bit x16 operation;
     * synchronous, smartcard, half-duplex and IrDA disabled.
     *
     * Caller: break_irq = 1: the handler clears LBDF with USART_ClearModeFlags.
     *
     * @param[in] instance    USART instance.
     * @param[in] enable      1: LINEN = 1.
     * @param[in] break_11bit 1: 11-bit, 0: 10-bit break detection (LBDL).
     * @param[in] break_irq   1: LBDIE = 1.
     * @retval SPL_OK              LIN settings updated.
     * @retval SPL_ERR_PARAM       Invalid argument.
     * @retval SPL_ERR_STATE       UE set or another mode / frame setting conflicts.
     * @retval SPL_ERR_UNSUPPORTED No LIN mode on the instance.
     */
    /* Level: PARAM[F] STATE[F] UNSUPPORTED[F] */
    SPL_Status USART_LinConfig(USART_TypeDef *instance, uint8_t enable, uint8_t break_11bit,
                               uint8_t break_irq);

    /**
     * @brief Requests transmission of a break character.
     *
     * In LIN mode the break is 13 bits. UE and TE must be set. Writes one request, does not
     * wait for completion.
     *
     * Caller: none beyond the file-header duties.
     *
     * @param[in] instance USART instance.
     * @retval SPL_OK        RQR.SBKRQ written.
     * @retval SPL_BUSY      The previous break request is still pending (SBKF = 1).
     * @retval SPL_ERR_STATE UE or TE cleared.
     * @retval SPL_ERR_PARAM Invalid instance.
     */
    /* Level: PARAM[F] STATE[F] BUSY[r] */
    SPL_Status USART_SendBreak(USART_TypeDef *instance);

    /**
     * @brief Sets automatic baud rate detection while UE = 0.
     *
     * USART/UART instances only.
     *
     * ERRATA ES0431 2.12.3 / ES0430 2.16.3 / ES0523 2.12.3: clearing ABREN during reception
     * corrupts the data; here ABREN changes only while UE = 0 (else STATE), so it is never
     * cleared during reception.
     *
     * Caller: none beyond the file-header duties.
     *
     * @param[in] instance USART instance.
     * @param[in] enable   1: ABREN = 1.
     * @param[in] mode     ABRMODE: 0 start bit, 1 falling edge, 2 0x7F frame,
     *                     3 0x55 frame.
     * @retval SPL_OK              Auto baud settings updated.
     * @retval SPL_ERR_PARAM       Invalid instance, enable or mode.
     * @retval SPL_ERR_STATE       UE set.
     * @retval SPL_ERR_UNSUPPORTED No automatic baud rate detection on the instance.
     */
    /* Level: PARAM[F] STATE[F] UNSUPPORTED[F] */
    SPL_Status USART_AutoBaudConfig(USART_TypeDef *instance, uint8_t enable, uint8_t mode);

    /**
     * @brief Restarts an enabled automatic baud rate detection cycle.
     *
     * Caller: none beyond the file-header duties.
     *
     * @param[in] instance USART instance.
     * @retval SPL_OK              RQR.ABRRQ written.
     * @retval SPL_ERR_STATE       Automatic baud rate detection disabled (USART_AutoBaudConfig).
     * @retval SPL_ERR_UNSUPPORTED No automatic baud rate detection on the instance.
     * @retval SPL_ERR_PARAM       Invalid instance.
     */
    /* Level: PARAM[F] STATE[F] UNSUPPORTED[F] */
    SPL_Status USART_AutoBaudRequest(USART_TypeDef *instance);

    /**
     * @brief Gives a snapshot of the automatic baud rate detection completion and error.
     *
     * Caller: none beyond the file-header duties.
     *
     * @param[in]  instance USART instance.
     * @param[out] done     ABRF (0/1).
     * @param[out] error    ABRE (0/1).
     * @retval SPL_OK              Both outputs written.
     * @retval SPL_ERR_UNSUPPORTED No automatic baud rate detection on the instance.
     * @retval SPL_ERR_PARAM       Invalid instance or NULL output.
     */
    /* Level: PARAM[F] UNSUPPORTED[F] */
    SPL_Status USART_AutoBaudStatus(USART_TypeDef *instance, uint8_t *done, uint8_t *error);

    /**
     * @brief Sets wake-up from Stop mode while UE = 0.
     *
     * Present on every USART/UART/LPUART instance of the G4.
     *
     * Caller: kernel clock that keeps running in Stop (HSI16 or LSE source, RCC); EXTI / NVIC
     * setup for the wake-up and clearing WUF with USART_ClearModeFlags.
     *
     * @param[in] instance USART instance.
     * @param[in] enable   1: UESM = 1.
     * @param[in] source   WUS: 0 address match, 2 start bit, 3 RXNE; 1 reserved.
     * @param[in] irq      1: WUFIE = 1.
     * @retval SPL_OK              Wake-up settings updated.
     * @retval SPL_ERR_PARAM       Invalid instance, boolean value or source.
     * @retval SPL_ERR_STATE       UE set.
     * @retval SPL_ERR_UNSUPPORTED No wake-up from Stop on the instance.
     */
    /* Level: PARAM[F] STATE[F] UNSUPPORTED[F] */
    SPL_Status USART_WakeupConfig(USART_TypeDef *instance, uint8_t enable, uint8_t source,
                                  uint8_t irq);

    /**
     * @brief Sets the receiver timeout and the smartcard block length while UE = 0.
     *
     * USART/UART instances only. RTOF/EOBF are watched with USART_ModeFlags.
     *
     * Caller: clear RTOF / EOBF with USART_ClearModeFlags after each event.
     *
     * @param[in] instance     USART instance.
     * @param[in] enable       1: RTOEN = 1.
     * @param[in] timeout      In bit times, limited to 24 bits.
     * @param[in] block_length RTOR.BLEN.
     * @retval SPL_OK              Timeout settings updated.
     * @retval SPL_ERR_PARAM       Invalid instance, enable or timeout.
     * @retval SPL_ERR_STATE       UE set.
     * @retval SPL_ERR_UNSUPPORTED No receiver timeout on the instance (LPUART1).
     */
    /* Level: PARAM[F] STATE[F] UNSUPPORTED[F] */
    SPL_Status USART_RxTimeoutConfig(USART_TypeDef *instance, uint8_t enable, uint32_t timeout,
                                     uint8_t block_length);

    /**
     * @brief Sets the multiprocessor mute mode and address detection while UE = 0.
     *
     * Caller: none beyond the file-header duties.
     *
     * @param[in] instance     USART instance.
     * @param[in] enable       1: MME = 1.
     * @param[in] address_mark 0: idle line, 1: wake-up on address mark (WAKE).
     * @param[in] address      CR2.ADD; <= 15 in 4-bit mode.
     * @param[in] addr7        1: 7-bit address comparison (ADDM7), 0: 4-bit.
     * @retval SPL_OK        Mute mode and address settings updated.
     * @retval SPL_ERR_PARAM Invalid instance, boolean value or address.
     * @retval SPL_ERR_STATE UE set.
     */
    /* Level: PARAM[F] STATE[F] */
    SPL_Status USART_MuteConfig(USART_TypeDef *instance, uint8_t enable, uint8_t address_mark,
                                uint8_t address, uint8_t addr7);

    /**
     * @brief Requests entry into mute mode.
     *
     * Mute mode must have been enabled with USART_MuteConfig. RWU is watched with
     * USART_ModeFlags.
     *
     * Caller: none beyond the file-header duties.
     *
     * @param[in] instance USART instance.
     * @retval SPL_OK        RQR.MMRQ written.
     * @retval SPL_ERR_STATE Multiprocessor mute mode disabled.
     * @retval SPL_ERR_PARAM Invalid instance.
     */
    /* Level: PARAM[F] STATE[F] */
    SPL_Status USART_EnterMute(USART_TypeDef *instance);

/* ISR flags of the modes above, and the subset with a same-position ICR clear bit. */
#define USART_MODE_FLAGS_CLEARABLE                                                                 \
    (USART_ISR_LBDF | USART_ISR_CMF | USART_ISR_WUF | USART_ISR_RTOF | USART_ISR_EOBF |            \
     USART_ISR_UDR)
#define USART_MODE_FLAGS                                                                           \
    (USART_MODE_FLAGS_CLEARABLE | USART_ISR_ABRE | USART_ISR_ABRF | USART_ISR_SBKF |               \
     USART_ISR_RWU | USART_ISR_TCBGT)
    /**
     * @brief Gives a snapshot of the status flags of the extended communication modes.
     *
     * Caller: none beyond the file-header duties.
     *
     * @param[in]  instance USART instance.
     * @param[out] flags    ISR & USART_MODE_FLAGS.
     * @retval SPL_OK        Snapshot written.
     * @retval SPL_ERR_PARAM Invalid instance or NULL output.
     */
    /* Level: PARAM[F] */
    SPL_Status USART_ModeFlags(USART_TypeDef *instance, uint32_t *flags);

    /**
     * @brief Clears the selected extended mode flags with a direct W1C write.
     *
     * ICR is written once, ISR is read back before return. LBDF/RTOF/EOBF exist on USART/UART,
     * UDR on USART1..3 only, CMF/WUF on every instance (RM0440 Table 370).
     *
     * Caller: none beyond the file-header duties.
     *
     * @param[in] instance USART instance.
     * @param[in] flags    Non-empty subset of USART_MODE_FLAGS_CLEARABLE present on the
     *                     instance.
     * @retval SPL_OK              Selected flags cleared.
     * @retval SPL_ERR_PARAM       Invalid instance, empty mask or non-clearable bit.
     * @retval SPL_ERR_UNSUPPORTED Requested flag not on the instance.
     */
    /* Level: PARAM[F] UNSUPPORTED[F] */
    SPL_Status USART_ClearModeFlags(USART_TypeDef *instance, uint32_t flags);

#ifdef __cplusplus
}
#endif
#endif
