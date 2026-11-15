#define SPL_LIBRARY_BUILD 1
#include <stddef.h>
#include "stm32g4_rtc.h"

/* RM0440 37.3.8: write-protection keys. */
#define RTC_WPR_KEY1 0xCAU
#define RTC_WPR_KEY2 0x53U
#define RTC_WPR_LOCK 0xFFU

/* RM0440 37.3.6: seconds comparison needs PREDIV_S >= 3. */
#define RTC_ALARM_MIN_PREDIV_S 3U

/* RTC_CR write-only command bits: never write them back in a read-modify-write. */
#define RTC_CR_COMMANDS (RTC_CR_ADD1H | RTC_CR_SUB1H)

static uint8_t Rtc_DbpSet(void)
{
    return ((PWR->CR1 & PWR_CR1_DBP) != 0U) ? 1U : 0U;
}

static uint8_t Rtc_InitReadyFlag(void)
{
    return ((RTC->ICSR & RTC_ICSR_INITF) != 0U) ? 1U : 0U;
}

static uint32_t Rtc_CrBase(void)
{
    return RTC->CR & (uint32_t)~RTC_CR_COMMANDS;
}

static uint32_t Rtc_Bcd(uint32_t value, uint32_t tens_pos, uint32_t units_pos)
{
    return ((value / 10U) << tens_pos) | ((value % 10U) << units_pos);
}

static uint8_t Rtc_FromBcd(uint32_t reg, uint32_t tens_msk, uint32_t tens_pos, uint32_t units_msk,
                           uint32_t units_pos)
{
    uint32_t tens = (reg & tens_msk) >> tens_pos;
    uint32_t units = (reg & units_msk) >> units_pos;
    return (uint8_t)((tens * 10U) + units);
}

/* Hours valid for the current FMT bit (RM0440 37.6.1 PM bit, 37.6.7 FMT). */
static uint8_t Rtc_HoursValid(uint8_t hours, uint8_t pm)
{
    if ((RTC->CR & RTC_CR_FMT) != 0U)
    {
        return ((hours >= 1U) && (hours <= 12U)) ? 1U : 0U;
    }
    return ((hours <= 23U) && (pm == 0U)) ? 1U : 0U;
}

static uint8_t Rtc_DaysInMonth(uint8_t month, uint8_t year)
{
    static const uint8_t days[12] = {31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U};
    uint8_t n = days[month - 1U];

    /* Years 2000..2099: divisible by 4 is the exact Gregorian rule (2000 is leap). */
    if ((month == 2U) && ((year % 4U) == 0U))
    {
        n = 29U;
    }
    return n;
}

static uint8_t Rtc_OscillatorState(uint32_t source)
{
    /* 0 = off, 1 = on not ready, 2 = ready. */
    uint32_t on;
    uint32_t ready;

    if (source == RTC_CLOCK_LSE)
    {
        on = RCC->BDCR & RCC_BDCR_LSEON;
        ready = RCC->BDCR & RCC_BDCR_LSERDY;
    }
    else if (source == RTC_CLOCK_LSI)
    {
        on = RCC->CSR & RCC_CSR_LSION;
        ready = RCC->CSR & RCC_CSR_LSIRDY;
    }
    else
    {
        on = RCC->CR & RCC_CR_HSEON;
        ready = RCC->CR & RCC_CR_HSERDY;
    }
    if (on == 0U)
    {
        return 0U;
    }
    return (ready != 0U) ? 2U : 1U;
}

/* RSF, ALRAWF and WUTWF are set from the RTCCLK domain. 1 when RTCCLK is
 * absent so these flags can never set: RTCEN = 0, RTCSEL = 00 (e.g. after
 * RTC_BackupDomainReset) or the selected oscillator off (e.g. HSE after an
 * MCU reset; RM0440 7.4.27 RCC_BDCR). An oscillator that is on but not yet
 * ready is starting: not reported as missing (caller retries). */
static uint8_t Rtc_ClockMissing(void)
{
    uint32_t bdcr = RCC->BDCR;
    uint32_t source = (bdcr & RCC_BDCR_RTCSEL_Msk) >> RCC_BDCR_RTCSEL_Pos;

    if (((bdcr & RCC_BDCR_RTCEN) == 0U) || (source == 0U))
    {
        return 1U;
    }
    return (Rtc_OscillatorState(source) == 0U) ? 1U : 0U;
}

/* Status for a call that must wait on an RTCCLK-synchronised flag. */
static SPL_Status Rtc_WaitStatus(void)
{
    return (Rtc_ClockMissing() != 0U) ? SPL_ERR_STATE : SPL_WOULD_BLOCK;
}

SPL_Status RTC_LSEStart(uint32_t drive, uint8_t bypass)
{
    uint32_t bdcr;
    uint32_t cfg;

    /* SAFE: Prevent unsupported drive encoding or bypass option. */
    SPL_REQUIRE_SAFE(!((drive > 3U) || (bypass > 1U)), SPL_ERR_PARAM);
    if (Rtc_DbpSet() == 0U)
    {
        return SPL_ERR_STATE;
    }
    bdcr = RCC->BDCR;
    if ((bdcr & RCC_BDCR_BDRST) != 0U)
    {
        return SPL_ERR_STATE;
    }

    cfg = drive << RCC_BDCR_LSEDRV_Pos;
    if (bypass != 0U)
    {
        cfg |= RCC_BDCR_LSEBYP;
    }
    if ((bdcr & RCC_BDCR_LSEON) != 0U)
    {
        /* Already running: accept only the identical configuration. */
        if ((bdcr & (RCC_BDCR_LSEDRV_Msk | RCC_BDCR_LSEBYP)) == cfg)
        {
            return SPL_OK;
        }
        return SPL_ERR_STATE;
    }
    if ((bdcr & RCC_BDCR_LSERDY) != 0U)
    {
        /* LSERDY falls six LSE cycles after LSEON is cleared (RM0440 7.4.27). */
        return SPL_BUSY;
    }

    /* LSEBYP is writable only with LSEON = 0 and LSERDY = 0: configure, then start. */
    bdcr = (bdcr & (uint32_t)~(RCC_BDCR_LSEDRV_Msk | RCC_BDCR_LSEBYP)) | cfg;
    RCC->BDCR = bdcr;
    RCC->BDCR = bdcr | RCC_BDCR_LSEON;
    return SPL_OK;
}

SPL_Status RTC_LSEReady(uint8_t *ready)
{
    /* SAFE: Reject invalid output pointer. */
    SPL_REQUIRE_SAFE(!(ready == NULL), SPL_ERR_PARAM);
    *ready = ((RCC->BDCR & RCC_BDCR_LSERDY) != 0U) ? 1U : 0U;
    return SPL_OK;
}

SPL_Status RTC_ClockConfig(uint32_t source)
{
    uint32_t bdcr;
    uint32_t current;
    uint32_t wanted;
    uint8_t osc;

    /* SAFE: Prevent invalid RTCSEL selection. */
    SPL_REQUIRE_SAFE(!((source < RTC_CLOCK_LSE) || (source > RTC_CLOCK_HSE_DIV32)), SPL_ERR_PARAM);
    if (Rtc_DbpSet() == 0U)
    {
        return SPL_ERR_STATE;
    }
    bdcr = RCC->BDCR;
    if ((bdcr & RCC_BDCR_BDRST) != 0U)
    {
        return SPL_ERR_STATE;
    }
    /* RM0440 7.4.27: once selected, RTCSEL changes only by an RTC domain reset. */
    current = (bdcr & RCC_BDCR_RTCSEL_Msk) >> RCC_BDCR_RTCSEL_Pos;
    if ((current != 0U) && (current != source))
    {
        return SPL_ERR_STATE;
    }
    osc = Rtc_OscillatorState(source);
    if (osc == 0U)
    {
        return SPL_ERR_STATE;
    }
    if (osc == 1U)
    {
        return SPL_WOULD_BLOCK;
    }

    wanted =
        (bdcr & (uint32_t)~RCC_BDCR_RTCSEL_Msk) | (source << RCC_BDCR_RTCSEL_Pos) | RCC_BDCR_RTCEN;
    if (wanted != bdcr)
    {
        RCC->BDCR = wanted;
    }
    return SPL_OK;
}

SPL_Status RTC_BackupDomainReset(uint32_t key)
{
    uint32_t keep;

    if (key != RTC_BDRST_KEY)
    {
        return SPL_ERR_PARAM;
    }
    if (Rtc_DbpSet() == 0U)
    {
        return SPL_ERR_STATE;
    }
    /* LSCOEN/LSCOSEL are reset only by RTC domain power-on reset (RM0440 7.4.27). */
    keep = RCC->BDCR & (RCC_BDCR_LSCOEN | RCC_BDCR_LSCOSEL);
    RCC->BDCR = keep | RCC_BDCR_BDRST;
    RCC->BDCR = keep;
    return SPL_OK;
}

SPL_Status RTC_BackupDomainFullReset(uint32_t key)
{
    if (key != RTC_BDRST_FULL_KEY)
    {
        return SPL_ERR_PARAM;
    }
    if (Rtc_DbpSet() == 0U)
    {
        return SPL_ERR_STATE;
    }
    /* ERRATA ES0431 2.2.5 / ES0430 2.2.5 / ES0523 2.2.3: after a missed backup-domain POR, write
     * BDCR = 0x0001 0000 (BDRST, every other bit incl. LSCOEN/LSCOSEL cleared), read
     * BDCR back to lengthen the reset, then BDCR = 0 (workaround step 3c). */
    RCC->BDCR = RCC_BDCR_BDRST;
    (void)RCC->BDCR;
    RCC->BDCR = 0U;
    return SPL_OK;
}

SPL_Status RTC_WriteProtectDisable(void)
{
    if (Rtc_DbpSet() == 0U)
    {
        return SPL_ERR_STATE;
    }
    RTC->WPR = RTC_WPR_KEY1;
    RTC->WPR = RTC_WPR_KEY2;
    return SPL_OK;
}

SPL_Status RTC_WriteProtectEnable(void)
{
    if (Rtc_DbpSet() == 0U)
    {
        return SPL_ERR_STATE;
    }
    RTC->WPR = RTC_WPR_LOCK;
    return SPL_OK;
}

SPL_Status RTC_EnterInit(void)
{
    if (Rtc_DbpSet() == 0U)
    {
        return SPL_ERR_STATE;
    }
    if ((RTC->ICSR & RTC_ICSR_INIT) != 0U)
    {
        return SPL_OK;
    }
    /* ERRATA ES0431 2.10.1 / ES0430 2.14.1 / ES0523 2.10.1: INIT set 1..2 RTCCLK after it was
     * cleared can corrupt the calendar: BYPSHAD = 0 and RSF risen are needed before INIT = 1. */
    if ((RTC->CR & RTC_CR_BYPSHAD) != 0U)
    {
        return SPL_ERR_STATE; /* RSF is held at 0 while BYPSHAD = 1 (RM0440 37.6.4) */
    }
    if ((RTC->ICSR & RTC_ICSR_RSF) == 0U)
    {
        return Rtc_WaitStatus();
    }
    /* Whole-value write: INIT is the only rw bit; RSF (rc_w0) is cleared by hardware
     * in init mode anyway; the other bits are read-only (RM0440 37.6.4). */
    RTC->ICSR = RTC_ICSR_INIT;
    return SPL_OK;
}

SPL_Status RTC_InitReady(uint8_t *ready)
{
    /* SAFE: Reject invalid output pointer. */
    SPL_REQUIRE_SAFE(!(ready == NULL), SPL_ERR_PARAM);
    if ((RTC->ICSR & RTC_ICSR_INIT) == 0U)
    {
        return SPL_ERR_STATE;
    }
    *ready = Rtc_InitReadyFlag();
    return SPL_OK;
}

SPL_Status RTC_ExitInit(void)
{
    if (Rtc_DbpSet() == 0U)
    {
        return SPL_ERR_STATE;
    }
    if ((RTC->ICSR & RTC_ICSR_INIT) == 0U)
    {
        return SPL_OK;
    }
    /* INIT = 0; RSF written 0: wait for RSF before reading (RM0440 37.3.9). */
    RTC->ICSR = 0U;
    return SPL_OK;
}

SPL_Status RTC_SetPrescaler(uint32_t async_div, uint32_t sync_div)
{
    uint32_t prer_s;

    /* SAFE: Prevent prescaler field overflow. */
    SPL_REQUIRE_SAFE(!((async_div > (RTC_PRER_PREDIV_A_Msk >> RTC_PRER_PREDIV_A_Pos)) ||
                       (sync_div > (RTC_PRER_PREDIV_S_Msk >> RTC_PRER_PREDIV_S_Pos))),
                     SPL_ERR_PARAM);
    if ((Rtc_DbpSet() == 0U) || (Rtc_InitReadyFlag() == 0U))
    {
        return SPL_ERR_STATE;
    }
    /* RM0440 37.6.5: two separate write accesses. */
    prer_s = sync_div << RTC_PRER_PREDIV_S_Pos;
    RTC->PRER = prer_s;
    RTC->PRER = prer_s | (async_div << RTC_PRER_PREDIV_A_Pos);
    return SPL_OK;
}

SPL_Status RTC_SetHourFormat(uint32_t format)
{
    uint32_t cr;

    /* SAFE: Prevent invalid 12/24-hour mode encoding. */
    SPL_REQUIRE_SAFE(!(format > RTC_FORMAT_12H), SPL_ERR_PARAM);
    if ((Rtc_DbpSet() == 0U) || (Rtc_InitReadyFlag() == 0U))
    {
        return SPL_ERR_STATE;
    }
    cr = Rtc_CrBase() & (uint32_t)~RTC_CR_FMT;
    if (format == RTC_FORMAT_12H)
    {
        cr |= RTC_CR_FMT;
    }
    RTC->CR = cr;
    return SPL_OK;
}

SPL_Status RTC_SetTime(const RTC_TimeTypeDef *time)
{
    uint32_t tr;

    /* SAFE: Prevent invalid time BCD or invalid input pointer. */
    SPL_REQUIRE_SAFE(
        !((time == NULL) || (time->minutes > 59U) || (time->seconds > 59U) || (time->pm > 1U)),
        SPL_ERR_PARAM);
    if ((Rtc_DbpSet() == 0U) || (Rtc_InitReadyFlag() == 0U))
    {
        return SPL_ERR_STATE;
    }
    /* SAFE: Prevent invalid time BCD or invalid input pointer. */
    SPL_REQUIRE_SAFE(!(Rtc_HoursValid(time->hours, time->pm) == 0U), SPL_ERR_PARAM);
    tr = Rtc_Bcd(time->hours, RTC_TR_HT_Pos, RTC_TR_HU_Pos) |
         Rtc_Bcd(time->minutes, RTC_TR_MNT_Pos, RTC_TR_MNU_Pos) |
         Rtc_Bcd(time->seconds, RTC_TR_ST_Pos, RTC_TR_SU_Pos);
    if (time->pm != 0U)
    {
        tr |= RTC_TR_PM;
    }
    RTC->TR = tr;
    return SPL_OK;
}

SPL_Status RTC_SetDate(const RTC_DateTypeDef *date)
{
    /* SAFE: Prevent invalid calendar BCD or invalid input pointer. */
    SPL_REQUIRE_SAFE(!((date == NULL) || (date->year > 99U) || (date->month < 1U) ||
                       (date->month > 12U) || (date->weekday < 1U) || (date->weekday > 7U) ||
                       (date->day < 1U)),
                     SPL_ERR_PARAM);
    /* SAFE: Prevent invalid calendar BCD or invalid input pointer. */
    SPL_REQUIRE_SAFE(!(date->day > Rtc_DaysInMonth(date->month, date->year)), SPL_ERR_PARAM);
    if ((Rtc_DbpSet() == 0U) || (Rtc_InitReadyFlag() == 0U))
    {
        return SPL_ERR_STATE;
    }
    RTC->DR = Rtc_Bcd(date->year, RTC_DR_YT_Pos, RTC_DR_YU_Pos) |
              ((uint32_t)date->weekday << RTC_DR_WDU_Pos) |
              Rtc_Bcd(date->month, RTC_DR_MT_Pos, RTC_DR_MU_Pos) |
              Rtc_Bcd(date->day, RTC_DR_DT_Pos, RTC_DR_DU_Pos);
    return SPL_OK;
}

SPL_Status RTC_SetBypassShadow(uint8_t enable)
{
    uint32_t cr;

    /* SAFE: Prevent invalid shadow bypass setting. */
    SPL_REQUIRE_SAFE(!(enable > 1U), SPL_ERR_PARAM);
    if (Rtc_DbpSet() == 0U)
    {
        return SPL_ERR_STATE;
    }
    cr = Rtc_CrBase() & (uint32_t)~RTC_CR_BYPSHAD;
    if (enable != 0U)
    {
        cr |= RTC_CR_BYPSHAD;
    }
    RTC->CR = cr;
    return SPL_OK;
}

SPL_Status RTC_SyncReady(uint8_t *synced)
{
    /* SAFE: Reject invalid output pointer. */
    SPL_REQUIRE_SAFE(!(synced == NULL), SPL_ERR_PARAM);
    /* RSF is held at 0 by hardware in init mode and with BYPSHAD = 1 (RM0440 37.6.4). */
    if (((RTC->CR & RTC_CR_BYPSHAD) != 0U) || ((RTC->ICSR & RTC_ICSR_INIT) != 0U))
    {
        return SPL_ERR_STATE;
    }
    *synced = ((RTC->ICSR & RTC_ICSR_RSF) != 0U) ? 1U : 0U;
    return SPL_OK;
}

SPL_Status RTC_ClearSync(void)
{
    if ((Rtc_DbpSet() == 0U) || ((RTC->ICSR & RTC_ICSR_INIT) != 0U))
    {
        return SPL_ERR_STATE;
    }
    /* RSF is rc_w0; INIT stays 0; other bits are read-only. */
    RTC->ICSR = 0U;
    /* Read back: the clear reaches the peripheral before return (METHOD 1.7, issue #23). */
    (void)RTC->ICSR;
    return SPL_OK;
}

SPL_Status RTC_GetTimeDate(RTC_TimeTypeDef *time, RTC_DateTypeDef *date)
{
    uint32_t tr;
    uint32_t dr;

    /* SAFE: Reject invalid calendar output pointer. */
    SPL_REQUIRE_SAFE(!((time == NULL) || (date == NULL)), SPL_ERR_PARAM);
    if ((RTC->ICSR & RTC_ICSR_INIT) != 0U)
    {
        return SPL_ERR_STATE;
    }
    if ((RTC->CR & RTC_CR_BYPSHAD) == 0U)
    {
        if ((RTC->ICSR & RTC_ICSR_RSF) == 0U)
        {
            return Rtc_WaitStatus();
        }
        /* Reading TR locks the DR shadow until DR is read (RM0440 37.3.9). */
        tr = RTC->TR;
        dr = RTC->DR;
    }
    else
    {
        /* Direct counter access: read twice and compare (RM0440 37.3.9). */
        uint32_t tr2;
        uint32_t dr2;
        tr = RTC->TR;
        dr = RTC->DR;
        tr2 = RTC->TR;
        dr2 = RTC->DR;
        if ((tr != tr2) || (dr != dr2))
        {
            return SPL_WOULD_BLOCK;
        }
    }

    time->hours = Rtc_FromBcd(tr, RTC_TR_HT_Msk, RTC_TR_HT_Pos, RTC_TR_HU_Msk, RTC_TR_HU_Pos);
    time->minutes = Rtc_FromBcd(tr, RTC_TR_MNT_Msk, RTC_TR_MNT_Pos, RTC_TR_MNU_Msk, RTC_TR_MNU_Pos);
    time->seconds = Rtc_FromBcd(tr, RTC_TR_ST_Msk, RTC_TR_ST_Pos, RTC_TR_SU_Msk, RTC_TR_SU_Pos);
    time->pm = ((tr & RTC_TR_PM) != 0U) ? 1U : 0U;
    date->year = Rtc_FromBcd(dr, RTC_DR_YT_Msk, RTC_DR_YT_Pos, RTC_DR_YU_Msk, RTC_DR_YU_Pos);
    date->month = Rtc_FromBcd(dr, RTC_DR_MT_Msk, RTC_DR_MT_Pos, RTC_DR_MU_Msk, RTC_DR_MU_Pos);
    date->day = Rtc_FromBcd(dr, RTC_DR_DT_Msk, RTC_DR_DT_Pos, RTC_DR_DU_Msk, RTC_DR_DU_Pos);
    date->weekday = (uint8_t)((dr & RTC_DR_WDU_Msk) >> RTC_DR_WDU_Pos);
    return SPL_OK;
}

typedef struct
{
    volatile uint32_t *alrmr;
    volatile uint32_t *alrmssr;
    uint32_t enable;    /* CR.ALRxE */
    uint32_t irq;       /* CR.ALRxIE */
    uint32_t writeable; /* ICSR.ALRxWF */
    uint32_t flag;      /* SR.ALRxF */
    uint32_t clear;     /* SCR.CALRxF */
} Rtc_AlarmRegs;

static Rtc_AlarmRegs Rtc_Alarm(uint32_t b)
{
    Rtc_AlarmRegs r;

    if (b == 0U)
    {
        r.alrmr = &RTC->ALRMAR;
        r.alrmssr = &RTC->ALRMASSR;
        r.enable = RTC_CR_ALRAE;
        r.irq = RTC_CR_ALRAIE;
        r.writeable = RTC_ICSR_ALRAWF;
        r.flag = RTC_SR_ALRAF;
        r.clear = RTC_SCR_CALRAF;
    }
    else
    {
        r.alrmr = &RTC->ALRMBR;
        r.alrmssr = &RTC->ALRMBSSR;
        r.enable = RTC_CR_ALRBE;
        r.irq = RTC_CR_ALRBIE;
        r.writeable = RTC_ICSR_ALRBWF;
        r.flag = RTC_SR_ALRBF;
        r.clear = RTC_SCR_CALRBF;
    }
    return r;
}

static SPL_Status Rtc_AlarmConfig(uint32_t b, const RTC_AlarmTypeDef *alarm)
{
    const Rtc_AlarmRegs r = Rtc_Alarm(b);
    uint32_t alrmr;
    uint8_t day_max;

    SPL_REQUIRE_SAFE(alarm != NULL, SPL_ERR_PARAM);
    day_max = (alarm->weekday_sel != 0U) ? 7U : 31U;
    /* SAFE: BCD / alan tasmasi. */
    SPL_REQUIRE_SAFE((alarm->minutes <= 59U) && (alarm->seconds <= 59U) && (alarm->day >= 1U) &&
                         (alarm->day <= day_max),
                     SPL_ERR_PARAM);
    /* normal: pm / weekday_sel 1 gibi; tanimsiz maske bitleri kullanilmaz. */
    SPL_REQUIRE((alarm->pm <= 1U) && (alarm->weekday_sel <= 1U) &&
                    ((alarm->mask & (uint8_t)~RTC_ALARM_MASK_ALL) == 0U),
                SPL_ERR_PARAM);
    if (Rtc_DbpSet() == 0U)
    {
        return SPL_ERR_STATE;
    }
    SPL_REQUIRE_SAFE((RTC->CR & r.enable) == 0U,
                     SPL_ERR_STATE); /* SAFE: ALRMxR yazimi yok sayilir */
    SPL_REQUIRE_SAFE(Rtc_HoursValid(alarm->hours, alarm->pm) != 0U, SPL_ERR_PARAM); /* SAFE */
    /* SAFE (RM0440 37.3.6): saniye karsilastirmasi PREDIV_S < 3 ile sessizce calismaz. */
    SPL_REQUIRE_SAFE(((alarm->mask & RTC_ALARM_MASK_SECONDS) != 0U) ||
                         (((RTC->PRER & RTC_PRER_PREDIV_S_Msk) >> RTC_PRER_PREDIV_S_Pos) >=
                          RTC_ALARM_MIN_PREDIV_S),
                     SPL_ERR_STATE);
    /* RM0440 37.6.14 / 37.6.16: writable when ALRxWF = 1 or in initialization mode. */
    if (((RTC->ICSR & r.writeable) == 0U) && (Rtc_InitReadyFlag() == 0U))
    {
        return Rtc_WaitStatus();
    }

    alrmr = Rtc_Bcd(alarm->day, RTC_ALRMAR_DT_Pos, RTC_ALRMAR_DU_Pos) |
            Rtc_Bcd(alarm->hours, RTC_ALRMAR_HT_Pos, RTC_ALRMAR_HU_Pos) |
            Rtc_Bcd(alarm->minutes, RTC_ALRMAR_MNT_Pos, RTC_ALRMAR_MNU_Pos) |
            Rtc_Bcd(alarm->seconds, RTC_ALRMAR_ST_Pos, RTC_ALRMAR_SU_Pos);
    if (alarm->weekday_sel != 0U)
    {
        alrmr |= RTC_ALRMAR_WDSEL;
    }
    if (alarm->pm != 0U)
    {
        alrmr |= RTC_ALRMAR_PM;
    }
    if ((alarm->mask & RTC_ALARM_MASK_SECONDS) != 0U)
    {
        alrmr |= RTC_ALRMAR_MSK1;
    }
    if ((alarm->mask & RTC_ALARM_MASK_MINUTES) != 0U)
    {
        alrmr |= RTC_ALRMAR_MSK2;
    }
    if ((alarm->mask & RTC_ALARM_MASK_HOURS) != 0U)
    {
        alrmr |= RTC_ALRMAR_MSK3;
    }
    if ((alarm->mask & RTC_ALARM_MASK_DATE) != 0U)
    {
        alrmr |= RTC_ALRMAR_MSK4;
    }
    *r.alrmr = alrmr;
    /* MASKSS = 0: no sub-second comparison (RM0440 37.6.15); RTC_AlarmSetSubsecond after. */
    *r.alrmssr = 0U;
    return SPL_OK;
}

static SPL_Status Rtc_AlarmEnable(uint32_t b, uint8_t interrupt)
{
    const Rtc_AlarmRegs r = Rtc_Alarm(b);
    uint32_t cr;

    SPL_REQUIRE(interrupt <= 1U, SPL_ERR_PARAM); /* normal */
    /* A stale ALRxF would fire the new alarm at once: the caller clears it with
     * RTC_AlarmxClearFlag (diagnostic flags are not cleared silently). */
    SPL_REQUIRE_SAFE(Rtc_DbpSet() != 0U, SPL_ERR_STATE);       /* SAFE: DBP = 0 yazim kaybolur */
    SPL_REQUIRE_SAFE((RTC->SR & r.flag) == 0U, SPL_ERR_STATE); /* SAFE: eski ALRxF hemen tetikler */
    cr = (Rtc_CrBase() & (uint32_t)~r.irq) | r.enable;
    if (interrupt != 0U)
    {
        cr |= r.irq;
    }
    RTC->CR = cr;
    return SPL_OK;
}

static SPL_Status Rtc_AlarmDisable(uint32_t b)
{
    const Rtc_AlarmRegs r = Rtc_Alarm(b);

    if (Rtc_DbpSet() == 0U)
    {
        return SPL_ERR_STATE;
    }
    RTC->CR = Rtc_CrBase() & (uint32_t)~(r.enable | r.irq);
    return SPL_OK;
}

static SPL_Status Rtc_AlarmFlag(uint32_t b, uint8_t *set)
{
    const Rtc_AlarmRegs r = Rtc_Alarm(b);

    SPL_REQUIRE_SAFE(set != NULL, SPL_ERR_PARAM);
    *set = ((RTC->SR & r.flag) != 0U) ? 1U : 0U;
    return SPL_OK;
}

static SPL_Status Rtc_AlarmClearFlag(uint32_t b)
{
    const Rtc_AlarmRegs r = Rtc_Alarm(b);

    if (Rtc_DbpSet() == 0U)
    {
        return SPL_ERR_STATE;
    }
    /* RTC_SCR is write-1-to-clear (RM0440 37.6.20). */
    RTC->SCR = r.clear;
    /* Geri okuma: temizleme donmeden once cevre birimine ulasir (METHOD 1.7, issue #23). */
    (void)RTC->SR;
    return SPL_OK;
}

SPL_Status RTC_AlarmAConfig(const RTC_AlarmTypeDef *alarm)
{
    return Rtc_AlarmConfig(0U, alarm);
}

SPL_Status RTC_AlarmAEnable(uint8_t interrupt)
{
    return Rtc_AlarmEnable(0U, interrupt);
}

SPL_Status RTC_AlarmADisable(void)
{
    return Rtc_AlarmDisable(0U);
}

SPL_Status RTC_AlarmAFlag(uint8_t *set)
{
    return Rtc_AlarmFlag(0U, set);
}

SPL_Status RTC_AlarmAClearFlag(void)
{
    return Rtc_AlarmClearFlag(0U);
}

SPL_Status RTC_AlarmBConfig(const RTC_AlarmTypeDef *alarm)
{
    return Rtc_AlarmConfig(1U, alarm);
}

SPL_Status RTC_AlarmBEnable(uint8_t interrupt)
{
    return Rtc_AlarmEnable(1U, interrupt);
}

SPL_Status RTC_AlarmBDisable(void)
{
    return Rtc_AlarmDisable(1U);
}

SPL_Status RTC_AlarmBFlag(uint8_t *set)
{
    return Rtc_AlarmFlag(1U, set);
}

SPL_Status RTC_AlarmBClearFlag(void)
{
    return Rtc_AlarmClearFlag(1U);
}

SPL_Status RTC_AlarmSetSubsecond(uint8_t alarm_b, uint32_t subsecond, uint8_t compare_bits)
{
    const Rtc_AlarmRegs r = Rtc_Alarm((alarm_b != 0U) ? 1U : 0U);

    SPL_REQUIRE(alarm_b <= 1U, SPL_ERR_PARAM); /* normal: 1 gibi */
    /* SAFE: SS / MASKSS tasmasi. */
    SPL_REQUIRE_SAFE((subsecond <= (RTC_ALRMASSR_SS_Msk >> RTC_ALRMASSR_SS_Pos)) &&
                         (compare_bits <= 15U),
                     SPL_ERR_PARAM);
    if (Rtc_DbpSet() == 0U)
    {
        return SPL_ERR_STATE;
    }
    SPL_REQUIRE_SAFE((RTC->CR & r.enable) == 0U,
                     SPL_ERR_STATE); /* SAFE: ALRMxR yazimi yok sayilir */
    if (((RTC->ICSR & r.writeable) == 0U) && (Rtc_InitReadyFlag() == 0U))
    {
        return Rtc_WaitStatus();
    }
    /* MASKSS = number of SS[14:0] LSBs compared (RM0440 37.6.15); 0 = no sub-second match. */
    *r.alrmssr =
        (subsecond << RTC_ALRMASSR_SS_Pos) | ((uint32_t)compare_bits << RTC_ALRMASSR_MASKSS_Pos);
    return SPL_OK;
}

SPL_Status RTC_WakeupConfig(uint32_t clock_sel, uint32_t reload)
{
    /* SAFE: Prevent invalid wakeup reload/clock combination. */
    SPL_REQUIRE_SAFE(!((clock_sel > (RTC_CR_WUCKSEL_Msk >> RTC_CR_WUCKSEL_Pos)) ||
                       (reload > (RTC_WUTR_WUT_Msk >> RTC_WUTR_WUT_Pos))),
                     SPL_ERR_PARAM);
    /* RM0440 37.6.6: WUT = 0 with WUCKSEL = 011 (RTCCLK/2) is forbidden. */
    /* SAFE: Prevent invalid wakeup reload/clock combination. */
    SPL_REQUIRE_SAFE(!((reload == 0U) && (clock_sel == 3U)), SPL_ERR_PARAM);
    if ((Rtc_DbpSet() == 0U) || ((RTC->CR & RTC_CR_WUTE) != 0U))
    {
        return SPL_ERR_STATE;
    }
    /* RM0440 37.3.8: wait WUTWF = 1 (skipped in init mode). */
    if (((RTC->ICSR & RTC_ICSR_WUTWF) == 0U) && (Rtc_InitReadyFlag() == 0U))
    {
        return Rtc_WaitStatus();
    }
    RTC->WUTR = reload << RTC_WUTR_WUT_Pos;
    RTC->CR = (Rtc_CrBase() & (uint32_t)~RTC_CR_WUCKSEL_Msk) | (clock_sel << RTC_CR_WUCKSEL_Pos);
    return SPL_OK;
}

SPL_Status RTC_WakeupEnable(uint8_t interrupt)
{
    uint32_t cr;

    /* SAFE: Prevent invalid wakeup interrupt control. */
    SPL_REQUIRE_SAFE(!(interrupt > 1U), SPL_ERR_PARAM);
    /* Stale WUTF: same rule as RTC_AlarmAEnable (clear with RTC_WakeupClearFlag). */
    if ((Rtc_DbpSet() == 0U) || ((RTC->CR & RTC_CR_WUTE) != 0U) || ((RTC->SR & RTC_SR_WUTF) != 0U))
    {
        return SPL_ERR_STATE;
    }
    /* RM0440 37.6.7 WUTE note: after disabling, wait WUTWF = 1 before enabling again. */
    if (((RTC->ICSR & RTC_ICSR_WUTWF) == 0U) && (Rtc_InitReadyFlag() == 0U))
    {
        return Rtc_WaitStatus();
    }
    cr = (Rtc_CrBase() & (uint32_t)~RTC_CR_WUTIE) | RTC_CR_WUTE;
    if (interrupt != 0U)
    {
        cr |= RTC_CR_WUTIE;
    }
    RTC->CR = cr;
    return SPL_OK;
}

SPL_Status RTC_WakeupDisable(void)
{
    if (Rtc_DbpSet() == 0U)
    {
        return SPL_ERR_STATE;
    }
    RTC->CR = Rtc_CrBase() & (uint32_t)~(RTC_CR_WUTE | RTC_CR_WUTIE);
    return SPL_OK;
}

SPL_Status RTC_WakeupFlag(uint8_t *set)
{
    /* SAFE: Reject invalid output pointer. */
    SPL_REQUIRE_SAFE(!(set == NULL), SPL_ERR_PARAM);
    *set = ((RTC->SR & RTC_SR_WUTF) != 0U) ? 1U : 0U;
    return SPL_OK;
}

SPL_Status RTC_WakeupClearFlag(void)
{
    if (Rtc_DbpSet() == 0U)
    {
        return SPL_ERR_STATE;
    }
    RTC->SCR = RTC_SCR_CWUTF;
    /* Geri okuma: temizleme donmeden once cevre birimine ulasir (METHOD 1.7, issue #23). */
    (void)RTC->SR;
    return SPL_OK;
}

SPL_Status RTC_BackupWrite(uint32_t index, uint32_t value)
{
    volatile uint32_t *bkp = &TAMP->BKP0R;

    /* SAFE: Prevent access outside backup-register array. */
    SPL_REQUIRE_SAFE(!(index >= RTC_BACKUP_NB), SPL_ERR_PARAM);
    /* RM0440 38.3.3: backup registers are write-protected by DBP. */
    if (Rtc_DbpSet() == 0U)
    {
        return SPL_ERR_STATE;
    }
    bkp[index] = value;
    return SPL_OK;
}

SPL_Status RTC_BackupRead(uint32_t index, uint32_t *value)
{
    const volatile uint32_t *bkp = &TAMP->BKP0R;

    /* SAFE: Prevent out-of-range or invalid output pointer. */
    SPL_REQUIRE_SAFE(!((index >= RTC_BACKUP_NB) || (value == NULL)), SPL_ERR_PARAM);
    *value = bkp[index];
    return SPL_OK;
}

SPL_Status RTC_TimestampConfig(uint8_t enable, uint8_t falling_edge, uint8_t internal, uint8_t irq)
{
    uint32_t cr;

    SPL_REQUIRE((enable <= 1U) && (falling_edge <= 1U) && (internal <= 1U) && (irq <= 1U),
                SPL_ERR_PARAM);
    if (Rtc_DbpSet() == 0U)
    {
        return SPL_ERR_STATE;
    }
    cr = Rtc_CrBase() & (uint32_t)~(RTC_CR_TSE | RTC_CR_TSEDGE | RTC_CR_ITSE | RTC_CR_TSIE);
    /* RM0440 37.6.7: TSE must be 0 while TSEDGE changes (else a spurious TSF). */
    RTC->CR = cr;
    cr |= ((falling_edge != 0U) ? RTC_CR_TSEDGE : 0U);
    RTC->CR = cr;
    cr |= ((enable != 0U) ? RTC_CR_TSE : 0U) | ((internal != 0U) ? RTC_CR_ITSE : 0U) |
          ((irq != 0U) ? RTC_CR_TSIE : 0U);
    RTC->CR = cr;
    return SPL_OK;
}

SPL_Status RTC_TimestampGet(RTC_TimeTypeDef *time, RTC_DateTypeDef *date, uint32_t *subsecond,
                            uint8_t *overflow)
{
    uint32_t sr;
    uint32_t tstr;
    uint32_t tsdr;
    uint32_t scr;

    SPL_REQUIRE_SAFE((time != NULL) && (date != NULL) && (subsecond != NULL) && (overflow != NULL),
                     SPL_ERR_PARAM);
    if (Rtc_DbpSet() == 0U)
    {
        return SPL_ERR_STATE;
    }
    sr = RTC->SR;
    if ((sr & (RTC_SR_TSF | RTC_SR_ITSF)) == 0U)
    {
        return SPL_WOULD_BLOCK;
    }
    /* RM0440 37.3.15: read TSTR / TSDR / TSSSR before clearing TSF (the clear frees them). */
    tstr = RTC->TSTR;
    tsdr = RTC->TSDR;
    *subsecond = RTC->TSSSR & RTC_TSSSR_SS_Msk;
    time->hours =
        Rtc_FromBcd(tstr, RTC_TSTR_HT_Msk, RTC_TSTR_HT_Pos, RTC_TSTR_HU_Msk, RTC_TSTR_HU_Pos);
    time->minutes =
        Rtc_FromBcd(tstr, RTC_TSTR_MNT_Msk, RTC_TSTR_MNT_Pos, RTC_TSTR_MNU_Msk, RTC_TSTR_MNU_Pos);
    time->seconds =
        Rtc_FromBcd(tstr, RTC_TSTR_ST_Msk, RTC_TSTR_ST_Pos, RTC_TSTR_SU_Msk, RTC_TSTR_SU_Pos);
    time->pm = ((tstr & RTC_TSTR_PM) != 0U) ? 1U : 0U;
    date->year = 0U; /* not captured by the timestamp (RM0440 37.6.12) */
    date->month =
        Rtc_FromBcd(tsdr, RTC_TSDR_MT_Msk, RTC_TSDR_MT_Pos, RTC_TSDR_MU_Msk, RTC_TSDR_MU_Pos);
    date->day =
        Rtc_FromBcd(tsdr, RTC_TSDR_DT_Msk, RTC_TSDR_DT_Pos, RTC_TSDR_DU_Msk, RTC_TSDR_DU_Pos);
    date->weekday = (uint8_t)((tsdr & RTC_TSDR_WDU_Msk) >> RTC_TSDR_WDU_Pos);
    *overflow = ((sr & RTC_SR_TSOVF) != 0U) ? 1U : 0U;
    scr = RTC_SCR_CTSF | RTC_SCR_CITSF;
    if (*overflow != 0U)
    {
        scr |= RTC_SCR_CTSOVF;
    }
    RTC->SCR = scr;
    /* Read back: the clear reaches the peripheral before return (METHOD 1.7, issue #23). */
    (void)RTC->SR;
    return SPL_OK;
}

SPL_Status RTC_SetCalibration(uint8_t add_512, uint32_t minus_pulses, uint32_t period)
{
    uint32_t calr;

    SPL_REQUIRE(add_512 <= 1U, SPL_ERR_PARAM); /* normal */
    /* SAFE: CALM tasmasi; gecersiz periyot sessizce 32 s. */
    SPL_REQUIRE_SAFE((minus_pulses <= (RTC_CALR_CALM_Msk >> RTC_CALR_CALM_Pos)) &&
                         ((period == 32U) || (period == 16U) || (period == 8U)),
                     SPL_ERR_PARAM);
    /* RM0440 37.3.12: 16 s / 8 s periods drop CALM LSBs (CALW16: bit 0, CALW8: bits 1:0 = 0).
     * SAFE: bitler sessizce duser. */
    SPL_REQUIRE_SAFE(((period != 16U) || ((minus_pulses & 1U) == 0U)) &&
                         ((period != 8U) || ((minus_pulses & 3U) == 0U)),
                     SPL_ERR_PARAM);
    if (Rtc_DbpSet() == 0U)
    {
        return SPL_ERR_STATE;
    }
    /* RM0440 37.3.12: a new value only when RECALPF = 0. */
    if ((RTC->ICSR & RTC_ICSR_RECALPF) != 0U)
    {
        return SPL_BUSY;
    }
    calr = (minus_pulses << RTC_CALR_CALM_Pos) | ((add_512 != 0U) ? RTC_CALR_CALP : 0U);
    if (period == 16U)
    {
        calr |= RTC_CALR_CALW16;
    }
    else if (period == 8U)
    {
        calr |= RTC_CALR_CALW8;
    }
    else
    {
        /* 32 s cycle: no window bit */
    }
    RTC->CALR = calr;
    return SPL_OK;
}

SPL_Status RTC_OutputConfig(const RTC_OutputTypeDef *out)
{
    uint32_t cr;

    SPL_REQUIRE_SAFE(out != NULL, SPL_ERR_PARAM);
    /* SAFE: OSEL tasmasi; open_drain_pullup 3 sessizce cekmesiz acik savak. */
    SPL_REQUIRE_SAFE((out->select <= 3U) && (out->open_drain_pullup <= 2U), SPL_ERR_PARAM);
    SPL_REQUIRE((out->calib_out <= 1U) && (out->calib_1hz <= 1U) && (out->polarity_low <= 1U) &&
                    (out->out2 <= 1U),
                SPL_ERR_PARAM); /* normal */
    if (Rtc_DbpSet() == 0U)
    {
        return SPL_ERR_STATE;
    }
    cr = Rtc_CrBase() & (uint32_t)~(RTC_CR_OSEL_Msk | RTC_CR_COE | RTC_CR_COSEL | RTC_CR_POL |
                                    RTC_CR_TAMPALRM_TYPE | RTC_CR_TAMPALRM_PU | RTC_CR_OUT2EN);
    cr |= ((uint32_t)out->select << RTC_CR_OSEL_Pos) | ((out->calib_out != 0U) ? RTC_CR_COE : 0U) |
          ((out->calib_1hz != 0U) ? RTC_CR_COSEL : 0U) |
          ((out->polarity_low != 0U) ? RTC_CR_POL : 0U) | ((out->out2 != 0U) ? RTC_CR_OUT2EN : 0U);
    if (out->open_drain_pullup != 0U)
    {
        cr |= RTC_CR_TAMPALRM_TYPE | ((out->open_drain_pullup == 2U) ? RTC_CR_TAMPALRM_PU : 0U);
    }
    RTC->CR = cr;
    return SPL_OK;
}

SPL_Status RTC_DaylightSaving(int8_t hour_step, uint8_t backup_bit)
{
    uint32_t cr;

    SPL_REQUIRE((hour_step >= -1) && (hour_step <= 1) && (backup_bit <= 1U),
                SPL_ERR_PARAM); /* normal */
    if (Rtc_DbpSet() == 0U)
    {
        return SPL_ERR_STATE;
    }
    cr = (Rtc_CrBase() & (uint32_t)~RTC_CR_BKP) | ((backup_bit != 0U) ? RTC_CR_BKP : 0U);
    if (hour_step > 0)
    {
        cr |= RTC_CR_ADD1H; /* write-only command (RM0440 37.6.7) */
    }
    else if (hour_step < 0)
    {
        cr |= RTC_CR_SUB1H;
    }
    else
    {
        /* BKP only */
    }
    RTC->CR = cr;
    return SPL_OK;
}

/* ---- TAMP (RM0444 31) ---- */

#define TAMP_INPUTS 3U /* RM0440 38: supported G4 profiles expose TAMP1..3. */

SPL_Status TAMP_InputConfig(uint8_t input, const TAMP_InputTypeDef *cfg)
{
    uint32_t bit;
    uint32_t cr2;
    uint32_t ier;

    SPL_REQUIRE_SAFE((input >= 1U) && (input <= TAMP_INPUTS),
                     SPL_ERR_PARAM); /* SAFE: baska giris */
    SPL_REQUIRE_SAFE(cfg != NULL, SPL_ERR_PARAM);
    SPL_REQUIRE((cfg->enable <= 1U) && (cfg->active_high <= 1U) && (cfg->no_erase <= 1U) &&
                    (cfg->masked <= 1U) && (cfg->irq <= 1U),
                SPL_ERR_PARAM); /* normal */
    /* RM0440 38.6.2: the interrupt must not be enabled for a masked tamper. SAFE. */
    SPL_REQUIRE_SAFE((cfg->masked == 0U) || (cfg->irq == 0U), SPL_ERR_PARAM);
    if (Rtc_DbpSet() == 0U)
    {
        return SPL_ERR_STATE;
    }
    bit = (uint32_t)input - 1U;
    TAMP->CR1 &= ~((uint32_t)TAMP_CR1_TAMP1E << bit); /* reconfigure with the input disabled */
    cr2 = (uint32_t)(((cfg->active_high != 0U) ? TAMP_CR2_TAMP1TRG : 0U) |
                     ((cfg->masked != 0U) ? TAMP_CR2_TAMP1MSK : 0U) |
                     ((cfg->no_erase != 0U) ? TAMP_CR2_TAMP1NOERASE : 0U));
    ier = (uint32_t)((cfg->irq != 0U) ? TAMP_IER_TAMP1IE : 0U);
    TAMP->CR2 =
        (TAMP->CR2 &
         ~((uint32_t)(TAMP_CR2_TAMP1TRG | TAMP_CR2_TAMP1MSK | TAMP_CR2_TAMP1NOERASE) << bit)) |
        (cr2 << bit);
    TAMP->IER = (TAMP->IER & ~((uint32_t)TAMP_IER_TAMP1IE << bit)) | (ier << bit);
    if (cfg->enable != 0U)
    {
        TAMP->CR1 |= (uint32_t)TAMP_CR1_TAMP1E << bit;
    }
    return SPL_OK;
}

SPL_Status TAMP_FilterConfig(uint8_t frequency, uint8_t filter, uint8_t precharge,
                             uint8_t pullup_disable)
{
    SPL_REQUIRE_SAFE((frequency <= 7U) && (filter <= 3U) && (precharge <= 3U),
                     SPL_ERR_PARAM);                  /* SAFE */
    SPL_REQUIRE(pullup_disable <= 1U, SPL_ERR_PARAM); /* normal */
    if (Rtc_DbpSet() == 0U)
    {
        return SPL_ERR_STATE;
    }
    /* Shared by every external input: change it with the inputs disabled. SAFE: sahte kurcalama
     * yedek register'lari siler. */
    SPL_REQUIRE_SAFE((TAMP->CR1 & TAMP_EXTERNAL_ENABLES) == 0U, SPL_ERR_STATE);
    TAMP->FLTCR = ((uint32_t)frequency << TAMP_FLTCR_TAMPFREQ_Pos) |
                  ((uint32_t)filter << TAMP_FLTCR_TAMPFLT_Pos) |
                  ((uint32_t)precharge << TAMP_FLTCR_TAMPPRCH_Pos) |
                  ((pullup_disable != 0U) ? TAMP_FLTCR_TAMPPUDIS : 0U);
    return SPL_OK;
}

SPL_Status TAMP_InternalConfig(uint32_t enable_mask, uint32_t irq_mask)
{
    /* SAFE: CR1'de dis giris etkinlestirmeleri degisirdi. */
    SPL_REQUIRE_SAFE(((enable_mask & ~TAMP_INTERNAL_ALL) == 0U) &&
                         ((irq_mask & ~TAMP_INTERNAL_ALL) == 0U),
                     SPL_ERR_PARAM);
    if (Rtc_DbpSet() == 0U)
    {
        return SPL_ERR_STATE;
    }
    /* TAMP_INTERNAL_* are CR1 ITAMPxE positions; IER uses the same positions (RM0440 38.6.4). */
    TAMP->CR1 = (TAMP->CR1 & ~TAMP_INTERNAL_ALL) | enable_mask;
    TAMP->IER = (TAMP->IER & ~TAMP_INTERNAL_ALL) | irq_mask;
    return SPL_OK;
}

SPL_Status TAMP_GetFlags(uint32_t *flags)
{
    SPL_REQUIRE_SAFE(flags != NULL, SPL_ERR_PARAM);
    *flags = TAMP->SR & TAMP_FLAGS_ALL;
    return SPL_OK;
}

SPL_Status TAMP_ClearFlags(uint32_t flags)
{
    SPL_REQUIRE(flags != 0U, SPL_ERR_PARAM); /* normal: etkisiz */
    /* SAFE (EDR-003): ayrilmis bite yazim. */
    SPL_REQUIRE_SAFE((flags & ~TAMP_FLAGS_ALL) == 0U, SPL_ERR_PARAM);
    if (Rtc_DbpSet() == 0U)
    {
        return SPL_ERR_STATE;
    }
    TAMP->SCR = flags; /* SCR clear bits share the SR positions (RM0440 38.6.7) */
    /* Read back: the clear reaches the peripheral before return (METHOD 1.7, issue #23). */
    (void)TAMP->SR;
    return SPL_OK;
}
