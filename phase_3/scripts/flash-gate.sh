#!/usr/bin/env bash
# Phase 2 physical-flash gate.
#
# --check is build-only and is the default. It never opens a serial device and
# never invokes a flash or eFuse command. --flash requires a reviewed board,
# an explicit serial path, an exact flash-size match, a read-only eFuse summary,
# and typed interactive approval. This script never writes eFuses, never runs a
# flash erase, and never enables Secure Boot or flash encryption.
set -euo pipefail

PROJECT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
MODE=check
PORT=
BOARD_LABEL=
FLASH_SIZE=

EXPECTED_IDF_VERSION="ESP-IDF v5.2.2"
EXPECTED_PROJECT="phase_2"
EXPECTED_TARGET="esp32s3"
EXPECTED_FLASH_SIZE="8MB"

usage() {
    cat <<'EOF'
Usage:
  scripts/flash-gate.sh [--check]
  scripts/flash-gate.sh --flash --port DEVICE --board-label LABEL --flash-size SIZE

--check is build-only and is the default. It contacts no device and changes no
flash or eFuse state. --flash requires a verified Phase 2 build, an explicit
serial device path, the board's actual flash size, a read-only eFuse summary
review, and typed interactive confirmation.
EOF
}

fail() {
    printf 'FLASH GATE BLOCKED: %s\n' "$*" >&2
    exit 1
}

while (($#)); do
    case "$1" in
        --check)
            MODE=check
            shift
            ;;
        --flash)
            MODE=flash
            shift
            ;;
        --port|--board-label|--flash-size)
            (($# >= 2)) || fail "missing value for $1"
            case "$1" in
                --port) PORT=$2 ;;
                --board-label) BOARD_LABEL=$2 ;;
                --flash-size) FLASH_SIZE=$2 ;;
            esac
            shift 2
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            usage >&2
            fail "unknown option: $1"
            ;;
    esac
done

cd "$PROJECT_DIR"

# Fail closed before touching the toolchain if this is not the Phase 2 project.
# This prevents a copied/shared gate from silently operating on Phase 1.
[[ -f "$PROJECT_DIR/CMakeLists.txt" ]] ||
    fail "refusing: missing project CMakeLists.txt"
grep -Eq '^[[:space:]]*project[[:space:]]*\([[:space:]]*phase_2[[:space:]]*\)' \
    "$PROJECT_DIR/CMakeLists.txt" ||
    fail "refusing: this is not the SuperNova Phase 2 project (expected project(phase_2))"
[[ -f "$PROJECT_DIR/components/sn_app/include/sn_app.h" ]] ||
    fail "refusing: missing Phase 2 sn_app component"
[[ -f "$PROJECT_DIR/main/main.c" ]] && grep -q 'sn_app_run' "$PROJECT_DIR/main/main.c" ||
    fail "refusing: main/main.c does not run the Phase 2 application"

command -v idf.py >/dev/null 2>&1 || fail "source the ESP-IDF environment first"
[[ -n "${IDF_PATH:-}" ]] || fail "IDF_PATH is unset; source ~/devenv/env.sh first"

idf_version="$(idf.py --version 2>&1)"
[[ "$idf_version" == *"$EXPECTED_IDF_VERSION"* ]] ||
    fail "expected $EXPECTED_IDF_VERSION, found: $idf_version"

if [[ "$MODE" == flash ]]; then
    [[ -n "$PORT" && -n "$BOARD_LABEL" && -n "$FLASH_SIZE" ]] ||
        fail "--flash requires --port, --board-label, and --flash-size"
    [[ "$PORT" == /* && -e "$PORT" && -r "$PORT" && -w "$PORT" ]] ||
        fail "serial port must be an accessible absolute device path: $PORT"
    [[ "$FLASH_SIZE" =~ ^[0-9]+MB$ ]] ||
        fail "flash size must be an explicit size such as 2MB or 8MB"
    [[ "$BOARD_LABEL" =~ ^[A-Za-z0-9._-]+$ ]] ||
        fail "board label must contain only letters, digits, dot, underscore, or dash"
fi

build_log="$(mktemp)"
trap 'rm -f "$build_log"' EXIT
if ! idf.py build >"$build_log" 2>&1; then
    cat "$build_log" >&2
    fail "ESP-IDF build failed"
fi
if grep -q '^Project build complete' "$build_log"; then
    printf 'ESP-IDF build complete.\n'
fi
grep -E 'warning:|error:' "$build_log" || true

SDKCONFIG="$PROJECT_DIR/sdkconfig"
[[ -s "$SDKCONFIG" ]] || fail "missing generated sdkconfig"

grep -Fxq "CONFIG_IDF_TARGET=\"$EXPECTED_TARGET\"" "$SDKCONFIG" ||
    fail "sdkconfig target is not ESP32-S3"

require_disabled() {
    local symbol="$1"
    local label="$2"
    if grep -Eq "^CONFIG_${symbol}=[yY1]$" "$SDKCONFIG"; then
        fail "$label is enabled in sdkconfig; refusing to flash"
    fi
    grep -Fxq "# CONFIG_${symbol} is not set" "$SDKCONFIG" ||
        fail "required safe setting not confirmed: # CONFIG_${symbol} is not set"
}

require_disabled SECURE_BOOT "Secure Boot"
require_disabled SECURE_FLASH_ENC_ENABLED "flash encryption"
require_disabled FLASH_ENCRYPTION_ENABLED "flash encryption"

if grep -Eq '^CONFIG_SN_ENABLE_ESPNOW=[yY1]$' "$SDKCONFIG"; then
    fail "ESP-NOW is enabled; this gate only flashes the radio-disabled default build"
fi
grep -Fxq '# CONFIG_SN_ENABLE_ESPNOW is not set' "$SDKCONFIG" ||
    fail "required safe setting not confirmed: # CONFIG_SN_ENABLE_ESPNOW is not set"

configured_flash_size="$(
    sed -n 's/^CONFIG_ESPTOOLPY_FLASHSIZE="\([^"]*\)"$/\1/p' "$SDKCONFIG"
)"
[[ -n "$configured_flash_size" ]] || fail "could not determine configured flash size"
[[ "$configured_flash_size" == "$EXPECTED_FLASH_SIZE" ]] ||
    fail "configured flash size $configured_flash_size is not the Phase 2 default $EXPECTED_FLASH_SIZE"

# Reject any application-source eFuse write/program/protect call or automatic
# erase/format call. Scanning source, not build output, keeps vendored IDF code
# from producing false positives.
if grep -RniE \
    'esp_efuse_[A-Za-z0-9_]*[[:space:]]*\(.*(write|burn|program|protect)|esp_efuse_(write|burn|program|protect)[A-Za-z0-9_]*[[:space:]]*\(|espefuse.*(burn|write|program|protect)' \
    "$PROJECT_DIR/main" "$PROJECT_DIR/components" --include='*.c' --include='*.h'
then
    fail "eFuse write/program/protect code found in application source"
fi

if grep -RniE \
    'nvs_flash_erase|esp_partition_erase_range|esp_flash_erase|spi_flash_erase|esp_spiffs_format|spiffs_format|esp_littlefs_format|littlefs_format|fopen[[:space:]]*\([^,]*,[[:space:]]*"w' \
    "$PROJECT_DIR/main" "$PROJECT_DIR/components" --include='*.c' --include='*.h'
then
    fail "automatic erase/format code found in application source"
fi

[[ -s "$PROJECT_DIR/build/phase_2.bin" ]] ||
    fail "build did not produce build/phase_2.bin"

printf '\nBuild-only preflight passed.\n'
printf '  ESP-IDF: %s\n' "$idf_version"
printf '  target: ESP32-S3\n'
printf '  configured flash size: %s\n' "$configured_flash_size"
printf '  Secure Boot / flash encryption: disabled in sdkconfig\n'
printf '  ESP-NOW: disabled in sdkconfig\n'
printf '  application eFuse write/program/protect calls: none found\n'
printf '  application auto-erase/format calls: none found\n'
printf '  firmware image: %s\n' "$PROJECT_DIR/build/phase_2.bin"

if [[ "$MODE" == check ]]; then
    printf 'No device was contacted; no flash or eFuse state was changed.\n'
    exit 0
fi

# Every remaining check happens before any device access.
[[ "$FLASH_SIZE" == "$configured_flash_size" ]] ||
    fail "board flash size $FLASH_SIZE does not match sdkconfig ($configured_flash_size)"
[[ -t 0 ]] || fail "--flash requires an interactive terminal for the final confirmation"
command -v espefuse.py >/dev/null 2>&1 ||
    fail "espefuse.py is required for the read-only board preflight"

cat <<EOF

Read-only eFuse summary for the selected board follows. This command reads and
prints eFuse state only; it does not program eFuses. Review Secure Boot,
flash-encryption, and download-mode state; cancel if any state is enabled or
unclear.
EOF
espefuse.py --chip esp32s3 --port "$PORT" summary

cat <<EOF

This will write the bootloader, partition table, and application to:
  board: $BOARD_LABEL
  port:  $PORT

The project has Secure Boot and flash encryption disabled. This gate does not
automatically certify the displayed eFuse state or the board's provisioning
history, and it does not erase flash.
EOF
read -r -p "Type REVIEWED FUSES AND FLASH $BOARD_LABEL to continue: " confirmation
[[ "$confirmation" == "REVIEWED FUSES AND FLASH $BOARD_LABEL" ]] ||
    fail "confirmation did not match"

# The only flash invocation in this project, reachable only after approval.
idf.py -p "$PORT" flash
