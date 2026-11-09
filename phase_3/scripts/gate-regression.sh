#!/usr/bin/env bash
# Regression checks for scripts/flash-gate.sh.
#
# These checks use a fake IDF/espefuse toolchain and fixture projects. They
# prove that --check never contacts a device and that unsafe configurations and
# incomplete --flash requests are refused before any device access. They do not
# prove that a deliberate direct-command bypass is impossible.
set -euo pipefail

PROJECT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
GATE="$PROJECT_DIR/scripts/flash-gate.sh"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

FAKEBIN="$TMP/bin"
FAKE_IDF_PATH="$TMP/fake-idf"
mkdir -p "$FAKEBIN" "$FAKE_IDF_PATH"
FAKE_IDF_LOG="$TMP/fake-idf.log"
FAKE_ESPEFUSE_LOG="$TMP/fake-espefuse.log"
: >"$FAKE_IDF_LOG"
: >"$FAKE_ESPEFUSE_LOG"

cat >"$FAKEBIN/idf.py" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
printf 'idf.py %s\n' "$*" >>"${FAKE_IDF_LOG:?}"
case "${1:-}" in
    --version)
        printf 'ESP-IDF v5.2.2\n'
        ;;
    build)
        mkdir -p build
        printf 'fake-phase-2-image\n' >build/phase_2.bin
        printf 'Project build complete.\n'
        ;;
    -p)
        printf 'FAKE IDF: unexpected flash invocation: %s\n' "$*" >&2
        exit 99
        ;;
    *)
        ;;
esac
EOF
chmod +x "$FAKEBIN/idf.py"

cat >"$FAKEBIN/espefuse.py" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
printf 'espefuse.py %s\n' "$*" >>"${FAKE_ESPEFUSE_LOG:?}"
printf 'FAKE EFUSE SUMMARY (read-only)\n'
EOF
chmod +x "$FAKEBIN/espefuse.py"

make_fixture() {
    local name="$1"
    local dir="$TMP/fixture-$name"
    mkdir -p "$dir/scripts" "$dir/main" "$dir/components/sn_app/include"
    cp "$PROJECT_DIR/CMakeLists.txt" "$dir/CMakeLists.txt"
    cp "$PROJECT_DIR/main/main.c" "$dir/main/main.c"
    cp "$PROJECT_DIR/components/sn_app/include/sn_app.h" \
        "$dir/components/sn_app/include/sn_app.h"
    cp "$GATE" "$dir/scripts/flash-gate.sh"
    cp "$PROJECT_DIR/sdkconfig" "$dir/sdkconfig"
    printf '%s\n' "$dir"
}

FAKE_SERIAL="$TMP/fake-serial-device"
: >"$FAKE_SERIAL"

CASE_STATUS=0
run_case() {
    local id="$1"
    local fixture="$2"
    shift 2
    : >"$FAKE_IDF_LOG"
    : >"$FAKE_ESPEFUSE_LOG"
    set +e
    (
        cd "$fixture"
        PATH="$FAKEBIN:$PATH" \
        IDF_PATH="$FAKE_IDF_PATH" \
        FAKE_IDF_LOG="$FAKE_IDF_LOG" \
        FAKE_ESPEFUSE_LOG="$FAKE_ESPEFUSE_LOG" \
            "$fixture/scripts/flash-gate.sh" "$@" </dev/null \
            >"$TMP/$id.out" 2>&1
    )
    CASE_STATUS=$?
    set -e
}

fail_case() {
    local id="$1"
    shift
    printf 'gate regression FAIL: %s: %s\n' "$id" "$*" >&2
    printf '%s\n' '--- gate output ---' >&2
    cat "$TMP/$id.out" >&2 || true
    printf '%s\n' '--- fake idf log ---' >&2
    cat "$FAKE_IDF_LOG" >&2 || true
    exit 1
}

expect_success() {
    local id="$1"
    local pattern="$2"
    ((CASE_STATUS == 0)) || fail_case "$id" "expected success, got status $CASE_STATUS"
    grep -Fq -- "$pattern" "$TMP/$id.out" ||
        fail_case "$id" "missing output: $pattern"
    printf 'gate regression PASS: %s\n' "$id"
}

expect_refusal() {
    local id="$1"
    local pattern="$2"
    ((CASE_STATUS != 0)) || fail_case "$id" "expected refusal, gate succeeded"
    grep -Fq -- "$pattern" "$TMP/$id.out" ||
        fail_case "$id" "missing refusal text: $pattern"
    if grep -Eq '^idf\.py -p ' "$FAKE_IDF_LOG"; then
        fail_case "$id" "gate reached idf.py flash"
    fi
    printf 'gate regression PASS: %s\n' "$id"
}

expect_no_espefuse() {
    local id="$1"
    [[ ! -s "$FAKE_ESPEFUSE_LOG" ]] ||
        fail_case "$id" "gate contacted espefuse before refusing"
}

# gate.check.no_device: --check builds and validates, but contacts no device.
fixture="$(make_fixture check-no-device)"
run_case gate.check.no_device "$fixture" --check
expect_success gate.check.no_device "No device was contacted; no flash or eFuse state was changed."
expect_no_espefuse gate.check.no_device
grep -Eq '^idf\.py --version$' "$FAKE_IDF_LOG" ||
    fail_case gate.check.no_device "gate did not verify the IDF version"
grep -Eq '^idf\.py build$' "$FAKE_IDF_LOG" ||
    fail_case gate.check.no_device "gate did not run the build"
if grep -Eq '^idf\.py -p ' "$FAKE_IDF_LOG"; then
    fail_case gate.check.no_device "gate reached a flash command"
fi

# gate.check.phase_scope: a Phase 1 project is refused.
fixture="$(make_fixture phase-scope)"
sed -i 's/project(phase_2)/project(phase_1)/' "$fixture/CMakeLists.txt"
run_case gate.check.phase_scope "$fixture" --check
expect_refusal gate.check.phase_scope "not the SuperNova Phase 2 project"
[[ ! -s "$FAKE_IDF_LOG" ]] ||
    fail_case gate.check.phase_scope "gate touched the toolchain before refusing scope"
expect_no_espefuse gate.check.phase_scope

# gate.check.secure_boot: an enabled Secure Boot setting is refused.
fixture="$(make_fixture secure-boot)"
printf 'CONFIG_SECURE_BOOT=y\n' >>"$fixture/sdkconfig"
run_case gate.check.secure_boot "$fixture" --check
expect_refusal gate.check.secure_boot "Secure Boot is enabled in sdkconfig"
expect_no_espefuse gate.check.secure_boot

# gate.check.flash_encryption: either flash-encryption switch is refused.
fixture="$(make_fixture flash-encryption)"
printf 'CONFIG_SECURE_FLASH_ENC_ENABLED=y\nCONFIG_FLASH_ENCRYPTION_ENABLED=y\n' \
    >>"$fixture/sdkconfig"
run_case gate.check.flash_encryption "$fixture" --check
expect_refusal gate.check.flash_encryption "flash encryption is enabled in sdkconfig"
expect_no_espefuse gate.check.flash_encryption

# gate.check.efuse_write: eFuse write/program API in application source is refused.
fixture="$(make_fixture efuse-write)"
cat >"$fixture/main/efuse_write.c" <<'EOF'
void bad_efuse_write(void)
{
    esp_efuse_write_field_blob(0, 0, 0);
}
EOF
run_case gate.check.efuse_write "$fixture" --check
expect_refusal gate.check.efuse_write "eFuse write/program/protect code"
expect_no_espefuse gate.check.efuse_write

# gate.check.auto_erase: an automatic erase/format call is refused.
fixture="$(make_fixture auto-erase)"
cat >"$fixture/main/auto_erase.c" <<'EOF'
void bad_auto_erase(void)
{
    nvs_flash_erase();
}
EOF
run_case gate.check.auto_erase "$fixture" --check
expect_refusal gate.check.auto_erase "automatic erase/format code"
expect_no_espefuse gate.check.auto_erase

# gate.flash.missing_args: --flash without board details is refused.
fixture="$(make_fixture flash-missing-args)"
run_case gate.flash.missing_args "$fixture" --flash
expect_refusal gate.flash.missing_args "--flash requires --port, --board-label, and --flash-size"
expect_no_espefuse gate.flash.missing_args

# gate.flash.bad_port: a nonexistent serial path is refused.
fixture="$(make_fixture flash-bad-port)"
run_case gate.flash.bad_port "$fixture" --flash \
    --port "$TMP/no-such-serial-device" --board-label TESTBOARD --flash-size 8MB
expect_refusal gate.flash.bad_port "serial port must be an accessible absolute device path"
expect_no_espefuse gate.flash.bad_port

# gate.flash.size_mismatch: a board size that differs from sdkconfig is refused
# before any device access.
fixture="$(make_fixture flash-size-mismatch)"
run_case gate.flash.size_mismatch "$fixture" --flash \
    --port "$FAKE_SERIAL" --board-label TESTBOARD --flash-size 4MB
expect_refusal gate.flash.size_mismatch "does not match sdkconfig"
expect_no_espefuse gate.flash.size_mismatch

# gate.flash.noninteractive: --flash without a terminal is refused before the
# read-only eFuse summary or any flash command.
fixture="$(make_fixture flash-noninteractive)"
run_case gate.flash.noninteractive "$fixture" --flash \
    --port "$FAKE_SERIAL" --board-label TESTBOARD --flash-size 8MB
expect_refusal gate.flash.noninteractive "requires an interactive terminal"
expect_no_espefuse gate.flash.noninteractive

printf '\ngate regression: all checks passed.\n'
