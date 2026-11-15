/* Denetim seviyesi sembolu (METHOD 1.11, Docs/CHECK_LEVEL.md 3.2).
 * Kutuphane hangi SPL_CHECK_LEVEL ile derlendiyse o seviyenin sembolunu tanimlar
 * (spl_check_level_full / _mix / _fast). Uygulamanin ve kutuphanenin her ceviri birimi (bu
 * dosya haric, SPL_CHECK_LEVEL_DEFINER) spl_check.h uzerinden kendi seviyesinin sembolunu ister;
 * seviyeler farkliysa baglama "undefined symbol" ile durur. Bu dosya kutuphaneyle birlikte her
 * hedef projeye eklenmelidir. */
#define SPL_LIBRARY_BUILD 1
/* Sembolu tanimlayan tek birim: kendisi referans uretmez (spl_check.h, BULGU-02). */
#define SPL_CHECK_LEVEL_DEFINER 1
#include "stm32g0_status.h"

SPL_CHECK_LEVEL_EXPORT const unsigned char SPL_CHECK_LEVEL_SYMBOL = (unsigned char)SPL_CHECK_LEVEL;
