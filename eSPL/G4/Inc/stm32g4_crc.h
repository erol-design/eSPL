/* SPDX-License-Identifier: MIT */
#ifndef STM32G4_CRC_H
#define STM32G4_CRC_H
#include <stdint.h>
#include "stm32g4xx.h"
#include "stm32g4_status.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* CRC calculation unit driver for STM32G4 (RM0440 Rev 9 section 16).
 * Scope: one instance on every supported SKU; programmable polynomial (7/8/16/32 bit), init
 * value, input/output bit reversal, 8/16/32-bit feeds. No final XOR, no context save/restore,
 * no interrupt or DMA feeding (the unit has no done flag and no DMA request of its own).
 * Usage: CRC_Config, then CRC_Feed8 / CRC_Feed16 / CRC_Feed32 per data item, CRC_GetResult;
 * CRC_Reset restarts from INIT with the same configuration.
 * Common rules: no static state, no function waits (the AHB stalls a feed if needed); output
 * parameters are unchanged unless SPL_OK (METHOD 1.8).
 * Caller duties (all APIs; not done by the driver):
 *  - Clock: AHB clock of the unit (RCC_AHB1ENR.CRCEN) before any call.
 *  - Serialize all callers: one computation at a time; an ISR that uses the CRC unit corrupts
 *    a computation running in thread context.
 *  - Byte order: a 32-bit DR write is processed from bit 31 down; for a byte stream feed bytes
 *    or build words with the first byte in bits 31:24 (contract, Caller notes).
 *  - Final XOR in software; a finalized result is not a valid init for a continuation.
 * Errata: no ES0430 / ES0431 / ES0523 item for this unit.
 * Contract: Docs/v0.11_CRC_CONTRACT.md. */

/* CRC_CR.POLYSIZE field values (RM0440 16.4.3). */
#define CRC_POLYSIZE_32 0U
#define CRC_POLYSIZE_16 1U
#define CRC_POLYSIZE_8 2U
#define CRC_POLYSIZE_7 3U

/* CRC_CR.REV_IN field values (RM0440 16.4.3). */
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

    /* Validates everything, then: dummy DR read (ends any computation, RM0440
     * 16.3.3), POL, CR (RESET=0), INIT. The INIT write also loads DR, so a new
     * computation starts from init. PARAM: NULL, field out of range, even
     * polynomial, polynomial/init wider than polysize.
     * Caller: none beyond the file-header duties. */
    /* Level: PARAM[F] */
    SPL_Status CRC_Config(const CRC_ConfigTypeDef *config);
    /* Sets CR.RESET (self-clearing) keeping REV_OUT/REV_IN/POLYSIZE: DR = INIT. Always OK.
     * Caller: CRC_Config done before (INIT and POL are not checked). */
    /* Level: - */
    SPL_Status CRC_Reset(void);
    /* Feed one byte / half-word / word with an 8/16/32-bit access to DR
     * (RM0440 16.4.1: right-aligned byte and half-word accesses). The access size selects the
     * computation width; REV_IN applies to the written size. Always OK, no check.
     * Caller: CRC_Config or CRC_Reset for this computation first; the caller decides the byte
     * order of mixed-width feeds. */
    /* Level: - */
    SPL_Status CRC_Feed8(uint8_t data);
    /* Caller: as CRC_Feed8. */
    /* Level: - */
    SPL_Status CRC_Feed16(uint16_t data);
    /* Caller: as CRC_Feed8; see the file-header byte-order duty. */
    /* Level: - */
    SPL_Status CRC_Feed32(uint32_t data);
    /* 32-bit DR read, masked to the configured polysize (7/8/16/32 bits); REV_OUT included,
     * no final XOR. PARAM: NULL.
     * Caller: apply any final XOR in software. */
    /* Level: PARAM[F] */
    SPL_Status CRC_GetResult(uint32_t *result);

#ifdef __cplusplus
}
#endif
#endif
