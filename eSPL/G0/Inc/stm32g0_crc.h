#ifndef STM32G0_CRC_H
#define STM32G0_CRC_H
#include <stdint.h>
#include "stm32g0xx.h"
#include "stm32g0_status.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* CRC calculation unit (RM0444 Rev 6 section 14). Single instance on every
 * supported SKU, no static state, no waits. The caller enables the AHB clock
 * (RCC_AHBENR.CRCEN, RCC_EnablePeriphClock(RCC_PERIPH_CRC)) before any call. Serialize all callers:
 * one CRC computation at a time; an ISR that uses the CRC unit corrupts a computation running in
 * thread context. Contract: Docs/v0.3_CRC_CONTRACT.md. */

/* CRC_CR.POLYSIZE field values (RM0444 14.4.3). */
#define CRC_POLYSIZE_32 0U
#define CRC_POLYSIZE_16 1U
#define CRC_POLYSIZE_8 2U
#define CRC_POLYSIZE_7 3U

/* CRC_CR.REV_IN field values (RM0444 14.4.3). */
#define CRC_REV_IN_NONE 0U
#define CRC_REV_IN_BYTE 1U
#define CRC_REV_IN_HALFWORD 2U
#define CRC_REV_IN_WORD 3U

typedef struct
{
    uint32_t polysize;   /* CRC_POLYSIZE_x. */
    uint32_t polynomial; /* Odd, fits in polysize bits (CRC-32: 0x04C11DB7). */
    uint32_t init;       /* Initial value, fits in polysize bits. */
    uint32_t rev_in;     /* CRC_REV_IN_x. */
    uint32_t rev_out;    /* 0: normal, 1: bit-reversed output. */
} CRC_ConfigTypeDef;

/* Validates everything, then: dummy DR read (ends any computation, RM0444
 * 14.3.3), POL, CR (RESET=0), INIT. The INIT write also loads DR, so a new
 * computation starts from init. PARAM: NULL, field out of range, even
 * polynomial, polynomial/init wider than polysize. */
/* Seviye: PARAM[S] */
SPL_Status CRC_Config(const CRC_ConfigTypeDef *config);
/* Sets CR.RESET (self-clearing) keeping REV_OUT/REV_IN/POLYSIZE: DR = INIT. */
/* Seviye: - */
SPL_Status CRC_Reset(void);
/* Feed one byte / half-word / word with an 8/16/32-bit access to DR
 * (RM0444 14.3.3: right-aligned byte and half-word accesses). */
/* Seviye: - */
SPL_Status CRC_Feed8(uint8_t data);
/* Seviye: - */
SPL_Status CRC_Feed16(uint16_t data);
/* Seviye: - */
SPL_Status CRC_Feed32(uint32_t data);
/* 32-bit DR read, masked to the configured polysize (7/8/16/32 bits). */
/* Seviye: PARAM[S] */
SPL_Status CRC_GetResult(uint32_t *result);

#ifdef __cplusplus
}
#endif
#endif
