# RIG-Puppy technical reference

[Project overview](../README.md) · [中文入门](../README.zh-CN.md) · [Installation](../docs/FLASHING.md)

RIG-Muse uses the Muse Gadget SDK's BLE pairing, Wi-Fi, TLS and Noise control session directly on ESP32-S3. Puppy-specific protocol and devices live under `main/boards/rig_puppy/`; a build links one selected board backend. No runtime Puppy's calibration is written by these tools.

## Tool catalog

| Tool | Behavior |
|---|---|
| `rig.catalog` | Actual board tools, four movements and 24 preset entries with timings |
| `rig.status` | Readiness, calibration validity, fresh joint feedback and last job state |
| `rig.prepare` | Bring five joints to the stored-calibration standing pose |
| `rig.perform` | Named body/media performance |
| `rig.move` | Finite forward/backward walking or left/right turning |
| `rig.stop` | Cancel body/media work and close the light accessory |
| `rig.imu` | Pose/event telemetry and optional idle reactions |
| `rig.battery` | Voltage, approximate percentage and data freshness |
| `rig.voice` | Record/send/cancel/status of a voice note |
| `camera.capture` | Fresh 240×240 JPEG result with `data_base64` |
| `rig.laser` | Light accessory on/off/mode with a bounded timer |
| `rig.gesture` | Small diagnostic waist gesture; use `rig.perform greet` for a full greeting |

The SDK adds `device.health`. Camera is a separately named tool, not a `rig.*` command. A local catalog, registration acknowledgment, server `status=registered`, app discovery and physical success are distinct validation steps.

## Presets

Original actions: `wave`, `naughty`, `lookup`, `swing`, `rolling`, `angry`, `swimming`, `pee`, `stretch`, `bouncing`, `shaking`, `sit`, `scratch`, `hug`, `keep_sit`, `sit_reset`.

Aliases: `greet=wave`, `wake=stretch`, `happy=swing`. `reset` prepares standing. `sleep`, `curious`, `shy` and `surprised` are media-only. Total: 24 entries. `keep_sit` finishes seated; `sit_reset` rises directly from sitting. Original formula provenance and reference vectors are committed, so normal tests/builds do not require another RIG checkout.

## Motion and feedback

- One UART executor owns the five joints. Body jobs are not queued.
- Commands return `accepted` and `job_id`; poll `rig.status` until `completed`, `failed` or `cancelled` before another body job.
- Standing uses calibration-relative offsets `[-550,+550,-550,+550,0]`, bounded preparation and fresh position confirmation. Calibration values must be within `200..2800`.
- Original action timing uses a 5 ms control loop with alternating 10/15 ms logical updates and original speed fields. Large scheduling delays fail rather than replaying a burst of stale frames.
- Calibration-relative action ranges are scaled when needed to stay inside positional limits; excessively reduced motion is rejected. Final feedback verifies observed movement and target posture, not the entire intermediate trajectory.
- `rig.move` directions are `forward/backward/left/right`; left/right are turns. Speed is `10..100` (default 40); duration is `100..5000` ms in 5 ms increments (default 1000).
- Movement returns to standing and checks feedback, without distance/heading odometry. Persistent tilt can stop movement; there is no automatic self-righting.
- Stale feedback, lost enable state, disconnection, cancellation or BOOT stop interrupts work. Healthy joints hold their current position; stale joints may be released. No automatic retry.
- Torque is returned as telemetry. This firmware does not use torque thresholds as overload protection.

## Hardware

The profile targets ESP32-S3, 16 MB flash and octal PSRAM. GPIO19/20 belong to the GC9A01 display, so the console uses the USB UART bridge rather than native USB Serial/JTAG. The original five `uint32` servo zero positions reside at `0xFFF000` and are read-only.

QMI8658C uses I2C0 SDA48/SCL14. Camera GC0308 uses its own I2C bus. The microphone is on I2S1 BCLK2/WS1/DIN42 at 16 kHz mono. The light accessory uses GPIO46. Full pins and protocol constants are in `main/boards/rig_puppy/board_config.h`; do not reuse them for Arm.

Battery voltage comes from servo 1, sampled about every five seconds while idle. Data older than 15 seconds is stale. Percentage is a linear estimate across 6.6..8.4 V, not a fuel-gauge measurement; charging state is unknown.

IMU reactions use acceleration/tilt features with debounce. Pickup-like acceleration does not prove the robot remains held. Reactions only play expressions/sound while idle and are suppressed during motion or recording. Disable with `rig.imu {"reactions":false}`.

## Buttons and voice

While idle and connected, double-click BOOT starts recording; double-click again sends. A single click cancels; double-click during sending cancels. The maximum recording is 15 seconds, after which sending is automatic. During motion, button handling prioritizes stopping. A five-second hold clears Muse/Wi-Fi setup.

Recording has a start cue and listening face; sending, acknowledgment, failure and cancellation have distinct feedback. Microphone capture starts after the cue drains. Voice responses are viewed in Muse, not spoken by the robot. Cloud upload acceptance remains a separate field check from microphone initialization and host tests.

## Local console

Open the USB UART console at 115200 baud. Do not compete with another monitor/flasher. Opening the port may reset the board. Examples:

```text
>rig.catalog
>rig.status
>rig.prepare
>rig.perform greet
>rig.perform keep_sit
>rig.perform sit_reset
>rig.move {"direction":"forward","speed":30,"duration_ms":500}
>rig.battery
>rig.imu {"reactions":true}
>rig.laser {"mode":1,"duration_ms":1000}
>rig.voice {"operation":"status"}
>rig.stop
```

Replies start with `@rig `. Body commands still require a Muse connection, valid calibration and fresh servo feedback; the console is not a bypass.

`setup.status`, `setup.sdk_token`, `setup.proxy`, `setup.clear` and `setup.restart` are **USB-only**, never registered to Muse. Use `tools/provision_rig.py` instead of pasting a token into terminal history. Saved settings take effect after restart. Device-level USB settings survive the normal Muse/Wi-Fi setup reset; clear them separately before transferring a device. The community NVS profile is unencrypted.

## Expressions and new boards

`CONFIG_RIG_PARTICLE_FACE` selects the pure-C particle engine; failed allocation falls back to flat eyes. Vortex, burst and dust reassembly styles are selectable. The face benchmark option is debug-only and disabled by default. Sound effects are synthesized, without TTS.

See [board architecture](main/boards/README.md) for another backend. Arm needs its own UART protocol, endianness, joint limits, calibration interpretation and IK; [the Arm placeholder](main/boards/rig_arm/README.md) is not firmware support.

## Build and validation

Use ESP-IDF 6.0.1, `tools/board.sh rig-puppy build` and `tools/rig_host_tests.sh`. Configure local settings in `build-rig-puppy/sdkconfig`. Run `python3 tools/prepare_flash.py` to prepare your own build for the calibration-checking installer. See [the source build guide](../docs/FLASHING.md) and [development guide](../docs/DEVELOPMENT.md). This repository distributes source only.

Body actions, stance, light control and camera use have prior Puppy field checks. Local USB provisioning and proxy default/failure paths have host tests. Record each firmware/board combination separately; a passing host test cannot replace on-device setup, service registration or observed motion.
