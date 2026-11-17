/* SPDX-License-Identifier: MIT */
#ifndef STM32G4_RTC_H
#define STM32G4_RTC_H
#include <stddef.h>
#include <stdint.h>
#include "stm32g4xx.h"
#include "stm32g4_status.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* RTC + backup-domain clock + TAMP (RM0440 Rev 9 sections 37, 38, 7.4.27): LSE and RTCCLK,
 * calendar, alarms A/B, wake-up timer, timestamp, smooth calibration, RTC output, daylight
 * saving, TAMP inputs 1..3, internal tampers 3..6 and backup registers.
 * Usage: RTC_LSEStart / RTC_LSEReady, RTC_ClockConfig, RTC_WriteProtectDisable, RTC_EnterInit /
 *   RTC_InitReady, RTC_SetPrescaler, RTC_SetHourFormat, RTC_SetTime / RTC_SetDate, RTC_ExitInit,
 *   RTC_WriteProtectEnable, RTC_SyncReady, RTC_GetTimeDate; then alarms, wake-up timer,
 *   timestamp and TAMP.
 * Common rules: every write service requires PWR_CR1.DBP = 1 (backup-domain write access) and
 *   returns SPL_ERR_STATE without writing otherwise. No function waits: a flag that is not set
 *   yet gives WOULD_BLOCK / BUSY (STATE when RTCCLK is absent, see RTC_GetTimeDate). RTC_CR
 *   read-modify-writes never write ADD1H/SUB1H back. Flag clears write only the requested W1C
 *   bits and read the status register back. Single RTC instance, no static state.
 * Caller duties (all APIs): RTC APB clock (RCC_APB1ENR1.RTCAPBEN) and PWR clock (RCC module);
 *   PWR_CR1.DBP = 1 before writes; RTC_WriteProtectDisable before writing RTC registers (not
 *   checked: RTC_WPR cannot be read and a locked RTC ignores the write; RCC_BDCR and TAMP need
 *   only DBP); serialize all calls (ISR included); interrupts need their EXTI line and NVIC
 *   setup (RTC_Alarm_IRQn, RTC_WKUP_IRQn, RTC_TAMP_LSECSS_IRQn), outside this driver.
 * Contract: Docs/v0.10_RTC_CONTRACT.md, Docs/v0.18_RTC_TAMP_CONTRACT.md. */

/* RTC clock source, RCC_BDCR.RTCSEL encodings (RM0440 7.4.27). */
#define RTC_CLOCK_LSE 1U
#define RTC_CLOCK_LSI 2U
#define RTC_CLOCK_HSE_DIV32 3U

/* Explicit confirmation key for RTC_BackupDomainReset (guards accidental calls). */
#define RTC_BDRST_KEY 0xBDC0FFEEUL
/* Confirmation key for RTC_BackupDomainFullReset (ES0431/ES0430 2.2.5 full clear). */
#define RTC_BDRST_FULL_KEY 0xBDC0C1EAUL

/* Hour format (RTC_CR.FMT). */
#define RTC_FORMAT_24H 0U
#define RTC_FORMAT_12H 1U

/* Alarm field masks (driver bits mapped to RTC_ALRMxR.MSK1..MSK4; 1 = field ignored). */
#define RTC_ALARM_MASK_SECONDS 0x01U
#define RTC_ALARM_MASK_MINUTES 0x02U
#define RTC_ALARM_MASK_HOURS 0x04U
#define RTC_ALARM_MASK_DATE 0x08U
#define RTC_ALARM_MASK_ALL 0x0FU

    /* Binary (not BCD) calendar values; the driver converts to/from BCD. */
    typedef struct
    {
        uint8_t hours;   /* 0..23 in 24h format, 1..12 in 12h format */
        uint8_t minutes; /* 0..59 */
        uint8_t seconds; /* 0..59 */
        uint8_t pm;      /* 0 = AM/24h, 1 = PM (12h format only) */
    } RTC_TimeTypeDef;

    typedef struct
    {
        uint8_t year;    /* 0..99 = 2000..2099 */
        uint8_t month;   /* 1..12 */
        uint8_t day;     /* 1..28/29/30/31 */
        uint8_t weekday; /* 1 = Monday .. 7 = Sunday */
    } RTC_DateTypeDef;

    typedef struct
    {
        uint8_t hours;       /* as RTC_TimeTypeDef, checked against the current FMT */
        uint8_t minutes;     /* 0..59 */
        uint8_t seconds;     /* 0..59 */
        uint8_t pm;          /* 0/1, 12h format only */
        uint8_t day;         /* date 1..31, or weekday 1..7 when weekday_sel = 1 */
        uint8_t weekday_sel; /* 0 = day is a date, 1 = day is a weekday */
        uint8_t mask;        /* OR of RTC_ALARM_MASK_* */
    } RTC_AlarmTypeDef;

    typedef struct
    {
        uint8_t select;            /* OSEL: 0 off, 1 alarm A, 2 alarm B, 3 wake-up */
        uint8_t calib_out;         /* COE: calibration output */
        uint8_t calib_1hz;         /* COSEL: 1 Hz (else 512 Hz) */
        uint8_t polarity_low;      /* POL */
        uint8_t open_drain_pullup; /* 0 push-pull, 1 open drain, 2 open drain + pull-up */
        uint8_t out2;              /* OUT2EN: output on RTC_OUT2 */
    } RTC_OutputTypeDef;

#define TAMP_INTERNAL_LSE TAMP_CR1_ITAMP3E
#define TAMP_INTERNAL_HSE TAMP_CR1_ITAMP4E
#define TAMP_INTERNAL_CALENDAR TAMP_CR1_ITAMP5E
#define TAMP_INTERNAL_JTAG TAMP_CR1_ITAMP6E
#define TAMP_INTERNAL_ALL                                                                          \
    (TAMP_INTERNAL_LSE | TAMP_INTERNAL_HSE | TAMP_INTERNAL_CALENDAR | TAMP_INTERNAL_JTAG)
#define TAMP_EXTERNAL_ENABLES (TAMP_CR1_TAMP1E | TAMP_CR1_TAMP2E | TAMP_CR1_TAMP3E)
#define TAMP_FLAGS_ALL (TAMP_EXTERNAL_ENABLES | TAMP_INTERNAL_ALL)

    typedef struct
    {
        uint8_t enable;
        uint8_t active_high;
        uint8_t no_erase;
        uint8_t masked;
        uint8_t irq;
    } TAMP_InputTypeDef;

    /* --- Backup-domain clock (RCC_BDCR) --- */
    /* Start LSE. drive 0..3 (LSEDRV), bypass 0/1. STATE if LSE already on with a different
     * bypass/drive setting or BDRST is asserted. Does not wait for LSERDY.
     * Caller: drive and bypass for the board crystal / clock source (datasheet); poll
     * RTC_LSEReady (bounded) before RTC_ClockConfig. */
    /* Level: PARAM[F] STATE[r] BUSY[r] */
    SPL_Status RTC_LSEStart(uint32_t drive, uint8_t bypass);
    /* *ready = RCC_BDCR.LSERDY.
     * Caller: bound the poll (a missing crystal never sets LSERDY). */
    /* Level: PARAM[F] */
    SPL_Status RTC_LSEReady(uint8_t *ready);
    /* Select RTCCLK and set RTCEN. RTCSEL can be set only once: a different source than
     * the current non-zero RTCSEL is refused with STATE (use RTC_BackupDomainReset).
     * Source oscillator off => STATE; on but not ready => WOULD_BLOCK.
     * ERRATA ES0431 2.2.3 / ES0430 2.2.4 (not in ES0523): LSI as RTCCLK (or LSE CSS) can make LSI
     * unstable after a VDD-only reset: prefer LSE or HSE/32; with LSI, reset the backup domain on
     * each BORRSTF power-up and restore RTC/backup registers.
     * ERRATA ES0431 2.2.8 / ES0430 2.2.8 / ES0523 2.2.6: LQFP/UFQFPN: toggling PC13 disturbs the
     * LSE crystal (no workaround): keep PC13 static while LSE runs.
     * Caller: start the source oscillator first (LSE here, LSI / HSE in RCC); with LSI, reset
     * the backup domain on each BORRSTF power-up and restore (errata 2.2.3 / 2.2.4); keep PC13
     * static while LSE runs (errata 2.2.8 / 2.2.6). */
    /* Level: PARAM[F] STATE[r] WOULD_BLOCK[r] */
    SPL_Status RTC_ClockConfig(uint32_t source);
    /* DESTRUCTIVE: pulses BDRST. Loses RTC calendar/alarms/wakeup/prescaler, all TAMP
     * backup registers, LSE on/drive/bypass, RTCSEL and RTCEN. LSCOEN/LSCOSEL are kept
     * (not reset by BDRST). key must be RTC_BDRST_KEY.
     * ERRATA ES0431 2.2.5 / ES0430 2.2.5 / ES0523 2.2.3: does not clear LSCO bits; for the
     * erratum's boot-time erase use RTC_BackupDomainFullReset.
     * Caller: save what the application needs first; afterwards restart LSE, RTC_ClockConfig
     * and the calendar. */
    /* Level: PARAM[r] STATE[r] */
    SPL_Status RTC_BackupDomainReset(uint32_t key);
    /* DESTRUCTIVE, ERRATA ES0431 2.2.5 / ES0430 2.2.5 / ES0523 2.2.3 workaround step 3c: BDCR =
     * BDRST only (0x0001 0000), read back, BDCR = 0. Clears everything RTC_BackupDomainReset clears
     * plus LSCOEN/LSCOSEL. key must be RTC_BDRST_FULL_KEY (RTC_BDRST_KEY -> PARAM);
     * DBP = 0 -> STATE.
     * Caller: decide that the boot follows a power-on (BORRSTF, backup register marker/CRC,
     * tamper flags) and clear the reset flags with RMVF (RCC); afterwards restart LSE,
     * RTC_ClockConfig and the calendar. */
    /* Level: PARAM[r] STATE[r] */
    SPL_Status RTC_BackupDomainFullReset(uint32_t key);

    /* --- RTC core --- */
    /* Unlocks the RTC registers: WPR = 0xCA, 0x53.
     * Caller: relock with RTC_WriteProtectEnable when the RTC writes are done. */
    /* Level: STATE[r] */
    SPL_Status RTC_WriteProtectDisable(void);
    /* Relocks the RTC registers: WPR = 0xFF.
     * Caller: none beyond the file-header duties. */
    /* Level: STATE[r] */
    SPL_Status RTC_WriteProtectEnable(void);
    /* Request init mode (ICSR.INIT = 1); poll RTC_InitReady until INITF = 1.
     * ERRATA ES0431 2.10.1 / ES0430 2.14.1 / ES0523 2.10.1: with INIT = 0, BYPSHAD = 1 -> STATE
     * (clear it with RTC_SetBypassShadow(0)); RSF = 0 -> WOULD_BLOCK (retry; e.g. right after
     * RTC_ExitInit or an MCU reset), or STATE when RTCCLK is absent. Nothing written.
     * Caller: retry WOULD_BLOCK with a bound; poll RTC_InitReady (bounded) until ready = 1
     * before the init-mode writes, then RTC_ExitInit. */
    /* Level: STATE[r] WOULD_BLOCK[r] */
    SPL_Status RTC_EnterInit(void);
    /* *ready = ICSR.INITF.
     * Caller: bound the poll (an INIT write lost to write protection never sets INITF). */
    /* Level: PARAM[F] STATE[r] */
    SPL_Status RTC_InitReady(uint8_t *ready);
    /* Leaves init mode (ICSR = 0: INIT = 0, RSF cleared).
     * Caller: with BYPSHAD = 0 poll RTC_SyncReady before reading the calendar. */
    /* Level: STATE[r] */
    SPL_Status RTC_ExitInit(void);
    /* INITF = 1 required. async_div 0..127 (PREDIV_A), sync_div 0..32767 (PREDIV_S).
     * Caller: dividers for the actual RTCCLK frequency (ck_spre = 1 Hz; not checked). */
    /* Level: PARAM[F] STATE[r] */
    SPL_Status RTC_SetPrescaler(uint32_t async_div, uint32_t sync_div);
    /* RTC_CR.FMT; INITF = 1 required.
     * Caller: set before RTC_SetTime and the alarms (hours are checked against FMT). */
    /* Level: PARAM[F] STATE[r] */
    SPL_Status RTC_SetHourFormat(uint32_t format);
    /* TR = BCD of *time; INITF = 1 required; hours checked against the current FMT.
     * Caller: none beyond the file-header duties. */
    /* Level: PARAM[F] STATE[r] */
    SPL_Status RTC_SetTime(const RTC_TimeTypeDef *time);
    /* DR = BCD of *date; INITF = 1 required; day checked against month and year.
     * Caller: weekday must match the date (not checked). */
    /* Level: PARAM[F] STATE[r] */
    SPL_Status RTC_SetDate(const RTC_DateTypeDef *date);
    /* BYPSHAD: 1 = read calendar counters directly (required when fPCLK < 7 x fRTCCLK).
     * Caller: enable = 1 when fPCLK < 7 x fRTCCLK (frequencies not checked). */
    /* Level: PARAM[F] STATE[r] */
    SPL_Status RTC_SetBypassShadow(uint8_t enable);
    /* RSF query (BYPSHAD = 0 only).
     * Caller: bound the poll. */
    /* Level: PARAM[F] STATE[r] */
    SPL_Status RTC_SyncReady(uint8_t *synced);
    /* Clear RSF (e.g. after Stop/Standby wakeup), then poll RTC_SyncReady.
     * Caller: call after Stop/Standby wake-up; poll RTC_SyncReady before RTC_GetTimeDate. */
    /* Level: STATE[r] */
    SPL_Status RTC_ClearSync(void);
    /* RTCCLK check (GetTimeDate with BYPSHAD = 0, AlarmAConfig, WakeupConfig,
     * WakeupEnable): when the awaited RSF/ALRAWF/WUTWF flag is 0 and RTCCLK is
     * absent (RCC_BDCR.RTCEN = 0, RTCSEL = 00 or the selected oscillator off)
     * the result is STATE instead of WOULD_BLOCK: the flag can never set.
     * Selected oscillator on but not ready (starting) stays WOULD_BLOCK. */
    /* Consistent read: TR then DR. BYPSHAD = 0: RSF must be set (else WOULD_BLOCK).
     * BYPSHAD = 1: TR/DR are read twice; a mismatch returns WOULD_BLOCK (retry).
     * Caller: retry WOULD_BLOCK with a bound. */
    /* Level: PARAM[F] STATE[r] WOULD_BLOCK[r] */
    SPL_Status RTC_GetTimeDate(RTC_TimeTypeDef *time, RTC_DateTypeDef *date);

    /* --- Alarm A --- */
    /* ALRAE must be 0 (STATE). ALRAWF = 0 and INITF = 0 => WOULD_BLOCK.
     * Caller: none beyond the file-header duties. */
    /* Level: PARAM[F] STATE[F,r] WOULD_BLOCK[r] */
    SPL_Status RTC_AlarmAConfig(const RTC_AlarmTypeDef *alarm);
    /* CR.ALRAE = 1, ALRAIE = interrupt. Stale ALRAF -> STATE (clear with RTC_AlarmAClearFlag).
     * Caller: configure with RTC_AlarmAConfig first; interrupt = 1: EXTI line and
     * RTC_Alarm_IRQn handler, which clears ALRAF (RTC_AlarmAClearFlag). */
    /* Level: PARAM[F] STATE[F] */
    SPL_Status RTC_AlarmAEnable(uint8_t interrupt);
    /* CR.ALRAE = 0, ALRAIE = 0; ALRAF is not cleared.
     * Caller: none beyond the file-header duties. */
    /* Level: STATE[r] */
    SPL_Status RTC_AlarmADisable(void);
    /* *set = SR.ALRAF; no write.
     * Caller: none. */
    /* Level: PARAM[F] */
    SPL_Status RTC_AlarmAFlag(uint8_t *set);
    /* SCR = CALRAF only, then SR read back.
     * Caller: none beyond the file-header duties. */
    /* Level: STATE[r] */
    SPL_Status RTC_AlarmAClearFlag(void);

    /* --- Alarm B and sub-second alarm comparison --- */
    /* Same rules as alarm A (ALRMBR / ALRBWF / ALRBF).
     * Caller: none beyond the file-header duties. */
    /* Level: PARAM[F] STATE[F,r] WOULD_BLOCK[r] */
    SPL_Status RTC_AlarmBConfig(const RTC_AlarmTypeDef *alarm);
    /* CR.ALRBE = 1, ALRBIE = interrupt. Stale ALRBF -> STATE (clear with RTC_AlarmBClearFlag).
     * Caller: configure with RTC_AlarmBConfig first; interrupt = 1: EXTI line and
     * RTC_Alarm_IRQn handler, which clears ALRBF (RTC_AlarmBClearFlag). */
    /* Level: PARAM[F] STATE[F] */
    SPL_Status RTC_AlarmBEnable(uint8_t interrupt);
    /* CR.ALRBE = 0, ALRBIE = 0; ALRBF is not cleared.
     * Caller: none beyond the file-header duties. */
    /* Level: STATE[r] */
    SPL_Status RTC_AlarmBDisable(void);
    /* *set = SR.ALRBF; no write.
     * Caller: none. */
    /* Level: PARAM[F] */
    SPL_Status RTC_AlarmBFlag(uint8_t *set);
    /* SCR = CALRBF only, then SR read back.
     * Caller: none beyond the file-header duties. */
    /* Level: STATE[r] */
    SPL_Status RTC_AlarmBClearFlag(void);
    /* alarm_b: 0=A, 1=B; subsecond <= 0x7fff; compare_bits 0..15 (number of SS LSBs compared,
     * 0 = no sub-second match). Alarm enabled -> STATE; ALRxWF = 0 outside init -> WOULD_BLOCK.
     * Caller: call after RTC_AlarmAConfig / RTC_AlarmBConfig (they write MASKSS = 0). */
    /* Level: PARAM[F] STATE[F,r] WOULD_BLOCK[r] */
    SPL_Status RTC_AlarmSetSubsecond(uint8_t alarm_b, uint32_t subsecond, uint8_t compare_bits);

    /* --- Wakeup timer --- */
    /* WUTE must be 0 (STATE). WUTWF = 0 and INITF = 0 => WOULD_BLOCK.
     * clock_sel = WUCKSEL 0..7, reload = WUT 0..0xFFFF.
     * Caller: clock_sel / reload for the actual RTCCLK frequency. */
    /* Level: PARAM[F] STATE[r] WOULD_BLOCK[r] */
    SPL_Status RTC_WakeupConfig(uint32_t clock_sel, uint32_t reload);
    /* CR.WUTE = 1, WUTIE = interrupt. Stale WUTF -> STATE; WUTWF = 0 outside init ->
     * WOULD_BLOCK.
     * Caller: configure with RTC_WakeupConfig first; interrupt = 1: EXTI line and
     * RTC_WKUP_IRQn handler, which clears WUTF (RTC_WakeupClearFlag). */
    /* Level: PARAM[F] STATE[r] WOULD_BLOCK[r] */
    SPL_Status RTC_WakeupEnable(uint8_t interrupt);
    /* CR.WUTE = 0, WUTIE = 0; WUTF is not cleared.
     * Caller: none beyond the file-header duties. */
    /* Level: STATE[r] */
    SPL_Status RTC_WakeupDisable(void);
    /* *set = SR.WUTF; no write.
     * Caller: none. */
    /* Level: PARAM[F] */
    SPL_Status RTC_WakeupFlag(uint8_t *set);
    /* SCR = CWUTF only, then SR read back.
     * Caller: none beyond the file-header duties. */
    /* Level: STATE[r] */
    SPL_Status RTC_WakeupClearFlag(void);

    /* --- TAMP backup registers, index 0..RTC_BKP_COUNT-1 --- */
    /* Number of backup registers the driver accesses: RTC_BACKUP_NB of the CMSIS device header,
     * limited to the BKPxR members of the CMSIS TAMP_TypeDef. The two disagree in
     * stm32g411xc.h (RTC_BACKUP_NB 32, TAMP_TypeDef BKP0R..BKP15R; Docs/HW_DEFINITIONS.md HW-007):
     * the driver never addresses past the register block CMSIS declares. 16 on G431 / G441 /
     * G411xB / G411xC, 32 on the other SKUs. */
#define RTC_BKP_STRUCT_WORDS                                                                       \
    ((uint32_t)((sizeof(TAMP_TypeDef) - offsetof(TAMP_TypeDef, BKP0R)) / sizeof(uint32_t)))
#define RTC_BKP_COUNT                                                                              \
    (((uint32_t)RTC_BACKUP_NB < RTC_BKP_STRUCT_WORDS) ? (uint32_t)RTC_BACKUP_NB                    \
                                                      : RTC_BKP_STRUCT_WORDS)
    /* TAMP BKPxR = value (DBP only, no RTC_WPR).
     * Caller: none beyond the file-header duties. */
    /* Level: PARAM[F] STATE[r] */
    SPL_Status RTC_BackupWrite(uint32_t index, uint32_t value);
    /* *value = TAMP BKPxR.
     * Caller: none beyond the file-header duties. */
    /* Level: PARAM[F] */
    SPL_Status RTC_BackupRead(uint32_t index, uint32_t *value);

    /* --- Timestamp, calibration, output and daylight saving --- */
    /* CR.TSE / TSEDGE / ITSE / TSIE; TSE is cleared while TSEDGE changes (RM0440 37.6.7).
     * Caller duty (CCL-2 KSM-12): irq = 1 does not check TSF/ITSF; clear them first
     * (RTC_TimestampGet reads and clears them) before enabling the interrupt; irq = 1 also
     * needs the EXTI line and the RTC_TAMP_LSECSS_IRQn handler. */
    /* Level: PARAM[F] STATE[r] */
    SPL_Status RTC_TimestampConfig(uint8_t enable, uint8_t falling_edge, uint8_t internal,
                                   uint8_t irq);
    /* Side effect: TSF / ITSF (TSOVF when set) are cleared after TSTR / TSDR / TSSSR are read.
     * TSF / ITSF = 0 -> WOULD_BLOCK. date->year = 0 (not captured by the timestamp).
     * Caller: none beyond the file-header duties. */
    /* Level: PARAM[F] STATE[r] WOULD_BLOCK[r] */
    SPL_Status RTC_TimestampGet(RTC_TimeTypeDef *time, RTC_DateTypeDef *date, uint32_t *subsecond,
                                uint8_t *overflow);
    /* Smooth calibration (RM0440 37.3.12): CALP = add_512, CALM = minus_pulses (0..511), cycle
     * period 32, 16 (CALM bit 0 = 0) or 8 s (CALM bits 1:0 = 0), else PARAM. RECALPF = 1 -> BUSY.
     * Caller: retry BUSY with a bound. */
    /* Level: PARAM[F] STATE[r] BUSY[r] */
    SPL_Status RTC_SetCalibration(uint8_t add_512, uint32_t minus_pulses, uint32_t period);
    /* RTC_CR OSEL / COE / COSEL / POL / TAMPALRM type and pull-up / OUT2EN in one write.
     * Caller: RTC_OUT1 is on PC13: an output that toggles there disturbs a running LSE (errata
     * 2.2.8 / 2.2.6 at RTC_ClockConfig); open_drain_pullup for the board circuit. */
    /* Level: PARAM[F] STATE[r] */
    SPL_Status RTC_OutputConfig(const RTC_OutputTypeDef *out);
    /* hour_step +1 (ADD1H), -1 (SUB1H), 0 (none); BKP = backup_bit (RM0440 37.6.7).
     * Caller: none beyond the file-header duties. */
    /* Level: PARAM[F] STATE[r] */
    SPL_Status RTC_DaylightSaving(int8_t hour_step, uint8_t backup_bit);

    /* --- TAMP external and internal event control --- */
    /* ERRATA ES0431 2.10.3 / ES0430 2.14.3 / ES0523 2.10.3 (no workaround, caller): with
     * timestamp-on-tamper (RTC_CR.TAMPTS, set outside this API) a tamper event can be lost for four
     * APB cycles and two ck_apre cycles after clearing TSF; do not use that interval as the only
     * security signal.
     * ERRATA ES0431 2.10.5 / ES0430 2.14.5 / ES0523 2.10.5 (no workaround, caller): with TAMPTS an
     * LSE failure can erase or block secrets without setting ITAMP3F; monitor the RCC LSE CSS
     * independently when ITAMP3 is security relevant. */
    /* Input 1..3: input disabled while CR2 / IER are written, then TAMPxE; masked + irq ->
     * PARAM (RM0440 38.6.2).
     * Caller duty (CCL-2 KSM-12): the irq bit does not check TAMP_SR; clear stale flags with
     * TAMP_ClearFlags before enabling the interrupt; TAMP_FilterConfig before enabling any
     * input; irq = 1 needs the EXTI line and the RTC_TAMP_LSECSS_IRQn handler; tamper pin
     * setup is outside this driver. */
    /* Level: PARAM[F] STATE[r] */
    SPL_Status TAMP_InputConfig(uint8_t input, const TAMP_InputTypeDef *cfg);
    /* FLTCR: frequency 0..7, filter 0..3, precharge 0..3, pull-up disable. An external input
     * enabled -> STATE (FLTCR is shared by all inputs).
     * Caller: none beyond the file-header duties. */
    /* Level: PARAM[F] STATE[F,r] */
    SPL_Status TAMP_FilterConfig(uint8_t frequency, uint8_t filter, uint8_t precharge,
                                 uint8_t pullup_disable);
    /* CR1.ITAMPxE = enable_mask, IER.ITAMPxIE = irq_mask (subsets of TAMP_INTERNAL_ALL).
     * Caller duty (CCL-2 KSM-12): irq_mask does not check TAMP_SR; clear stale flags with
     * TAMP_ClearFlags before enabling the interrupt; with TAMP_INTERNAL_LSE also watch the RCC
     * LSE CSS (errata 2.10.5 above). */
    /* Level: PARAM[F] STATE[r] */
    SPL_Status TAMP_InternalConfig(uint32_t enable_mask, uint32_t irq_mask);
    /* *flags = TAMP_SR & TAMP_FLAGS_ALL; no write.
     * Caller: none. */
    /* Level: PARAM[F] */
    SPL_Status TAMP_GetFlags(uint32_t *flags);
    /* SCR = flags (non-empty subset of TAMP_FLAGS_ALL), then SR read back.
     * Caller: none beyond the file-header duties. */
    /* Level: PARAM[F] STATE[r] */
    SPL_Status TAMP_ClearFlags(uint32_t flags);

#ifdef __cplusplus
}
#endif
#endif
