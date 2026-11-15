/* eSPL - derleyiciye ozgu ozellikler icin ortak makrolar (butun aileler).
 *
 * Kutuphane kaynaginda GNU / clang sozdizimi (__attribute__) dogrudan yazilmaz; bu makrolar
 * kullanilir. Desteklenen ve dogrulanan derleyiciler aile README'sinde yazilir (eSPL G0: Keil AC6,
 * Arm GCC). Tanimadigi derleyicide makro bos kalir: kod derlenir, yalniz ipucu kaybolur.
 * Denetim: METHOD/tests/run_spl_check.sh (derleme), aile Tests/run_gcc_compile.sh. */
#ifndef SPL_COMPILER_H
#define SPL_COMPILER_H

#if defined(__GNUC__) || defined(__clang__) || defined(__ARMCC_VERSION)
/* GCC, clang ve Keil AC6 (clang tabanli, __ARMCC_VERSION >= 6000000) GNU ozniteliklerini anlar. */
#define SPL_HAS_GNU_ATTRIBUTES 1
#else
#define SPL_HAS_GNU_ATTRIBUTES 0
#endif

/* Fonksiyon gomulmesin: paylasilan tek kopya (or. DMA konum yardimcilari, CHECK_LEVEL.md 7.2:
 * gomulunce her API taban adres sabitlerini yeniden yukler, FULL buyur). Yalniz boyut ipucu;
 * davranisi degistirmez. */
#if SPL_HAS_GNU_ATTRIBUTES
#define SPL_NOINLINE __attribute__((noinline))
#else
#define SPL_NOINLINE
#endif

#endif /* SPL_COMPILER_H */
