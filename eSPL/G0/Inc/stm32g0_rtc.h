/* SPDX-License-Identifier: MIT */
#ifndef STM32G0_RTC_H
#define STM32G0_RTC_H
#include <stdint.h>
#include "stm32g0xx.h"
#include "stm32g0_status.h"

/* RM0444 31.2: TAMP_BKP0R..TAMP_BKP4R on G0B1 (G4: up to 32; the G4 CMSIS defined the count). */
#ifndef RTC_BACKUP_NB
#define RTC_BACKUP_NB 5U
#endif

#ifdef __cplusplus
extern "C"
{
#endif

/* RTC + backup-domain clock + TAMP backup registers (RM0444 Rev 6 sections 30, 31, 5.4.23),
 * ported from the STM32G4 SPL v0.13.4. G0B1: 5 backup registers. v0.11 adds alarm B, timestamp,
 * smooth calibration, RTC output and the TAMP inputs (ES0548 2.10.2 caller note at
 * RTC_TimestampConfig).
 * Usage: RTC_LSEStart / RTC_LSEReady, RTC_ClockConfig, RTC_WriteProtectDisable, RTC_EnterInit /
 * RTC_InitReady, RTC_SetPrescaler, RTC_SetHourFormat, RTC_SetTime / RTC_SetDate, RTC_ExitInit;
 * then alarms, wake-up timer, timestamp and TAMP.
 * Common rules: every write service requires PWR_CR1.DBP = 1 (backup-domain write access) and
 * returns SPL_ERR_STATE without writing otherwise. No function waits: a flag that is not set yet
 * gives WOULD_BLOCK / BUSY (STATE when RTCCLK is absent, see RTC_GetTimeDate). Single RTC
 * instance; not reentrant: serialize all calls (ISR included).
 * Contract: Docs/v0.4_RTC_CONTRACT.md.
 * Caller duties (all APIs): RTC APB (RCC_PERIPH_RTCAPB) and PWR clocks (RCC module);
 * PWR_SetBackupAccess(1) before writes;
 * RTC_WriteProtectDisable before writing RTC registers (not checked: a locked RTC ignores the
 * write; RCC_BDCR and TAMP need only DBP); serialize all calls (ISR included); interrupts via
 * EXTI line 19 (RTC) / 21 (TAMP) and RTC_TAMP_IRQn. */

/* RTC clock source, RCC_BDCR.RTCSEL encodings (RM0444 5.4.23). */
#define RTC_CLOCK_LSE 1U
#define RTC_CLOCK_LSI 2U
#define RTC_CLOCK_HSE_DIV32 3U

/* Explicit confirmation key for RTC_BackupDomainReset (guards accidental calls). */
#define RTC_BDRST_KEY 0xBDC0FFEEUL
/* Confirmation key for RTC_BackupDomainFullReset (ES0548 2.2.11 full clear). */
#define RTC_BDRST_FULL_KEY 0xBDC0C1EAUL

/* Hour format (RTC_CR.FMT). */
#define RTC_FORMAT_24H 0U
#define RTC_FORMAT_12H 1U

/* Alarm A field masks (driver bits mapped to RTC_ALRMAR.MSK1..MSK4; 1 = field ignored). */
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

/* --- Backup-domain clock (RCC_BDCR) --- */
/* Start LSE. drive 0..3 (LSEDRV), bypass 0/1. STATE if LSE already on with a different
 * bypass/drive setting or BDRST is asserted. Does not wait for LSERDY. */
/* Caller: poll RTC_LSEReady (bounded) before RTC_ClockConfig. */
/* Level: PARAM[F] STATE[F] BUSY[r] */
SPL_Status RTC_LSEStart(uint32_t drive, uint8_t bypass);
/* Caller: bound the poll (a missing crystal never sets LSERDY). */
/* Level: PARAM[F] */
SPL_Status RTC_LSEReady(uint8_t *ready);
/* Select RTCCLK and set RTCEN. RTCSEL can be set only once: a different source than
 * the current non-zero RTCSEL is refused with STATE (use RTC_BackupDomainReset).
 * Source oscillator off => STATE; on but not ready => WOULD_BLOCK.
 * ERRATA ES0548 2.2.1: LSI as RTCCLK (or LSE CSS) can make LSI
 * unstable after a VDD-only reset: prefer LSE or HSE/32; with LSI, reset the backup domain on
 * each PWRRSTF (ES0548: BORRSTF) power-up and restore RTC/backup registers.
 * ERRATA ES0548 2.2.6: LQFP/UFQFPN: toggling PC13 disturbs the
 * LSE crystal (no workaround): keep PC13 static while LSE runs. */
/* Caller: with LSI, reset the backup domain on each PWRRSTF power-up and restore
 * (ES0548 2.2.1); keep PC13 static while LSE runs (ES0548 2.2.6). */
/* Level: PARAM[F] STATE[F,r] WOULD_BLOCK[r] */
SPL_Status RTC_ClockConfig(uint32_t source);
/* DESTRUCTIVE: pulses BDRST. Loses RTC calendar/alarms/wakeup/prescaler, all TAMP
 * backup registers, LSE on/drive/bypass, RTCSEL and RTCEN. LSCOEN/LSCOSEL are kept
 * (not reset by BDRST). key must be RTC_BDRST_KEY.
 * ERRATA ES0548 2.2.11: does not clear LSCO bits; for the
 * erratum's boot-time erase use RTC_BackupDomainFullReset. */
/* Caller: save what the application needs first; afterwards restart LSE, RTC_ClockConfig
 * and the calendar. */
/* Level: PARAM[r] STATE[F] */
SPL_Status RTC_BackupDomainReset(uint32_t key);
/* DESTRUCTIVE, ERRATA ES0548 2.2.11 workaround step 3c: BDCR =
 * BDRST only (0x0001 0000), read back, BDCR = 0. Clears everything RTC_BackupDomainReset clears
 * plus LSCOEN/LSCOSEL. key must be RTC_BDRST_FULL_KEY (RTC_BDRST_KEY -> PARAM);
 * DBP = 0 -> STATE. Deciding that the boot follows a power-on (PWRRSTF (ES0548: BORRSTF), backup
 * register marker/CRC, tamper flags) and RMVF are the caller's duty (contract). */
/* Caller: decide the power-on boot and RMVF (see above); afterwards restart LSE,
 * RTC_ClockConfig and the calendar. */
/* Level: PARAM[r] STATE[F] */
SPL_Status RTC_BackupDomainFullReset(uint32_t key);

/* --- RTC core --- */
/* Caller: none beyond the file-header duties. */
/* Level: STATE[F] */
SPL_Status RTC_WriteProtectDisable(void);
/* Caller: none beyond the file-header duties. */
/* Level: STATE[F] */
SPL_Status RTC_WriteProtectEnable(void);
/* Request init mode (ICSR.INIT = 1); poll RTC_InitReady until INITF = 1.
 * ERRATA ES0548 2.10.1: with INIT = 0, BYPSHAD = 1 -> STATE
 * (clear it with RTC_SetBypassShadow(0)); RSF = 0 -> WOULD_BLOCK (retry; e.g. right after
 * RTC_ExitInit or an MCU reset), or STATE when RTCCLK is absent. Nothing written. */
/* Caller: poll RTC_InitReady (bounded) until ready = 1 before the init-mode writes, then
 * RTC_ExitInit; retry WOULD_BLOCK with a bound. */
/* Level: STATE[F,r] WOULD_BLOCK[r] */
SPL_Status RTC_EnterInit(void);
/* Caller: bound the poll (an INIT write lost to write protection never sets INITF). */
/* Level: PARAM[F] STATE[F] */
SPL_Status RTC_InitReady(uint8_t *ready);
/* Caller: none beyond the file-header duties. */
/* Level: STATE[F] */
SPL_Status RTC_ExitInit(void);
/* INITF = 1 required. async_div 0..127 (PREDIV_A), sync_div 0..32767 (PREDIV_S). */
/* Caller: dividers for the actual RTCCLK frequency (ck_spre = 1 Hz). */
/* Level: PARAM[F] STATE[F] */
SPL_Status RTC_SetPrescaler(uint32_t async_div, uint32_t sync_div);
/* Caller: set before RTC_SetTime and the alarms (hours are checked against FMT). */
/* Level: PARAM[F] STATE[F] */
SPL_Status RTC_SetHourFormat(uint32_t format);
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] STATE[F] */
SPL_Status RTC_SetTime(const RTC_TimeTypeDef *time);
/* Caller: weekday must match the date (not checked). */
/* Level: PARAM[F] STATE[F] */
SPL_Status RTC_SetDate(const RTC_DateTypeDef *date);
/* BYPSHAD: 1 = read calendar counters directly (required when fPCLK < 7 x fRTCCLK). */
/* Caller: enable = 1 when fPCLK < 7 x fRTCCLK (frequencies not checked). */
/* Level: PARAM[F] STATE[F] */
SPL_Status RTC_SetBypassShadow(uint8_t enable);
/* RSF query (BYPSHAD = 0 only). */
/* Caller: bound the poll. */
/* Level: PARAM[F] STATE[F] */
SPL_Status RTC_SyncReady(uint8_t *synced);
/* Clear RSF (e.g. after Stop/Standby wakeup), then poll RTC_SyncReady. */
/* Caller: after Stop/Standby wake-up; poll RTC_SyncReady before RTC_GetTimeDate. */
/* Level: STATE[F] */
SPL_Status RTC_ClearSync(void);
/* RTCCLK check (GetTimeDate with BYPSHAD = 0, AlarmAConfig, WakeupConfig,
 * WakeupEnable): when the awaited RSF/ALRAWF/WUTWF flag is 0 and RTCCLK is
 * absent (RCC_BDCR.RTCEN = 0, RTCSEL = 00 or the selected oscillator off)
 * the result is STATE instead of WOULD_BLOCK: the flag can never set.
 * Selected oscillator on but not ready (starting) stays WOULD_BLOCK. */
/* Consistent read: TR then DR. BYPSHAD = 0: RSF must be set (else WOULD_BLOCK).
 * BYPSHAD = 1: TR/DR are read twice; a mismatch returns WOULD_BLOCK (retry). */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] STATE[F,r] WOULD_BLOCK[r] */
SPL_Status RTC_GetTimeDate(RTC_TimeTypeDef *time, RTC_DateTypeDef *date);

/* --- Alarm A --- */
/* ALRAE must be 0 (STATE). ALRAWF = 0 and INITF = 0 => WOULD_BLOCK. */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] STATE[F,r] WOULD_BLOCK[r] */
SPL_Status RTC_AlarmAConfig(const RTC_AlarmTypeDef *alarm);
/* Caller: interrupt = 1: EXTI line 19 and the RTC_TAMP_IRQn handler, which clears ALRAF
 * (RTC_AlarmAClearFlag). */
/* Level: PARAM[F] STATE[F] */
SPL_Status RTC_AlarmAEnable(uint8_t interrupt);
/* Caller: none beyond the file-header duties. */
/* Level: STATE[F] */
SPL_Status RTC_AlarmADisable(void);
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status RTC_AlarmAFlag(uint8_t *set);
/* Caller: none beyond the file-header duties. */
/* Level: STATE[F] */
SPL_Status RTC_AlarmAClearFlag(void);
/* --- Alarm B (v0.11): same rules as alarm A (ALRMBR / ALRBWF / ALRBF, RM0444 30.6.16) --- */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] STATE[F,r] WOULD_BLOCK[r] */
SPL_Status RTC_AlarmBConfig(const RTC_AlarmTypeDef *alarm);
/* Caller: interrupt = 1: EXTI line 19 and the RTC_TAMP_IRQn handler, which clears ALRBF
 * (RTC_AlarmBClearFlag). */
/* Level: PARAM[F] STATE[F] */
SPL_Status RTC_AlarmBEnable(uint8_t interrupt);
/* Caller: none beyond the file-header duties. */
/* Level: STATE[F] */
SPL_Status RTC_AlarmBDisable(void);
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status RTC_AlarmBFlag(uint8_t *set);
/* Caller: none beyond the file-header duties. */
/* Level: STATE[F] */
SPL_Status RTC_AlarmBClearFlag(void);
/* v0.11: sub-second match of alarm A (alarm_b = 0) or B (1): SS = subsecond (<= 0x7FFF), MASKSS =
 * compare_bits (0..15 = number of SS LSBs compared; 0 = no sub-second match). Alarm enabled ->
 * STATE; ALRxWF = 0 outside init -> WOULD_BLOCK. Call after RTC_AlarmxConfig (which writes MASKSS =
 * 0). */
/* Caller: call after RTC_AlarmAConfig / RTC_AlarmBConfig. */
/* Level: PARAM[F] STATE[F,r] WOULD_BLOCK[r] */
SPL_Status RTC_AlarmSetSubsecond(uint8_t alarm_b, uint32_t subsecond, uint8_t compare_bits);

/* --- Wakeup timer --- */
/* WUTE must be 0 (STATE). WUTWF = 0 and INITF = 0 => WOULD_BLOCK.
 * clock_sel = WUCKSEL 0..7, reload = WUT 0..0xFFFF. */
/* Caller: clock_sel / reload for the actual RTCCLK frequency. */
/* Level: PARAM[F] STATE[F,r] WOULD_BLOCK[r] */
SPL_Status RTC_WakeupConfig(uint32_t clock_sel, uint32_t reload);
/* Caller: interrupt = 1: EXTI line 19 and the RTC_TAMP_IRQn handler, which clears WUTF
 * (RTC_WakeupClearFlag). */
/* Level: PARAM[F] STATE[F,r] WOULD_BLOCK[r] */
SPL_Status RTC_WakeupEnable(uint8_t interrupt);
/* Caller: none beyond the file-header duties. */
/* Level: STATE[F] */
SPL_Status RTC_WakeupDisable(void);
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status RTC_WakeupFlag(uint8_t *set);
/* Caller: none beyond the file-header duties. */
/* Level: STATE[F] */
SPL_Status RTC_WakeupClearFlag(void);

/* --- TAMP backup registers, index 0..RTC_BACKUP_NB-1 --- */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] STATE[F] */
SPL_Status RTC_BackupWrite(uint32_t index, uint32_t value);
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status RTC_BackupRead(uint32_t index, uint32_t *value);

/* ---- v0.11: timestamp, smooth calibration, outputs, daylight saving, TAMP inputs ----
 * Docs/v0.11_TIME_ANALOG_SYSTEM.md. DBP = 0 -> STATE everywhere; RTC registers also need
 * RTC_WriteProtectDisable (caller). */
/* CR.TSE / TSEDGE / ITSE (internal tamper events timestamped) / TSIE. TSE is cleared while TSEDGE
 * changes (RM0444 30.6.7), then the new value is written. */
/* Caller: irq = 1: EXTI line 19 and the RTC_TAMP_IRQn handler, which reads and clears
 * the timestamp (RTC_TimestampGet). */
/* Level: PARAM[F] STATE[F] */
SPL_Status RTC_TimestampConfig(uint8_t enable, uint8_t falling_edge, uint8_t internal, uint8_t irq);
/* TSF / ITSF = 0 -> WOULD_BLOCK. Else TSTR / TSDR / TSSSR are read (year not captured: date->year =
 * 0), *overflow = TSOVF, then TSF / ITSF (and TSOVF) cleared. */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] STATE[F] WOULD_BLOCK[r] */
SPL_Status RTC_TimestampGet(RTC_TimeTypeDef *time, RTC_DateTypeDef *date, uint32_t *subsecond,
                            uint8_t *overflow);
/* Smooth calibration (RM0444 30.3.12): CALP = add_512, CALM = minus_pulses (0..511), cycle period
 * 32, 16 (CALM bit 0 must be 0) or 8 s (CALM bits 1:0 must be 0), else PARAM. RECALPF = 1 -> BUSY.
 */
/* Caller: retry BUSY with a bound. */
/* Level: PARAM[F] STATE[F] BUSY[r] */
SPL_Status RTC_SetCalibration(uint8_t add_512, uint32_t minus_pulses, uint32_t period);
typedef struct
{
    uint8_t select;            /* OSEL: 0 off, 1 alarm A, 2 alarm B, 3 wake-up */
    uint8_t calib_out;         /* COE: calibration output */
    uint8_t calib_1hz;         /* COSEL: 1 Hz (else 512 Hz) */
    uint8_t polarity_low;      /* POL */
    uint8_t open_drain_pullup; /* 0 push-pull, 1 open drain, 2 open drain + pull-up (TAMPALRM) */
    uint8_t out2;              /* OUT2EN: output on RTC_OUT2 */
} RTC_OutputTypeDef;
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] STATE[F] */
SPL_Status RTC_OutputConfig(const RTC_OutputTypeDef *out);
/* Daylight saving: hour_step +1 (ADD1H), -1 (SUB1H), 0 (none); BKP = backup_bit (RM0444 30.6.7). */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] STATE[F] */
SPL_Status RTC_DaylightSaving(int8_t hour_step, uint8_t backup_bit);

/* TAMP: external inputs 1..2 (1..3 on G0B0 / G0B1 / G0C1), internal tampers 3..6 (RM0444 31).
 * ERRATA ES0548 2.10.2 (G0B1): with the timestamp on tamper (TAMPTS) enabled, the LSE failure on
 * internal tamper 3 does not raise ITAMP3F / ITAMP3MF (secrets are still erased / blocked); no
 * workaround: watch the LSE CSS (RCC) as well. */
#define TAMP_INTERNAL_LSE TAMP_CR1_ITAMP3E      /* LSE monitoring */
#define TAMP_INTERNAL_HSE TAMP_CR1_ITAMP4E      /* HSE monitoring */
#define TAMP_INTERNAL_CALENDAR TAMP_CR1_ITAMP5E /* RTC calendar overflow */
#define TAMP_INTERNAL_JTAG TAMP_CR1_ITAMP6E     /* ST manufacturer readout (RM0444 31.6.1) */
#define TAMP_INTERNAL_ALL                                                                          \
    (TAMP_INTERNAL_LSE | TAMP_INTERNAL_HSE | TAMP_INTERNAL_CALENDAR | TAMP_INTERNAL_JTAG)
#if defined(TAMP_CR1_TAMP3E)
#define TAMP_EXTERNAL_ENABLES (TAMP_CR1_TAMP1E | TAMP_CR1_TAMP2E | TAMP_CR1_TAMP3E)
#else
#define TAMP_EXTERNAL_ENABLES (TAMP_CR1_TAMP1E | TAMP_CR1_TAMP2E)
#endif
/* SR flags (and SCR clear bits, same positions): external inputs + internal tampers. */
#define TAMP_FLAGS_ALL (TAMP_EXTERNAL_ENABLES | TAMP_INTERNAL_ALL)
typedef struct
{
    uint8_t enable;      /* TAMPxE */
    uint8_t active_high; /* TAMPxTRG */
    uint8_t no_erase;    /* TAMPxNOER: backup registers not erased on the event */
    uint8_t masked;      /* TAMPxMSK (no interrupt allowed then) */
    uint8_t irq;         /* TAMPxIE */
} TAMP_InputTypeDef;
/* Input 1..TAMP_INPUTS: CR2 / IER bits, input disabled while reconfigured, then TAMPxE. masked +
 * irq
 * -> PARAM (RM0444 31.6.2). */
/* Caller: TAMP_FilterConfig before enabling any input; irq = 1: EXTI line 21 and the
 * RTC_TAMP_IRQn handler, which clears the flags (TAMP_ClearFlags). */
/* Level: PARAM[F] STATE[F] */
SPL_Status TAMP_InputConfig(uint8_t input, const TAMP_InputTypeDef *cfg);
/* FLTCR (passive inputs): frequency 0..7, filter 0..3, precharge 0..3, pull-up disable. An external
 * input enabled -> STATE (shared by all inputs). */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] STATE[F] */
SPL_Status TAMP_FilterConfig(uint8_t frequency, uint8_t filter, uint8_t precharge,
                             uint8_t pullup_disable);
/* CR1.ITAMPxE = enable_mask, IER.ITAMPxIE = irq_mask (subsets of TAMP_INTERNAL_ALL). */
/* Caller: with TAMP_INTERNAL_LSE also watch the LSE CSS (ES0548 2.10.2); irq: EXTI
 * line 21 and the RTC_TAMP_IRQn handler, which clears the flags. */
/* Level: PARAM[F] STATE[F] */
SPL_Status TAMP_InternalConfig(uint32_t enable_mask, uint32_t irq_mask);
/* *flags = SR & TAMP_FLAGS_ALL. */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] */
SPL_Status TAMP_GetFlags(uint32_t *flags);
/* SCR = flags (non-empty subset of TAMP_FLAGS_ALL), then SR read back. */
/* Caller: none beyond the file-header duties. */
/* Level: PARAM[F] STATE[F] */
SPL_Status TAMP_ClearFlags(uint32_t flags);

#ifdef __cplusplus
}
#endif
#endif
