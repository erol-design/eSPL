#ifndef STM32G0_STATUS_H
#define STM32G0_STATUS_H

/* Library version (single source; README, CHANGELOG and Tools/run_verification.py follow it). */
#define SPL_VERSION_MAJOR 0U
#define SPL_VERSION_MINOR 20U
#define SPL_VERSION_PATCH 0U

/* SPL_Status is shared by every family (METHOD section 9 option (b)); never defined here.
 * Relative path: no extra include directory in Keil projects or host scripts. */
#include "../../Common/Inc/spl_status.h"
/* Denetim seviyesi (SPL_CHECK_LEVEL, SPL_REQUIRE*): METHOD 1.11, Docs/CHECK_LEVEL.md. */
#include "../../Common/Inc/spl_check.h"

#endif /* STM32G0_STATUS_H */
