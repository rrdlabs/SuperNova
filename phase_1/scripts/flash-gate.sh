#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
MODE=check
PORT=
BOARD_LABEL=
FLASH_SIZE=

usage() {
    cat <<'EOF'
Usage:
  scripts/flash-gate.sh [--check]
  scripts/flash-gate.sh --flash --port DEVICE --board-label LABEL --flash-size SIZE

--check is build-only and is the default. --flash requires a verified project
configuration, an explicit device/board/flash-size, and interactive confirmation.
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

command -v idf.py >/dev/null 2>&1 || fail "source the ESP-IDF environment first"
[[ -n "${IDF_PATH:-}" ]] || fail "IDF_PATH is unset; source ~/devenv/env.sh first"

idf_version="$(idf.py --version 2>&1)"
[[ "$idf_version" == *"ESP-IDF v5.2.2"* ]] ||
    fail "expected ESP-IDF v5.2.2, found: $idf_version"

if [[ "$MODE" == flash ]]; then
    [[ -n "$PORT" && -n "$BOARD_LABEL" && -n "$FLASH_SIZE" ]] ||
        fail "--flash requires --port, --board-label, and --flash-size"
    [[ "$PORT" == /* && -e "$PORT" && -r "$PORT" && -w "$PORT" ]] ||
        fail "serial port must be an accessible absolute device path: $PORT"
    [[ "$FLASH_SIZE" =~ ^[0-9]+MB$ ]] ||
        fail "flash size must be an explicit size such as 2MB or 8MB"
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
grep -Fxq 'CONFIG_IDF_TARGET="esp32s3"' "$SDKCONFIG" ||
    fail "sdkconfig target is not ESP32-S3"

for disabled in \
    '# CONFIG_SECURE_BOOT is not set' \
    '# CONFIG_SECURE_FLASH_ENC_ENABLED is not set' \
    '# CONFIG_FLASH_ENCRYPTION_ENABLED is not set'
do
    grep -Fxq "$disabled" "$SDKCONFIG" ||
        fail "required safe setting not confirmed: $disabled"
done

configured_flash_size="$(
    sed -n 's/^CONFIG_ESPTOOLPY_FLASHSIZE="\([^"]*\)"$/\1/p' "$SDKCONFIG"
)"
[[ -n "$configured_flash_size" ]] || fail "could not determine configured flash size"

if grep -RniE \
    'esp_efuse_.*(write|burn|program)|espefuse.*(burn|write|program|protect)' \
    "$PROJECT_DIR/main" --include='*.c' --include='*.h'
then
    fail "eFuse write/program code found under main/"
fi

[[ -s "$PROJECT_DIR/build/phase_1.bin" ]] ||
    fail "build did not produce build/phase_1.bin"

printf '\nBuild-only preflight passed.\n'
printf '  ESP-IDF: %s\n' "$idf_version"
printf '  target: ESP32-S3\n'
printf '  configured flash size: %s\n' "$configured_flash_size"
printf '  Secure Boot / flash encryption: disabled in sdkconfig\n'
printf '  application eFuse write/program calls: none found\n'
printf '  firmware image: %s\n' "$PROJECT_DIR/build/phase_1.bin"

if [[ "$MODE" == check ]]; then
    printf 'No device was contacted; no flash or eFuse state was changed.\n'
    exit 0
fi

[[ "$FLASH_SIZE" == "$configured_flash_size" ]] ||
    fail "board flash size $FLASH_SIZE does not match sdkconfig ($configured_flash_size)"
[[ -t 0 ]] || fail "--flash requires an interactive terminal for the final confirmation"
command -v espefuse.py >/dev/null 2>&1 ||
    fail "espefuse.py is required for the read-only board preflight"

cat <<EOF

Read-only eFuse summary for the selected board follows. This command does not
program eFuses. Review Secure Boot, flash-encryption, and download-mode state;
cancel if any state is enabled or unclear.
EOF
espefuse.py --chip esp32s3 --port "$PORT" summary

cat <<EOF

This will write the bootloader, partition table, and application to:
  board: $BOARD_LABEL
  port:  $PORT

The project has Secure Boot and flash encryption disabled. This gate does not
automatically certify the displayed eFuse state or the board's provisioning history.
EOF
read -r -p "Type REVIEWED FUSES AND FLASH $BOARD_LABEL to continue: " confirmation
[[ "$confirmation" == "REVIEWED FUSES AND FLASH $BOARD_LABEL" ]] ||
    fail "confirmation did not match"

idf.py -p "$PORT" flash
