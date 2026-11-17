/* SPDX-License-Identifier: MIT */
/* Check level symbol (METHOD 1.11), eSPL G4 check-level link anchor.
 * Defines the symbol of the SPL_CHECK_LEVEL the library was built with (spl_check_level_full /
 * _fast). Every other translation unit of the application and of the library requests the
 * symbol of its own level through spl_check.h; if the levels differ, linking stops with
 * "undefined symbol". The build MUST compile and link this translation unit exactly once,
 * together with the library. Public contract: Common/Inc/spl_check.h. */
#define SPL_LIBRARY_BUILD 1
/* The only unit that defines the symbol: it emits no reference itself (spl_check.h). */
#define SPL_CHECK_LEVEL_DEFINER 1
#include "stm32g4_status.h"

SPL_CHECK_LEVEL_EXPORT const unsigned char SPL_CHECK_LEVEL_SYMBOL = (unsigned char)SPL_CHECK_LEVEL;
