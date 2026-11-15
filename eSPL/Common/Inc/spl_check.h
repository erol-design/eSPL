/* eSPL - ortak denetim seviyesi (METHOD/README.md 1.11, LER-001 v1.2 API-14 / COD-13).
 *
 * Butun aileler icin tek tanim. Ayni kaynak derleme zamaninda secilen seviyeyle calisma ani
 * dogrulamasinin ne kadarini derler:
 *   SPL_CHECK_FULL (0, varsayilan): butun dogrulamalar
 *   SPL_CHECK_MIX  (1): yalniz SAFE sinifi (denetim yoksa sessiz bozulma / donanim riski)
 *   SPL_CHECK_FAST (2): calisma ani dogrulamasi yok
 * Her seviyede ayni kalanlar: sonuc kodlari (BUSY, WOULD_BLOCK, ERR_HW, ERR_TIMEOUT), gelen veri
 * ve donanim olayi denetimleri (result sinifi; makro KULLANILMAZ, duz if), register yazim sirasi,
 * errata adimlari, geri okumalar, geri alinamaz islem anahtarlari, yapisal SKU dallari.
 *
 * Seviye yalniz proje geneli tanimla verilir (-DSPL_CHECK_LEVEL=n); kutuphane kaynaginda yazilmaz.
 * Kutuphane ve uygulama ayni seviyede derlenmelidir (asagidaki karisma korumasi).
 * Denetim: METHOD/tests/run_spl_check.sh. */
#ifndef SPL_CHECK_H
#define SPL_CHECK_H

/* Derleyiciye ozgu makrolar (SPL_NOINLINE ...): spl_check.h'yi iceren her kaynak alir. */
#include "spl_compiler.h"

#define SPL_CHECK_FULL 0
#define SPL_CHECK_MIX 1
#define SPL_CHECK_FAST 2

/* Tanimsiz makro #if icinde 0 sayilir: yapilandirmayi unutan en guvenli seviyeye (FULL) duser. */
#ifndef SPL_CHECK_LEVEL
#define SPL_CHECK_LEVEL SPL_CHECK_FULL
#endif

#if (SPL_CHECK_LEVEL != SPL_CHECK_FULL) && (SPL_CHECK_LEVEL != SPL_CHECK_MIX) &&                   \
    (SPL_CHECK_LEVEL != SPL_CHECK_FAST)
#error "SPL_CHECK_LEVEL: 0 (SPL_CHECK_FULL), 1 (SPL_CHECK_MIX) ya da 2 (SPL_CHECK_FAST) olmali"
#endif

/* Dogrulama makrolari (MISRA sapmasi D-11, METHOD 5.2).
 * - Kosul SAF olmalidir (PURE CHECK, METHOD 1.11 / COD-13): MMIO yazimi, okumasi yan etkili
 *   register (read-to-clear, FIFO) okumasi, cikti / durum degisikligi, atama, ++ / -- ve
 *   degisken uzunluklu dizi icermez. Kapali seviyede kosul HIC degerlendirilmez.
 * - Kapali seviyede ((void)sizeof(cond)) kod uretmez; kosuldaki degiskenler "kullanilmis" sayilir.
 * - Acik seviyede ilk MMIO yazimindan once gelir (METHOD 1.3); her makro tek kosul tasir. */

#if defined(SPL_CHECK_PURITY_ANALYSIS)
/* Yalniz saflik araci icin (aile Tools/check_pure.py, -fsyntax-only): kosul bir yoklama
 * fonksiyonuna arguman olur, arac AST'de argumanin alt agacini tarar. Hedef ya da test derlemesinde
 * TANIMLANMAZ; tanimsiz yoklama fonksiyonu baglamada hata verir. */
int spl_check_pure_probe_(int safe_class, int cond);
#define SPL_REQUIRE(cond, err) ((void)spl_check_pure_probe_(0, (cond) ? 1 : 0))
#define SPL_REQUIRE_SAFE(cond, err) ((void)spl_check_pure_probe_(1, (cond) ? 1 : 0))
/* Normal sinif: denetim yoksa hata hemen ve acikca gorunur. Yalniz FULL. */
#elif SPL_CHECK_LEVEL == SPL_CHECK_FULL
#define SPL_REQUIRE(cond, err)                                                                     \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            return (err);                                                                          \
        }                                                                                          \
    } while (0)
#else
#define SPL_REQUIRE(cond, err) ((void)sizeof(cond))
#endif

/* Kutuphane kaynaginda yalniz denetim kosulunda gecen static inline yardimcilar (or. zamanlayici
 * tur / yetenek sorgulari) kapali seviyede yalniz sizeof icinde kalir; clang bunu "gereksiz,
 * uretilmeyecek" diye uyarir (-Wunneeded-internal-declaration). Istenen sonuc tam olarak budur:
 * uyari yalniz kutuphane birimlerinde (SPL_LIBRARY_BUILD) ve yalniz MIX / FAST'ta kapatilir.
 * Uygulama kodu etkilenmez (A4 dalga 3, CHECK_LEVEL.md 7.5). */
#if defined(__clang__) && defined(SPL_LIBRARY_BUILD) && (SPL_CHECK_LEVEL != SPL_CHECK_FULL)
#pragma clang diagnostic ignored "-Wunneeded-internal-declaration"
#endif

/* SAFE sinif: denetim yoksa sessiz bozulma ya da donanim riski. FULL ve MIX. */
#if defined(SPL_CHECK_PURITY_ANALYSIS)
/* analiz kipinde yukarida tanimli */
#elif SPL_CHECK_LEVEL <= SPL_CHECK_MIX
#define SPL_REQUIRE_SAFE(cond, err)                                                                \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            return (err);                                                                          \
        }                                                                                          \
    } while (0)
#else
#define SPL_REQUIRE_SAFE(cond, err) ((void)sizeof(cond))
#endif

/* Karisma korumasi (CHECK_LEVEL.md 3.2, A2-0 deneyi).
 * 1. katman (zorunlu): derleme sistemi tek seviye uygular.
 * 2. katman: kutuphane seviyenin sembolunu tanimlar (aile Src/stm32<fam>_check.c); her UYGULAMA
 *    ceviri birimi onu atilamayan bir referansla ister. Farkli seviyeyle derlenen uygulama
 *    baglanirken "undefined symbol spl_check_level_<seviye>" hatasi alir.
 *    (void)&sembol YETMEZ (etkisiz ifade atilir); yalniz used GNU ld --gc-sections'ta yetmez;
 *    used + retain AC6 (varsayilan, --remove, LTO, arsiv) ve GNU ld (gc-sections, LTO, arsiv)
 *    yollarinda denendi. Maliyet: referans basina 4 bayt RO.
 * - Kutuphanenin kendi birimleri de referans uretir (2026-11-14, dis inceleme BULGU-02): usart.o
 *   FULL + tim.o FAST gibi karisik seviyeli kutuphane nesneleri de baglayicida durur. Referans
 *   uretmeyen tek birim sembolu TANIMLAYAN birimdir: SPL_CHECK_LEVEL_DEFINER tanimlar (aile
 *   Src/stm32<fam>_check.c). SPL_LIBRARY_BUILD artik yalniz clang uyarisini (yukarida) etkiler.
 * - Referans yalniz hedef (Arm) derlemesinde uretilir; host testleri butun kaynaklari tek seviyede
 *   derler. Host'ta denemek icin SPL_CHECK_FORCE_LINK_GUARD tanimlanir (METHOD testi). */
#if SPL_CHECK_LEVEL == SPL_CHECK_FULL
#define SPL_CHECK_LEVEL_SYMBOL spl_check_level_full
#elif SPL_CHECK_LEVEL == SPL_CHECK_MIX
#define SPL_CHECK_LEVEL_SYMBOL spl_check_level_mix
#else
#define SPL_CHECK_LEVEL_SYMBOL spl_check_level_fast
#endif

#ifdef __cplusplus
extern "C"
{
#endif
extern const unsigned char SPL_CHECK_LEVEL_SYMBOL;
#ifdef __cplusplus
}
#endif

/* Kutuphanenin seviye sembolu tanimi bununla yazilir (aile Src/stm32<fam>_check.c):
 *   SPL_CHECK_LEVEL_EXPORT const unsigned char SPL_CHECK_LEVEL_SYMBOL = ...;
 * GCC -flto, referansi yalniz asm'de goren sembolu yerellestirir; ayni seviyede bile "undefined"
 * verir. externally_visible bunu onler. Oteki derleyicilerde bos (AC6 imaji degismez). */
#if defined(__GNUC__) && !defined(__clang__)
#define SPL_CHECK_LEVEL_EXPORT __attribute__((used, externally_visible))
#else
#define SPL_CHECK_LEVEL_EXPORT
#endif

#if !defined(SPL_CHECK_LEVEL_DEFINER) &&                                                           \
    (defined(__arm__) || defined(__thumb__) || defined(SPL_CHECK_FORCE_LINK_GUARD))
#if defined(__GNUC__) && !defined(__clang__)
/* GCC (Arm GNU araclari): retain ozniteligi derleyicinin yapilandirmasina bagli; Ubuntu
 * gcc-arm-none-eabi 14.2 "retain attribute ignored" der (-Werror ile derleme durur) ve referans
 * --gc-sections ile atilir. Bunun yerine referans dogrudan assemblere (gas) yazilir: "R" bayragi
 * (SHF_GNU_RETAIN, binutils 2.36+) bolumu gc'den korur; .rodata.* adi alisilmis baglayici
 * betiklerinde FLASH'a yerlesir. GNU ld varsayilan, --gc-sections, -flto ve arsiv yollarinda
 * denendi (run_gcc_compile.sh). Maliyet ayni: ceviri birimi basina 4 bayt RO. */
#define SPL_CHECK_STR2_(x) #x
#define SPL_CHECK_STR_(x) SPL_CHECK_STR2_(x)
__asm__(
    ".pushsection .rodata.spl_check_guard,\"aR\",%progbits\n\t.balign 4\n\t.long " SPL_CHECK_STR_(
        SPL_CHECK_LEVEL_SYMBOL) "\n\t.popsection");
#else
#if defined(__has_attribute)
#if __has_attribute(retain)
#define SPL_CHECK_KEEP_ __attribute__((used, retain))
#endif
#endif
#ifndef SPL_CHECK_KEEP_
/* retain yok (eski derleyici): GNU ld --gc-sections ile koruma sayilmaz (CHECK_LEVEL.md 3.2). */
#define SPL_CHECK_KEEP_ __attribute__((used))
#endif
SPL_CHECK_KEEP_ static const void *const spl_check_level_ref = &SPL_CHECK_LEVEL_SYMBOL;
#endif /* GCC / clang tabanli */
#endif

#endif /* SPL_CHECK_H */
