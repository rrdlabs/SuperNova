# QEMU evaluation

This document describes the Phase 2 emulator harness and the exact evidence it
produces. QEMU evidence is **software/emulator evidence only**. It is not a
hardware test, a radio test, a timing measurement, an entropy-quality result, or
a production/security claim.

## Pinned environment

| Item | Value |
| --- | --- |
| ESP-IDF | `ESP-IDF v5.2.2` |
| QEMU release | Espressif `esp_develop_9.2.2_20261109` |
| QEMU version string | `QEMU emulator version 9.2.2 (esp_develop_9.2.2_20261109)` |
| QEMU asset | `qemu-xtensa-softmmu-esp_develop_9.2.2_20261109-x86_64-linux-gnu.tar.xz` |
| QEMU asset SHA-256 | `fbc3bffa9fa71192847969a627a584aabe2e86d85bed4902a096ff06c2b8a815` |
| Local executable | `~/devenv/qemu-s3/qemu-system-xtensa` (wrapper) |
| Machine | `esp32s3` |
| Target flash size | 8 MB |

The local wrapper sets `LD_LIBRARY_PATH` for the unpacked QEMU libraries and
executes `~/devenv/qemu-s3/bin/qemu-system-xtensa`. Override the executable with
`QEMU_SYSTEM_XTENSA=/path/to/qemu-system-xtensa`.

Load the toolchain before building or running:

```bash
source ~/.bashrc
source ~/devenv/env.sh
cd ~/projects/SuperNova/phase_2
```

## Harness

`scripts/check-qemu.sh` verifies the executable, the pinned version/build
string, and the `esp32s3` machine. `scripts/qemu-run.sh` performs the full run:

1. verify ESP-IDF v5.2.2 and the pinned QEMU;
2. build the complete ESP32-S3 image with `idf.py build`;
3. check the generated `sdkconfig` for target, 8 MB flash, disabled Secure Boot,
   disabled flash encryption, and disabled ESP-NOW;
4. merge `build/flash_args` into an 8 MB flash image with
   `esptool.py merge_bin --fill-flash-size 8MB`;
5. boot QEMU with a flash drive only (no eFuse drive) and radios disabled;
6. capture UART output, stop after `RUN_COMPLETE`/`APP_EXIT`, and record the
   QEMU exit status;
7. write a per-run manifest and fail unless the summary is present and every
   test passed.

Run it with:

```bash
./scripts/qemu-run.sh
# optional:
./scripts/qemu-run.sh --scenario phase2-full-suite --timeout 300
```

The QEMU command is:

```text
qemu-system-xtensa -nographic -machine esp32s3 \
  -drive file=<run>/flash.bin,if=mtd,format=raw
```

Do not add an eFuse drive. The harness never programs eFuses, never starts
Wi-Fi/Bluetooth, and never transmits.

`-monitor none` is intentionally **not** used: in this S3 QEMU build it
suppressed the guest serial stream. The harness also does not pass the
`timer.esp32c3.timg` watchdog global, which this machine rejects as an invalid
class name. QEMU is stopped by the harness after the firmware reports
completion; the firmware itself does not power the emulated chip off.

## Run artifacts

Each run is written under `qemu-runs/<UTC timestamp>-<pid>/`:

| File | Contents |
| --- | --- |
| `manifest.txt` | run ID, UTC time, scenario, source revision/dirty count, IDF and QEMU versions, target, `sdkconfig` hash, build/merge/QEMU commands, flash image, log paths, QEMU exit status, completion flags, summary, result, limitations |
| `uart.log` | complete UART stream including boot messages and `SN2|...` test lines |
| `build.log` | `idf.py build` output |
| `merge.log` | `esptool.py merge_bin` output |
| `flash.bin` | 8 MB flash image assembled from `build/flash_args` |

`build/` and `qemu-runs/` are ignored by Git.

The harness fails on any `SN2|event=TEST|...|status=FAIL`, a missing
`SN2|event=SUMMARY`, a missing `SN2|event=RUN_COMPLETE`, a non-`PASS` run result,
or a flash image whose size is not exactly 8 MB.

## Current result

Run `20261109T211903Z-73931` completed on 2026-11-09:

```text
SN2|event=SUMMARY|total=177|passed=177|failed=0|sim_ms=177
SN2|event=RUN_COMPLETE|result=PASS|sim_ms=177
```

This is **QEMU-verified** evidence for the deterministic software suite only.
The log includes bootloader and application output from the complete 8 MB image.

## Limitations

- Espressif's S3 QEMU model does not emulate Wi-Fi or Bluetooth. The ESP-NOW
  adapter is compiled out by default and the QEMU harness fails if it is enabled.
  No radio behavior, range, coexistence, or mesh routing is exercised.
- QEMU's RNG, timer, power, and eFuse behavior is simulated. Results must not be
  reported as hardware entropy, energy, latency, throughput, or performance
  measurements.
- Secure Boot is not supported by the referenced S3 QEMU guide. The build keeps
  Secure Boot and flash encryption disabled; QEMU does not validate them.
- The emulated eFuse model is not used and must not be used to justify physical
  provisioning.
- The suite runs on the main task with a 64 KiB stack because its wire, block,
  ledger, snapshot, and loopback buffers are bounded but large. The suite also
  uses the performance optimization level so software P-256 operations finish in
  bounded emulator time. Neither choice changes the protocol or test semantics.
- A QEMU pass does not establish hardware correctness, production readiness,
  side-channel resistance, or security.

## Evidence levels

- **Build verified:** ESP-IDF v5.2.2 compiled `build/phase_2.bin` for ESP32-S3.
- **QEMU verified:** the pinned S3 QEMU booted the complete image and the
  deterministic suite passed as captured in a run manifest.
- **Hardware verified:** not performed. Physical flashing remains behind
  `scripts/flash-gate.sh --flash` and requires a separate approved hardware run.
