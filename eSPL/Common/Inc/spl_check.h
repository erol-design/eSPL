/* eSPL - shared check level (METHOD/README.md 1.11, LER-001 v1.2 API-14 / COD-13).
 *
 * One definition for every family. The same source compiles as much run-time validation as the
 * level selected at compile time:
 *   SPL_CHECK_FULL (0, default): every validation
 *   SPL_CHECK_FAST (2): no run-time validation
 * The intermediate level (former MIX, 1) and the SAFE / normal class split were removed by user
 * decision on 2026-11-16: SPL_CHECK_LEVEL=1 is a compile error; FAST keeps the value 2 (project
 * defines stay valid).
 * The same at every level: result codes (BUSY, WOULD_BLOCK, ERR_HW, ERR_TIMEOUT), checks of
 * incoming data and hardware events (result class; NO macro, plain if), register write order,
 * errata steps, read-backs, irreversible-operation keys, structural SKU branches.
 *
 * The level is given only as a project-wide define (-DSPL_CHECK_LEVEL=n); never in library source.
 * Library and application must be built at the same level (mix-up guard below).
 * Checked by: METHOD/tests/run_spl_check.sh. */
#ifndef SPL_CHECK_H
#define SPL_CHECK_H

/* Compiler-specific macros (SPL_NOINLINE ...): every source including spl_check.h gets them. */
#include "spl_compiler.h"

#define SPL_CHECK_FULL 0
#define SPL_CHECK_FAST 2

/* An undefined macro counts as 0 in #if: a missing configuration falls to the safest level. */
#ifndef SPL_CHECK_LEVEL
#define SPL_CHECK_LEVEL SPL_CHECK_FULL
#endif

#if (SPL_CHECK_LEVEL != SPL_CHECK_FULL) && (SPL_CHECK_LEVEL != SPL_CHECK_FAST)
#error "SPL_CHECK_LEVEL: 0 (SPL_CHECK_FULL) or 2 (SPL_CHECK_FAST) required (1 = MIX removed)"
#endif

/* Validation macros (MISRA deviation D-11, METHOD 5.2).
 * - The condition must be PURE (PURE CHECK, METHOD 1.11 / COD-13): no MMIO write, no read of a
 *   register with read side effects (read-to-clear, FIFO), no output / state change, assignment,
 *   ++ / -- or variable-length array. At the disabled level the condition is NEVER evaluated.
 * - At the disabled level ((void)sizeof(cond)) emits no code; variables in the condition count as
 *   "used".
 * - At the enabled level it comes before the first MMIO write (METHOD 1.3); one condition per
 *   macro. */

#if defined(SPL_CHECK_PURITY_ANALYSIS)
/* Only for the purity tool (family Tools/check_pure.py, -fsyntax-only): the condition becomes an
 * argument of a probe function and the tool scans the argument subtree in the AST. NEVER defined in
 * a target or test build; the undefined probe function fails at link time. */
int spl_check_pure_probe_(int safe_class, int cond);
#define SPL_REQUIRE(cond, err) ((void)spl_check_pure_probe_(0, (cond) ? 1 : 0))
/* Compiled only at FULL. */
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

/* static inline helpers used in library source only inside a check condition (e.g. timer type /
 * capability queries) remain only inside sizeof at the disabled level; clang warns that they are
 * "unneeded, will not be emitted" (-Wunneeded-internal-declaration). That is exactly the intended
 * result: the warning is disabled only in library units (SPL_LIBRARY_BUILD) and only at FAST.
 * Application code is not affected (A4 wave 3, CHECK_LEVEL.md 7.5). */
#if defined(__clang__) && defined(SPL_LIBRARY_BUILD) && (SPL_CHECK_LEVEL != SPL_CHECK_FULL)
#pragma clang diagnostic ignored "-Wunneeded-internal-declaration"
#endif

/* Mix-up guard (CHECK_LEVEL.md 3.2, A2-0 experiment).
 * Layer 1 (mandatory): the build system applies a single level.
 * Layer 2: the library defines the level symbol (family Src/stm32<fam>_check.c); every APPLICATION
 *    translation unit requires it through a reference that cannot be discarded. An application
 *    built at another level gets "undefined symbol spl_check_level_<level>" at link time.
 *    (void)&symbol is NOT enough (the no-effect expression is dropped); used alone is not enough
 *    with GNU ld --gc-sections; used + retain was tried on the AC6 (default, --remove, LTO,
 *    archive) and GNU ld (gc-sections, LTO, archive) paths. Cost: 4 bytes RO per reference.
 * - The library's own units emit the reference too (2026-11-14, external review BULGU-02): mixed
 *   level library objects such as usart.o FULL + tim.o FAST also stop at the linker. The only unit
 *   without a reference is the one that DEFINES the symbol: it defines SPL_CHECK_LEVEL_DEFINER
 *   (family Src/stm32<fam>_check.c). SPL_LIBRARY_BUILD now only affects the clang warning (above).
 * - The reference is emitted only in the target (Arm) build; host tests build every source at one
 *   level. Define SPL_CHECK_FORCE_LINK_GUARD to try it on the host (METHOD test). */
#if SPL_CHECK_LEVEL == SPL_CHECK_FULL
#define SPL_CHECK_LEVEL_SYMBOL spl_check_level_full
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

/* The library's level symbol is defined with this (family Src/stm32<fam>_check.c):
 *   SPL_CHECK_LEVEL_EXPORT const unsigned char SPL_CHECK_LEVEL_SYMBOL = ...;
 * GCC -flto localises a symbol whose reference it sees only in asm and reports "undefined" even
 * at the same level; externally_visible prevents that. Empty on other compilers (AC6 image
 * unchanged). */
#if defined(__GNUC__) && !defined(__clang__)
#define SPL_CHECK_LEVEL_EXPORT __attribute__((used, externally_visible))
#else
#define SPL_CHECK_LEVEL_EXPORT
#endif

#if !defined(SPL_CHECK_LEVEL_DEFINER) &&                                                           \
    (defined(__arm__) || defined(__thumb__) || defined(SPL_CHECK_FORCE_LINK_GUARD))
#if defined(__GNUC__) && !defined(__clang__)
/* GCC (Arm GNU tools): the retain attribute depends on the compiler configuration; Ubuntu
 * gcc-arm-none-eabi 14.2 says "retain attribute ignored" (the build stops with -Werror) and the
 * reference is dropped by --gc-sections. Instead the reference is written directly for the
 * assembler (gas): the "R" flag (SHF_GNU_RETAIN, binutils 2.36+) protects the section from gc; the
 * .rodata.* name lands in FLASH in the usual linker scripts. Tried on the GNU ld default,
 * --gc-sections, -flto and archive paths (run_gcc_compile.sh). Same cost: 4 bytes RO per
 * translation unit. */
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
/* No retain (older compiler): no guard with GNU ld --gc-sections (CHECK_LEVEL.md 3.2). */
#define SPL_CHECK_KEEP_ __attribute__((used))
#endif
SPL_CHECK_KEEP_ static const void *const spl_check_level_ref = &SPL_CHECK_LEVEL_SYMBOL;
#endif /* GCC / clang based */
#endif

#endif /* SPL_CHECK_H */
