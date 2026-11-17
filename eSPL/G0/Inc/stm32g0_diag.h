/* SPDX-License-Identifier: MIT */
#ifndef STM32G0_DIAG_H
#define STM32G0_DIAG_H

/* Compile-time argument check (Docs/FAST_PATH.md section 3).
 *
 * SPL_DIAG(condition, message) is attached to a function declaration. If the argument at the
 * call site is a CONSTANT, the compiler evaluates the condition at compile time; if the condition
 * is true, the build stops with an ERROR. If the argument is a variable it does nothing and
 * generates no code: the run-time check (if any) applies in that case.
 *
 * Why: errors made with constant arguments (channel 5, empty pin mask) are known the moment the
 * code is written; instead of asking again at run time on every call, the compiler proves it once.
 *
 * Limit: scalar arguments only. CMSIS peripheral pointers ((TIM_TypeDef *)TIM1_BASE) are not
 * constant expressions in C; instance validity cannot be checked this way (it is ensured by the
 * board mapping header and Init).
 *
 * Where it is active: clang-based compilers (Keil AC6, clang) and only on the ARM target. Host
 * register-model tests exercise the run-time path with bad constant arguments, so it stays off
 * there; defining SPL_DIAG_OFF turns it off on the ARM target too. On other compilers it is an
 * empty macro: run-time behaviour does not change, only the compile-time guarantee is missing.
 *
 * -pedantic warns about diagnose_if as a "clang extension" (-Wgcc-compat); the warning is
 * disabled only around this attribute, the rest of the translation unit is not affected. */

#if defined(__clang__) && defined(__arm__) && !defined(SPL_DIAG_OFF)
#define SPL_DIAG(cond, msg)                                                                        \
    _Pragma("clang diagnostic push") _Pragma("clang diagnostic ignored \"-Wgcc-compat\"")          \
        __attribute__((diagnose_if((cond), msg, "error"))) _Pragma("clang diagnostic pop")
#define SPL_DIAG_ENABLED 1
#else
#define SPL_DIAG(cond, msg)
#define SPL_DIAG_ENABLED 0
#endif

#endif /* STM32G0_DIAG_H */
