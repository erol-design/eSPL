#ifndef STM32G0_UCPD_H
#define STM32G0_UCPD_H
#include <stdint.h>
#include "stm32g0xx.h"
#include "stm32g0_status.h"
#include "stm32g0_sku.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* v0.9 SKU profile: this API exists only where the device has the peripheral (SPL_G0_HAS_UCPD):
 * STM32G071 / G081 / G0B1 / G0C1 (UCPD1 and UCPD2). */
#if SPL_G0_HAS_UCPD
/* USB Type-C / USB Power Delivery interface (RM0444 Rev 6 section 38; Docs/v0.9_UCPD_CONTRACT.md).
 * New for G0. Physical layer only: Type-C pull-ups / pull-downs and CC voltage states, BMC
 * transmit / receive of ordered sets and payload bytes, Hard Reset, Cable Reset, BIST carrier,
 * fast role swap. No static state, no waits.
 * Caller duties (not done by the driver):
 *  - Clocks: RCC_EnablePeriphClock(RCC_PERIPH_UCPD1 / UCPD2); ucpd_ker_ck is always HSI16
 * (RM0444 5.2, HSI16 must run); PSC so that ucpd_clk is 6..9 MHz, HBITCLKDIV / IFRGAP / TRANSWIN
 * per RM0444 38.8.1.
 *  - The Type-C and USB PD state machines (attach / detach debouncing, VBUS, VCONN, GoodCRC,
 *    MessageID, retries, timers), cable orientation (PHYCCSEL), DMA channels.
 *  - ERRATA ES0418 2.15.1 / ES0412 2.16.1 / ES0548 2.17.1 / ES0549 2.18.1: a Hard Reset issued at
 *    the instant of a Tx data underflow can corrupt the CRC of the next packet. Workaround: use
 *    DMA for transmission (UCPD_ConfigTypeDef.tx_dma = 1); the protocol layer retransmits on a bad
 *    CRC. The byte API stays available but is exposed to the erratum.
 *  - ERRATA ES0418 2.15.3 / ES0412 2.16.3 (G071 / G081): Rpdb is 3.9 kOhm instead of 5.1 kOhm
 *    from power-on until UCPD_Strobe; in dead-battery applications call UCPD_Strobe within
 *    tCCDebounce (100 ms) of power-on.
 *  - ERRATA ES0418 2.15.2 / ES0412 2.16.2 / ES0548 2.17.2 / ES0549 2.18.2: an ordered set with
 *    several errors in one K-code is reported invalid (RXERR); no workaround, still USB PD
 *    compliant. ES0548 2.17.3 / ES0549 2.18.3: Tx high level marginal at low temperature (board).
 * HW-011: CMSIS v1.4.4 has no UCPD_CR_DBATTEN (RM0444 38.8.3 bit 15); the dead-battery enable
 * is not provided until the bit is defined and checked on hardware.
 * Serialize callers per instance (ISR included). */

/* 5b K-codes (RM0444 Table 244; 5b column read as a binary number, bit 4 on the left) and the
 * ordered sets of RM0444 Table 245: K-code #1 in bits 4:0 (transmitted first, Figure 406). */
#define UCPD_KCODE_SYNC1 0x18U
#define UCPD_KCODE_SYNC2 0x11U
#define UCPD_KCODE_SYNC3 0x06U
#define UCPD_KCODE_RST1 0x07U
#define UCPD_KCODE_RST2 0x19U
#define UCPD_ORDSET(k1, k2, k3, k4)                                                                \
    ((uint32_t)(k1) | ((uint32_t)(k2) << 5U) | ((uint32_t)(k3) << 10U) | ((uint32_t)(k4) << 15U))
#define UCPD_ORDSET_SOP                                                                            \
    UCPD_ORDSET(UCPD_KCODE_SYNC1, UCPD_KCODE_SYNC1, UCPD_KCODE_SYNC1, UCPD_KCODE_SYNC2)
#define UCPD_ORDSET_SOP1                                                                           \
    UCPD_ORDSET(UCPD_KCODE_SYNC1, UCPD_KCODE_SYNC1, UCPD_KCODE_SYNC3, UCPD_KCODE_SYNC3)
#define UCPD_ORDSET_SOP2                                                                           \
    UCPD_ORDSET(UCPD_KCODE_SYNC1, UCPD_KCODE_SYNC3, UCPD_KCODE_SYNC1, UCPD_KCODE_SYNC3)
#define UCPD_ORDSET_HARD_RESET                                                                     \
    UCPD_ORDSET(UCPD_KCODE_RST1, UCPD_KCODE_RST1, UCPD_KCODE_RST1, UCPD_KCODE_RST2)
#define UCPD_ORDSET_CABLE_RESET                                                                    \
    UCPD_ORDSET(UCPD_KCODE_RST1, UCPD_KCODE_SYNC1, UCPD_KCODE_RST1, UCPD_KCODE_SYNC3)
#define UCPD_ORDSET_SOP1_DEBUG                                                                     \
    UCPD_ORDSET(UCPD_KCODE_SYNC1, UCPD_KCODE_RST2, UCPD_KCODE_RST2, UCPD_KCODE_SYNC3)
#define UCPD_ORDSET_SOP2_DEBUG                                                                     \
    UCPD_ORDSET(UCPD_KCODE_SYNC1, UCPD_KCODE_RST2, UCPD_KCODE_SYNC3, UCPD_KCODE_SYNC2)
#define UCPD_ORDSET_MAX 0xFFFFFUL
#define UCPD_PAYLOAD_MAX 1023U

/* RXORDSETEN bits (UCPD_ConfigTypeDef.rx_ordered_sets, RM0444 38.8.1). */
#define UCPD_RX_SOP 0x001U
#define UCPD_RX_SOP1 0x002U
#define UCPD_RX_SOP2 0x004U
#define UCPD_RX_HARD_RESET 0x008U
#define UCPD_RX_CABLE_RESET 0x010U
#define UCPD_RX_SOP1_DEBUG 0x020U
#define UCPD_RX_SOP2_DEBUG 0x040U
#define UCPD_RX_EXT1 0x080U
#define UCPD_RX_EXT2 0x100U

/* UCPD_SR event flags (clearable ones have the same bit position in UCPD_ICR). */
#define UCPD_TX_FLAGS                                                                              \
    (UCPD_SR_TXIS | UCPD_SR_TXMSGDISC | UCPD_SR_TXMSGSENT | UCPD_SR_TXMSGABT | UCPD_SR_HRSTDISC |  \
     UCPD_SR_HRSTSENT | UCPD_SR_TXUND)
#define UCPD_RX_FLAGS                                                                              \
    (UCPD_SR_RXNE | UCPD_SR_RXORDDET | UCPD_SR_RXHRSTDET | UCPD_SR_RXOVR | UCPD_SR_RXMSGEND |      \
     UCPD_SR_RXERR)
#define UCPD_TYPEC_FLAGS (UCPD_SR_TYPECEVT1 | UCPD_SR_TYPECEVT2 | UCPD_SR_FRSEVT)
#define UCPD_FLAGS_ALL (UCPD_TX_FLAGS | UCPD_RX_FLAGS | UCPD_TYPEC_FLAGS)
/* IMR interrupt enables: SR bit positions, but RXERR (bit 13) has no IMR bit (RM0444 38.8.4: bit 13
 * reserved; no CMSIS UCPD_IMR_RXERRIE). 2026-11-14 capability audit. */
#define UCPD_IT_ALL (UCPD_FLAGS_ALL & ~UCPD_SR_RXERR)
#define UCPD_FLAGS_CLEARABLE                                                                       \
    (UCPD_SR_TXMSGDISC | UCPD_SR_TXMSGSENT | UCPD_SR_TXMSGABT | UCPD_SR_HRSTDISC |                 \
     UCPD_SR_HRSTSENT | UCPD_SR_TXUND | UCPD_SR_RXORDDET | UCPD_SR_RXHRSTDET | UCPD_SR_RXOVR |     \
     UCPD_SR_RXMSGEND | UCPD_TYPEC_FLAGS)

typedef struct
{
    uint8_t prescaler;        /* PSC_UCPDCLK: 0..4 = /1, /2, /4, /8, /16 */
    uint8_t transition_win;   /* TRANSWIN: 1..31 (0 not supported) */
    uint8_t interframe_gap;   /* IFRGAP: 1..31 (0 not supported) */
    uint8_t half_bit_div;     /* HBITCLKDIV: 0..63 */
    uint16_t rx_ordered_sets; /* RXORDSETEN: UCPD_RX_* mask, 0..0x1FF */
    uint8_t rx_dma;           /* RXDMAEN */
    uint8_t tx_dma;           /* TXDMAEN (errata workaround, see above) */
    uint8_t rx_filter_off;    /* CFGR2.RXFILTDIS */
    uint8_t rx_filter_2;      /* CFGR2.RXFILT2N3: 1 = 2 samples, 0 = 3 */
    uint8_t force_clock;      /* CFGR2.FORCECLK */
    uint8_t wakeup;           /* CFGR2.WUPEN: wake-up from Stop */
} UCPD_ConfigTypeDef;

typedef struct
{
    uint8_t sink;       /* ANAMODE: 0 = source (Rp), 1 = sink (Rd) */
    uint8_t rp_current; /* ANASUBMODE (source): 0 = off, 1 = default USB, 2 = 1.5 A, 3 = 3.0 A;
                         * sink: must be 0 */
    uint8_t cc_enable;  /* CCENABLE: 0 none, 1 CC1, 2 CC2, 3 both */
    uint8_t cc1_detector_off; /* CC1TCDIS */
    uint8_t cc2_detector_off; /* CC2TCDIS */
    uint8_t rdch;             /* RDCH (source VCONN discharge, Type-C ECN) */
} UCPD_TypeCTypeDef;

/* Writes CFGR1 (UCPDEN = 0, fields above) and CFGR2. Both are effective only with UCPDEN = 0
 * (RM0444 38.8.1 / 38.8.2) -> UCPDEN = 1 -> STATE. Not UCPD1 / UCPD2 or a field out of range ->
 * PARAM. Nothing written on refusal. */
/* Seviye: PARAM[S,n] STATE[S] */
SPL_Status UCPD_Config(UCPD_TypeDef *ucpd, const UCPD_ConfigTypeDef *cfg);
/* RX_ORDEXT1 / RX_ORDEXT2 (20-bit patterns for UCPD_RX_EXT1 / EXT2); UCPDEN = 0 only (38.8.13). */
/* Seviye: PARAM[S] STATE[S] */
SPL_Status UCPD_SetRxExtensions(UCPD_TypeDef *ucpd, uint32_t ext1, uint32_t ext2);
/* UCPDEN = 1 / 0. Disabling resets every control bit (RM0444 38.8.1): configure CR / IMR again
 * after enabling. */
/* Seviye: PARAM[S] */
SPL_Status UCPD_Enable(UCPD_TypeDef *ucpd);
/* Seviye: PARAM[S] */
SPL_Status UCPD_Disable(UCPD_TypeDef *ucpd);
/* CR Type-C fields (UCPDEN = 0 -> STATE: CR writes are ignored then, 38.8.3). ANAMODE and RDCH take
 * effect only after UCPD_Strobe (RM0444 38.8.3, SYSCFG_CFGR1). Command bits are written as 0. */
/* Seviye: PARAM[S,n] STATE[S] */
SPL_Status UCPD_TypeCConfig(UCPD_TypeDef *ucpd, const UCPD_TypeCTypeDef *tc);
/* SYSCFG_CFGR1.UCPDx_STROBE = 1 (RM0444 SYSCFG): UCPD disabled -> internal dead-battery
 * pull-downs released; enabled -> CR pull configuration applied. */
/* Seviye: PARAM[S] */
SPL_Status UCPD_Strobe(const UCPD_TypeDef *ucpd);
/* TYPEC_VSTATE_CC1 / CC2 (0..3; meaning per RM0444 Table 248 and the ANAMODE / ANASUBMODE set). */
/* Seviye: PARAM[S] */
SPL_Status UCPD_GetCcState(UCPD_TypeDef *ucpd, uint8_t *cc1, uint8_t *cc2);
/* PHYCCSEL = cc2, PHYRXEN = rx_enable, RXMODE = bist_rx (BIST test data receive). UCPDEN = 1. */
/* Seviye: PARAM[S,n] STATE[S] */
SPL_Status UCPD_PhyConfig(UCPD_TypeDef *ucpd, uint8_t cc2, uint8_t rx_enable, uint8_t bist_rx);
/* FRSRXEN (FRS event detection on the PHYCCSEL line). UCPDEN = 1. */
/* Seviye: PARAM[S,n] STATE[S] */
SPL_Status UCPD_FrsConfig(UCPD_TypeDef *ucpd, uint8_t rx_enable);
/* FRSTX = 1 (cleared by hardware after the signalling, 38.8.3). UCPDEN = 1; FRSTX still 1 -> BUSY.
 */
/* Seviye: PARAM[S] STATE[S] BUSY[r] */
SPL_Status UCPD_FrsSignal(UCPD_TypeDef *ucpd);
/* IMR = mask (subset of UCPD_IT_ALL; IMR bit n enables SR bit n, RXERR has none). UCPDEN = 1
 * (38.8.4). */
/* Seviye: PARAM[S] STATE[S] */
SPL_Status UCPD_InterruptConfig(UCPD_TypeDef *ucpd, uint32_t mask);
/* TX_ORDSET = ordered_set (<= 0xFFFFF), TX_PAYSZ = payload_size (header included, CRC excluded,
 * <= 1023), then TXMODE = 0 and TXSEND = 1 (RM0444 38.5.3). UCPDEN = 0 -> STATE; TXSEND or TXHRST
 * still set (previous request not started) -> BUSY. TXMSGDISC later: retry (collision avoidance).
 */
/* Seviye: PARAM[S] STATE[S] BUSY[r] */
SPL_Status UCPD_StartMessage(UCPD_TypeDef *ucpd, uint32_t ordered_set, uint32_t payload_size);
/* Next payload byte: TXDMAEN = 1 -> STATE (DMA owns TXDR); TXUND set -> ERR_HW (message ends with
 * a bad CRC, RM0444 38.4.4); TXIS = 0 -> WOULD_BLOCK; else TXDR = data. */
/* Seviye: PARAM[S] STATE[S] HW[r] WOULD_BLOCK[r] */
SPL_Status UCPD_TryWriteByte(UCPD_TypeDef *ucpd, uint8_t data);
/* TXHRST = 1: Hard Reset, takes precedence over an ongoing message (RM0444 38.5.3). UCPDEN = 0 ->
 * STATE; TXHRST already set -> BUSY. See the TXHRST erratum above. */
/* Seviye: PARAM[S] STATE[S] BUSY[r] */
SPL_Status UCPD_SendHardReset(UCPD_TypeDef *ucpd);
/* TXMODE = 1 (Cable Reset) or 2 (BIST Carrier Mode 2) with TXSEND = 1; other modes -> PARAM.
 * Quit BIST carrier by UCPD_Disable (RM0444 38.8.3). UCPDEN / pending rules as UCPD_StartMessage.
 */
/* Seviye: PARAM[S] STATE[S] BUSY[r] */
SPL_Status UCPD_SendSpecial(UCPD_TypeDef *ucpd, uint32_t txmode);
/* RXDMAEN = 1 -> STATE; RXOVR set -> ERR_HW (nothing read); RXNE = 0 -> WOULD_BLOCK; else RXDR. */
/* Seviye: PARAM[S] STATE[S] HW[r] WOULD_BLOCK[r] */
SPL_Status UCPD_TryReadByte(UCPD_TypeDef *ucpd, uint8_t *data);
/* RXORDDET = 0 -> WOULD_BLOCK; else *code = RX_ORDSET.RXORDSET (0 SOP .. 7 extension 2, RM0444
 * 38.8.10) and RXORDDET cleared. */
/* Seviye: PARAM[S] WOULD_BLOCK[r] */
SPL_Status UCPD_GetRxOrderedSet(UCPD_TypeDef *ucpd, uint8_t *code);
/* RXMSGEND = 0 -> WOULD_BLOCK; else *size = RX_PAYSZ, *bad = RXERR (discard the bytes read when 1,
 * RM0444 38.5.4) and RXMSGEND cleared. */
/* Seviye: PARAM[S] WOULD_BLOCK[r] */
SPL_Status UCPD_GetRxMessageEnd(UCPD_TypeDef *ucpd, uint32_t *size, uint8_t *bad);
/* *flags = SR & UCPD_FLAGS_ALL. */
/* Seviye: PARAM[S] */
SPL_Status UCPD_GetFlags(UCPD_TypeDef *ucpd, uint32_t *flags);
/* ICR = mask (non-empty subset of UCPD_FLAGS_CLEARABLE). */
/* Seviye: PARAM[S,n] */
SPL_Status UCPD_ClearFlags(UCPD_TypeDef *ucpd, uint32_t mask);

#endif /* SPL_G0_HAS_UCPD */

#ifdef __cplusplus
}
#endif
#endif
