/* SPDX-License-Identifier: MIT */
/* RTC: calendar clock on the 32.768 kHz LSE crystal (X2 on NUCLEO-G0B1RE). The time is set once
 * (2026-01-01 12:00:00, Thursday) and then printed every second. A marker in backup register 0
 * tells a reset from a power-up: after a reset (NRST button) the RTC keeps running and the time
 * is not set again.
 * Prescalers for 32768 Hz: (127 + 1) x (255 + 1) = 32768 -> 1 Hz calendar clock.
 * Shows: backup-domain write access (PWR clock + DBP) before any RTC / RCC_BDCR write, LSE start
 * and bounded ready poll (a missing crystal never gets ready), RTC unlock and init mode, a
 * consistent time / date read (WOULD_BLOCK until the shadow registers are synchronized).
 * ES0548 2.2.6: toggling PC13 disturbs the LSE crystal; this example keeps PC13 (button) still
 * and does not read it. */
#include "ex_board.h"

#define BKP_MARKER_INDEX 0U
#define BKP_MARKER 0x52544331UL /* "RTC1" */
#define LSE_DRIVE 1U            /* LSEDRV medium-low; raise it if the crystal does not start */

static void Print2(uint32_t v)
{
    if (v < 10U)
    {
        Ex_Print("0");
    }
    Ex_PrintU32(v);
}

static void RtcSetCalendar(void)
{
    RTC_TimeTypeDef time = {12U, 0U, 0U, 0U};
    RTC_DateTypeDef date = {26U, 1U, 1U, 4U}; /* 2026-01-01, weekday 4 = Thursday */
    uint8_t ready = 0U;

    EX_CHECK(RTC_WriteProtectDisable());
    EX_WAIT(RTC_EnterInit(), 1100U);
    EX_WAIT_FLAG(RTC_InitReady(&ready), ready, 100U);
    EX_CHECK(RTC_SetPrescaler(127U, 255U));
    EX_CHECK(RTC_SetHourFormat(RTC_FORMAT_24H));
    EX_CHECK(RTC_SetTime(&time));
    EX_CHECK(RTC_SetDate(&date));
    EX_CHECK(RTC_ExitInit());
    EX_CHECK(RTC_WriteProtectEnable());
    EX_CHECK(RTC_BackupWrite(BKP_MARKER_INDEX, BKP_MARKER));
}

int main(void)
{
    RTC_TimeTypeDef time;
    RTC_DateTypeDef date;
    uint32_t marker = 0U;
    uint8_t last_second = 0xFFU;
    uint8_t ready = 0U;

    Ex_Init();
    Ex_Print("\r\nex_rtc: calendar on LSE\r\n");

    EX_CHECK(RCC_EnablePeriphClock(RCC_PERIPH_PWR));
    EX_CHECK(RCC_EnablePeriphClock(RCC_PERIPH_RTCAPB));
    EX_CHECK(PWR_SetBackupAccess(1U)); /* DBP = 1: backup domain writable */

    EX_CHECK(RTC_LSEStart(LSE_DRIVE, 0U)); /* OK also when the LSE already runs (reset) */
    EX_WAIT_FLAG(RTC_LSEReady(&ready), ready, 3000U); /* crystal start-up can take ~2 s */
    EX_WAIT(RTC_ClockConfig(RTC_CLOCK_LSE), 10U);     /* RTCSEL = LSE, RTCEN = 1 */

    EX_CHECK(RTC_BackupRead(BKP_MARKER_INDEX, &marker));
    if (marker != BKP_MARKER)
    {
        Ex_Print("first start: setting 2026-01-01 12:00:00\r\n");
        RtcSetCalendar();
    }
    else
    {
        Ex_Print("RTC already running, time kept\r\n");
    }

    for (;;)
    {
        /* WOULD_BLOCK until RSF: the shadow registers are synchronized with the RTC domain. */
        EX_WAIT(RTC_GetTimeDate(&time, &date), 1100U);
        if (time.seconds != last_second)
        {
            last_second = time.seconds;
            Ex_Print("20");
            Print2(date.year);
            Ex_Print("-");
            Print2(date.month);
            Ex_Print("-");
            Print2(date.day);
            Ex_Print(" ");
            Print2(time.hours);
            Ex_Print(":");
            Print2(time.minutes);
            Ex_Print(":");
            Print2(time.seconds);
            Ex_Print("\r\n");
            Ex_LedToggle();
        }
        Ex_DelayMs(100U);
    }
}
