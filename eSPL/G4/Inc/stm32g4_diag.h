/* SPDX-License-Identifier: MIT */
#ifndef STM32G4_DIAG_H
#define STM32G4_DIAG_H

/* Compile-time argument check, G4 counterpart of the G0 facility.
 * SPL_DIAG(condition, message) is attached to a function declaration. If the argument at the
 * call site is a constant and the condition is true, the build stops with an error; with a
 * variable argument it does nothing and generates no code (the run-time check, if any, applies).
 * Limit: scalar arguments only (CMSIS peripheral pointers are not constant expressions).
 * Active only with clang on the ARM target (Keil AC6, clang) and without SPL_DIAG_OFF; off in
 * host register-model tests, which exercise the run-time path with bad constant arguments.
 * Declares no API; no caller duty. */
#if defined(__clang__) && defined(__arm__) && !defined(SPL_DIAG_OFF)
#define SPL_DIAG(cond, msg)                                                                        \
    _Pragma("clang diagnostic push") _Pragma("clang diagnostic ignored \"-Wgcc-compat\"")          \
        __attribute__((diagnose_if((cond), msg, "error"))) _Pragma("clang diagnostic pop")
#define SPL_DIAG_ENABLED 1
#else
#define SPL_DIAG(cond, msg)
#define SPL_DIAG_ENABLED 0
#endif

#endif /* STM32G4_DIAG_H */
