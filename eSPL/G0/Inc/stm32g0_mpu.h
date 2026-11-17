#ifndef STM32G0_MPU_H
#define STM32G0_MPU_H
#include <stdint.h>
#include "stm32g0xx.h"
#include "stm32g0_status.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* Cortex-M0+ (Armv6-M) MPU, every STM32G0 SKU (__MPU_PRESENT = 1; Docs/v0.9_MPU_CONTRACT.md).
 * Adapted from the STM32H7 SPL Armv7-M MPU driver: no caches, no TEX (always written 0), no
 * background region erratum. No static state, no waits. Sources: CMSIS Core(M) 5.6 core_cm0plus.h
 * (register and bit definitions). PM0223 (Cortex-M0+ programming manual) is not in the repository:
 * the Armv6-M rules used here (8 regions read from MPU_TYPE at run time, minimum region size 256
 * bytes, TEX = 000 only) are the conservative choice and are marked DOC-PENDING in the contract
 * until PM0223 is reviewed. Rules: regions change only while the MPU is disabled (STATE otherwise);
 * the higher region number wins on overlap; peripherals (0x40000000 - 0x5FFFFFFF, IOPORT included)
 * are never made Normal. Fault handling: no MemManage on Armv6-M, MPU faults are HardFaults. Not
 * reentrant.
 * Caller duties (all APIs): single owner: serialise MPU calls; the HardFault handler that
 * receives MPU faults is the application's. */

typedef enum
{
    MPU_MEM_STRONGLY_ORDERED = 0, /* C 0 B 0, execute never */
    MPU_MEM_DEVICE = 1,           /* shareable Device: S 1 C 0 B 1, execute never */
    MPU_MEM_NORMAL = 2            /* Normal: S 0 C 1 B 1 (no cache on Cortex-M0+: ordering only) */
} MPU_MemTypeDef;

/* RASR.AP encodings (Armv6-M / Armv7-M PMSA). */
typedef enum
{
    MPU_AP_NONE = 0,
    MPU_AP_PRIV_RW = 1,
    MPU_AP_PRIV_RW_USER_RO = 2,
    MPU_AP_FULL = 3,
    MPU_AP_PRIV_RO = 5,
    MPU_AP_RO = 6
} MPU_AccessTypeDef;

/* RASR.SIZE: region = 2^(size + 1) bytes; 7 (256 B) .. 31 (4 GB). */
#define MPU_SIZE_MIN 7U
#define MPU_SIZE_MAX 31U

typedef struct
{
    uint32_t number; /* 0 .. MPU_TYPE.DREGION - 1 */
    uint32_t base;   /* aligned to the region size */
    uint32_t size;   /* MPU_SIZE_MIN .. MPU_SIZE_MAX */
    uint32_t srd;    /* sub-region disable 0..0xFE (0xFF would disable the whole region) */
    MPU_MemTypeDef type;
    MPU_AccessTypeDef access;
    uint32_t execute; /* 0 = XN, 1 = instruction fetch allowed (Normal only) */
} MPU_RegionTypeDef;

/* *count = MPU_TYPE.DREGION; 0 -> UNSUPPORTED. */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] UNSUPPORTED[r] */
SPL_Status MPU_GetRegionCount(uint32_t *count);
/* PARAM: number >= DREGION, size out of range or base not aligned, srd = 0xFF or > 0xFF, type /
 * access unknown, execute with Strongly-ordered / Device, Normal overlapping the peripheral area.
 * MPU enabled -> STATE. Writes RNR, RBAR (VALID = 0), RASR. Nothing written on refusal. */
/* Caller: MPU_Disable first; overlap with other regions is not checked (the higher region number
 * wins). */
/* Level: PARAM[F] STATE[F] UNSUPPORTED[r] */
SPL_Status MPU_ConfigRegion(const MPU_RegionTypeDef *cfg);
/* RASR = 0 for the region (ENABLE = 0). number >= DREGION -> PARAM; MPU enabled -> STATE. */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] STATE[F] UNSUPPORTED[r] */
SPL_Status MPU_DisableRegion(uint32_t number);
/* CTRL = ENABLE | PRIVDEFENA (privileged accesses outside every region use the default map) |
 * HFNMIENA (MPU stays active in HardFault / NMI). Already enabled -> STATE. Barriers DSB + ISB. */
/* Caller: with privileged_default = 0 the regions must cover code, stack, vector table and used
 * peripherals, else the next access faults; unprivileged code always needs a region. */
/* Level: PARAM[F] STATE[F] UNSUPPORTED[r] */
SPL_Status MPU_Enable(uint8_t privileged_default, uint8_t in_fault_handlers);
/* CTRL = 0. Already disabled: SPL_OK without a write. */
/* Caller: none beyond the file-header duties. */
/* Level: - */
SPL_Status MPU_Disable(void);
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] UNSUPPORTED[r] */
SPL_Status MPU_IsEnabled(uint32_t *enabled);

#ifdef __cplusplus
}
#endif
#endif
