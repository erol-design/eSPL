# eSPL STM32G4 release notes

## v0.18.0-dev (2026-11-15)

- 33 driver modules with FULL / MIX / FAST validation levels from a single source.
- Validation level tags on every public status API header.
- Hardware tests passed on NUCLEO-G431RB and on an STM32G474 board for the tested modules.
- Errata ES0430 / ES0431 Rev 10 and ES0523 Rev 7 reviewed and applied.

Known limitations: pre-release; not every API is hardware-verified. HRTIM is not hardware-tested.
USB and UCPD are not part of this library.
