#include <stddef.h>
#include "stm32g4_exti.h"

/* Line masks from the per-SKU CMSIS bit macros (Docs/v0.10_EXTI_CONTRACT.md 1.1).
 * Bank 1 = lines 0..31, bank 2 = lines 32..42. */

/* Configurable lines share one layout for RTSR/FTSR/SWIER/PR. Check the variant
 * bits so a single mask derived from RTSR is valid for all four registers. */
#if defined(EXTI_RTSR1_RT31_Pos) != defined(EXTI_FTSR1_FT31_Pos) ||                                \
    defined(EXTI_RTSR1_RT31_Pos) != defined(EXTI_SWIER1_SWI31_Pos) ||                              \
    defined(EXTI_RTSR1_RT31_Pos) != defined(EXTI_PR1_PIF31_Pos)
#error "EXTI line 31: RTSR/FTSR/SWIER/PR layouts differ in CMSIS"
#endif
#if defined(EXTI_RTSR2_RT32_Pos) != defined(EXTI_PR2_PIF32_Pos) ||                                 \
    defined(EXTI_RTSR2_RT33_Pos) != defined(EXTI_PR2_PIF33_Pos) ||                                 \
    defined(EXTI_RTSR2_RT32_Pos) != defined(EXTI_SWIER2_SWI32_Pos) ||                              \
    defined(EXTI_RTSR2_RT33_Pos) != defined(EXTI_FTSR2_FT33_Pos)
#error "EXTI lines 32/33: RTSR/FTSR/SWIER/PR layouts differ in CMSIS"
#endif

/* RM0440 15.5.3: RTSR1 bits 17:0, 22:19, 31:29 (31 only where COMP5 exists). */
#if defined(EXTI_RTSR1_RT31_Pos)
#define EXTI_CFG1_RT31 EXTI_RTSR1_RT31_Msk
#else
#define EXTI_CFG1_RT31 0UL
#endif
#define EXTI_CFG1_MASK                                                                             \
    (EXTI_RTSR1_RT0_Msk | EXTI_RTSR1_RT1_Msk | EXTI_RTSR1_RT2_Msk | EXTI_RTSR1_RT3_Msk |           \
     EXTI_RTSR1_RT4_Msk | EXTI_RTSR1_RT5_Msk | EXTI_RTSR1_RT6_Msk | EXTI_RTSR1_RT7_Msk |           \
     EXTI_RTSR1_RT8_Msk | EXTI_RTSR1_RT9_Msk | EXTI_RTSR1_RT10_Msk | EXTI_RTSR1_RT11_Msk |         \
     EXTI_RTSR1_RT12_Msk | EXTI_RTSR1_RT13_Msk | EXTI_RTSR1_RT14_Msk | EXTI_RTSR1_RT15_Msk |       \
     EXTI_RTSR1_RT16_Msk | EXTI_RTSR1_RT17_Msk | EXTI_RTSR1_RT19_Msk | EXTI_RTSR1_RT20_Msk |       \
     EXTI_RTSR1_RT21_Msk | EXTI_RTSR1_RT22_Msk | EXTI_RTSR1_RT29_Msk | EXTI_RTSR1_RT30_Msk |       \
     EXTI_CFG1_RT31)

/* Bank 2 configurable: 32/33 (COMP6/7, SKU-dependent), 38..41 (PVM1..4). */
#if defined(EXTI_RTSR2_RT32_Pos)
#define EXTI_CFG2_RT32 EXTI_RTSR2_RT32_Msk
#else
#define EXTI_CFG2_RT32 0UL
#endif
#if defined(EXTI_RTSR2_RT33_Pos)
#define EXTI_CFG2_RT33 EXTI_RTSR2_RT33_Msk
#else
#define EXTI_CFG2_RT33 0UL
#endif
#define EXTI_CFG2_MASK                                                                             \
    (EXTI_CFG2_RT32 | EXTI_CFG2_RT33 | EXTI_RTSR2_RT38_Msk | EXTI_RTSR2_RT39_Msk |                 \
     EXTI_RTSR2_RT40_Msk | EXTI_RTSR2_RT41_Msk)

/* Existing lines. Bank 1 from the CMSIS aggregate. Bank 2 from the per-bit
 * macros: the G491/G4A1 aggregate EXTI_IMR2_IM_Msk (0x2FC) omits line 40 although
 * IM40/RT40 are defined in the same header. */
#define EXTI_LINES1_MASK EXTI_IMR1_IM_Msk
#if defined(EXTI_IMR2_IM32_Pos)
#define EXTI_L32 EXTI_IMR2_IM32_Msk
#else
#define EXTI_L32 0UL
#endif
#if defined(EXTI_IMR2_IM33_Pos)
#define EXTI_L33 EXTI_IMR2_IM33_Msk
#else
#define EXTI_L33 0UL
#endif
#if defined(EXTI_IMR2_IM35_Pos)
#define EXTI_L35 EXTI_IMR2_IM35_Msk
#else
#define EXTI_L35 0UL
#endif
#if defined(EXTI_IMR2_IM42_Pos)
#define EXTI_L42 EXTI_IMR2_IM42_Msk
#else
#define EXTI_L42 0UL
#endif
#define EXTI_LINES2_MASK                                                                           \
    (EXTI_L32 | EXTI_L33 | EXTI_IMR2_IM34_Msk | EXTI_L35 | EXTI_IMR2_IM36_Msk |                    \
     EXTI_IMR2_IM37_Msk | EXTI_IMR2_IM38_Msk | EXTI_IMR2_IM39_Msk | EXTI_IMR2_IM40_Msk |           \
     EXTI_IMR2_IM41_Msk | EXTI_L42)

/* RM0440 10.2.3-10.2.6: EXTIx[3:0] is a 4-bit field (CMSIS _Msk is 3 bits). */
#define EXTI_EXTICR_FIELD 0xFU
#define EXTI_PORT_CODE_G 6U
#define EXTI_PORT_G_MAX_LINE 10U

typedef enum
{
    EXTI_CLASS_INVALID = 0, /* outside 0..42 */
    EXTI_CLASS_ABSENT = 1,  /* not on this SKU */
    EXTI_CLASS_DIRECT = 2,
    EXTI_CLASS_CONFIGURABLE = 3
} Exti_Class;

/* Compile-time check (CCL-2 YAP-03): every line 0..EXTI_LINE_COUNT-1 maps to bank 1 (IMR1..,
 * lines 0..31) or bank 2 (IMR2.., lines 32..63) of 32 bits each, as Exti_Classify assumes. */
typedef char Exti_LineCountCheck[((EXTI_LINE_COUNT > 32U) && (EXTI_LINE_COUNT <= (2U * 32U))) ? 1
                                                                                             : -1];

static uint32_t Exti_Bit(uint8_t line)
{
    return (uint32_t)1U << ((uint32_t)line & 31U);
}

static Exti_Class Exti_Classify(uint8_t line)
{
    uint32_t bit;
    uint32_t lines;
    uint32_t cfg;

    if (line >= EXTI_LINE_COUNT)
    {
        return EXTI_CLASS_INVALID;
    }
    bit = Exti_Bit(line);
    if (line < 32U)
    {
        lines = (uint32_t)EXTI_LINES1_MASK;
        cfg = (uint32_t)EXTI_CFG1_MASK;
    }
    else
    {
        lines = EXTI_LINES2_MASK;
        cfg = EXTI_CFG2_MASK;
    }
    if ((cfg & bit) != 0U)
    {
        return EXTI_CLASS_CONFIGURABLE;
    }
    if ((lines & bit) != 0U)
    {
        return EXTI_CLASS_DIRECT;
    }
    return EXTI_CLASS_ABSENT;
}

/* Status for a line that must exist (any type). */
static SPL_Status Exti_CheckLine(Exti_Class cls)
{
    if (cls == EXTI_CLASS_INVALID)
    {
        return SPL_ERR_PARAM;
    }
    if (cls == EXTI_CLASS_ABSENT)
    {
        return SPL_ERR_UNSUPPORTED;
    }
    return SPL_OK;
}

/* Status for a line that must be configurable (has RTSR/FTSR/SWIER/PR). */
static SPL_Status Exti_CheckConfigurable(Exti_Class cls)
{
    SPL_Status status = Exti_CheckLine(cls);

    if ((status == SPL_OK) && (cls != EXTI_CLASS_CONFIGURABLE))
    {
        status = SPL_ERR_PARAM;
    }
    return status;
}

/* RM0440 10.2.3: 0000=PA ... 0110=PG. Only ports defined by CMSIS are accepted. */
static uint32_t Exti_PortCode(const GPIO_TypeDef *port, uint8_t *valid)
{
    uint32_t code = 0xFFU;

    if (port == GPIOA)
    {
        code = 0U;
    }
    else if (port == GPIOB)
    {
        code = 1U;
    }
    else if (port == GPIOC)
    {
        code = 2U;
    }
#if defined(GPIOD)
    else if (port == GPIOD)
    {
        code = 3U;
    }
#endif
#if defined(GPIOE)
    else if (port == GPIOE)
    {
        code = 4U;
    }
#endif
#if defined(GPIOF)
    else if (port == GPIOF)
    {
        code = 5U;
    }
#endif
#if defined(GPIOG)
    else if (port == GPIOG)
    {
        code = EXTI_PORT_CODE_G;
    }
#endif
    else
    {
        code = 0xFFU;
    }
    *valid = (uint8_t)((code != 0xFFU) ? 1U : 0U);
    return code;
}

SPL_Status EXTI_SetGpioSource(GPIO_TypeDef *port, uint8_t line)
{
    uint8_t valid;
    uint32_t code = Exti_PortCode(port, &valid);
    uint32_t shift;
    uint32_t index;
    uint32_t value;

    /* SAFE: gecersiz port veya EXTI hattinda yanlis EXTICR yazimi. */
    SPL_REQUIRE_SAFE(valid != 0U, SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE(line <= 15U, SPL_ERR_PARAM);
    SPL_REQUIRE_SAFE((code != EXTI_PORT_CODE_G) || (line <= EXTI_PORT_G_MAX_LINE), SPL_ERR_PARAM);

    index = (uint32_t)line >> 2U;
    shift = ((uint32_t)line & 3U) * 4U;
    value = SYSCFG->EXTICR[index] & ~((uint32_t)EXTI_EXTICR_FIELD << shift);
    SYSCFG->EXTICR[index] = value | (code << shift);

    /* Writes are ignored while the SYSCFG clock is off (caller duty). */
    if (((SYSCFG->EXTICR[index] >> shift) & EXTI_EXTICR_FIELD) != code)
    {
        return SPL_ERR_STATE;
    }
    return SPL_OK;
}

static void Exti_SetBit(volatile uint32_t *reg, uint32_t bit, uint32_t on)
{
    if (on != 0U)
    {
        *reg = *reg | bit;
    }
    else
    {
        *reg = *reg & ~bit;
    }
}

SPL_Status EXTI_LineConfig(uint8_t line, EXTI_TriggerTypeDef trigger, uint32_t mode)
{
    const Exti_Class cls = Exti_Classify(line);
    const uint32_t trig = (uint32_t)trigger;
    const uint32_t bit = Exti_Bit(line);
    SPL_Status status = Exti_CheckLine(cls);

    if (status != SPL_OK)
    {
        return status;
    }
    /* SAFE: tetik/mode bitleri komsu register alanlarini bozabilir. */
    SPL_REQUIRE_SAFE(trig <= (uint32_t)EXTI_TRIGGER_BOTH, SPL_ERR_PARAM);
    SPL_REQUIRE(mode != 0U, SPL_ERR_PARAM); /* Normal: maske kapali kalir. */
    SPL_REQUIRE_SAFE((mode & ~(EXTI_MODE_INTERRUPT | EXTI_MODE_EVENT)) == 0U, SPL_ERR_PARAM);
    /* SAFE: direct hatta trigger yazma istegi sessizce yok sayilir. */
    SPL_REQUIRE_SAFE((cls != EXTI_CLASS_DIRECT) || (trig == (uint32_t)EXTI_TRIGGER_NONE), SPL_ERR_PARAM);

    /* Triggers first, masks last: never unmask with a stale edge selection. */
    if (line < 32U)
    {
        if (cls == EXTI_CLASS_CONFIGURABLE)
        {
            Exti_SetBit(&EXTI->RTSR1, bit, trig & (uint32_t)EXTI_TRIGGER_RISING);
            Exti_SetBit(&EXTI->FTSR1, bit, trig & (uint32_t)EXTI_TRIGGER_FALLING);
        }
        Exti_SetBit(&EXTI->IMR1, bit, mode & EXTI_MODE_INTERRUPT);
        Exti_SetBit(&EXTI->EMR1, bit, mode & EXTI_MODE_EVENT);
    }
    else
    {
        if (cls == EXTI_CLASS_CONFIGURABLE)
        {
            Exti_SetBit(&EXTI->RTSR2, bit, trig & (uint32_t)EXTI_TRIGGER_RISING);
            Exti_SetBit(&EXTI->FTSR2, bit, trig & (uint32_t)EXTI_TRIGGER_FALLING);
        }
        Exti_SetBit(&EXTI->IMR2, bit, mode & EXTI_MODE_INTERRUPT);
        Exti_SetBit(&EXTI->EMR2, bit, mode & EXTI_MODE_EVENT);
    }
    return SPL_OK;
}

SPL_Status EXTI_LineDisable(uint8_t line)
{
    const Exti_Class cls = Exti_Classify(line);
    const uint32_t bit = Exti_Bit(line);
    SPL_Status status = Exti_CheckLine(cls);

    if (status != SPL_OK)
    {
        return status;
    }

    /* Masks first so no request is generated while triggers change. */
    if (line < 32U)
    {
        EXTI->IMR1 = EXTI->IMR1 & ~bit;
        EXTI->EMR1 = EXTI->EMR1 & ~bit;
        if (cls == EXTI_CLASS_CONFIGURABLE)
        {
            EXTI->RTSR1 = EXTI->RTSR1 & ~bit;
            EXTI->FTSR1 = EXTI->FTSR1 & ~bit;
        }
    }
    else
    {
        EXTI->IMR2 = EXTI->IMR2 & ~bit;
        EXTI->EMR2 = EXTI->EMR2 & ~bit;
        if (cls == EXTI_CLASS_CONFIGURABLE)
        {
            EXTI->RTSR2 = EXTI->RTSR2 & ~bit;
            EXTI->FTSR2 = EXTI->FTSR2 & ~bit;
        }
    }
    return SPL_OK;
}

SPL_Status EXTI_SoftwareTrigger(uint8_t line)
{
    SPL_Status status = Exti_CheckConfigurable(Exti_Classify(line));

    if (status != SPL_OK)
    {
        return status;
    }
    /* Only this line's bit: writing 1 sets PR when the bit was 0 (RM 15.5.5). */
    if (line < 32U)
    {
        EXTI->SWIER1 = Exti_Bit(line);
    }
    else
    {
        EXTI->SWIER2 = Exti_Bit(line);
    }
    return SPL_OK;
}

SPL_Status EXTI_GetPending(uint8_t line, uint8_t *pending)
{
    SPL_Status status = Exti_CheckConfigurable(Exti_Classify(line));
    uint32_t pr;

    if (status != SPL_OK)
    {
        return status;
    }
    /* SAFE: gecersiz cikti pointer'i belirsiz sonuc uretir. */
    SPL_REQUIRE_SAFE(pending != NULL, SPL_ERR_PARAM);
    if (line < 32U)
    {
        pr = EXTI->PR1;
    }
    else
    {
        pr = EXTI->PR2;
    }
    *pending = (uint8_t)(((pr & Exti_Bit(line)) != 0U) ? 1U : 0U);
    return SPL_OK;
}

SPL_Status EXTI_ClearPending(uint8_t line)
{
    SPL_Status status = Exti_CheckConfigurable(Exti_Classify(line));

    if (status != SPL_OK)
    {
        return status;
    }
    /* PR is rc_w1: a read-modify-write would clear every pending line. */
    if (line < 32U)
    {
        EXTI->PR1 = Exti_Bit(line);
    }
    else
    {
        EXTI->PR2 = Exti_Bit(line);
    }
    /* Read back: the clear reaches the peripheral before return (METHOD 1.7, issue #23). */
    (void)EXTI->PR1;
    return SPL_OK;
}
