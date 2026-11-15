/* eSPL G4 check-level link anchor.
 * The build MUST compile and link this translation unit exactly once. */
#define SPL_LIBRARY_BUILD 1
#define SPL_CHECK_LEVEL_DEFINER 1
#include "stm32g4_status.h"

SPL_CHECK_LEVEL_EXPORT const unsigned char SPL_CHECK_LEVEL_SYMBOL = (unsigned char)SPL_CHECK_LEVEL;
