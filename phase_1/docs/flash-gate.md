# Firmware flash gate

## What is and is not permanent

The Phase 1 work so far changed project files only: `idf.py set-target` and
`idf.py build` ran on the host. No board was contacted. The application contains
no eFuse write/program API calls, and the generated `sdkconfig` has Secure Boot
and flash encryption disabled. These build settings do not burn eFuses.

An ordinary ESP-IDF application flash overwrites SPI-flash regions (bootloader,
partition table, and app) but does not by itself make future flash writes
impossible. The project currently inherits a **2 MB flash-size setting** from the
ESP-IDF default; the actual board's flash capacity is unknown and must be
confirmed before a hardware flash. Existing eFuse settings on any particular
board are also unknown. Secure Boot, flash encryption, download-mode locks, or
other pre-existing provisioning can change what updates that board accepts.

## Use the gate

Load ESP-IDF v5.2.2, then run a build-only check:

```bash
source ~/.bashrc
source ~/devenv/env.sh
cd ~/projects/SuperNova/phase_1
./scripts/flash-gate.sh --check
```

The gate rebuilds the project and fails closed unless the target is ESP32-S3,
security features are explicitly disabled, the app contains no eFuse
write/program calls, and the image exists. It does not contact a device.
It captures the normal ESP-IDF build output so the build tool's suggested
direct-flash commands are not presented as the project workflow.

An actual application flash requires all of the following:

- A specifically identified expendable test board and its serial device path.
- The board's actual flash size, matching the generated `sdkconfig`.
- A successful preflight, read-only eFuse summary review, and interactive typed
  confirmation.

Example shape (replace every placeholder with verified values):

```bash
./scripts/flash-gate.sh --flash \
  --port /dev/serial/by-id/DEVICE \
  --board-label "identified ESP32-S3 test board" \
  --flash-size 2MB
```

The gate runs the read-only `espefuse.py ... summary` command, then runs only
`idf.py -p PORT flash` after the operator reviews the summary and confirms.
It does not run flash erase, eFuse programming, Secure Boot, or flash-encryption
provisioning commands. The summary is not parsed into a safety verdict; if
security or download-mode state is enabled or unclear, cancel and consult the
board's provisioning record. Do not bypass the gate with a direct flash command.

This gate cannot prevent another command or tool from bypassing it, and it
cannot make an already-provisioned board reversible. Its checks are a safeguard
for this project, not a claim that the hardware is unprovisioned.
