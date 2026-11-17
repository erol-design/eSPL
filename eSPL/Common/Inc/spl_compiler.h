/* eSPL - shared macros for compiler-specific features (every family).
 *
 * Library source never writes GNU / clang syntax (__attribute__) directly; it uses these macros.
 * The supported and verified compilers are listed in the family README (eSPL G0: Keil AC6, Arm
 * GCC). On an unknown compiler the macro is empty: the code compiles, only the hint is lost.
 * Checked by: METHOD/tests/run_spl_check.sh (compile), family Tests/run_gcc_compile.sh. */
#ifndef SPL_COMPILER_H
#define SPL_COMPILER_H

#if defined(__GNUC__) || defined(__clang__) || defined(__ARMCC_VERSION)
/* GCC, clang and Keil AC6 (clang based, __ARMCC_VERSION >= 6000000) accept GNU attributes. */
#define SPL_HAS_GNU_ATTRIBUTES 1
#else
#define SPL_HAS_GNU_ATTRIBUTES 0
#endif

/* Do not inline the function: one shared copy (e.g. DMA location helpers, CHECK_LEVEL.md 7.2:
 * when inlined every API reloads the base address constants and FULL grows). A size hint only;
 * behaviour is unchanged. */
#if SPL_HAS_GNU_ATTRIBUTES
#define SPL_NOINLINE __attribute__((noinline))
#else
#define SPL_NOINLINE
#endif

#endif /* SPL_COMPILER_H */
