# Phase 1 bench test plan

## Objective

Establish a reproducible ESP32-S3 / ESP-IDF baseline before implementing
identity provisioning, ledger state, persistent storage, or networking.
Collect board-specific measurements only after recording the actual module,
chip revision, flash/PSRAM, power supply, and firmware configuration.

## Current firmware checks

1. **Build and target** — clean ESP-IDF build for ESP32-S3; record ESP-IDF
   version and build configuration.
2. **Boot inventory** — log chip model/revision, core count, feature flags, and
   free heap.
3. **SHA-256 correctness** — compare the digest of `"abc"` with the standard
   known-answer vector; fail the deterministic check on mismatch.
4. **SHA-256 baseline** — after 32 warm-up calls, measure 1,000 hashes of a
   1 KiB input using `esp_timer_get_time()`; report mean, median, p95, and p99.
   Record clock, build options, board (incl. temperature, clock speeds, memory available/remaining), and environment before comparing runs.
5. **RNG API smoke test** — temporarily enable the documented internal entropy
   source, request 64 bytes, disable the source, and report only elapsed time.
   The test does not log the bytes or claim statistical/cryptographic validation*.

   * Future phases of testing this firmware will however log this data to prove the entropy source is verifibly true in generated randomness. 

## Deliberately deferred

- Secure Boot V2, flash encryption, eFuse identity/DS provisioning, and JTAG
  lockout.
- Private-key generation or signing benchmarks.
- NVS, flash writes, partition changes, and recovery/erase behavior.
- Wi-Fi, ESP-NOW, MQTT, transaction formats, UTXO state, consensus, and gateway
  integration.
- Any real funds, production identity, or security guarantee.

The thesis's later evaluation plan calls for at least 1,000 warmed-up iterations,
board and toolchain details, failures, and distribution statistics. The current
hash benchmark implements the first narrow baseline only; it is not a complete
evaluation or proof of a functioning ledger.

## Hardware run gate

This task builds firmware but does not flash a board. Before a hardware run,
identify the expendable test board and serial port, review the generated
`sdkconfig`, and verify that Secure Boot and flash encryption remain disabled.
An ordinary application flash is permitted for an approved test run; do not
erase flash as a setup shortcut.
