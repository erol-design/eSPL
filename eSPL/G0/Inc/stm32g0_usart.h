#ifndef STM32G0_USART_H
#define STM32G0_USART_H
#include <stdint.h>
#include "stm32g0xx.h"
#include "stm32g0_status.h"

#ifdef __cplusplus
extern "C"
{
#endif
/* USART / LPUART driver for STM32G0 (RM0444 Rev 6 sections 33 and 34), ported from the STM32G4
 * SPL v0.13.3.
 * Scope: G0B1 instance classes (RM0444 Table 183/184):
 *  - FULL: USART1, USART2, USART3 (FIFO, prescaler, smartcard, ...);
 *  - BASIC: USART4, USART5, USART6: no FIFO, no prescaler (PRESC register absent), no
 *    smartcard/IrDA/LIN/auto baud/receiver timeout/wake-up; kernel clock = PCLK;
 *  - LP: LPUART1, LPUART2 (BRR = 256 * f / baud, no OVER8, FIFO and prescaler present).
 * Usage: USART_InitEx (UE = 0), optional USART_FifoConfig / USART_DriverEnableConfig and mode
 * settings, USART_Enable, poll USART_Ready, then the Try* transfers. USART_Init is the legacy
 * shortcut (sets UE = 1 itself). Kernel clock: RCC_GetUsartKernelClock gives the value to pass as
 * kernel_hz.
 * Common rules: no dynamic memory, no static state; no function waits (BUSY / WOULD_BLOCK are
 * returned instead); not reentrant per instance (see the serialization duty below); output
 * parameters are unchanged unless SPL_OK (METHOD 1.8).
 * Caller duties (all APIs; not done by the driver):
 *  - Clock: RCC bus clock and kernel clock source of the instance; kernel_hz passed to the init
 *    APIs must be the real kernel clock frequency (the driver only computes BRR from it).
 *  - Pins: TX / RX (CK, CTS, RTS / DE when used) in their GPIO alternate function.
 *  - Interrupts / DMA: NVIC line, priority and handler; DMA channel / DMAMUX setup and buffers.
 *  - Serialize all calls per instance (ISR included); CR1 / CR3 are read-modify-written.
 * Contract: Docs/v0.3_USART_CONTRACT.md; communication modes: Docs/v0.10_COMM_MODES.md. */
typedef struct
{
    uint32_t kernel_hz;
    uint32_t baudrate;
} USART_InitTypeDef;
/* Legacy asynchronous 8N1 x16 for USART1..6 (no FIFO, parity, flow control or interrupts).
 * LPUART1/2 -> SPL_ERR_PARAM (BRR formula differs): use USART_InitEx. Not concurrency-safe; only
 * initialize with the peripheral disabled. */
/* Caller: none beyond the file-header duties (kernel_hz from RCC_GetUsartKernelClock; this API
 * sets UE = 1 itself). */
/* Level: PARAM[F] STATE[F] */
SPL_Status USART_Init(USART_TypeDef *instance, const USART_InitTypeDef *cfg);
/* Single-step nonblocking accesses. No register write on any non-OK result.
 * Accept USART1..6 and LPUART1/2.
 * SPL_ERR_STATE: UE with TE (write) or RE (read) is not enabled.
 * SPL_WOULD_BLOCK: enabled but TXE / RXNE not set yet; retry later.
 * SPL_ERR_HW (read only): PE/FE/NE/ORE set; RDR is not read. Capture
 * USART_ErrorFlags(), then clear with USART_ClearErrors() or
 * USART_ClearErrorFlags(). TryWrite/TryRead transfer 8 bits; use
 * USART_TryWrite9/USART_TryRead9 for 9-bit frames.
 * v0.13.3: a 9-bit data field (M0=1, M1=0, PCE=0) gives SPL_ERR_STATE
 * (checked after UE/TE/RE, before any data access): bit 8 would be lost.
 */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] STATE[F] WOULD_BLOCK[r] */
SPL_Status USART_TryWrite(USART_TypeDef *instance, uint8_t byte);
/* Caller: after ERR_HW capture USART_ErrorFlags, then clear (USART_ClearErrorFlags); the
 * received frame is lost. */
/* Level: PARAM[F] STATE[F] HW[r] WOULD_BLOCK[r] */
SPL_Status USART_TryRead(USART_TypeDef *instance, uint8_t *byte);
/* Caller: capture USART_ErrorFlags first; this clears all four flags, so an error arriving after
 * the capture is lost (use USART_ClearErrorFlags to keep it). */
/* Level: PARAM[F] */
SPL_Status USART_ClearErrors(USART_TypeDef *instance);
/* Readiness: SPL_ERR_STATE when UE=0. With UE=1 returns SPL_OK; each output
 * is 1 only if its TE/RE is enabled and TEACK/REACK is acknowledged.
 * Never polls or waits. Both outputs must be non-NULL. */
/* Caller: poll until the needed output is 1 before relying on TE / RE (the driver never waits);
 * bound the poll. */
/* Level: PARAM[F] STATE[F] */
SPL_Status USART_Ready(USART_TypeDef *instance, uint8_t *tx_ready, uint8_t *rx_ready);
/* Snapshot raw error bits (ISR PE/FE/NE/ORE); caller decides how to recover. */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status USART_ErrorFlags(USART_TypeDef *instance, uint32_t *errors);
/* Caller: TC stays set until USART_ClearTxComplete; clear it after use so the next check sees
 * only the new transmission. */
/* Level: PARAM[F] */
SPL_Status USART_TxComplete(USART_TypeDef *instance, uint8_t *complete);

/* ---- extended configuration, LPUART, FIFO, RS485, IRQ/DMA ---- */
/* Full contract: Docs/v0.3_USART_CONTRACT.md. All configuration APIs below
 * that touch "UE=0 only" fields (RM0444 33.8 / 34.7) return SPL_ERR_STATE
 * while UE=1.
 * ERRATA ES0548 2.12.2: RX data may be corrupted by a glitch (< half bit) in the second half
 * of the stop bit: keep RX noiseless or filter it (board duty).
 * ERRATA ES0548 2.12.3 (documentation): the prescaler is absent on BASIC instances: a prescaler
 * other than DIV1 -> SPL_ERR_UNSUPPORTED there; USART_FifoConfig -> UNSUPPORTED on BASIC.
 * ERRATA ES0548 2.12.1 / 2.12.4: not applicable (no synchronous slave, no ABREN control).
 * ERRATA ES0548 2.12.5: NE may be set with ONEBIT = 1; ONEBIT is not exposed (always 0). */

/* USART_InitExTypeDef.word_length: data bits including the parity bit. */
#define USART_WORDLENGTH_7B 7U
#define USART_WORDLENGTH_8B 8U
#define USART_WORDLENGTH_9B 9U
/* USART_InitExTypeDef.parity */
#define USART_PARITY_NONE 0U
#define USART_PARITY_EVEN 1U
#define USART_PARITY_ODD 2U
/* USART_InitExTypeDef.stop_bits (value = CR2.STOP encoding). 0.5 and 1.5
 * are smartcard-oriented (RM0444 Table 184, 33.5.5): accepted on FULL instances only (v0.8,
 * per SKU group: G0B1 USART1/2/3, G071 USART1/2, G031 USART1); BASIC and LPUART => UNSUPPORTED. */
#define USART_STOPBITS_1 0U
#define USART_STOPBITS_0_5 1U
#define USART_STOPBITS_2 2U
#define USART_STOPBITS_1_5 3U
/* USART_InitExTypeDef.oversampling. LPUART1/2 have no OVER8 => x8 UNSUPPORTED. */
#define USART_OVERSAMPLING_16 0U
#define USART_OVERSAMPLING_8 1U
/* USART_InitExTypeDef.prescaler: PRESC.PRESCALER code (RM0444 33.8.14). */
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
/* USART_OPT_HALF_DUPLEX on a wire shared with other nodes: TX pin in alternate function open drain
 * with a pull-up (RM0444 33.5.16), else two push-pull drivers can fight on the line. */

/* Asynchronous frame configuration. 16 bytes, no padding. */
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

/* UE=0 only. Writes CR1 (M, PCE, PS, OVER8, TE, RE; everything else 0
 * incl. FIFOEN, DEAT/DEDT and interrupt enables), CR2 (STOP + options;
 * rest 0), CR3 (RTSE/CTSE/HDSEL; rest 0), PRESC (not on BASIC), BRR. Leaves UE=0: call
 * USART_FifoConfig / USART_DriverEnableConfig if needed, then USART_Enable.
 * Accepts LPUART1/2 (BRR = round(256*f/baud), 0x300..0xFFFFF).
 * ERRATA ES0548 2.13.1: LPUART1/2 with BRR in 0x301..0x3FF and
 * BRR & 0xFF != 0 (kernel/baud ratio non-integer in 3..4, e.g. 32768 Hz /
 * 9600 Bd) -> PARAM, no write; raise the kernel clock or lower the baud. */
/* Caller: none beyond the file-header duties (kernel_hz is the kernel clock before PRESC). */
/* Level: PARAM[F] STATE[F] UNSUPPORTED[F] */
SPL_Status USART_InitEx(USART_TypeDef *instance, const USART_InitExTypeDef *cfg);
/* Set CR1.UE (idempotent: UE=1 -> OK, no write). STATE when BRR is below the
 * instance minimum (USART 0x10, LPUART 0x300; reset BRR=0): call USART_InitEx
 * first. Poll USART_Ready for TEACK/REACK. */
/* Caller: poll USART_Ready for TEACK / REACK before the first transfer. */
/* Level: PARAM[F] STATE[F] */
SPL_Status USART_Enable(USART_TypeDef *instance);
/* Clear CR1.UE after the last frame left the line. v0.4 (G4 v0.13.4): UE=1, TE=1 and TC=0 ->
 * SPL_BUSY, no write (poll again; RM0444 33.5.5 step 8). UE=0 also resets the DMA requests:
 * disable the DMA channel BEFORE this call (RM0444 33.8.1 UE). */
/* Caller: disable the DMA channel first; bound the BUSY poll (a transmitter held by CTS never
 * completes: USART_Abort). */
/* Level: PARAM[F] BUSY[r] */
SPL_Status USART_Disable(USART_TypeDef *instance);
/* Clear CR1.UE immediately; frames in progress are discarded (old USART_Disable behaviour; e.g.
 * a transmitter held by CTS). */
/* Caller: frames in progress are discarded by design; disable the DMA channel first (UE = 0
 * resets the DMA requests). */
/* Level: PARAM[F] */
SPL_Status USART_Abort(USART_TypeDef *instance);

/* FIFO thresholds (CR3 TXFTCFG/RXFTCFG). EMPTY_FULL = TXFIFO empty / RXFIFO full. */
#define USART_FIFO_THRESHOLD_1_8 0U
#define USART_FIFO_THRESHOLD_1_4 1U
#define USART_FIFO_THRESHOLD_1_2 2U
#define USART_FIFO_THRESHOLD_3_4 3U
#define USART_FIFO_THRESHOLD_7_8 4U
#define USART_FIFO_THRESHOLD_EMPTY_FULL 5U
/* UE=0 only. CR1.FIFOEN = enable, CR3.TXFTCFG/RXFTCFG = thresholds. */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] STATE[F] UNSUPPORTED[F] */
SPL_Status USART_FifoConfig(USART_TypeDef *instance, uint8_t enable, uint8_t tx_threshold,
                            uint8_t rx_threshold);
/* UE=0 only. RS485 driver enable on the RTS/DE pin: CR3.DEM = enable,
 * CR3.DEP = active_low, CR1.DEAT/DEDT = 0..31 (sample-time units on
 * USART, kernel-clock based on LPUART: RM0444 33.5.20 / 34.4.13).
 * enable=1 while CR3.RTSE=1 => SPL_ERR_STATE (same pin). */
/* Caller: none beyond the file-header duties (RTS / DE pin in its alternate function). */
/* Level: PARAM[F] STATE[F] */
SPL_Status USART_DriverEnableConfig(USART_TypeDef *instance, uint8_t enable, uint8_t active_low,
                                    uint8_t assert_time, uint8_t deassert_time);

/* 9-bit capable Try* variants (TDR/RDR[8:0]). TryWrite9: data must fit the
 * configured data field (9/8/7 bits minus the parity bit when PCE=1),
 * else SPL_ERR_PARAM. TryRead9 returns RDR[8:0] raw (includes the parity
 * bit when PCE=1). Same state/WOULD_BLOCK/ERR_HW rules as TryWrite/TryRead. */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] STATE[F] WOULD_BLOCK[r] */
SPL_Status USART_TryWrite9(USART_TypeDef *instance, uint16_t data);
/* Caller: after ERR_HW capture USART_ErrorFlags, then clear (USART_ClearErrorFlags); the
 * received frame is lost. */
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
/* Set (enable=1) or clear (enable=0) the selected enables; other bits
 * untouched. Allowed with UE=1. Read-modify-write of CR1/CR3: do not call
 * concurrently with other CR1/CR3 writers of the same instance. */
/* Caller: clear stale flags (errors, IDLE, TC) before enabling their interrupt; the handler
 * clears ORE too (USART_IT_RXNE also enables the ORE interrupt), else the interrupt repeats. */
/* Level: PARAM[F] */
SPL_Status USART_ITConfig(USART_TypeDef *instance, uint32_t it_mask, uint8_t enable);

/* DMA request flags for USART_DmaConfig. */
#define USART_DMA_TX 0x01U   /* CR3.DMAT */
#define USART_DMA_RX 0x02U   /* CR3.DMAR */
#define USART_DMA_DDRE 0x04U /* CR3.DDRE: DMA disabled on reception error */
/* CR3.DMAT/DMAR/DDRE = flags. DMAT/DMAR may change with UE=1; a DDRE
 * change while UE=1 => SPL_ERR_STATE (UE=0 only field). */
/* Caller: DMA buffers stay valid until the DMA channel is disabled; disable the channel before
 * USART_Disable / USART_Abort. */
/* Level: PARAM[F] STATE[F] */
SPL_Status USART_DmaConfig(USART_TypeDef *instance, uint32_t dma_flags);

/* ISR.IDLE snapshot / ICR.IDLECF write (W1C, no read-modify-write). */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status USART_IdleDetected(USART_TypeDef *instance, uint8_t *idle);
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status USART_ClearIdle(USART_TypeDef *instance);
/* ICR.TCCF write. */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status USART_ClearTxComplete(USART_TypeDef *instance);
/* Error flags (ISR bit positions) reported by USART_ErrorFlags. */
#define USART_ERRORS_ALL 0x0FU /* PE | FE | NE | ORE */
/* Clear only the given error flags (non-zero subset of USART_ERRORS_ALL,
 * typically the value captured with USART_ErrorFlags) so that an error
 * arriving after the capture is not lost. */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status USART_ClearErrorFlags(USART_TypeDef *instance, uint32_t errors);

/* ---- v0.10 (G0): communication modes, Docs/v0.10_COMM_MODES.md ----
 * Call after USART_InitEx (which rewrites CR1..CR3) and before USART_Enable: every mode setting
 * needs UE = 0 (else SPL_ERR_STATE). Instance support follows RM0444 Table 184 per SKU group
 * (v0.8 classes): synchronous on FULL and BASIC; smartcard, IrDA, LIN, auto baud and receiver
 * timeout on FULL only; wake-up on FULL and LPUART; multiprocessor (mute) everywhere. Others ->
 * SPL_ERR_UNSUPPORTED. Mutually exclusive modes (RM0444 33.5.13 .. 33.5.18 "must be kept
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
/* CR2 synchronous fields (all 0 = asynchronous). LPUART -> UNSUPPORTED. Enabling with LINEN,
 * SCEN, HDSEL or IREN set -> STATE.
 * ERRATA ES0548 2.12.1 (family): in synchronous slave mode at a low baud rate TC can rise before
 * the last bit is shifted out; after TC wait until the clock line is idle for more than half a
 * clock period before disabling the USART (caller's duty). */
/* Caller: CK pin in its alternate function; synchronous slave: after TC wait for an idle clock
 * line longer than half a clock period before disabling (ES0548 2.12.1 above). */
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
/* FULL only. enable = 1 requires (else STATE) LINEN / HDSEL / IREN cleared, 8 data bits + parity
 * (M0 = 1, M1 = 0, PCE = 1), 1.5 or 0.5 stop bits, OVER8 = 0 (RM0444 33.5.17). Writes GTPR, CR3
 * SCEN / NACK / SCARCNT and CR2 CLKEN. enable = 0 clears SCEN / NACK / SCARCNT only. */
/* Caller: TX (card I/O) pin in alternate function open drain and CK pin when clock_out = 1
 * (RM0444 33.5.17); card protocol (ATR, retries policy) is the caller's. */
/* Level: PARAM[F] STATE[F] UNSUPPORTED[F] */
SPL_Status USART_SmartcardConfig(USART_TypeDef *instance, const USART_SmartcardTypeDef *cfg);
/* FULL only. IREN / IRLP; prescaler 1..255 (GTPR.PSC, 0 reserved). enable = 1 requires LINEN,
 * STOP, CLKEN, SCEN, HDSEL cleared and OVER8 = 0 (RM0444 33.5.18). GT is kept. */
/* Caller: low_power: prescaler chosen for the IrDA low-power frequency from the kernel clock
 * (RM0444 33.5.18); IR transceiver on the board. */
/* Level: PARAM[F] STATE[F] UNSUPPORTED[F] */
SPL_Status USART_IrdaConfig(USART_TypeDef *instance, uint8_t enable, uint8_t low_power,
                            uint8_t prescaler);
/* FULL only. LINEN / LBDL (11-bit break detection) / LBDIE. enable = 1 requires STOP = 1 bit,
 * CLKEN, SCEN, HDSEL, IREN cleared, 8-bit words, OVER8 = 0 (RM0444 33.5.13). */
/* Caller: break_irq = 1: the handler clears LBDF with USART_ClearModeFlags. */
/* Level: PARAM[F] STATE[F] UNSUPPORTED[F] */
SPL_Status USART_LinConfig(USART_TypeDef *instance, uint8_t enable, uint8_t break_11bit,
                           uint8_t break_irq);
/* RQR.SBKRQ (break; 13 bits in LIN mode). UE and TE required (else STATE); SBKF = 1 -> BUSY. */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] STATE[F] BUSY[r] */
SPL_Status USART_SendBreak(USART_TypeDef *instance);
/* FULL only. ABREN / ABRMODE (0 start bit, 1 falling edge, 2 0x7F frame, 3 0x55 frame).
 * ERRATA ES0548 2.12.4 (family): clearing ABREN during a reception corrupts data; ABREN changes
 * only with UE = 0 here (STATE otherwise), so it is never cleared while receiving. */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] STATE[F] UNSUPPORTED[F] */
SPL_Status USART_AutoBaudConfig(USART_TypeDef *instance, uint8_t enable, uint8_t mode);
/* RQR.ABRRQ: restart the detection (ABREN = 0 -> STATE). */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] STATE[F] */
SPL_Status USART_AutoBaudRequest(USART_TypeDef *instance);
/* *done = ABRF, *error = ABRE. */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status USART_AutoBaudStatus(USART_TypeDef *instance, uint8_t *done, uint8_t *error);
/* FULL and LPUART (BASIC -> UNSUPPORTED). UESM = enable, WUS = source (0 address match, 2 start
 * bit, 3 RXNE; 1 reserved -> PARAM), WUFIE = irq. Stop 0 / Stop 1 wake-up (RM0444 Table 184). */
/* Caller: kernel clock running in Stop (HSI16 or LSE source); EXTI / NVIC setup for the wake-up
 * and clearing WUF with USART_ClearModeFlags. */
/* Level: PARAM[F] STATE[F] UNSUPPORTED[F] */
SPL_Status USART_WakeupConfig(USART_TypeDef *instance, uint8_t enable, uint8_t source, uint8_t irq);
/* FULL only. RTOR = timeout (bit times, <= 0xFFFFFF) | block_length (smartcard BLEN), CR2.RTOEN.
 * RTOF / EOBF: USART_ModeFlags. */
/* Caller: clear RTOF / EOBF with USART_ClearModeFlags after each event. */
/* Level: PARAM[F] STATE[F] UNSUPPORTED[F] */
SPL_Status USART_RxTimeoutConfig(USART_TypeDef *instance, uint8_t enable, uint32_t timeout,
                                 uint8_t block_length);
/* Multiprocessor communication (RM0444 33.5.10): MME = enable, WAKE = address_mark (0 idle line,
 * 1 address mark), ADD = address, ADDM7 = addr7 (4-bit mode: address <= 15, else PARAM). */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] STATE[F] */
SPL_Status USART_MuteConfig(USART_TypeDef *instance, uint8_t enable, uint8_t address_mark,
                            uint8_t address, uint8_t addr7);
/* RQR.MMRQ: enter mute mode (MME = 0 -> STATE). RWU: USART_ModeFlags. */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] STATE[F] */
SPL_Status USART_EnterMute(USART_TypeDef *instance);

/* ISR flags of the modes above, and the subset with a same-position ICR clear bit. */
#define USART_MODE_FLAGS_CLEARABLE                                                                 \
    (USART_ISR_LBDF | USART_ISR_CMF | USART_ISR_WUF | USART_ISR_RTOF | USART_ISR_EOBF |            \
     USART_ISR_UDR)
#define USART_MODE_FLAGS                                                                           \
    (USART_MODE_FLAGS_CLEARABLE | USART_ISR_ABRE | USART_ISR_ABRF | USART_ISR_SBKF |               \
     USART_ISR_RWU | USART_ISR_TCBGT)
/* *flags = ISR & USART_MODE_FLAGS. */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status USART_ModeFlags(USART_TypeDef *instance, uint32_t *flags);
/* ICR = flags (non-empty subset of USART_MODE_FLAGS_CLEARABLE), then ISR read back. Flags of a
 * feature the instance class lacks -> UNSUPPORTED: LBDF / RTOF / EOBF / UDR only on FULL USARTs,
 * WUF on FULL and LPUART, BASIC USARTs CMF only (RM0444 Table 184). */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] UNSUPPORTED[F] */
SPL_Status USART_ClearModeFlags(USART_TypeDef *instance, uint32_t flags);

#ifdef __cplusplus
}
#endif
#endif
