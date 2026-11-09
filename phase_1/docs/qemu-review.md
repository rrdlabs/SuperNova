# ESP32-S3 QEMU verification review

## Finding

QEMU can provide useful **software-level** evidence for firmware boot,
application control flow, deterministic SHA-256 results, and some peripheral
interactions when the relevant ESP32-S3 model is supported. It cannot
fundamentally verify real-device behavior, physical entropy quality, RF,
power/energy, or performance timings. Simulator timings must not be reported as
hardware measurements.

The current local environment cannot run this project in QEMU:

- The installed ESP-IDF is v5.2.2. Its `tools/tools.json` registers
  `qemu-xtensa` (Espressif build 8.2.0-20240122) for target `esp32` only, not
  `esp32s3`.
- No `qemu-system-xtensa` executable is installed.
- Therefore this build has **not** been QEMU-executed. Do not label it
  emulator-verified.

Espressif's current QEMU documentation has an ESP32-S3 target and lists
emulation for CPU, UART, flash, RNG, SHA, AES, RSA, and Digital Signature.
It also states that the QEMU project is unsupported by Espressif and that
important peripherals are absent, including Wi-Fi and Bluetooth. Its S3 guide
states that Secure Boot is not supported. The upstream QEMU model is therefore
potentially useful for later smoke tests, but it does not replace hardware tests
or validate this exact ESP-IDF v5.2.2 firmware without installing and checking a
compatible QEMU version.

QEMU's eFuse model is simulated; it does not touch physical eFuses. This project
does not need to configure emulated eFuse storage for its Phase 1 tests. Do not
use QEMU eFuse programming examples or add eFuse provisioning to this phase.

## Appropriate claims

- **Build verified:** ESP-IDF compiled the ESP32-S3 image.
- **QEMU smoke-tested:** only after a supported S3 emulator actually boots the
  image and the expected serial output is captured.
- **Hardware verified:** only after a real identified board is tested.
- RNG output, benchmark timings, power, radio, and eFuse behavior require their
  respective real-device/instrument validation; QEMU does not establish them.

## References

- Espressif ESP-IDF v5.2.2 tool metadata:
  `~/devenv/esp-idf/tools/tools.json` (the `qemu-xtensa` entry is ESP32-only).
- Espressif QEMU overview and feature matrix:
  <https://github.com/espressif/esp-toolchain-docs/blob/main/qemu/README.md>
- Espressif ESP32-S3 QEMU guide:
  <https://github.com/espressif/esp-toolchain-docs/blob/main/qemu/esp32s3/README.md>
- Espressif QEMU fork:
  <https://github.com/espressif/qemu>
- ESP-IDF v5.2.2 unit testing:
  <https://docs.espressif.com/projects/esp-idf/en/v5.2.2/esp32s3/api-guides/unit-tests.html>

The official web documentation is cited and summarized here rather than copied
verbatim. User-provided thesis and manuscript sources are copied under
`docs/sources/`.
