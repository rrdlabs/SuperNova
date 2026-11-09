# Physical flash gate

`scripts/flash-gate.sh` is the only project path that may flash a physical
ESP32-S3. It is Phase-2-local and refuses to run against any project whose root
`CMakeLists.txt` does not declare `project(phase_2)` and whose `main/main.c`
does not run the Phase 2 application.

This gate is a procedural safeguard, not a security boundary. It cannot stop a
deliberate direct invocation of `idf.py flash` or another flashing tool.

## Default: build-only `--check`

```bash
source ~/.bashrc
source ~/devenv/env.sh
cd ~/projects/SuperNova/phase_2
./scripts/flash-gate.sh --check
```

`--check` is the default. It:

- verifies the Phase 2 project identity before touching the toolchain;
- requires ESP-IDF v5.2.2 and `IDF_PATH`;
- runs `idf.py build`;
- requires an ESP32-S3 `sdkconfig` with an 8 MB flash size;
- requires Secure Boot, `SECURE_FLASH_ENC_ENABLED`, and
  `FLASH_ENCRYPTION_ENABLED` to be disabled;
- requires `CONFIG_SN_ENABLE_ESPNOW` to be disabled;
- scans application `.c`/`.h` files for eFuse write/burn/program/protect calls
  and automatic erase/format calls;
- requires `build/phase_2.bin`;
- prints the preflight result.

It never opens a serial device, never invokes `espefuse.py`, and never invokes a
flash command. It changes no flash or eFuse state.

## Hardware path: `--flash`

The hardware path is **not** run by the implementation agent. It requires a
separately approved, identified expendable board.

```bash
./scripts/flash-gate.sh --flash \
  --port /dev/ttyUSB0 \
  --board-label LAB-S3-01 \
  --flash-size 8MB
```

Before any device access, `--flash` requires:

- `--port` to be an accessible absolute serial-device path;
- a restricted `--board-label` (letters, digits, dot, underscore, dash);
- `--flash-size` to be explicit and to match the generated `sdkconfig` exactly;
- an interactive terminal for the final confirmation;
- `espefuse.py` to be available.

It then runs `espefuse.py --chip esp32s3 --port <port> summary`, which is
read-only. The operator must review Secure Boot, flash-encryption, and
download-mode state. The gate does not certify that state or the board's
provisioning history.

After review, the operator must type exactly:

```text
REVIEWED FUSES AND FLASH <board-label>
```

Only then does the gate run `idf.py -p <port> flash`.

The gate never writes or burns eFuses, never enables Secure Boot or flash
encryption, never erases flash, and never auto-formats storage. Secure Boot and
flash encryption setup/first boot can irreversibly change fuses, so both stay
disabled.

## Regression checks

```bash
./scripts/gate-regression.sh
```

The regression suite uses a fake `idf.py`/`espefuse.py` and fixture projects. It
proves that:

| ID | Check |
| --- | --- |
| `gate.check.no_device` | `--check` builds/validates but contacts no device and runs no flash command |
| `gate.check.phase_scope` | a non-Phase-2 project is refused before the toolchain is touched |
| `gate.check.secure_boot` | enabled Secure Boot is refused |
| `gate.check.flash_encryption` | enabled flash encryption is refused |
| `gate.check.efuse_write` | eFuse write/program API in application source is refused |
| `gate.check.auto_erase` | automatic erase/format call in application source is refused |
| `gate.flash.missing_args` | `--flash` without port/board/size is refused |
| `gate.flash.bad_port` | nonexistent serial path is refused |
| `gate.flash.size_mismatch` | board size different from `sdkconfig` is refused before device access |
| `gate.flash.noninteractive` | `--flash` without a terminal is refused before the eFuse summary or flash command |

The regression suite also asserts that no refusal path reached `idf.py -p ...`
and that refusal paths before device access did not call `espefuse.py`.

Current result: all ten gate regression checks pass.

## Evidence level

- The build-only preflight and all gate regressions are **build/tooling
  verified**.
- No physical board was contacted, flashed, or provisioned during Phase 2
  implementation.
- A hardware claim requires a separate approved `--flash` run on an identified
  board plus the operator's review of the read-only eFuse summary.
