#ifndef STM32G0_AES_H
#define STM32G0_AES_H
#include <stdint.h>
#include "stm32g0xx.h"
#include "stm32g0_status.h"
#include "stm32g0_sku.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* AES hardware accelerator driver (RM0444 Rev 6 section 20). New for G0 (the G4 SPL has no AES).
 * Scope: single instance. v0.9 SKU profile: this API exists only where the device has the
 * peripheral (SPL_G0_HAS_AES): STM32G041 / G061 / G081 / G0C1.
 * Chaining: ECB, CBC, CTR, GCM (GMAC = GCM without payload phase), CCM; key 128 or 256 bit.
 * Usage: AES_Config, AES_SetKey, AES_SetIv, (GCM / CCM: AES_SetPhase), AES_Enable, then per block
 * AES_WriteBlock and AES_TryReadBlock / AES_TryComplete; AES_Disable at the end.
 * Common rules: no static state, no waits: every wait is the caller's poll of AES_TryReadBlock /
 * AES_TryComplete / AES_GetEnabled.
 * Caller duties (all APIs; not done by the driver):
 *  - AHB clock: RCC_EnablePeriphClock(RCC_PERIPH_AES).
 *  - Message formatting: GCM ICB / CCM B0 in the IV registers (RM0444 Tables 109 / 110), CCM B1,
 *    zero padding of the last block, GCM / CCM length block in the final phase, ciphertext
 *    stealing for ECB / CBC (RM0444 20.4.6), tag masking and tag comparison.
 *  - Sequence: the procedures of RM0444 20.4.4 .. 20.4.12; the phase rules below are checked only
 *    where the register state shows the violation.
 *  - DMA channels and DMAMUX requests (AES_IN 6, AES_OUT 7, RM0444 Table 55).
 *  - Interrupts (AES_InterruptConfig): NVIC enable and handler of the AES vector.
 *  - Concurrency: serialize callers (ISR included); AES_TryReadBlock and AES_TryComplete must
 *    not run concurrently.
 * Word order (no guessing): key[i] is written to AES_KEYRi (key[0] = KEY[31:0], RM0444 Table 111);
 * iv[i] to AES_IVRi (iv[0] = IVI[31:0]); data blocks are four words in AES_DINR / AES_DOUTR
 * order, block[0] = bits [127:96] first (RM0444 20.4.13).
 * v0.11: suspend / resume (AES_Suspend / AES_Resume, below).
 * Errata: none of ES0488 Rev 7, ES0546 Rev 4, ES0412 Rev 6, ES0549 Rev 4 lists an AES item.
 * Contract: Docs/v0.9_AES_CONTRACT.md. */
#if SPL_G0_HAS_AES

typedef enum
{
    AES_CHAIN_ECB = 0,
    AES_CHAIN_CBC = 1,
    AES_CHAIN_CTR = 2,
    AES_CHAIN_GCM = 3, /* also GMAC */
    AES_CHAIN_CCM = 4
} AES_ChainTypeDef;

typedef enum
{
    AES_MODE_ENCRYPT = 0,               /* Mode 1 */
    AES_MODE_KEY_DERIVATION = 1,        /* Mode 2: decryption key preparation (ECB / CBC) */
    AES_MODE_DECRYPT = 2,               /* Mode 3 */
    AES_MODE_KEY_DERIVATION_DECRYPT = 3 /* Mode 4: ECB / CBC only */
} AES_ModeTypeDef;

typedef enum
{
    AES_DATATYPE_32BIT = 0, /* no swapping */
    AES_DATATYPE_16BIT = 1, /* half-word swapping */
    AES_DATATYPE_8BIT = 2,  /* byte swapping */
    AES_DATATYPE_1BIT = 3   /* bit swapping */
} AES_DataTypeTypeDef;

typedef enum
{
    AES_PHASE_INIT = 0,
    AES_PHASE_HEADER = 1,
    AES_PHASE_PAYLOAD = 2,
    AES_PHASE_FINAL = 3
} AES_PhaseTypeDef;

typedef struct
{
    uint8_t chaining;  /* AES_ChainTypeDef */
    uint8_t mode;      /* AES_ModeTypeDef */
    uint8_t key_256;   /* KEYSIZE: 0 = 128 bit, 1 = 256 bit */
    uint8_t data_type; /* AES_DataTypeTypeDef */
} AES_ConfigTypeDef;

/* AES_SR error flags reported by AES_GetErrors. */
#define AES_ERRORS_ALL (AES_SR_RDERR | AES_SR_WRERR)

/* EN = 1 -> STATE (RM0444 20.7.1: MODE / CHMOD / DATATYPE / KEYSIZE writes are ignored while EN =
 * 1). PARAM: chaining > CCM, mode > 3, key_256 > 1, data_type > 3; Mode 4 with other than ECB / CBC
 * (hardware would silently use Mode 3, RM0444 20.7.1); Mode 2 or 4 with GCM / CCM (forbidden,
 * RM0444 Table 109 note); Mode 2 or 4 while DMAINEN / DMAOUTEN is set (no effect / not
 * recommended, 20.7.1: call AES_DmaConfig(0, 0) first).
 * Writes CR: the fields above, GCMPH = init, NPBLB = 0, EN = 0; CCFIE / ERRIE and DMA enables kept.
 */
/* Caller: ECB / CBC decryption in Mode 3 needs a Mode 2 key derivation first (or use
 * Mode 4) (RM0444 20.4.6). */
/* Level: PARAM[F] STATE[F] */
SPL_Status AES_Config(const AES_ConfigTypeDef *cfg);
/* words = 4 (KEYSIZE = 0) or 8 (KEYSIZE = 1), else PARAM. EN = 1 -> STATE (key registers are
 * writable only with EN = 0, RM0444 20.4.14). key[i] -> AES_KEYRi. Key registers are write-only. */
/* Caller: AES_Config first (words are checked against the KEYSIZE it set). */
/* Level: PARAM[F] STATE[F] */
SPL_Status AES_SetKey(const uint32_t *key, uint32_t words);
/* EN = 1 -> STATE (writes would be ignored, RM0444 20.4.15). iv[i] -> AES_IVRi. */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] STATE[F] */
SPL_Status AES_SetIv(const uint32_t iv[4]);
/* Latest counter / IV value, iv[i] = AES_IVRi (RM0444 20.4.15: updated after each block). */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status AES_GetIv(uint32_t iv[4]);
/* EN = 1. Mode 2 (key derivation) and the GCM / CCM init phase clear EN by hardware at the end
 * (RM0444 20.7.1): poll AES_TryComplete, then AES_GetEnabled shows 0. */
/* Caller: key, IV and phase set before; after Mode 2 or the GCM / CCM init phase
 * poll AES_TryComplete until OK. */
/* Level: - */
SPL_Status AES_Enable(void);
/* EN = 0 (re-initializes the AES core state, RM0444 20.7.1). Always allowed. */
/* Caller: the message in progress is lost (use AES_Suspend to keep it); stop the
 * AES DMA channels first. */
/* Level: - */
SPL_Status AES_Disable(void);
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status AES_GetEnabled(uint8_t *enabled);
/* GCMPH = phase. Chaining other than GCM / CCM -> PARAM (field has no effect there). Allowed with
 * EN = 1 (header -> payload -> final keep EN = 1, RM0444 20.4.10 / 20.4.12). The driver does not
 * check the phase order. */
/* Caller: keep the phase order init -> header -> payload -> final (RM0444 20.4.10 /
 * 20.4.12) and collect each block before the next phase. */
/* Level: PARAM[F] */
SPL_Status AES_SetPhase(AES_PhaseTypeDef phase);
/* NPBLB = number of padding bytes of the last block, 0..15, else PARAM. Used by hardware in GCM
 * payload encryption and CCM payload decryption only (RM0444 20.4.4 step 2d); write it before the
 * last block. */
/* Caller: call before writing the last block; zero-pad that block (file header). */
/* Level: PARAM[F] */
SPL_Status AES_SetPadding(uint32_t bytes);
/* Writes in[0..3] to AES_DINR (bits [127:96] first). EN = 0 -> STATE; CCF = 1 (previous result
 * not collected: the write would raise WRERR, RM0444 20.4.17) -> STATE; DMAINEN = 1 -> STATE (DMA
 * owns the input). Nothing written on refusal. */
/* Caller: collect the result (AES_TryReadBlock / AES_TryComplete) before the next
 * block. */
/* Level: PARAM[F] STATE[F] */
SPL_Status AES_WriteBlock(const uint32_t in[4]);
/* RDERR / WRERR set -> ERR_HW (nothing read; AES_ClearErrors after diagnosis). CCF = 0 ->
 * WOULD_BLOCK. Else reads four words of AES_DOUTR into out[0..3] (bits [127:96] first) and clears
 * CCF (CCFC). out written on OK only. Payload, ECB/CBC/CTR blocks and the final-phase tag. */
/* Caller: on ERR_HW treat the message as failed; AES_ClearErrors after diagnosis.
 * Final phase: tag comparison is the caller's (file header). */
/* Level: PARAM[F] HW[r] WOULD_BLOCK[r] */
SPL_Status AES_TryReadBlock(uint32_t out[4]);
/* Same flag rules as AES_TryReadBlock, but reads no data: for key derivation (Mode 2), GCM / CCM
 * init phase and header blocks, which produce no output (RM0444 20.4.5, 20.4.10). */
/* Caller: on ERR_HW treat the message as failed; AES_ClearErrors after diagnosis. */
/* Level: HW[r] WOULD_BLOCK[r] */
SPL_Status AES_TryComplete(void);
/* *errors = AES_SR & AES_ERRORS_ALL. */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status AES_GetErrors(uint32_t *errors);
/* ERRC = 1: clears RDERR and WRERR (RM0444 20.7.1; 20.4.17 says "ERRIE" for RDERR, HW-009). */
/* Caller: none beyond the file-header duties. */
/* Level: - */
SPL_Status AES_ClearErrors(void);
/* CCFIE / ERRIE, each 0/1. */
/* Caller: the handler collects the result (clears CCF) or calls
 * AES_ClearErrors, else the interrupt repeats. */
/* Level: PARAM[F] */
SPL_Status AES_InterruptConfig(uint8_t ccf, uint8_t error);
/* DMAINEN / DMAOUTEN, each 0/1. Enabling either in Mode 2 (no effect) or Mode 4 (not recommended)
 * -> PARAM (RM0444 20.7.1). GCM encryption / CCM decryption: the last padded block must not go
 * through DMA (RM0444 20.4.4): disable DMA, AES_SetPadding, AES_WriteBlock, AES_TryReadBlock. */
/* Caller: program the DMA channels and DMAMUX requests before enabling; keep the
 * buffers valid until the DMA transfers complete. */
/* Level: PARAM[F] */
SPL_Status AES_DmaConfig(uint8_t input, uint8_t output);
/* AES_SR.BUSY (GCM payload encryption only; ignore it otherwise, RM0444 20.7.2). */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status AES_IsBusy(uint8_t *busy);

/* v0.11 suspend / resume of a message (RM0444 20.4.8 .. 20.4.12) to process a higher priority
 * one in between. The context holds CR (configuration and interrupt enables), IVRx and SUSPxR; the
 * key is not readable and is passed again to AES_Resume.
 * AES_Suspend: PARAM on NULL; Mode 2 or ECB / CBC Mode 3 -> UNSUPPORTED (resume would need a new
 * key derivation: use Mode 4); EN = 0, DMAINEN / DMAOUTEN set (caller stops DMA first and saves
 * the DMA state) or CCF = 1 (collect the result with AES_TryReadBlock / AES_TryComplete first) ->
 * STATE; BUSY -> BUSY; RDERR / WRERR -> ERR_HW. On OK: context saved, EN = 0; key registers kept
 * (caller overwrites them with the new message's key).
 * AES_Resume: EN = 1 -> STATE; key words must match the saved KEYSIZE; context CR with bits
 * outside configuration / interrupt enables -> PARAM. Writes CR, key, IV, SUSPxR, then EN = 1.
 * DMA enables are not restored: reprogram the DMA channels, then AES_DmaConfig. */
typedef struct
{
    uint32_t cr;
    uint32_t iv[4];
    uint32_t susp[8];
} AES_ContextTypeDef;
/* Caller: stop the AES DMA channels and save their state first; keep ctx and the key
 * until AES_Resume. */
/* Level: PARAM[F] STATE[F] UNSUPPORTED[F] HW[r] BUSY[r] */
SPL_Status AES_Suspend(AES_ContextTypeDef *ctx);
/* Caller: pass the key of the suspended message and the unchanged ctx from
 * AES_Suspend (only ctx->cr is checked); then reprogram DMA and AES_DmaConfig. */
/* Level: PARAM[F] STATE[F] */
SPL_Status AES_Resume(const AES_ContextTypeDef *ctx, const uint32_t *key, uint32_t words);
#endif /* SPL_G0_HAS_AES */

#ifdef __cplusplus
}
#endif
#endif
