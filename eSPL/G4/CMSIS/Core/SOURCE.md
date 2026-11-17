# CMSIS Core(M) (third party)

Source: the same files as `eSPL/G0/CMSIS/Core/Include` (Arm CMSIS_5 Core(M) 5.6, ST `cmsis_core`
copy; details and SHA-256 list in `eSPL/G0/CMSIS/Core/SOURCE.md`). Imported 2026-11-15.
Files are unmodified. License: Apache 2.0 (`LICENSE.md`, SPDX line in every file).

Why: a G4 user does not need the Keil CMSIS pack or the G0 folder for `core_cm4.h`;
`Examples/G431_Template` (Keil and CMake / GCC) uses this folder. Existing Keil projects keep
using the RTE CMSIS component.
