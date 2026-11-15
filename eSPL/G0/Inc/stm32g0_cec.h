#ifndef STM32G0_CEC_H
#define STM32G0_CEC_H
#include <stdint.h>
#include "stm32g0xx.h"
#include "stm32g0_status.h"
#include "stm32g0_sku.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* v0.9 SKU profile: this API exists only where the device has the peripheral (SPL_G0_HAS_CEC):
 * STM32G071 / G081 / G0B1 / G0C1. */
#if SPL_G0_HAS_CEC
/* HDMI-CEC controller (RM0444 Rev 6 section 39; Docs/v0.9_CEC_CONTRACT.md). New for G0. Single
 * instance, no static state, non-blocking byte-level API (TXBR / RXBR polled or used from the ISR).
 * Caller duties (not done by the driver):
 *  - Clocks: RCC_EnablePeriphClock(RCC_PERIPH_CEC); kernel clock RCC_SetKernelClock(RCC_KERNEL_CEC,
 *    RCC_KSRC_HSI16_DIV488 or RCC_KSRC_LSE) (RM0444 5.4.21, CECSEL).
 *  - CEC pin: GPIO alternate function, open drain, external pull-up (HDMI-CEC specification).
 *  - ERRATA ES0418 2.16.2 / ES0412 2.17.2 / ES0548 2.18.2 / ES0549 2.19.2 (every CEC SKU),
 * unexpected TXERR: the line rise time must reach VIH within two CEC clock cycles (~61 us); limit
 * the number of devices on the line (maximum equivalent 10 %..90 % rise time 111.5 us). Board
 * design, not software.
 *  - Message-level logic (opcodes, retries after TXACKE / TXERR / TXUDR, follower behaviour).
 * ERRATA ES0418 2.16.1 / ES0412 2.17.1 / ES0548 2.18.1 / ES0549 2.19.1 (every CEC SKU): in normal
 * receiving mode a message to another address fills the reception buffer and later messages to the
 * own address are NACKed. Workaround applied by the driver: only listen mode (LSTN = 1) is
 * accepted; the caller discards messages for other destinations with CEC_HeaderForUs.
 * Serialize callers (ISR included). */

typedef struct
{
    uint16_t own_addresses; /* OAR[14:0]: bit n = logical address n acknowledged, 0..0x7FFF */
    uint8_t listen;         /* LSTN: must be 1 (errata, see above); 0 -> UNSUPPORTED */
    uint8_t signal_free;    /* SFT[2:0]: 0 = hardware history rule, 1..7 = 0.5 .. 6.5 bit periods */
    uint8_t sft_option;     /* SFTOPT: 0 = SFT starts at TXSOM, 1 = at end of message */
    uint8_t rx_tolerance;   /* RXTOL: 0 = standard, 1 = extended */
    uint8_t bre_stop;       /* BRESTP: stop reception on bit rising error */
    uint8_t bre_error_bit;  /* BREGEN: error bit on BRE (requires bre_stop = 1, else PARAM) */
    uint8_t lbpe_error_bit; /* LBPEGEN: error bit on long bit period error */
    uint8_t no_broadcast_error_bit; /* BRDNOGEN */
} CEC_ConfigTypeDef;

/* CEC_ISR receive / transmit error and event groups. */
#define CEC_RX_ERRORS (CEC_ISR_RXOVR | CEC_ISR_BRE | CEC_ISR_SBPE | CEC_ISR_LBPE | CEC_ISR_RXACKE)
#define CEC_TX_ERRORS (CEC_ISR_TXUDR | CEC_ISR_TXERR | CEC_ISR_TXACKE)
#define CEC_FLAGS_ALL                                                                              \
    (CEC_ISR_RXBR | CEC_ISR_RXEND | CEC_RX_ERRORS | CEC_ISR_ARBLST | CEC_ISR_TXBR |                \
     CEC_ISR_TXEND | CEC_TX_ERRORS)
#define CEC_BROADCAST 0xFU

/* CFGR (RM0444 39.7.2, written only with CECEN = 0). CECEN = 1 -> STATE. PARAM: own_addresses >
 * 0x7FFF, signal_free > 7, any flag > 1, bre_error_bit = 1 with bre_stop = 0 (BREGEN acts only
 * with BRESTP). listen = 0 -> UNSUPPORTED (errata above). Nothing written on refusal. */
/* Seviye: PARAM[S,n] STATE[S] UNSUPPORTED[S] */
SPL_Status CEC_Config(const CEC_ConfigTypeDef *cfg);
/* IER = mask (subset of CEC_FLAGS_ALL, same bit positions as ISR; RM0444 39.7.6: written only with
 * CECEN = 0 -> else STATE). */
/* Seviye: PARAM[S] STATE[S] */
SPL_Status CEC_InterruptConfig(uint32_t mask);
/* CECEN = 1 (reception starts). */
/* Seviye: - */
SPL_Status CEC_Enable(void);
/* CECEN = 0: aborts reception / transmission, hardware clears TXSOM / TXEOM (RM0444 39.7.1). */
/* Seviye: - */
SPL_Status CEC_Disable(void);
/* Starts a message with its header byte (initiator in [7:4], destination in [3:0]; the initiator is
 * taken from the byte, not from OAR, RM0444 39.7.1). last = 1: one-byte message (ping), TXEOM is
 * set before TXSOM. CECEN = 0 -> STATE; TXSOM = 1 (a transmission is pending) -> BUSY. */
/* Seviye: PARAM[n] STATE[S] BUSY[r] */
SPL_Status CEC_StartMessage(uint8_t header, uint8_t last);
/* Next data byte on TXBR. TXUDR / TXERR / TXACKE set -> ERR_HW (message aborted by hardware;
 * CEC_ClearFlags after diagnosis). TXSOM = 0 -> STATE (no message). TXBR = 0 -> WOULD_BLOCK. Else
 * TXEOM is set first when last = 1, the byte is written to TXDR and TXBR cleared. Write within six
 * nominal bit periods of TXBR (RM0444 39.7.5) or TXUDR follows. */
/* Seviye: PARAM[n] STATE[S] HW[r] WOULD_BLOCK[r] */
SPL_Status CEC_TryWriteByte(uint8_t data, uint8_t last);
/* *done = TXEND (then TXEND cleared). TX errors -> ERR_HW as above. ARBLST is not an error: the
 * hardware retries (TXSOM stays set). */
/* Seviye: PARAM[S] HW[r] */
SPL_Status CEC_TxComplete(uint8_t *done);
/* RX errors -> ERR_HW (nothing read). RXBR = 0 -> WOULD_BLOCK. Else *data = RXDR, *end = RXEND,
 * RXBR (and RXEND when set) cleared. */
/* Seviye: PARAM[S] HW[r] WOULD_BLOCK[r] */
SPL_Status CEC_TryReadByte(uint8_t *data, uint8_t *end);
/* *flags = ISR & CEC_FLAGS_ALL. */
/* Seviye: PARAM[S] */
SPL_Status CEC_GetFlags(uint32_t *flags);
/* rc_w1: ISR = mask (non-empty subset of CEC_FLAGS_ALL). */
/* Seviye: PARAM[S,n] */
SPL_Status CEC_ClearFlags(uint32_t mask);
/* Errata workaround helper (pure function, no register access): *for_us = 1 when the header's
 * destination [3:0] is CEC_BROADCAST or enabled in own_addresses. own_addresses > 0x7FFF -> PARAM.
 */
/* Seviye: PARAM[S,n] */
SPL_Status CEC_HeaderForUs(uint8_t header, uint16_t own_addresses, uint8_t *for_us);

#endif /* SPL_G0_HAS_CEC */

#ifdef __cplusplus
}
#endif
#endif
