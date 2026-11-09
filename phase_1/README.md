# SuperNova Phase 1 — ESP32-S3 bench firmware

Minimal ESP-IDF test firmware for a reproducible early hardware baseline. It
reports board/runtime information, checks SHA-256 against a known-answer vector,
benchmarks 1 KiB SHA-256 calls, and exercises the RNG API without exposing sample
bytes. It does not implement a ledger, key provisioning, storage, or radio
networking.

## Environment and build

The Warroom DevEnv provides ESP-IDF v5.2.2:

```bash
source ~/.bashrc
source ~/devenv/env.sh
cd ~/projects/SuperNova/phase_1
idf.py set-target esp32s3
./scripts/flash-gate.sh --check
```

`set-target` and the gate's build operate on project files; no board is
contacted. This scaffold has not been flashed. For a later approved bench run,
identify the test board and serial port, confirm its flash capacity, and use
`scripts/flash-gate.sh --flash ...`. The gate builds and checks the configuration
then shows a read-only eFuse summary before requiring interactive confirmation.
Do not bypass it or use flash erase as a shortcut. The generated sdkconfig
currently selects 2 MB; verify whether that matches the actual board before
attempting a flash.

## Hardware safety

**Do not run eFuse burn/program operations.** eFuse bits are irreversible. This
phase contains no eFuse API calls or provisioning code. Keep Secure Boot V2 and
flash encryption disabled: enabling either can change eFuse state during device
provisioning or first boot. Do not run any security-provisioning workflow.

The RNG smoke test temporarily enables the internal entropy source and disables
it immediately afterward. It is valid only while this app does not initialize
Wi-Fi, Bluetooth, ADC, or I2S; review that lifecycle before adding any of them.
See `docs/research-notes.md` and `docs/test-plan.md`.

See `docs/flash-gate.md` for the non-permanence assessment and hardware-write
gate, and `docs/qemu-review.md` for the emulator limitations and local QEMU
availability.
