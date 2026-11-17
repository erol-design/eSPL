/* SPDX-License-Identifier: MIT */
/* AES driver implementation for STM32G0 (RM0444 Rev 6 section 20), new in v0.9; compiled only
 * when SPL_G0_HAS_AES. Public contract in stm32g0_aes.h and Docs/v0.9_AES_CONTRACT.md. */
/* Library unit: this unit also emits the spl_check.h mismatch-guard reference; the only unit
 * that does not is stm32g0_check.c (SPL_CHECK_LEVEL_DEFINER). BULGU-02, CHECK_LEVEL.md 3.2. */
#define SPL_LIBRARY_BUILD 1
#include <stddef.h>
#include "stm32g0_aes.h"

#if SPL_G0_HAS_AES

/* Check level (A4 wave 5, Docs/CHECK_LEVEL.md 5.4 AES table): SPL_REQUIRE = checked at FULL only
 * (not compiled at FAST). Error flags, no CCF and BUSY result: at every level. */

/* Write-1 command bits: never copied back by a read-modify-write of CR (they read as 0 on
 * hardware, but a stale 1 would clear CCF / the error flags unintentionally). */
#define AES_CR_COMMANDS (AES_CR_CCFC | AES_CR_ERRC)
#define AES_CR_DMA (AES_CR_DMAINEN | AES_CR_DMAOUTEN)
#define AES_CR_CONFIG                                                                              \
    (AES_CR_DATATYPE_Msk | AES_CR_MODE_Msk | AES_CR_CHMOD_Msk | AES_CR_KEYSIZE_Msk |               \
     AES_CR_GCMPH_Msk | AES_CR_NPBLB_Msk | AES_CR_EN)

static uint32_t Aes_Cr(void)
{
    return AES->CR & ~AES_CR_COMMANDS;
}

/* CHMOD[2:0] is split: bits 6:5 = CHMOD[1:0], bit 16 = CHMOD[2] (RM0444 20.7.1). */
static uint32_t Aes_ChainBits(uint32_t chain)
{
    return ((chain & 3U) << AES_CR_CHMOD_Pos) | (((chain & 4U) != 0U) ? AES_CR_CHMOD_2 : 0U);
}

static uint32_t Aes_Chain(uint32_t cr)
{
    return ((cr >> AES_CR_CHMOD_Pos) & 3U) | (((cr & AES_CR_CHMOD_2) != 0U) ? 4U : 0U);
}

static uint32_t Aes_Mode(uint32_t cr)
{
    return (cr & AES_CR_MODE_Msk) >> AES_CR_MODE_Pos;
}

static inline uint8_t Aes_IsGcmCcm(uint32_t chain)
{
    return ((chain == (uint32_t)AES_CHAIN_GCM) || (chain == (uint32_t)AES_CHAIN_CCM)) ? 1U : 0U;
}

SPL_Status AES_Config(const AES_ConfigTypeDef *cfg)
{
    uint32_t cr;
    uint32_t chain;
    uint32_t mode;

    SPL_REQUIRE(cfg != NULL, SPL_ERR_PARAM);
    /* reserved CHMOD / MODE / DATATYPE code, overflow. */
    SPL_REQUIRE((cfg->chaining <= (uint8_t)AES_CHAIN_CCM) &&
                    (cfg->mode <= (uint8_t)AES_MODE_KEY_DERIVATION_DECRYPT) &&
                    (cfg->data_type <= (uint8_t)AES_DATATYPE_1BIT),
                SPL_ERR_PARAM);
    SPL_REQUIRE(cfg->key_256 <= 1U, SPL_ERR_PARAM);
    chain = cfg->chaining;
    mode = cfg->mode;
    cr = Aes_Cr();
    /* Mode 4 outside ECB / CBC silently becomes Mode 3 in hardware (RM0444 20.7.1). GCM / CCM:
     * MODE 01 and 11 are forbidden (RM0444 Table 109 note, 20.4.12). DMA has no effect in Mode 2
     * and is not recommended in Mode 4 (RM0444 20.7.1). */
    SPL_REQUIRE(((mode != (uint32_t)AES_MODE_KEY_DERIVATION_DECRYPT) ||
                 (chain <= (uint32_t)AES_CHAIN_CBC)) &&
                    ((Aes_IsGcmCcm(chain) == 0U) || ((mode & 1U) == 0U)) &&
                    (((mode & 1U) == 0U) || ((cr & AES_CR_DMA) == 0U)),
                SPL_ERR_PARAM);
    SPL_REQUIRE((cr & AES_CR_EN) == 0U, SPL_ERR_STATE); /* running job overwritten */
    cr &= ~AES_CR_CONFIG;
    cr |= ((uint32_t)cfg->data_type << AES_CR_DATATYPE_Pos) | (mode << AES_CR_MODE_Pos) |
          Aes_ChainBits(chain);
    if (cfg->key_256 != 0U)
    {
        cr |= AES_CR_KEYSIZE;
    }
    AES->CR = cr;
    return SPL_OK;
}

SPL_Status AES_SetKey(const uint32_t *key, uint32_t words)
{
    const uint32_t cr = Aes_Cr();
    const uint32_t need = ((cr & AES_CR_KEYSIZE) != 0U) ? 8U : 4U;

    SPL_REQUIRE(key != NULL, SPL_ERR_PARAM);
    SPL_REQUIRE(words == need, SPL_ERR_PARAM);          /* read outside the array */
    SPL_REQUIRE((cr & AES_CR_EN) == 0U, SPL_ERR_STATE); /* the key is overwritten */
    AES->KEYR0 = key[0];
    AES->KEYR1 = key[1];
    AES->KEYR2 = key[2];
    AES->KEYR3 = key[3];
    if (need == 8U)
    {
        AES->KEYR4 = key[4];
        AES->KEYR5 = key[5];
        AES->KEYR6 = key[6];
        AES->KEYR7 = key[7];
    }
    return SPL_OK;
}

SPL_Status AES_SetIv(const uint32_t iv[4])
{
    SPL_REQUIRE(iv != NULL, SPL_ERR_PARAM);
    SPL_REQUIRE((AES->CR & AES_CR_EN) == 0U, SPL_ERR_STATE); /* the IV is overwritten */
    AES->IVR0 = iv[0];
    AES->IVR1 = iv[1];
    AES->IVR2 = iv[2];
    AES->IVR3 = iv[3];
    return SPL_OK;
}

SPL_Status AES_GetIv(uint32_t iv[4])
{
    SPL_REQUIRE(iv != NULL, SPL_ERR_PARAM);
    iv[0] = AES->IVR0;
    iv[1] = AES->IVR1;
    iv[2] = AES->IVR2;
    iv[3] = AES->IVR3;
    return SPL_OK;
}

SPL_Status AES_Enable(void)
{
    AES->CR = Aes_Cr() | AES_CR_EN;
    return SPL_OK;
}

SPL_Status AES_Disable(void)
{
    AES->CR = Aes_Cr() & ~AES_CR_EN;
    return SPL_OK;
}

SPL_Status AES_GetEnabled(uint8_t *enabled)
{
    SPL_REQUIRE(enabled != NULL, SPL_ERR_PARAM);
    *enabled = ((AES->CR & AES_CR_EN) != 0U) ? 1U : 0U;
    return SPL_OK;
}

SPL_Status AES_SetPhase(AES_PhaseTypeDef phase)
{
    const uint32_t cr = Aes_Cr();

    /* GCMPH overflow / the phase is silently not used. */
    SPL_REQUIRE(((uint32_t)phase <= (uint32_t)AES_PHASE_FINAL) &&
                    (Aes_IsGcmCcm(Aes_Chain(cr)) != 0U),
                SPL_ERR_PARAM);
    AES->CR = (cr & ~AES_CR_GCMPH_Msk) | ((uint32_t)phase << AES_CR_GCMPH_Pos);
    return SPL_OK;
}

SPL_Status AES_SetPadding(uint32_t bytes)
{
    SPL_REQUIRE(bytes <= 15U, SPL_ERR_PARAM); /* NPBLB overflow */
    AES->CR = (Aes_Cr() & ~AES_CR_NPBLB_Msk) | (bytes << AES_CR_NPBLB_Pos);
    return SPL_OK;
}

SPL_Status AES_WriteBlock(const uint32_t in[4])
{
    const uint32_t cr = AES->CR;

    SPL_REQUIRE(in != NULL, SPL_ERR_PARAM);
    /* the block is lost / enters the DMA stream / an unread result is overwritten. */
    SPL_REQUIRE(((cr & AES_CR_EN) != 0U) && ((cr & AES_CR_DMAINEN) == 0U) &&
                    ((AES->SR & AES_SR_CCF) == 0U),
                SPL_ERR_STATE);
    /* RM0444 20.4.13: most significant word first. */
    AES->DINR = in[0];
    AES->DINR = in[1];
    AES->DINR = in[2];
    AES->DINR = in[3];
    return SPL_OK;
}

/* Shared flag rules of AES_TryReadBlock / AES_TryComplete. */
static SPL_Status Aes_ResultReady(void)
{
    const uint32_t sr = AES->SR;

    if ((sr & AES_ERRORS_ALL) != 0U)
    {
        return SPL_ERR_HW;
    }
    return ((sr & AES_SR_CCF) != 0U) ? SPL_OK : SPL_WOULD_BLOCK;
}

static void Aes_ClearCcf(void)
{
    AES->CR = Aes_Cr() | AES_CR_CCFC;
    /* Read back: the clear reaches the peripheral before return (METHOD 1.7, issue #23). */
    (void)AES->SR;
}

SPL_Status AES_TryReadBlock(uint32_t out[4])
{
    SPL_Status st;
    uint32_t w0;
    uint32_t w1;
    uint32_t w2;
    uint32_t w3;

    SPL_REQUIRE(out != NULL, SPL_ERR_PARAM);
    st = Aes_ResultReady();
    if (st != SPL_OK)
    {
        return st;
    }
    w0 = AES->DOUTR;
    w1 = AES->DOUTR;
    w2 = AES->DOUTR;
    w3 = AES->DOUTR;
    Aes_ClearCcf();
    out[0] = w0;
    out[1] = w1;
    out[2] = w2;
    out[3] = w3;
    return SPL_OK;
}

SPL_Status AES_TryComplete(void)
{
    const SPL_Status st = Aes_ResultReady();

    if (st == SPL_OK)
    {
        Aes_ClearCcf();
    }
    return st;
}

SPL_Status AES_GetErrors(uint32_t *errors)
{
    SPL_REQUIRE(errors != NULL, SPL_ERR_PARAM);
    *errors = AES->SR & AES_ERRORS_ALL;
    return SPL_OK;
}

SPL_Status AES_ClearErrors(void)
{
    AES->CR = Aes_Cr() | AES_CR_ERRC;
    /* Read back: the clear reaches the peripheral before return (METHOD 1.7, issue #23). */
    (void)AES->SR;
    return SPL_OK;
}

SPL_Status AES_InterruptConfig(uint8_t ccf, uint8_t error)
{
    uint32_t cr;

    SPL_REQUIRE((ccf <= 1U) && (error <= 1U), SPL_ERR_PARAM);
    cr = Aes_Cr() & ~(AES_CR_CCFIE | AES_CR_ERRIE);
    if (ccf != 0U)
    {
        cr |= AES_CR_CCFIE;
    }
    if (error != 0U)
    {
        cr |= AES_CR_ERRIE;
    }
    AES->CR = cr;
    return SPL_OK;
}

SPL_Status AES_DmaConfig(uint8_t input, uint8_t output)
{
    uint32_t cr;

    SPL_REQUIRE((input <= 1U) && (output <= 1U), SPL_ERR_PARAM);
    cr = Aes_Cr();
    /* Mode 2: no effect; Mode 4: not recommended (RM0444 20.7.1); */
    SPL_REQUIRE((((uint32_t)input | (uint32_t)output) == 0U) || ((Aes_Mode(cr) & 1U) == 0U),
                SPL_ERR_PARAM);
    cr &= ~AES_CR_DMA;
    if (input != 0U)
    {
        cr |= AES_CR_DMAINEN;
    }
    if (output != 0U)
    {
        cr |= AES_CR_DMAOUTEN;
    }
    AES->CR = cr;
    return SPL_OK;
}

SPL_Status AES_IsBusy(uint8_t *busy)
{
    SPL_REQUIRE(busy != NULL, SPL_ERR_PARAM);
    *busy = ((AES->SR & AES_SR_BUSY) != 0U) ? 1U : 0U;
    return SPL_OK;
}

/* ---- v0.11: suspend / resume (RM0444 20.4.8, 20.4.9, 20.4.10, 20.4.12) ---- */

/* CR bits saved in a context: configuration (EN excluded) and the interrupt enables. */
#define AES_CR_CONTEXT ((AES_CR_CONFIG & ~AES_CR_EN) | AES_CR_CCFIE | AES_CR_ERRIE)

static void Aes_SuspRead(uint32_t susp[8])
{
    susp[0] = AES->SUSP0R;
    susp[1] = AES->SUSP1R;
    susp[2] = AES->SUSP2R;
    susp[3] = AES->SUSP3R;
    susp[4] = AES->SUSP4R;
    susp[5] = AES->SUSP5R;
    susp[6] = AES->SUSP6R;
    susp[7] = AES->SUSP7R;
}

static void Aes_SuspWrite(const uint32_t susp[8])
{
    AES->SUSP0R = susp[0];
    AES->SUSP1R = susp[1];
    AES->SUSP2R = susp[2];
    AES->SUSP3R = susp[3];
    AES->SUSP4R = susp[4];
    AES->SUSP5R = susp[5];
    AES->SUSP6R = susp[6];
    AES->SUSP7R = susp[7];
}

SPL_Status AES_Suspend(AES_ContextTypeDef *ctx)
{
    const uint32_t cr = Aes_Cr();
    const uint32_t sr = AES->SR;
    const uint32_t chain = Aes_Chain(cr);
    const uint32_t mode = Aes_Mode(cr);

    (void)chain;
    (void)mode;
    SPL_REQUIRE(ctx != NULL, SPL_ERR_PARAM);
    /* Mode 2 (key derivation) is not a message; ECB / CBC Mode 3 would need a new key derivation
     * on resume: use Mode 4 for a suspendable decryption (it derives the key on enable). */
    SPL_REQUIRE((mode != (uint32_t)AES_MODE_KEY_DERIVATION) &&
                    ((mode != (uint32_t)AES_MODE_DECRYPT) || (chain > (uint32_t)AES_CHAIN_CBC)),
                SPL_ERR_UNSUPPORTED);
    /* Step 1 / 2: DMA stopped by the caller (AES_DmaConfig(0, 0) after the last CCF), the last
     * result collected (CCF = 0) and no computation running. the context is saved wrongly. */
    SPL_REQUIRE(((cr & AES_CR_EN) != 0U) && ((cr & AES_CR_DMA) == 0U) && ((sr & AES_SR_CCF) == 0U),
                SPL_ERR_STATE);
    if ((sr & AES_SR_BUSY) != 0U)
    {
        return SPL_BUSY;
    }
    if ((sr & AES_ERRORS_ALL) != 0U)
    {
        return SPL_ERR_HW;
    }
    /* Steps 4 .. 7: SUSPxR, IVRx, then EN = 0 and the configuration. */
    Aes_SuspRead(ctx->susp);
    ctx->iv[0] = AES->IVR0;
    ctx->iv[1] = AES->IVR1;
    ctx->iv[2] = AES->IVR2;
    ctx->iv[3] = AES->IVR3;
    AES->CR = cr & ~AES_CR_EN;
    ctx->cr = cr & AES_CR_CONTEXT;
    return SPL_OK;
}

SPL_Status AES_Resume(const AES_ContextTypeDef *ctx, const uint32_t *key, uint32_t words)
{
    uint32_t cr;

    SPL_REQUIRE((ctx != NULL) && (key != NULL), SPL_ERR_PARAM);
    /* command / EN bits written to CR; read outside the key array. */
    SPL_REQUIRE(((ctx->cr & ~AES_CR_CONTEXT) == 0U) &&
                    (words == (((ctx->cr & AES_CR_KEYSIZE) != 0U) ? 8U : 4U)),
                SPL_ERR_PARAM);
    cr = Aes_Cr();
    (void)cr;
    /* the interrupting message is still running. */
    SPL_REQUIRE((cr & AES_CR_EN) == 0U, SPL_ERR_STATE);
    /* Resume steps 3 .. 6: CR (EN = 0, DMA off), key, IV, SUSPxR, then EN = 1. */
    AES->CR = ctx->cr;
    AES->KEYR0 = key[0];
    AES->KEYR1 = key[1];
    AES->KEYR2 = key[2];
    AES->KEYR3 = key[3];
    if (words == 8U)
    {
        AES->KEYR4 = key[4];
        AES->KEYR5 = key[5];
        AES->KEYR6 = key[6];
        AES->KEYR7 = key[7];
    }
    AES->IVR0 = ctx->iv[0];
    AES->IVR1 = ctx->iv[1];
    AES->IVR2 = ctx->iv[2];
    AES->IVR3 = ctx->iv[3];
    Aes_SuspWrite(ctx->susp);
    AES->CR = ctx->cr | AES_CR_EN;
    return SPL_OK;
}

#else
/* ISO C forbids an empty translation unit. */
typedef int stm32g0_aes_absent;
#endif /* SPL_G0_HAS_AES */
