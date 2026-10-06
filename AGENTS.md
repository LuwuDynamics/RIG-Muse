# RIG-Muse: project instructions for coding agents

## Scope and project context

RIG-Muse connects the Muse Gadget SDK to a RIG-Puppy directly on ESP32-S3. Muse owns conversation/tool selection; device firmware owns hardware execution and feedback. This repository distributes source only. Do not create GitHub Releases, publish binaries or upload personal build artifacts as part of the standard workflow.

Start with `README.md` or `README.zh-CN.md`, `docs/FLASHING.md` and `docs/DEVELOPMENT.md`. Read applicable nested instructions before editing:

- `esp32/AGENTS.md`: SDK build, pairing, logging and tests.
- `esp32/devices/AGENTS.md`: adding board profiles.
- `linux/AGENTS.md`: retained upstream Linux client, outside ordinary Puppy work.

For RIG-Puppy, use the source-only workflow below. Generic upstream examples for other boards do not select the Puppy profile. RIG-Arm is a placeholder, not a supported build.

## Build environment and commands

Use **ESP-IDF v6.0.1**, Python 3, Git and host C/C++ compilers. Verify `idf.py --version`. Do not reuse a RIG-Omni ESP-IDF 5.5 environment. Activate the user's installed IDF through its `export.sh`; use the actual installation path rather than assuming a maintainer's home directory.

From `esp32/`:

```sh
tools/board.sh rig-puppy build
# Configure the same ignored per-build sdkconfig:
idf.py -B build-rig-puppy -DSDKCONFIG=build-rig-puppy/sdkconfig menuconfig
# Rebuild after configuration or code changes:
tools/board.sh rig-puppy build
tools/rig_host_tests.sh
# Prepare local images for the calibration-checking installer:
python3 tools/prepare_flash.py
```

From the repository root:

```sh
python3 tools/audit_public.py
# Required before pushing publication-related changes:
python3 tools/audit_public.py --history
git diff --check
```

First build downloads managed components used by some host harnesses. Report skipped tests with their reasons. Host tests are not on-device or cloud acceptance.

## Configuration and credentials

- Shared defaults: `esp32/devices/sdkconfig.rig-puppy`.
- Available settings: `esp32/main/Kconfig.projbuild`.
- Personal settings: ignored `esp32/build-rig-puppy/sdkconfig`.
- Once generated, `sdkconfig` takes precedence over defaults; use `menuconfig` for an existing build.
- Source defaults have an empty SDK token and proxy host. An unconfigured device uses direct TLS. Token/host/port can be configured locally before compiling or supplied through `tools/provision_rig.py` after flashing.
- Saved USB settings override compiled settings and apply on restart. Reflashing preserves NVS; `--direct` explicitly overrides a compiled proxy, while `--clear` removes saved settings and restores compiled defaults after reboot.
- Do not ask users to paste tokens into chat. Guide local entry through `menuconfig` or the provisioning tool's hidden prompt. Do not read, echo, log or commit real credentials.
- Personal binaries, generated configs, NVS, ELF files, raw logs and device backups stay private. NVS is unencrypted in this community profile.
- `esp32/dev_signing_key.pem` is the SDK's intentionally public development key. Preserve attribution. Do not enable Secure Boot, flash encryption or eFuse authentication in ordinary development.
- `setup.*` commands are local USB-only, not remote Muse tools.

## Architecture and extension points

| Area | Files |
|---|---|
| Board-independent dispatch | `esp32/main/rig_robot.*`, `rig_board.h` |
| Puppy tool registration and execution | `esp32/main/boards/rig_puppy/puppy_robot.c` |
| Servo packets and limits | `rig_motion.*`, `board_config.h` under the Puppy backend |
| Original actions and gait | `puppy_actions.*`, `puppy_native.*`, `puppy_gait.*` |
| Media catalog and effects | `esp32/main/rig_show.*`, `rig_pface.*`, Puppy `rig_media.*` |
| Camera, IMU, light and voice | Puppy `puppy_camera.*`, `puppy_imu*`, `puppy_laser.*`, `puppy_voice.*` |
| Local setup | `esp32/main/rig_setup.*`, `esp32/tools/provision_rig.py` |
| Network proxy | `esp32/main/proxy_tls.c` |
| Local build/flash tooling | `esp32/tools/prepare_flash.py`, `flash_rig.py` |

Keep robot-specific protocol, pins, calibration and kinematics in its board backend. A new board requires its own profile, backend, command schema, tests and hardware acceptance. Do not reuse Puppy calibration or packets for Arm.

Changing a capability requires updating its dispatch, registration schema, `rig.catalog`, documentation and appropriate tests together. Keep preset count separate from tool count: 24 shows, four movement directions, 12 board tools plus SDK `device.health`. `camera.capture` is not a `rig.*` tool.

## Motion and hardware work

Preserve one motion owner, bounded speed/duration, fresh feedback and stop-before-cancellation reporting. Actions report accepted/completed/failed/cancelled; acceptance is not successful motion or goal achievement. Do not add automatic boot motion, repeated retries, unbounded travel or stale-frame catch-up.

Original Puppy calibration at `0xFFF000` is read-only. Do not erase flash wholesale, overwrite calibration/NVS, merge images with padded gaps, burn eFuses or copy calibration between devices. Do not infer board type from ESP32-S3 identity alone.

Only flash when the user requests flashing. Identify the attached Puppy and serial port; inspect port ownership and close only the relevant monitor. Use locally compiled images:

```sh
# From esp32/, after prepare_flash.py:
python3 build-rig-puppy/flash-rig/flash.py --port ACTUAL_PUPPY_PORT
# App-only update requires an existing matching partition table:
python3 build-rig-puppy/flash-rig/flash.py --port ACTUAL_PUPPY_PORT --update
```

The installer checks image hashes/regions/16 MB flash and compares calibration before/after. It does not prove the selected serial device is a Puppy. For motion tests, use the user's current authorization and placement conditions; ask for missing physical conditions when necessary. Report build, flash, boot, service registration and observed behavior separately.

## Validation and contribution standards

Run the relevant board build and host checks for code/tooling changes. Documentation-only work needs command/path/link review and a whitespace check. Do not flash solely to validate documentation. Record evidence and limits instead of treating static checks as physical acceptance.

Preserve upstream notices and third-party licenses. The upstream Jollybot avatar is outside Apache-2.0; do not relicense it. Describe capabilities as Muse-facing; preserve legacy protocol identifiers only where compatibility requires them. Read `CONTRIBUTING.md` and `SECURITY.md` before publishing changes.
