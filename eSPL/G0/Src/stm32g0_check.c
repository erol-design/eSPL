/* SPDX-License-Identifier: MIT */
/* Check level symbol (METHOD 1.11, Docs/CHECK_LEVEL.md 3.2).
 * Defines the symbol of the SPL_CHECK_LEVEL the library was built with
 * (spl_check_level_full / _mix / _fast). Every translation unit of the application and of the
 * library (except this file, SPL_CHECK_LEVEL_DEFINER) requests the symbol of its own level through
 * spl_check.h; if the levels differ, linking stops with "undefined symbol". This file must be
 * added to every target project together with the library. */
#define SPL_LIBRARY_BUILD 1
/* The only unit that defines the symbol: it emits no reference itself (spl_check.h, BULGU-02). */
#define SPL_CHECK_LEVEL_DEFINER 1
#include "stm32g0_status.h"

SPL_CHECK_LEVEL_EXPORT const unsigned char SPL_CHECK_LEVEL_SYMBOL = (unsigned char)SPL_CHECK_LEVEL;
