# eSPL — Enhanced Standard Peripheral Library

### Hardware-Aware. Contract-Driven. Deterministic.

**eSPL** is an embedded C peripheral driver library designed for developers who need direct hardware control, predictable execution, and explicit error handling.

The project combines low-level register access with hardware-aware validation, documented API contracts, and evidence-driven verification.

eSPL aims to provide the efficiency of lightweight peripheral drivers while addressing common sources of embedded software defects: incorrect register access, unsupported hardware configurations, silicon errata, and unexpected peripheral behavior.

> **Don't just access registers. Respect their behavior.**

---

## Philosophy

A peripheral register is not an ordinary memory location.

Some registers clear flags when written with zero. Others clear flags when written with one. Some reads consume data, certain configuration fields become locked after initialization, and some writes are ignored unless specific hardware conditions are satisfied.

Code can be syntactically correct and still be wrong for the hardware.

eSPL is built around a simple principle:

**Peripheral drivers should understand and respect the hardware contracts they operate on.**

This philosophy is reflected in five design principles:

- **Hardware-Aware:** Respect register semantics, peripheral capabilities, hardware states, and documented silicon limitations.
- **Contract-Driven:** Define preconditions, postconditions, side effects, and observable failure behavior.
- **Deterministic:** Prefer bounded execution, non-blocking operations, and explicit ownership of hardware resources.
- **Configurable Validation:** Balance runtime validation overhead against application-level responsibility.
- **Evidence-Based:** Support implementation claims with source references, static analysis, automated tests, and hardware verification.

## Key Features

### Register-Aware Peripheral Access

eSPL treats register access semantics as part of the driver design.

Depending on the peripheral, these may include:

- Read/write and write-only registers
- Write-one-to-clear (W1C) flags
- Write-zero-to-clear (W0C) flags
- Read-to-clear and destructive-read behavior
- Write-once configuration fields
- Hardware-modified register bits
- Reserved bit handling
- Configuration locking and hardware state restrictions

The goal is to prevent software operations that are legal in C but incorrect for the underlying hardware.

### Hardware Capability Validation

Not every peripheral instance has the same capabilities.

Even within a single MCU family, timers, DMA controllers, communication interfaces, and other peripherals may differ between devices.

eSPL uses device-specific capability checks to distinguish supported functionality from invalid configurations.

This helps detect problems such as unsupported timer channels, unavailable peripheral instances, and invalid register configurations.

### Non-Blocking APIs

eSPL favors non-blocking operations wherever appropriate.

For example, a USART transmission API can report whether the hardware accepted a byte without waiting for the transmitter to become available.

```c
SPL_Status status = USART_TryWrite(USART1, 0x55U);

if (status == SPL_OK)
{
    /* Byte accepted by the USART hardware. */
}
else if (status == SPL_WOULD_BLOCK)
{
    /* Transmitter not ready. Retry later. */
}
else
{
    /* Handle the reported error. */
}
```

`SPL_OK` indicates that the byte was written to the transmit data register, not that transmission on the physical interface has completed.

The caller retains responsibility for retrying data that was not accepted.

### Two Runtime Validation Levels

eSPL provides two compile-time validation configurations from a single source codebase (`SPL_CHECK_FULL` = 0, `SPL_CHECK_FAST` = 2; level 1 is rejected at compile time).

| Level | Description | Intended Use |
|---|---|---|
| **FULL** | All defined runtime validation checks enabled | Development, debugging, diagnostics |
| **FAST** | Optional runtime validation checks removed | Performance-critical applications with externally guaranteed preconditions |

The default is **FULL**.

```c
#define SPL_CHECK_LEVEL SPL_CHECK_FULL
```

Validation levels are intended to be configured consistently across the entire build.

Important hardware result checks, register access rules, required silicon errata workarounds, and selected structural protections remain independent of optional parameter validation.

**FAST does not remove the caller's obligations.** Applications must satisfy all documented API preconditions, even when the corresponding checks are compiled out.

The terms FULL and FAST describe validation policies. They do not represent functional safety certification levels.

### Explicit Error Reporting

eSPL uses status codes to make peripheral operation results visible to the application.

Examples include:

| Status | Meaning |
|---|---|
| `SPL_OK` | Operation completed according to its API contract |
| `SPL_ERR_PARAM` | Invalid parameter |
| `SPL_ERR_STATE` | Peripheral state does not permit the requested operation |
| `SPL_ERR_UNSUPPORTED` | Unsupported operation or hardware configuration |
| `SPL_ERR_HW` | Hardware error or failed hardware verification |
| `SPL_BUSY` | Peripheral or operation currently busy |
| `SPL_WOULD_BLOCK` | Operation cannot proceed immediately without waiting |
| `SPL_ERR_TIMEOUT` | Defined timeout condition occurred |

The exact meaning of success and failure is specified by each API.

A successful register write does not necessarily indicate that the corresponding physical operation has completed.

### Silicon Errata Awareness

MCU peripherals may exhibit behavior that differs from their nominal reference manual specification.

Where applicable, eSPL integrates documented silicon errata restrictions and workarounds into the driver implementation.

Errata handling is tied to the relevant MCU family, peripheral, and supported silicon revisions.

Not all errata can be mitigated in software. Unsupported or unresolved conditions should be documented explicitly.

### Lightweight and Predictable Execution

eSPL is designed with resource-constrained embedded systems in mind.

Design goals include:

- Low runtime overhead
- Small code footprint
- No required dynamic memory allocation in peripheral drivers
- Bounded execution paths
- Non-blocking API patterns
- Explicit ISR and DMA interaction
- Predictable register access sequences
- Minimal unnecessary abstraction

The project avoids adding software layers when direct, well-defined hardware access is sufficient.

Actual timing and memory characteristics depend on the target MCU, compiler, build configuration, and API.

## Supported Platforms

eSPL is initially focused on STMicroelectronics STM32 microcontrollers.

| MCU Family | Status |
|---|---|
| STM32G0 | Reference implementation (v0.20.0-dev); development paused, hardware test pending |
| STM32G4 | Release candidate (v0.18.0); FULL/FAST host-verified, selected tests hardware-verified on NUCLEO-G431RB and NUCLEO-G474RE |
| STM32H7 | Planned |
| Other MCU families | Future development |

Support is defined at the MCU, peripheral, and feature level. A family name does not imply that every device or peripheral feature has been implemented or verified.

### Future Architecture Support

The long-term goal is to extend eSPL beyond STM32 and beyond a single processor architecture.

The project is built around a reusable engineering approach:

**Common contracts. Native hardware behavior. Verifiable implementations.**

Hardware-specific implementations will preserve the capabilities and constraints of each target rather than forcing fundamentally different peripherals into an artificial common model.

## Verification Approach

Verification is a core part of the eSPL development process.

Each eSPL release is developed and checked with internal tooling for:

- Compile-time and static checks
- API validation behavior
- Register access modeling
- Multiple MCU/SKU configurations
- FULL/FAST configuration testing
- MMIO write-sequence equivalence
- Validation-expression purity analysis
- Mutation testing
- Host-based sanitizer testing
- Silicon errata traceability

Hardware-specific behavior requires additional verification on physical target devices.

### Verification Status

The project distinguishes between different levels of evidence:

| Status | Meaning |
|---|---|
| Implemented | Functionality exists in source code |
| Host Verified | Selected behavior passed host-based tests |
| Hardware Verified | Selected behavior passed tests on identified MCU hardware |
| Release Verified | Defined release criteria have been satisfied |

Passing a host test does not establish complete hardware correctness.

Test coverage, known limitations, supported silicon revisions, and verification results should be evaluated for each release.

### Repository Layout

| Path | Content |
|---|---|
| `eSPL/Common/Inc` | Shared headers: `SPL_Status`, validation level, compiler abstraction |
| `eSPL/G0` | STM32G0 drivers (`Inc`, `Src`, `Private`), CMSIS, README, release notes |
| `eSPL/G4` | STM32G4 drivers (`Inc`, `Src`, `Private`), CMSIS, example, README, release notes |

Each family README explains how to add the library to a project. Verification tooling, test suites and design documents are not part of the published package. ST reference manuals and errata sheets are cited by document number and are not redistributed.

## API Contracts

eSPL APIs are intended to have explicit behavioral contracts.

A contract may define:

- Valid parameters and ranges
- Required peripheral configuration
- Hardware capability restrictions
- Register read/write side effects
- Success conditions
- Error conditions
- Blocking behavior
- Concurrency and resource ownership assumptions
- Relevant silicon errata
- Verification requirements

This makes the intended behavior reviewable and testable rather than leaving critical assumptions undocumented.

## Design Priorities

eSPL is not intended to replace an RTOS, application framework, or complete software platform.

Its focus is the boundary between embedded software and MCU peripherals.

The primary goals are:

1. Correct hardware behavior
2. Explicit and predictable API semantics
3. Detectable failure conditions
4. Low runtime overhead
5. Maintainable, device-aware implementations
6. Reproducible verification

Performance matters, but not at the expense of undocumented hardware behavior.

## Development Status

**eSPL is under active development.**

The STM32G4 implementation is under active development; the STM32G0 implementation is kept as a reference. Additional hardware conformance tests, device coverage, and verification evidence remain part of the development roadmap.

Until a verified release is published, users should review the supported device list, known issues, and API contracts before deploying eSPL in production systems.

eSPL is not currently presented as an ISO 26262, IEC 61508, or other functional safety certified software component.

## Contributing

Contributions are welcome, particularly in areas such as:

- Peripheral register behavior
- MCU and SKU compatibility
- Silicon errata analysis
- Driver correctness
- Automated regression tests
- Hardware validation
- Documentation and API contracts
- Support for additional MCU families

Contributions involving hardware behavior should include references to the relevant reference manual, datasheet, or errata document whenever possible.

Bug reports should identify the MCU part number, silicon revision, compiler version, eSPL revision, expected behavior, and observed behavior.

## License

The license and applicable third-party component notices will be provided with the public release.

---

### eSPL — Enhanced Standard Peripheral Library

**Hardware-Aware. Contract-Driven. Deterministic.**

*Don't just access registers. Respect their behavior.*
