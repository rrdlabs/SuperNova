#!/usr/bin/env bash
# Verify the pinned Espressif ESP32-S3 QEMU build before a QEMU run.
set -euo pipefail

QEMU_BIN="${QEMU_SYSTEM_XTENSA:-$HOME/devenv/qemu-s3/qemu-system-xtensa}"
EXPECTED_QEMU_VERSION="QEMU emulator version 9.2.2"
EXPECTED_QEMU_BUILD="esp_develop_9.2.2_20261109"

fail() {
    printf 'QEMU CHECK FAILED: %s\n' "$*" >&2
    exit 1
}

[[ -x "$QEMU_BIN" ]] || fail "QEMU executable not found: $QEMU_BIN"

version_output="$("$QEMU_BIN" --version 2>&1)" ||
    fail "could not run $QEMU_BIN --version"
grep -Fq "$EXPECTED_QEMU_VERSION" <<<"$version_output" ||
    fail "expected '$EXPECTED_QEMU_VERSION', found: ${version_output%%$'\n'*}"
grep -Fq "$EXPECTED_QEMU_BUILD" <<<"$version_output" ||
    fail "expected build '$EXPECTED_QEMU_BUILD', found: ${version_output%%$'\n'*}"

machine_output="$("$QEMU_BIN" -machine help 2>&1)" ||
    fail "could not list QEMU machines"
grep -Eq '^esp32s3[[:space:]]' <<<"$machine_output" ||
    fail "QEMU does not advertise the esp32s3 machine"

printf 'QEMU check passed.\n'
printf '  path: %s\n' "$QEMU_BIN"
printf '  version: %s\n' "${version_output%%$'\n'*}"
printf '  machine: esp32s3\n'
