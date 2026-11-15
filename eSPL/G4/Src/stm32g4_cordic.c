#define SPL_LIBRARY_BUILD 1
#include <stddef.h>
#include "stm32g4_cordic.h"

/* RM0440 Rev 9 section 17. Contract: Docs/v0.13_CORDIC_CONTRACT.md. */

#define CORDIC_CSR_RW_BITS                                                                         \
    (CORDIC_CSR_FUNC_Msk | CORDIC_CSR_PRECISION_Msk | CORDIC_CSR_SCALE_Msk | CORDIC_CSR_IEN |      \
     CORDIC_CSR_DMAREN | CORDIC_CSR_DMAWEN | CORDIC_CSR_NRES | CORDIC_CSR_NARGS |                  \
     CORDIC_CSR_RESSIZE | CORDIC_CSR_ARGSIZE)
#define CORDIC_CSR_DMA_BITS (CORDIC_CSR_DMAREN | CORDIC_CSR_DMAWEN)
#define CORDIC_MAX_OUTSTANDING 2U
#define CORDIC_PRECISION_MAX 15U

/* q1.31 bounds (inclusive) derived from RM0440 Tables 111-117; see the
 * contract for the rounding rule (lower = ceil(L*2^31), "< U" = ceil-1,
 * "<= U" = floor(U*2^31)). */
#define CORDIC_HYP_ABS_MAX 1200443359  /* 0.559 */
#define CORDIC_ATANH_ABS_MAX 865435910 /* 0.403 */
#define CORDIC_LN1_MIN 114890376       /* 0.0535 <= A */
#define CORDIC_LN1_MAX 1073741823      /* A < 0.5 */
#define CORDIC_LN2_MIN 536870912       /* 0.25 */
#define CORDIC_LN2_MAX 1610612735      /* < 0.75 */
#define CORDIC_LN3_MIN 805306368       /* 0.375 */
#define CORDIC_LN3_MAX 1879048191      /* < 0.875 */
#define CORDIC_LN4_MIN 939524096       /* 0.4375 */
#define CORDIC_LN4_MAX 1254130450      /* < 0.584 */
#define CORDIC_SQRT0_MIN 57982059      /* 0.027 */
#define CORDIC_SQRT0_MAX 1610612735    /* < 0.75 */
#define CORDIC_SQRT1_MIN 805306368     /* 0.375 */
#define CORDIC_SQRT1_MAX 1879048191    /* < 0.875 */
#define CORDIC_SQRT2_MIN 939524096     /* 0.4375 */
#define CORDIC_SQRT2_MAX 1256277934    /* <= 0.585 */

/* Decoded CSR / config fields (0/1 flags for sizes, NARGS, NRES). */
typedef struct
{
    uint32_t function;
    uint32_t precision;
    uint32_t scale;
    uint32_t arg16;
    uint32_t res16;
    uint32_t nargs;
    uint32_t nres;
} Cordic_Fields;

static uint8_t Cordic_TwoArgs(uint32_t function)
{
    return (function <= CORDIC_FUNC_MODULUS) ? 1U : 0U;
}

static uint8_t Cordic_TwoResults(uint32_t function)
{
    uint8_t two = 0U;
    if ((function <= CORDIC_FUNC_MODULUS) || (function == CORDIC_FUNC_HCOSINE) ||
        (function == CORDIC_FUNC_HSINE))
    {
        two = 1U;
    }
    return two;
}

/* RM0440 17.3.2: SCALE per function. */
static uint8_t Cordic_ScaleValid(uint32_t function, uint32_t scale)
{
    uint8_t ok;
    switch (function)
    {
    case CORDIC_FUNC_ARCTANGENT:
        ok = (scale <= 7U) ? 1U : 0U;
        break;
    case CORDIC_FUNC_HCOSINE:
    case CORDIC_FUNC_HSINE:
    case CORDIC_FUNC_HARCTANGENT:
        ok = (scale == 1U) ? 1U : 0U;
        break;
    case CORDIC_FUNC_NATURALLOG:
        ok = ((scale >= 1U) && (scale <= 4U)) ? 1U : 0U;
        break;
    case CORDIC_FUNC_SQUAREROOT:
        ok = (scale <= 2U) ? 1U : 0U;
        break;
    default:
        ok = (scale == 0U) ? 1U : 0U;
        break;
    }
    return ok;
}

static uint8_t Cordic_FieldsValid(const Cordic_Fields *f)
{
    if ((f->function > CORDIC_FUNC_SQUAREROOT) || (f->precision == 0U) ||
        (f->precision > CORDIC_PRECISION_MAX) || (f->arg16 > 1U) || (f->res16 > 1U) ||
        (f->nargs > 1U) || (f->nres > 1U))
    {
        return 0U;
    }
    if (Cordic_ScaleValid(f->function, f->scale) == 0U)
    {
        return 0U;
    }
    /* RM0440 17.4.2 / 17.4.3: NARGS/NRES must be 0 with 16-bit data.
     * Driver choices S2/S3: no second write/read for one-argument/one-result
     * functions. */
    if ((f->nargs == 1U) && ((f->arg16 == 1U) || (Cordic_TwoArgs(f->function) == 0U)))
    {
        return 0U;
    }
    if ((f->nres == 1U) && ((f->res16 == 1U) || (Cordic_TwoResults(f->function) == 0U)))
    {
        return 0U;
    }
    return 1U;
}

static void Cordic_DecodeCsr(uint32_t csr, Cordic_Fields *f)
{
    f->function = (csr & CORDIC_CSR_FUNC_Msk) >> CORDIC_CSR_FUNC_Pos;
    f->precision = (csr & CORDIC_CSR_PRECISION_Msk) >> CORDIC_CSR_PRECISION_Pos;
    f->scale = (csr & CORDIC_CSR_SCALE_Msk) >> CORDIC_CSR_SCALE_Pos;
    f->arg16 = (csr & CORDIC_CSR_ARGSIZE_Msk) >> CORDIC_CSR_ARGSIZE_Pos;
    f->res16 = (csr & CORDIC_CSR_RESSIZE_Msk) >> CORDIC_CSR_RESSIZE_Pos;
    f->nargs = (csr & CORDIC_CSR_NARGS_Msk) >> CORDIC_CSR_NARGS_Pos;
    f->nres = (csr & CORDIC_CSR_NRES_Msk) >> CORDIC_CSR_NRES_Pos;
}

/* Two's complement word -> int32_t without implementation-defined conversion. */
static int32_t Cordic_ToQ31(uint32_t word)
{
    int32_t value;
    if (word < 0x80000000U)
    {
        value = (int32_t)word;
    }
    else
    {
        uint32_t magnitude = ~word; /* 0..0x7FFFFFFF */
        value = -(int32_t)magnitude - 1;
    }
    return value;
}

static uint8_t Cordic_InRange(int32_t value, int32_t min, int32_t max)
{
    return ((value >= min) && (value <= max)) ? 1U : 0U;
}

/* Argument ranges of RM0440 Tables 106-117. has_arg2: ARG2 is supplied. */
static uint8_t Cordic_ArgsValid(uint32_t function, uint32_t scale, int32_t arg1, int32_t arg2,
                                uint8_t has_arg2)
{
    static const int32_t ln_min[4] = {CORDIC_LN1_MIN, CORDIC_LN2_MIN, CORDIC_LN3_MIN,
                                      CORDIC_LN4_MIN};
    static const int32_t ln_max[4] = {CORDIC_LN1_MAX, CORDIC_LN2_MAX, CORDIC_LN3_MAX,
                                      CORDIC_LN4_MAX};
    static const int32_t sq_min[3] = {CORDIC_SQRT0_MIN, CORDIC_SQRT1_MIN, CORDIC_SQRT2_MIN};
    static const int32_t sq_max[3] = {CORDIC_SQRT0_MAX, CORDIC_SQRT1_MAX, CORDIC_SQRT2_MAX};
    uint8_t ok;

    switch (function)
    {
    case CORDIC_FUNC_COSINE:
    case CORDIC_FUNC_SINE:
        /* Tables 106/107: modulus m in [0, 1]. */
        ok = ((has_arg2 == 0U) || (arg2 >= 0)) ? 1U : 0U;
        break;
    case CORDIC_FUNC_HCOSINE:
    case CORDIC_FUNC_HSINE:
        ok = Cordic_InRange(arg1, -CORDIC_HYP_ABS_MAX, CORDIC_HYP_ABS_MAX);
        break;
    case CORDIC_FUNC_HARCTANGENT:
        ok = Cordic_InRange(arg1, -CORDIC_ATANH_ABS_MAX, CORDIC_ATANH_ABS_MAX);
        break;
    case CORDIC_FUNC_NATURALLOG:
        /* scale 1..4, checked by Cordic_FieldsValid. */
        ok = Cordic_InRange(arg1, ln_min[scale - 1U], ln_max[scale - 1U]);
        break;
    case CORDIC_FUNC_SQUAREROOT:
        /* scale 0..2, checked by Cordic_FieldsValid. */
        ok = Cordic_InRange(arg1, sq_min[scale], sq_max[scale]);
        break;
    default:
        /* Phase, modulus, arctangent: whole q1.31 range [-1, 1]. */
        ok = 1U;
        break;
    }
    return ok;
}

SPL_Status CORDIC_ContextInit(CORDIC_ContextTypeDef *ctx)
{
    /* SAFE: NULL context cannot hold outstanding count. */
    SPL_REQUIRE_SAFE(ctx != NULL, SPL_ERR_PARAM);
    ctx->outstanding = 0U;
    return SPL_OK;
}

SPL_Status CORDIC_Config(const CORDIC_ContextTypeDef *ctx, const CORDIC_ConfigTypeDef *config)
{
    Cordic_Fields f;
    uint32_t csr;

    /* SAFE: NULL context. */
    SPL_REQUIRE_SAFE(ctx != NULL, SPL_ERR_PARAM);
    /* SAFE: NULL config. */
    SPL_REQUIRE_SAFE(config != NULL, SPL_ERR_PARAM);
    /* SAFE: Argument width must fit CSR. */
    SPL_REQUIRE_SAFE(config->arg_size <= CORDIC_SIZE_16BIT, SPL_ERR_PARAM);
    /* SAFE: Result width must fit CSR. */
    SPL_REQUIRE_SAFE(config->res_size <= CORDIC_SIZE_16BIT, SPL_ERR_PARAM);
    /* SAFE: Number of writes is encoded as writes-1. */
    SPL_REQUIRE_SAFE(config->arg_writes >= 1U && config->arg_writes <= 2U, SPL_ERR_PARAM);
    /* SAFE: Number of reads is encoded as reads-1. */
    SPL_REQUIRE_SAFE(config->res_reads >= 1U && config->res_reads <= 2U, SPL_ERR_PARAM);
    f.function = config->function;
    f.precision = config->precision;
    f.scale = config->scale;
    f.arg16 = config->arg_size;
    f.res16 = config->res_size;
    f.nargs = config->arg_writes - 1U;
    f.nres = config->res_reads - 1U;
    /* SAFE: Invalid function/scale/width combination in CSR. */
    SPL_REQUIRE_SAFE(Cordic_FieldsValid(&f) != 0U, SPL_ERR_PARAM);

    csr = CORDIC->CSR;
    /* RM0440 17.3.9: no configuration change during DMA. Driver choice S1:
     * no change while a result is unread or a calculation is outstanding. */
    if (((csr & (CORDIC_CSR_DMA_BITS | CORDIC_CSR_RRDY)) != 0U) || (ctx->outstanding != 0U))
    {
        return SPL_ERR_STATE;
    }

    CORDIC->CSR = (csr & CORDIC_CSR_IEN) | (f.function << CORDIC_CSR_FUNC_Pos) |
                  (f.precision << CORDIC_CSR_PRECISION_Pos) | (f.scale << CORDIC_CSR_SCALE_Pos) |
                  (f.nargs << CORDIC_CSR_NARGS_Pos) | (f.nres << CORDIC_CSR_NRES_Pos) |
                  (f.arg16 << CORDIC_CSR_ARGSIZE_Pos) | (f.res16 << CORDIC_CSR_RESSIZE_Pos);
    return SPL_OK;
}

SPL_Status CORDIC_InterruptConfig(uint8_t enable)
{
    uint32_t csr;

    /* SAFE: Boolean value outside the documented range. */
    SPL_REQUIRE_SAFE(enable <= 1U, SPL_ERR_PARAM);
    /* RM0440 17.3.7: CSR may change during a calculation. RRDY is read-only
     * and masked out together with the reserved bits. */
    csr = CORDIC->CSR & (CORDIC_CSR_RW_BITS & ~CORDIC_CSR_IEN);
    if (enable != 0U)
    {
        csr |= CORDIC_CSR_IEN;
    }
    CORDIC->CSR = csr;
    return SPL_OK;
}

SPL_Status CORDIC_DmaConfig(const CORDIC_ContextTypeDef *ctx, uint8_t read_enable,
                            uint8_t write_enable)
{
    uint32_t csr;
    uint32_t dma = 0U;

    /* SAFE: NULL context. */
    SPL_REQUIRE_SAFE(ctx != NULL, SPL_ERR_PARAM);
    /* SAFE: Invalid DMA read bit. */
    SPL_REQUIRE_SAFE(read_enable <= 1U, SPL_ERR_PARAM);
    /* SAFE: Invalid DMA write bit. */
    SPL_REQUIRE_SAFE(write_enable <= 1U, SPL_ERR_PARAM);
    if (read_enable != 0U)
    {
        dma |= CORDIC_CSR_DMAREN;
    }
    if (write_enable != 0U)
    {
        dma |= CORDIC_CSR_DMAWEN;
    }
    csr = CORDIC->CSR;
    /* Entering DMA mode: the CPU path must be idle, else the DMA would take
     * results or arguments that belong to CPU calculations. */
    if (((read_enable != 0U) || (write_enable != 0U)) && ((csr & CORDIC_CSR_DMA_BITS) == 0U) &&
        (((csr & CORDIC_CSR_RRDY) != 0U) || (ctx->outstanding != 0U)))
    {
        return SPL_ERR_STATE;
    }
    CORDIC->CSR = (csr & (CORDIC_CSR_RW_BITS & ~CORDIC_CSR_DMA_BITS)) | dma;
    return SPL_OK;
}

SPL_Status CORDIC_TryWrite(CORDIC_ContextTypeDef *ctx, uint32_t wdata1, uint32_t wdata2)
{
    Cordic_Fields f;
    uint32_t csr;
    int32_t arg1;
    int32_t arg2;
    uint8_t has_arg2;

    /* SAFE: NULL context. */
    SPL_REQUIRE_SAFE(ctx != NULL, SPL_ERR_PARAM);
    csr = CORDIC->CSR;
    Cordic_DecodeCsr(csr, &f);
    /* RM0440 17.3.9 note: no CPU access to WDATA with DMA enabled.
     * RM0440 17.3.7: a write while an operation is pending cancels it. */
    if (((csr & CORDIC_CSR_DMA_BITS) != 0U) || (ctx->outstanding >= CORDIC_MAX_OUTSTANDING) ||
        (Cordic_FieldsValid(&f) == 0U))
    {
        return SPL_ERR_STATE;
    }

    if (f.arg16 != 0U)
    {
        /* RM0440 17.4.2: ARG1 in [15:0], ARG2 in [31:16]. */
        arg1 = Cordic_ToQ31((wdata1 & 0x0000FFFFU) << 16U);
        arg2 = Cordic_ToQ31(wdata1 & 0xFFFF0000U);
        has_arg2 = Cordic_TwoArgs(f.function);
    }
    else
    {
        arg1 = Cordic_ToQ31(wdata1);
        arg2 = Cordic_ToQ31(wdata2);
        has_arg2 = (uint8_t)f.nargs;
    }
    /* SAFE: Argument outside supported CORDIC mathematical domain. */
    SPL_REQUIRE_SAFE(Cordic_ArgsValid(f.function, f.scale, arg1, arg2, has_arg2) != 0U, SPL_ERR_PARAM);

    /* RM0440 17.3.6: ARG1 first, then ARG2; the last write starts or queues
     * the calculation. */
    CORDIC->WDATA = wdata1;
    if ((f.arg16 == 0U) && (f.nargs != 0U))
    {
        CORDIC->WDATA = wdata2;
    }
    ctx->outstanding++;
    return SPL_OK;
}

SPL_Status CORDIC_TryRead(CORDIC_ContextTypeDef *ctx, uint32_t *rdata1, uint32_t *rdata2)
{
    uint32_t csr;
    uint32_t res1;

    /* SAFE: NULL context. */
    SPL_REQUIRE_SAFE(ctx != NULL, SPL_ERR_PARAM);
    /* SAFE: NULL output pointer. */
    SPL_REQUIRE_SAFE(rdata1 != NULL, SPL_ERR_PARAM);
    /* CSR reads never stall (RM0440 17.3.7 polling mode). */
    csr = CORDIC->CSR;
    /* SAFE: Second output must be present when NRES is set. */
    SPL_REQUIRE_SAFE(((csr & CORDIC_CSR_NRES) == 0U) || (rdata2 != NULL), SPL_ERR_PARAM);
    /* RM0440 17.3.9 note: no CPU access to RDATA with DMA enabled. */
    if ((csr & CORDIC_CSR_DMA_BITS) != 0U)
    {
        return SPL_ERR_STATE;
    }
    if ((csr & CORDIC_CSR_RRDY) == 0U)
    {
        /* Never read RDATA here: that inserts bus wait states until the
         * calculation completes (RM0440 17.3.7). */
        if (ctx->outstanding == 0U)
        {
            return SPL_ERR_STATE;
        }
        return SPL_WOULD_BLOCK;
    }

    /* RM0440 17.4.3: RES1 first; with NRES=1 the second read gives RES2 and
     * clears RRDY. */
    res1 = CORDIC->RDATA;
    if ((csr & CORDIC_CSR_NRES) != 0U)
    {
        uint32_t res2 = CORDIC->RDATA;
        *rdata2 = res2;
    }
    *rdata1 = res1;
    if (ctx->outstanding != 0U)
    {
        ctx->outstanding--;
    }
    return SPL_OK;
}

SPL_Status CORDIC_PackQ15(int16_t arg1, int16_t arg2, uint32_t *wdata)
{
    /* SAFE: NULL output pointer. */
    SPL_REQUIRE_SAFE(wdata != NULL, SPL_ERR_PARAM);
    /* Signed -> unsigned conversion is defined (modulo 2^16). */
    *wdata = (uint32_t)(uint16_t)arg1 | ((uint32_t)(uint16_t)arg2 << 16U);
    return SPL_OK;
}

static int16_t Cordic_HalfToQ15(uint32_t half)
{
    int32_t value = (int32_t)half;
    if (half >= 0x8000U)
    {
        value -= 65536;
    }
    return (int16_t)value;
}

SPL_Status CORDIC_UnpackQ15(uint32_t rdata, int16_t *res1, int16_t *res2)
{
    /* SAFE: At least one result pointer is required. */
    SPL_REQUIRE_SAFE((res1 != NULL) || (res2 != NULL), SPL_ERR_PARAM);
    if (res1 != NULL)
    {
        *res1 = Cordic_HalfToQ15(rdata & 0x0000FFFFU);
    }
    if (res2 != NULL)
    {
        *res2 = Cordic_HalfToQ15(rdata >> 16U);
    }
    return SPL_OK;
}
