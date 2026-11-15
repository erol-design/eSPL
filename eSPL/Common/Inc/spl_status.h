/* eSPL - shared status codes (METHOD/README.md section 1.1, section 9 option (b)).
 *
 * The single definition of SPL_Status for every family (STM32G4, STM32G0, ...). Family headers
 * Inc/stm32<fam>_status.h keep only SPL_VERSION_* and include this file by relative path, so
 * existing #include "stm32<fam>_status.h" lines and include paths stay unchanged.
 *
 * The values MUST NOT change: callers, logs and protocol error fields read the same on every
 * family. A new code is added only by a METHOD revision, at the end.
 * Checked by METHOD/tests/run_spl_status.sh and each family's Tests/run_method_status.sh. */
#ifndef SPL_STATUS_H
#define SPL_STATUS_H

#ifdef __cplusplus
extern "C"
{
#endif

/* Shared SPL result codes. The hardware cause is available from the peripheral flags.
 * Rule of thumb: if waiting cannot fix it, it is SPL_ERR_STATE, never BUSY / WOULD_BLOCK. */
typedef enum
{
    SPL_OK = 0,              /* Success. */
    SPL_ERR_PARAM = 1,       /* Invalid argument; no register writes. */
    SPL_ERR_STATE = 2,       /* Not in an allowed state; waiting does not fix it; no writes. */
    SPL_BUSY = 3,            /* Active operation the hardware will finish; retry later. */
    SPL_WOULD_BLOCK = 4,     /* Try* operation cannot progress now; data/space will come. */
    SPL_ERR_HW = 5,          /* Peripheral error flags are present; clear with the clear API. */
    SPL_ERR_UNSUPPORTED = 6, /* Feature is unavailable on this device or instance. */
    SPL_ERR_TIMEOUT = 7      /* Reserved: only bounded-wait services with an explicit budget. */
} SPL_Status;

#ifdef __cplusplus
}
#endif

#endif /* SPL_STATUS_H */
