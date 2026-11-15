#ifndef STM32G4_DIAG_H
#define STM32G4_DIAG_H

/* Compile-time diagnostics for constant scalar arguments.  This is the G4
 * counterpart of the G0 facility: it emits no run-time code and stays off in
 * host register-model tests. */
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
