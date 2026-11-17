/* SPDX-License-Identifier: MIT */
/* FMAC driver. RM0440 Rev 9 section 18 (FMAC). Public contract: stm32g4_fmac.h. */
#define SPL_LIBRARY_BUILD 1
#include <stddef.h>
#include "stm32g4_fmac.h"

#if !defined(FMAC)
#error "FMAC is not defined by the CMSIS device header (every STM32G4 SKU has one)"
#endif

/* Data request sources; RM0440 18.3.5 recommends them off during preload. */
#define FMAC_CR_DATA_REQUESTS (FMAC_CR_RIEN | FMAC_CR_WIEN | FMAC_CR_DMAREN | FMAC_CR_DMAWEN)
#define FMAC_CR_DMA_ALL (FMAC_CR_DMAREN | FMAC_CR_DMAWEN)

#define FMAC_FIR_P_MIN 2U
#define FMAC_FIR_P_MAX 127U
#define FMAC_IIR_P_MIN 2U
#define FMAC_IIR_P_MAX 64U
#define FMAC_IIR_Q_MIN 1U
#define FMAC_IIR_Q_MAX 63U
#define FMAC_R_MAX 7U
#define FMAC_WM_MAX 3U

static uint32_t Fmac_Field(uint32_t reg, uint32_t mask, uint32_t pos)
{
    return (reg & mask) >> pos;
}

/* FMAC_STATE_* from one CR and one PARAM snapshot. */
static uint32_t Fmac_StateOf(uint32_t cr, uint32_t param)
{
    uint32_t func = Fmac_Field(param, FMAC_PARAM_FUNC_Msk, FMAC_PARAM_FUNC_Pos);
    uint32_t state;

    if ((cr & FMAC_CR_RESET) != 0U)
    {
        state = FMAC_STATE_RESET;
    }
    else if ((param & FMAC_PARAM_START) == 0U)
    {
        state = FMAC_STATE_IDLE;
    }
    else if ((func >= FMAC_FUNC_LOAD_X1) && (func <= FMAC_FUNC_LOAD_Y))
    {
        state = FMAC_STATE_LOAD;
    }
    else if ((func == FMAC_FUNC_FIR) || (func == FMAC_FUNC_IIR))
    {
        state = FMAC_STATE_FILTER;
    }
    else
    {
        state = FMAC_STATE_UNKNOWN;
    }
    return state;
}

/* Configuration gate (START = 0 required). Reset pending or a load that
 * hardware has not finished yet: BUSY. Filter or unknown function: STATE. */
static SPL_Status Fmac_IdleGate(uint32_t state)
{
    SPL_Status status;

    if ((state == FMAC_STATE_RESET) || (state == FMAC_STATE_LOAD))
    {
        status = SPL_BUSY;
    }
    else if (state != FMAC_STATE_IDLE)
    {
        status = SPL_ERR_STATE;
    }
    else
    {
        status = SPL_OK;
    }
    return status;
}

static uint32_t Fmac_End(uint8_t base, uint8_t size)
{
    return (uint32_t)base + (uint32_t)size;
}

static uint8_t Fmac_Overlap(uint8_t baseA, uint8_t sizeA, uint8_t baseB, uint8_t sizeB)
{
    uint8_t overlap = 0U;

    if (((uint32_t)baseA < Fmac_End(baseB, sizeB)) && ((uint32_t)baseB < Fmac_End(baseA, sizeA)))
    {
        overlap = 1U;
    }
    return overlap;
}

static uint8_t Fmac_AreaValid(uint8_t base, uint8_t size)
{
    /* RM0440 18.3.2: "base address + buffer size must be less than 256". */
    return (uint8_t)(((size != 0U) && (Fmac_End(base, size) < FMAC_MEMORY_WORDS)) ? 1U : 0U);
}

static void Fmac_ReadLayout(FMAC_LayoutTypeDef *layout)
{
    uint32_t x1 = FMAC->X1BUFCFG;
    uint32_t x2 = FMAC->X2BUFCFG;
    uint32_t y = FMAC->YBUFCFG;

    layout->x1Base = (uint8_t)Fmac_Field(x1, FMAC_X1BUFCFG_X1_BASE_Msk, FMAC_X1BUFCFG_X1_BASE_Pos);
    layout->x1Size =
        (uint8_t)Fmac_Field(x1, FMAC_X1BUFCFG_X1_BUF_SIZE_Msk, FMAC_X1BUFCFG_X1_BUF_SIZE_Pos);
    layout->x1FullWm =
        (uint8_t)Fmac_Field(x1, FMAC_X1BUFCFG_FULL_WM_Msk, FMAC_X1BUFCFG_FULL_WM_Pos);
    layout->x2Base = (uint8_t)Fmac_Field(x2, FMAC_X2BUFCFG_X2_BASE_Msk, FMAC_X2BUFCFG_X2_BASE_Pos);
    layout->x2Size =
        (uint8_t)Fmac_Field(x2, FMAC_X2BUFCFG_X2_BUF_SIZE_Msk, FMAC_X2BUFCFG_X2_BUF_SIZE_Pos);
    layout->yBase = (uint8_t)Fmac_Field(y, FMAC_YBUFCFG_Y_BASE_Msk, FMAC_YBUFCFG_Y_BASE_Pos);
    layout->ySize =
        (uint8_t)Fmac_Field(y, FMAC_YBUFCFG_Y_BUF_SIZE_Msk, FMAC_YBUFCFG_Y_BUF_SIZE_Pos);
    layout->yEmptyWm = (uint8_t)Fmac_Field(y, FMAC_YBUFCFG_EMPTY_WM_Msk, FMAC_YBUFCFG_EMPTY_WM_Pos);
}

SPL_Status FMAC_LayoutCheck(const FMAC_LayoutTypeDef *layout)
{
    if (layout == NULL)
    {
        return SPL_ERR_PARAM;
    }
    if ((Fmac_AreaValid(layout->x1Base, layout->x1Size) == 0U) ||
        (Fmac_AreaValid(layout->x2Base, layout->x2Size) == 0U) ||
        (Fmac_AreaValid(layout->yBase, layout->ySize) == 0U))
    {
        return SPL_ERR_PARAM;
    }
    if ((layout->x1FullWm > FMAC_WM_MAX) || (layout->yEmptyWm > FMAC_WM_MAX))
    {
        return SPL_ERR_PARAM;
    }
    /* RM0440 18.3.8/18.3.9: X2 (coefficients) must not overlap the others. */
    if ((Fmac_Overlap(layout->x2Base, layout->x2Size, layout->x1Base, layout->x1Size) != 0U) ||
        (Fmac_Overlap(layout->x2Base, layout->x2Size, layout->yBase, layout->ySize) != 0U))
    {
        return SPL_ERR_PARAM;
    }
    /* X1/Y: disjoint, or exactly coincident (RM0440 18.3.8 note, FIR only;
     * FMAC_FilterStart rejects it for IIR). */
    if (Fmac_Overlap(layout->x1Base, layout->x1Size, layout->yBase, layout->ySize) != 0U)
    {
        if ((layout->x1Base != layout->yBase) || (layout->x1Size != layout->ySize))
        {
            return SPL_ERR_PARAM;
        }
    }
    return SPL_OK;
}

SPL_Status FMAC_ConfigBuffers(const FMAC_LayoutTypeDef *layout)
{
    SPL_Status status;

    /* NULL layout pointer. */
    SPL_REQUIRE(layout != NULL, SPL_ERR_PARAM);
    /* Invalid buffer layout silently corrupts FMAC memory. */
    SPL_REQUIRE(FMAC_LayoutCheck(layout) == SPL_OK, SPL_ERR_PARAM);
    status = Fmac_IdleGate(Fmac_StateOf(FMAC->CR, FMAC->PARAM));
    if (status != SPL_OK)
    {
        return status;
    }

    FMAC->X1BUFCFG = ((uint32_t)layout->x1Base << FMAC_X1BUFCFG_X1_BASE_Pos) |
                     ((uint32_t)layout->x1Size << FMAC_X1BUFCFG_X1_BUF_SIZE_Pos) |
                     ((uint32_t)layout->x1FullWm << FMAC_X1BUFCFG_FULL_WM_Pos);
    FMAC->X2BUFCFG = ((uint32_t)layout->x2Base << FMAC_X2BUFCFG_X2_BASE_Pos) |
                     ((uint32_t)layout->x2Size << FMAC_X2BUFCFG_X2_BUF_SIZE_Pos);
    FMAC->YBUFCFG = ((uint32_t)layout->yBase << FMAC_YBUFCFG_Y_BASE_Pos) |
                    ((uint32_t)layout->ySize << FMAC_YBUFCFG_Y_BUF_SIZE_Pos) |
                    ((uint32_t)layout->yEmptyWm << FMAC_YBUFCFG_EMPTY_WM_Pos);
    return SPL_OK;
}

SPL_Status FMAC_ControlConfig(uint32_t flags)
{
    uint32_t cr;

    /* CR reserved/command bits must not be set. */
    SPL_REQUIRE((flags & ~FMAC_CR_CONFIG_ALL) == 0U, SPL_ERR_PARAM);
    /* RM0440 18.3.8 Table 120 note (a): not both interrupt and DMA for the
     * same direction. */
    /* CPU interrupt and DMA write request must not be enabled together. */
    SPL_REQUIRE(!(((flags & FMAC_CR_WIEN) != 0U) && ((flags & FMAC_CR_DMAWEN) != 0U)),
                SPL_ERR_PARAM);
    /* CPU interrupt and DMA read request must not be enabled together. */
    SPL_REQUIRE(!(((flags & FMAC_CR_RIEN) != 0U) && ((flags & FMAC_CR_DMAREN) != 0U)),
                SPL_ERR_PARAM);
    cr = FMAC->CR;
    if ((cr & FMAC_CR_RESET) != 0U)
    {
        return SPL_BUSY;
    }
    /* RM0440 18.4.5: DMAREN/DMAWEN can be modified only when START = 0. */
    if (((FMAC->PARAM & FMAC_PARAM_START) != 0U) &&
        ((cr & FMAC_CR_DMA_ALL) != (flags & FMAC_CR_DMA_ALL)))
    {
        return SPL_ERR_STATE;
    }

    FMAC->CR = flags;
    return SPL_OK;
}

SPL_Status FMAC_Reset(void)
{
    /* RESET is the only command bit in CR; reserved bits are written 0. */
    FMAC->CR = (FMAC->CR & FMAC_CR_CONFIG_ALL) | FMAC_CR_RESET;
    return SPL_OK;
}

/* Common preload: validate everything, then PARAM, then the WDATA writes. */
static SPL_Status Fmac_Load(uint32_t func, const int16_t *first, uint8_t n, const int16_t *second,
                            uint8_t m)
{
    FMAC_LayoutTypeDef layout;
    uint32_t cr;
    uint32_t size;
    uint32_t i;
    SPL_Status status;

    /* NULL input array. */
    SPL_REQUIRE(first != NULL, SPL_ERR_PARAM);
    /* Zero-length load does not define a preload transfer. */
    SPL_REQUIRE(n != 0U, SPL_ERR_PARAM);
    /* Secondary buffer must exist if m is nonzero. */
    SPL_REQUIRE((m == 0U) || (second != NULL), SPL_ERR_PARAM);
    cr = FMAC->CR;
    status = Fmac_IdleGate(Fmac_StateOf(cr, FMAC->PARAM));
    if (status != SPL_OK)
    {
        return status;
    }
    if ((cr & FMAC_CR_DATA_REQUESTS) != 0U)
    {
        return SPL_ERR_STATE;
    }
    Fmac_ReadLayout(&layout);
    if (func == FMAC_FUNC_LOAD_X1)
    {
        size = layout.x1Size;
    }
    else if (func == FMAC_FUNC_LOAD_X2)
    {
        size = layout.x2Size;
    }
    else
    {
        size = layout.ySize;
    }
    if (((uint32_t)n + (uint32_t)m) > size)
    {
        return SPL_ERR_STATE;
    }

    FMAC->PARAM = FMAC_PARAM_START | (func << FMAC_PARAM_FUNC_Pos) |
                  ((uint32_t)m << FMAC_PARAM_Q_Pos) | ((uint32_t)n << FMAC_PARAM_P_Pos);
    for (i = 0U; i < (uint32_t)n; ++i)
    {
        FMAC->WDATA = (uint32_t)(uint16_t)first[i];
    }
    for (i = 0U; i < (uint32_t)m; ++i)
    {
        FMAC->WDATA = (uint32_t)(uint16_t)second[i];
    }
    return SPL_OK;
}

SPL_Status FMAC_LoadX1(const int16_t *data, uint8_t n)
{
    return Fmac_Load(FMAC_FUNC_LOAD_X1, data, n, NULL, 0U);
}

SPL_Status FMAC_LoadX2(const int16_t *b, uint8_t n, const int16_t *a, uint8_t m)
{
    return Fmac_Load(FMAC_FUNC_LOAD_X2, b, n, a, m);
}

SPL_Status FMAC_LoadY(const int16_t *data, uint8_t n)
{
    return Fmac_Load(FMAC_FUNC_LOAD_Y, data, n, NULL, 0U);
}

static uint8_t Fmac_FilterArgsValid(uint32_t func, uint8_t p, uint8_t q, uint8_t r)
{
    uint8_t valid = 0U;

    if (func == FMAC_FUNC_FIR)
    {
        /* RM0440 18.3.6 FIR: P in [2:127], Q not used (required 0 here). */
        valid = (uint8_t)(((p >= FMAC_FIR_P_MIN) && (p <= FMAC_FIR_P_MAX) && (q == 0U)) ? 1U : 0U);
    }
    else if (func == FMAC_FUNC_IIR)
    {
        /* RM0440 18.3.6 IIR: P in [2:64], Q in [1:63]; 18.3.9: M <= N - 1. */
        valid = (uint8_t)(((p >= FMAC_IIR_P_MIN) && (p <= FMAC_IIR_P_MAX) &&
                           (q >= FMAC_IIR_Q_MIN) && (q <= FMAC_IIR_Q_MAX) && (q < p))
                              ? 1U
                              : 0U);
    }
    else
    {
        /* Load or reserved function codes are not filters. */
    }
    /* RM0440 18.3.6: gain R in [0:7] for both filters. */
    if (r > FMAC_R_MAX)
    {
        valid = 0U;
    }
    return valid;
}

/* Configured buffers against the filter parameters (RM0440 18.4.1/18.4.3
 * minimum sizes and watermark rules, 18.3.8/18.3.9 layouts). Each rule that
 * fails clears 'fits'. */
static uint8_t Fmac_FilterFits(const FMAC_LayoutTypeDef *layout, uint32_t cr, uint32_t func,
                               uint8_t p, uint8_t q)
{
    uint32_t fullThreshold = 1U << layout->x1FullWm;
    uint32_t emptyThreshold = 1U << layout->yEmptyWm;
    uint32_t taps = (uint32_t)p + (uint32_t)q;
    uint8_t fits = 1U;

    if (FMAC_LayoutCheck(layout) != SPL_OK)
    {
        fits = 0U;
    }
    if (((uint32_t)layout->x2Size < taps) ||
        ((uint32_t)layout->x1Size < (((uint32_t)p + fullThreshold) - 1U)) ||
        ((uint32_t)layout->ySize < ((uint32_t)q + emptyThreshold)))
    {
        fits = 0U;
    }
    if ((func == FMAC_FUNC_IIR) &&
        (Fmac_Overlap(layout->x1Base, layout->x1Size, layout->yBase, layout->ySize) != 0U))
    {
        fits = 0U;
    }
    if ((((cr & FMAC_CR_DMAWEN) != 0U) && (layout->x1FullWm != 0U)) ||
        (((cr & FMAC_CR_DMAREN) != 0U) && (layout->yEmptyWm != 0U)))
    {
        fits = 0U;
    }
    return fits;
}

SPL_Status FMAC_FilterStart(uint32_t func, uint8_t p, uint8_t q, uint8_t r)
{
    FMAC_LayoutTypeDef layout;
    uint32_t cr;
    SPL_Status status;

    /* Invalid FIR/IIR function, tap or gain coding. */
    SPL_REQUIRE(Fmac_FilterArgsValid(func, p, q, r) != 0U, SPL_ERR_PARAM);
    cr = FMAC->CR;
    status = Fmac_IdleGate(Fmac_StateOf(cr, FMAC->PARAM));
    if (status != SPL_OK)
    {
        return status;
    }
    Fmac_ReadLayout(&layout);
    if (Fmac_FilterFits(&layout, cr, func, p, q) == 0U)
    {
        return SPL_ERR_STATE;
    }

    FMAC->PARAM = FMAC_PARAM_START | (func << FMAC_PARAM_FUNC_Pos) |
                  ((uint32_t)r << FMAC_PARAM_R_Pos) | ((uint32_t)q << FMAC_PARAM_Q_Pos) |
                  ((uint32_t)p << FMAC_PARAM_P_Pos);
    return SPL_OK;
}

SPL_Status FMAC_FilterStop(void)
{
    uint32_t param = FMAC->PARAM;

    /* RM0440 18.4.4: resetting START by software stops the function. The
     * write clears START and keeps FUNC/P/Q/R (unchanged values). */
    if ((param & FMAC_PARAM_START) != 0U)
    {
        FMAC->PARAM = param & ~FMAC_PARAM_START;
    }
    return SPL_OK;
}

SPL_Status FMAC_TryWrite(int16_t sample)
{
    uint32_t state = Fmac_StateOf(FMAC->CR, FMAC->PARAM);
    uint32_t sr;

    if ((state == FMAC_STATE_RESET) || (state == FMAC_STATE_LOAD))
    {
        return SPL_BUSY;
    }
    if (state != FMAC_STATE_FILTER)
    {
        return SPL_ERR_STATE;
    }
    sr = FMAC->SR;
    if ((sr & FMAC_SR_OVFL) != 0U)
    {
        return SPL_ERR_HW;
    }
    if ((sr & FMAC_SR_X1FULL) != 0U)
    {
        return SPL_WOULD_BLOCK;
    }

    FMAC->WDATA = (uint32_t)(uint16_t)sample;
    return SPL_OK;
}

SPL_Status FMAC_TryRead(int16_t *sample)
{
    uint32_t state;
    uint32_t sr;

    /* NULL output pointer. */
    SPL_REQUIRE(sample != NULL, SPL_ERR_PARAM);
    /* PARAM before SR: START = 0 seen first means no new output can appear
     * after the SR snapshot. */
    state = Fmac_StateOf(FMAC->CR, FMAC->PARAM);
    if ((state == FMAC_STATE_RESET) || (state == FMAC_STATE_LOAD))
    {
        return SPL_BUSY;
    }
    sr = FMAC->SR;
    if ((sr & FMAC_SR_UNFL) != 0U)
    {
        return SPL_ERR_HW;
    }
    if ((sr & FMAC_SR_YEMPTY) != 0U)
    {
        return (state == FMAC_STATE_FILTER) ? SPL_WOULD_BLOCK : SPL_ERR_STATE;
    }

    /* Q1.15 two's complement in RDATA[15:0]. */
    *sample = (int16_t)(uint16_t)(FMAC->RDATA & FMAC_RDATA_RDATA_Msk);
    return SPL_OK;
}

SPL_Status FMAC_GetFlags(uint32_t *flags)
{
    /* NULL output pointer. */
    SPL_REQUIRE(flags != NULL, SPL_ERR_PARAM);
    *flags = FMAC->SR & FMAC_FLAGS_ALL;
    return SPL_OK;
}

SPL_Status FMAC_GetState(uint32_t *state)
{
    /* NULL output pointer. */
    SPL_REQUIRE(state != NULL, SPL_ERR_PARAM);
    *state = Fmac_StateOf(FMAC->CR, FMAC->PARAM);
    return SPL_OK;
}
