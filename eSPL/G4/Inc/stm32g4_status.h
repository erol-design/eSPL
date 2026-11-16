#ifndef STM32G4_STATUS_H
#define STM32G4_STATUS_H

/* Library version (single source; README/CHANGELOG/Tools/run_verification.py follow it). */
#define SPL_VERSION_MAJOR 0U
#define SPL_VERSION_MINOR 18U
#define SPL_VERSION_PATCH 0U

/* Library-wide caller duties (CCL-2 driver-library hand-over).
 * Caller duty (CCL-2 YAP-05): these macros are the only version source; the library builds no
 * image, so placing the version in the image and making it readable is the application's job.
 * Caller duty (CCL-2 YAP-06): the library provides no linker script, startup file or stack
 * placement; stack location, size and overflow detection (guard pattern, MPU) are the app's.
 * Caller duty (CCL-2 KSM-12): unless an API states that it rejects or clears a stale flag,
 * every interrupt-enable service (*_ITConfig, *_InterruptConfig, irq/interrupt parameters)
 * only sets enable bits: before enabling, the caller clears the source's pending event flag
 * and the NVIC pending bit (NVIC_ClearPendingIRQ), or makes its handler tolerate them. */

/* SPL_Status is shared by every family (METHOD section 9 option (b)); never defined here.
 * Relative path: no extra include directory in Keil projects or host scripts. */
#include "../../Common/Inc/spl_status.h"
#include "../../Common/Inc/spl_check.h"

#endif /* STM32G4_STATUS_H */
