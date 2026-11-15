/* Cortex-M0+ MPU driver (v0.9), adapted from STM32H7 SPL Src/stm32h7_mpu.c (Armv7-M) for Armv6-M.
 * Docs/v0.9_MPU_CONTRACT.md. */
/* Kutuphane birimi: spl_check.h karisma korumasi referansini bu birim de uretir; uretmeyen tek
 * birim stm32g0_check.c (SPL_CHECK_LEVEL_DEFINER). BULGU-02, CHECK_LEVEL.md 3.2. */
#define SPL_LIBRARY_BUILD 1
#include <stddef.h>
#include "stm32g0_mpu.h"

#define MPU_PERIPH_FIRST 0x40000000UL /* RM0444 Figure 2 memory map: APB, AHB, IOPORT */
#define MPU_PERIPH_LAST 0x5FFFFFFFUL
#define MPU_SRD_ALL 0xFFU

/* Denetim seviyesi (A4 dalga 5, Docs/CHECK_LEVEL.md 5.4 MPU tablosu): SPL_REQUIRE = normal
 * sinif (yalniz FULL), SPL_REQUIRE_SAFE = SAFE sinif (FULL ve MIX). DREGION = 0 (UNSUPPORTED)
 * donanimin cevabi, result: her seviyede. Yalniz denetimde kullanilan yardimcilar static inline. */

/* S / C / B per MPU_MemTypeDef; TEX always 000 on Armv6-M. */
static const uint32_t mpu_attr[3] = {0U, MPU_RASR_S_Msk | MPU_RASR_B_Msk,
                                     MPU_RASR_C_Msk | MPU_RASR_B_Msk};

static uint32_t Mpu_RegionCount(void)
{
    return (uint32_t)((MPU->TYPE & MPU_TYPE_DREGION_Msk) >> MPU_TYPE_DREGION_Pos);
}

static uint32_t Mpu_IsOn(void)
{
    return ((MPU->CTRL & MPU_CTRL_ENABLE_Msk) != 0U) ? 1U : 0U;
}

static inline uint32_t Mpu_AccessValid(uint32_t ap)
{
    return ((ap <= (uint32_t)MPU_AP_FULL) || (ap == (uint32_t)MPU_AP_PRIV_RO) ||
            (ap == (uint32_t)MPU_AP_RO))
               ? 1U
               : 0U;
}

/* SAFE: bolge numarasi, boyut, hiza, alt bolge, tip, erisim kodu, mimari kurallar. A4: `execute`
 * > 1 normal sinif, MPU_ConfigRegion'da ayri (Device / Strongly-ordered kurali `!= 0` ile). */
static inline uint32_t Mpu_RegionValid(const MPU_RegionTypeDef *cfg, uint32_t n)
{
    const uint32_t type = (uint32_t)cfg->type;
    uint32_t len_m1;
    uint32_t last;

    if ((cfg->number >= n) || (cfg->size < MPU_SIZE_MIN) || (cfg->size > MPU_SIZE_MAX))
    {
        return 0U;
    }
    /* Region length - 1 without overflow: 2U << 31 == 0 -> 0xFFFFFFFF for 4 GB. */
    len_m1 = ((uint32_t)2U << cfg->size) - 1U;
    if ((cfg->base & len_m1) != 0U)
    {
        return 0U;
    }
    last = cfg->base + len_m1; /* base aligned: no wrap */
    if ((cfg->srd >= MPU_SRD_ALL) || (type > (uint32_t)MPU_MEM_NORMAL) ||
        (Mpu_AccessValid((uint32_t)cfg->access) == 0U))
    {
        return 0U;
    }
    if ((type != (uint32_t)MPU_MEM_NORMAL) && (cfg->execute != 0U))
    {
        return 0U; /* Strongly-ordered / Device: execute never */
    }
    if ((type == (uint32_t)MPU_MEM_NORMAL) && (cfg->base <= MPU_PERIPH_LAST) &&
        (last >= MPU_PERIPH_FIRST))
    {
        return 0U; /* METHOD 1.7: peripherals are never made Normal */
    }
    return 1U;
}

SPL_Status MPU_GetRegionCount(uint32_t *count)
{
    uint32_t n;

    SPL_REQUIRE_SAFE(count != NULL, SPL_ERR_PARAM);
    n = Mpu_RegionCount();
    if (n == 0U)
    {
        return SPL_ERR_UNSUPPORTED;
    }
    *count = n;
    return SPL_OK;
}

SPL_Status MPU_ConfigRegion(const MPU_RegionTypeDef *cfg)
{
    uint32_t n;

    SPL_REQUIRE_SAFE(cfg != NULL, SPL_ERR_PARAM);
    n = Mpu_RegionCount();
    if (n == 0U)
    {
        return SPL_ERR_UNSUPPORTED; /* result: donanimin cevabi */
    }
    SPL_REQUIRE(cfg->execute <= 1U, SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(Mpu_RegionValid(cfg, n) != 0U, SPL_ERR_PARAM); /* SAFE */
    /* SAFE: calisirken bolge degisir (gecici yanlis izin). */
    SPL_REQUIRE_SAFE(Mpu_IsOn() == 0U, SPL_ERR_STATE);
    MPU->RNR = cfg->number;
    MPU->RBAR = cfg->base & MPU_RBAR_ADDR_Msk; /* VALID = 0: region from RNR */
    MPU->RASR = ((cfg->execute != 0U) ? 0U : MPU_RASR_XN_Msk) |
                ((uint32_t)cfg->access << MPU_RASR_AP_Pos) | mpu_attr[(uint32_t)cfg->type] |
                (cfg->srd << MPU_RASR_SRD_Pos) | (cfg->size << MPU_RASR_SIZE_Pos) |
                MPU_RASR_ENABLE_Msk;
    return SPL_OK;
}

SPL_Status MPU_DisableRegion(uint32_t number)
{
    const uint32_t n = Mpu_RegionCount();

    if (n == 0U)
    {
        return SPL_ERR_UNSUPPORTED;
    }
    SPL_REQUIRE_SAFE(number < n, SPL_ERR_PARAM);       /* SAFE: baska bolge */
    SPL_REQUIRE_SAFE(Mpu_IsOn() == 0U, SPL_ERR_STATE); /* SAFE */
    MPU->RNR = number;
    MPU->RASR = 0U;
    return SPL_OK;
}

SPL_Status MPU_Enable(uint8_t privileged_default, uint8_t in_fault_handlers)
{
    uint32_t ctrl = MPU_CTRL_ENABLE_Msk;

    SPL_REQUIRE((privileged_default <= 1U) && (in_fault_handlers <= 1U), SPL_ERR_PARAM);
    if (Mpu_RegionCount() == 0U)
    {
        return SPL_ERR_UNSUPPORTED; /* result: donanimin cevabi */
    }
    /* normal: CTRL istenen degerle yeniden yazilir (zararsiz). */
    SPL_REQUIRE(Mpu_IsOn() == 0U, SPL_ERR_STATE);
    if (privileged_default != 0U)
    {
        ctrl |= MPU_CTRL_PRIVDEFENA_Msk;
    }
    if (in_fault_handlers != 0U)
    {
        ctrl |= MPU_CTRL_HFNMIENA_Msk;
    }
    __DMB();
    MPU->CTRL = ctrl;
    __DSB();
    __ISB();
    return SPL_OK;
}

SPL_Status MPU_Disable(void)
{
    if (Mpu_IsOn() == 0U)
    {
        return SPL_OK;
    }
    __DMB();
    MPU->CTRL = 0U;
    __DSB();
    __ISB();
    return SPL_OK;
}

SPL_Status MPU_IsEnabled(uint32_t *enabled)
{
    SPL_REQUIRE_SAFE(enabled != NULL, SPL_ERR_PARAM);
    if (Mpu_RegionCount() == 0U)
    {
        return SPL_ERR_UNSUPPORTED;
    }
    *enabled = Mpu_IsOn();
    return SPL_OK;
}
