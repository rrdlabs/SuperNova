#!/usr/bin/env bash
# Build the Phase 3 ESP32-S3 image, assemble an 8 MB flash image from the
# generated ESP-IDF flash arguments, boot it under the pinned S3 QEMU, capture
# UART output, and write a per-run manifest.
#
# QEMU is emulator-only evidence. It does not emulate Wi-Fi/Bluetooth, and its
# RNG, timing, power, and eFuse behavior are simulated. This harness never adds
# an eFuse drive and never transmits.
set -euo pipefail

PROJECT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
QEMU_BIN="${QEMU_SYSTEM_XTENSA:-$HOME/devenv/qemu-s3/qemu-system-xtensa}"
RUN_ROOT="${SN_QEMU_RUN_ROOT:-$PROJECT_DIR/qemu-runs}"
SCENARIO="phase3-full-suite"
QEMU_TIMEOUT="${QEMU_TIMEOUT:-60}"
EXPECTED_IDF_VERSION="ESP-IDF v5.2.2"
EXPECTED_FLASH_BYTES=8388608

usage() {
    cat <<'EOF'
Usage:
  scripts/qemu-run.sh [--scenario NAME] [--timeout SECONDS]

Environment:
  QEMU_SYSTEM_XTENSA  path to qemu-system-xtensa (default ~/devenv/qemu-s3/...)
  SN_QEMU_RUN_ROOT    output directory (default <project>/qemu-runs)
  QEMU_TIMEOUT        boot/run timeout in seconds (default 60)
EOF
}

fail() {
    printf 'QEMU RUN FAILED: %s\n' "$*" >&2
    exit 1
}

while (($#)); do
    case "$1" in
        --scenario)
            (($# >= 2)) || fail "missing value for --scenario"
            SCENARIO=$2
            shift 2
            ;;
        --timeout)
            (($# >= 2)) || fail "missing value for --timeout"
            QEMU_TIMEOUT=$2
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

[[ "$SCENARIO" =~ ^[A-Za-z0-9._-]+$ ]] ||
    fail "scenario name must contain only letters, digits, dot, underscore, or dash"
[[ "$QEMU_TIMEOUT" =~ ^[1-9][0-9]*$ ]] || fail "timeout must be a positive integer"

cd "$PROJECT_DIR"

command -v idf.py >/dev/null 2>&1 || fail "source the ESP-IDF environment first"
[[ -n "${IDF_PATH:-}" ]] || fail "IDF_PATH is unset; source ~/devenv/env.sh first"
command -v esptool.py >/dev/null 2>&1 || fail "esptool.py is required to merge the flash image"

idf_version="$(idf.py --version 2>&1)"
[[ "$idf_version" == *"$EXPECTED_IDF_VERSION"* ]] ||
    fail "expected $EXPECTED_IDF_VERSION, found: $idf_version"

QEMU_SYSTEM_XTENSA="$QEMU_BIN" "$PROJECT_DIR/scripts/check-qemu.sh" ||
    fail "pinned QEMU verification failed"

run_id="$(date -u +%Y%m%dT%H%M%SZ)-$$"
run_dir="$RUN_ROOT/$run_id"
mkdir -p "$run_dir"

printf 'QEMU run directory: %s\n' "$run_dir"
printf 'Building the ESP32-S3 image...\n'
if ! idf.py build >"$run_dir/build.log" 2>&1; then
    tail -n 80 "$run_dir/build.log" >&2 || true
    fail "ESP-IDF build failed (see $run_dir/build.log)"
fi

SDKCONFIG="$PROJECT_DIR/sdkconfig"
[[ -s "$SDKCONFIG" ]] || fail "missing generated sdkconfig"
grep -Fxq 'CONFIG_IDF_TARGET="esp32s3"' "$SDKCONFIG" ||
    fail "sdkconfig target is not ESP32-S3"
grep -Fxq 'CONFIG_ESPTOOLPY_FLASHSIZE="8MB"' "$SDKCONFIG" ||
    fail "sdkconfig flash size is not 8MB"
for disabled in \
    '# CONFIG_SECURE_BOOT is not set' \
    '# CONFIG_SECURE_FLASH_ENC_ENABLED is not set' \
    '# CONFIG_FLASH_ENCRYPTION_ENABLED is not set' \
    '# CONFIG_SN_ENABLE_ESPNOW is not set'
do
    grep -Fxq "$disabled" "$SDKCONFIG" ||
        fail "required safe setting not confirmed: $disabled"
done
if grep -Eq '^CONFIG_SN_ENABLE_ESPNOW=[yY1]$' "$SDKCONFIG"; then
    fail "CONFIG_SN_ENABLE_ESPNOW is enabled; QEMU must run with radios disabled"
fi

[[ -s "$PROJECT_DIR/build/flash_args" ]] || fail "missing build/flash_args"
[[ -s "$PROJECT_DIR/build/phase_3.bin" ]] || fail "missing build/phase_3.bin"
grep -Fq -- '--flash_size 8MB' "$PROJECT_DIR/build/flash_args" ||
    fail "build/flash_args does not request an 8MB flash size"

printf 'Creating the 8 MB QEMU flash image from build/flash_args...\n'
merge_cmd=(
    esptool.py --chip esp32s3 merge_bin
    --fill-flash-size 8MB
    -o "$run_dir/flash.bin"
    @flash_args
)
if ! (cd "$PROJECT_DIR/build" && "${merge_cmd[@]}") >"$run_dir/merge.log" 2>&1; then
    tail -n 80 "$run_dir/merge.log" >&2 || true
    fail "esptool merge_bin failed (see $run_dir/merge.log)"
fi
flash_bytes="$(stat -c %s "$run_dir/flash.bin")"
[[ "$flash_bytes" == "$EXPECTED_FLASH_BYTES" ]] ||
    fail "flash image is $flash_bytes bytes, expected $EXPECTED_FLASH_BYTES"

uart_log="$run_dir/uart.log"
: >"$uart_log"

qemu_cmd=(
    "$QEMU_BIN"
    -nographic
    -machine esp32s3
    -drive "file=$run_dir/flash.bin,if=mtd,format=raw"
)

printf 'Booting the S3 image under QEMU (no eFuse drive, radios disabled)...\n'
"${qemu_cmd[@]}" >"$uart_log" 2>&1 &
qemu_pid=$!

deadline=$((SECONDS + QEMU_TIMEOUT))
complete_deadline=-1
timed_out=0
while true; do
    if ! kill -0 "$qemu_pid" 2>/dev/null; then
        break
    fi
    process_state="$(ps -o stat= -p "$qemu_pid" 2>/dev/null || true)"
    [[ -n "$process_state" ]] || break
    [[ "$process_state" == Z* ]] && break

    if grep -q 'SN2|event=RUN_COMPLETE' "$uart_log" 2>/dev/null; then
        if ((complete_deadline < 0)); then
            complete_deadline=$((SECONDS + 2))
        fi
        grep -q 'SN2|event=APP_EXIT' "$uart_log" 2>/dev/null && break
        ((SECONDS >= complete_deadline)) && break
    elif ((SECONDS >= deadline)); then
        timed_out=1
        break
    fi
    sleep 0.2
done

if kill -0 "$qemu_pid" 2>/dev/null; then
    kill "$qemu_pid" 2>/dev/null || true
fi
set +e
wait "$qemu_pid"
qemu_status=$?
set -e

run_complete_line="$(grep 'SN2|event=RUN_COMPLETE' "$uart_log" 2>/dev/null | tail -n 1 || true)"
summary_line="$(grep 'SN2|event=SUMMARY' "$uart_log" 2>/dev/null | tail -n 1 || true)"
app_exit_line="$(grep 'SN2|event=APP_EXIT' "$uart_log" 2>/dev/null | tail -n 1 || true)"

result=FAIL
if [[ -n "$run_complete_line" && -n "$summary_line" ]] &&
    [[ "$run_complete_line" == *'result=PASS'* ]] &&
    ! grep -q 'status=FAIL' "$uart_log"; then
    result=PASS
fi

source_revision="$(git -C "$PROJECT_DIR" rev-parse --verify HEAD 2>/dev/null || true)"
[[ -n "$source_revision" ]] || source_revision="no-commit"
source_dirty_files="$(git -C "$PROJECT_DIR" status --porcelain --untracked-files=all -- . 2>/dev/null | wc -l | tr -d ' ')"
sdkconfig_sha256="$(sha256sum "$SDKCONFIG" | awk '{print $1}')"
qemu_version="$("$QEMU_BIN" --version 2>&1 | head -n 1)"
timestamp_utc="$(date -u +%Y-%m-%dT%H:%M:%SZ)"

qemu_command_string="$(printf '%q ' "${qemu_cmd[@]}")"
merge_command_string="$(printf '%q ' "${merge_cmd[@]}")"

manifest="$run_dir/manifest.txt"
{
    printf 'run_id=%s\n' "$run_id"
    printf 'timestamp_utc=%s\n' "$timestamp_utc"
    printf 'scenario=%s\n' "$SCENARIO"
    printf 'source_revision=%s\n' "$source_revision"
    printf 'source_dirty_files=%s\n' "$source_dirty_files"
    printf 'idf_version=%s\n' "$idf_version"
    printf 'qemu_version=%s\n' "$qemu_version"
    printf 'qemu_path=%s\n' "$QEMU_BIN"
    printf 'target=esp32s3\n'
    printf 'sdkconfig_sha256=%s\n' "$sdkconfig_sha256"
    printf 'build_command=idf.py build\n'
    printf 'merge_command=%s\n' "$merge_command_string"
    printf 'qemu_command=%s\n' "$qemu_command_string"
    printf 'flash_image=%s\n' "$run_dir/flash.bin"
    printf 'flash_image_bytes=%s\n' "$flash_bytes"
    printf 'uart_log=%s\n' "$uart_log"
    printf 'build_log=%s\n' "$run_dir/build.log"
    printf 'merge_log=%s\n' "$run_dir/merge.log"
    printf 'qemu_exit_status=%s\n' "$qemu_status"
    printf 'timed_out=%s\n' "$timed_out"
    printf 'run_complete=%s\n' "$([[ -n "$run_complete_line" ]] && echo yes || echo no)"
    printf 'app_exit=%s\n' "$([[ -n "$app_exit_line" ]] && echo yes || echo no)"
    printf 'summary=%s\n' "$summary_line"
    printf 'result=%s\n' "$result"
    printf 'limitations=%s\n' \
        'emulator-only; no Wi-Fi/Bluetooth emulation; simulated RNG/timing/power/eFuse; no Secure Boot validation; no hardware or production claim'
} >"$manifest"

if [[ "$result" != PASS ]]; then
    printf '\nUART log tail:\n' >&2
    tail -n 80 "$uart_log" >&2 || true
    printf '\nManifest: %s\n' "$manifest" >&2
    fail "test result was not PASS (missing/failed RUN_COMPLETE or SUMMARY)"
fi

printf '\nQEMU run PASS.\n'
printf '  %s\n' "$summary_line"
printf '  %s\n' "$run_complete_line"
printf '  manifest: %s\n' "$manifest"
printf '  uart log: %s\n' "$uart_log"
printf 'Limitations: emulator-only evidence; no Wi-Fi/Bluetooth emulation; simulated\n'
printf 'RNG/timing/power/eFuse; Secure Boot not validated; not a hardware or production claim.\n'
