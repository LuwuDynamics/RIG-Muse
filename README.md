# RIG-Muse

**Give Muse a robot body.** Native ESP32 firmware for the RIG-Puppy robot dog.

[中文说明](README.zh-CN.md) · [Build from source](docs/FLASHING.md) · [Development guide](docs/DEVELOPMENT.md) · [Technical reference](esp32/README_RIG.md)

<p align="center">
  <a href="docs/media/puppy-greeting.mp4"><img src="docs/media/puppy-greeting.gif" width="300" alt="Puppy raises a front paw, waves and returns to standing"></a>
  <br><em>Muse-triggered greeting on a real Puppy. <a href="docs/media/puppy-greeting.mp4">Watch the MP4</a>.</em>
</p>

RIG-Muse connects Muse to a small physical companion that can greet you, move, show expressions, react to handling and share its camera view. It runs directly on the Puppy's ESP32-S3, using Meta's open-source [Muse Gadget SDK](https://github.com/facebookincubator/muse-gadget-sdk), without a separate Linux gateway.

This independent project is maintained by **LuwuDynamics**. It is not an official Meta robot product. The current implementation is **experimental and targets RIG-Puppy**. This repository distributes source code; users configure and compile their own builds.

## Our goal

Explore how an AI companion becomes more engaging when it has a body. A conversation can lead to a friendly gesture; a shake can become a surprised expression; a photo can bring the robot's surroundings into the conversation. We care about short, expressive interactions that feel responsive and make people smile.

We also want a reusable foundation: Muse handles conversation and tool selection, firmware exposes explicit device capabilities, and each robot backend owns its protocol, calibration and motion. Puppy is the first implementation. **RIG-Arm is planned, not supported today.**

## Capabilities

| Capability | Current implementation |
|---|---|
| Body expression | 16 original Puppy actions; aliases, reset and media-only expressions bring the catalog to 24 entries |
| Movement | Forward, backward, left turn and right turn with bounded speed and duration |
| Face and sound | Animated eyes, particle expressions and short synthesized effects; no TTS |
| Camera | On-demand 240 × 240 JPEG snapshots through `camera.capture` |
| IMU interaction | Idle expression/sound reactions to shaking, tilt and pickup-like acceleration |
| Battery | Servo-reported voltage and an approximate percentage |
| Light accessory | Timed on/off and mode switching for the light-sword accessory |
| Voice notes | Double-click BOOT to record, double-click again to send; replies appear in Muse |
| Feedback | Job IDs, completion/failure/cancellation, fresh joint feedback and stop control |

`rig.catalog` lists **24 preset entries, four movement directions and 12 board tools**; the SDK adds `device.health`. Preset count is not total capability count. Left/right means turning, not sideways walking. Motion completion does not measure distance traveled.

## Quick start

You need a **RIG-Puppy** with ESP32-S3, 16 MB flash, octal PSRAM and valid existing servo calibration; a USB data cable; a Muse account/app with community device support; your own [SDK token](https://gadgets.muse.ai/settings/sdk-tokens); and a 2.4 GHz Wi-Fi network that lets Puppy reach Muse. Review the [Gadget SDK Terms](https://gadgets.muse.ai/sdk-terms).

### 1. Clone and build

Install **ESP-IDF v6.0.1**, then activate that environment:

```sh
git clone https://github.com/LuwuDynamics/RIG-Muse.git
cd RIG-Muse/esp32
. "$IDF_PATH/export.sh"
python3 -m pip install 'esptool>=5,<6' pyserial
tools/board.sh rig-puppy build
```

The Puppy profile creates `build-rig-puppy/sdkconfig`. No token or personal network settings are committed. The default proxy host is empty, so a new device uses direct access.

### 2. Configure, rebuild and flash your own build

Edit your local configuration with:

```sh
idf.py -B build-rig-puppy \
  -DSDKCONFIG=build-rig-puppy/sdkconfig menuconfig
```

Under **ESP32 Device SDK**, set `CONFIG_GADGET_SDK_TOKEN` to your own token. Configure `CONFIG_RIG_HTTP_PROXY_HOST` and `CONFIG_RIG_HTTP_PROXY_PORT` only if your network needs a LAN proxy; otherwise leave the host empty. These settings can also be edited directly in the ignored `build-rig-puppy/sdkconfig`. Keep real values out of shared source files and commits.

```sh
tools/board.sh rig-puppy build
tools/rig_host_tests.sh
python3 tools/prepare_flash.py
python3 build-rig-puppy/flash-rig/flash.py --port YOUR_PUPPY_PORT
```

Select the actual Puppy USB UART port: `/dev/cu.usbmodem...` or `/dev/cu.usbserial...` on macOS, `/dev/ttyACM0` or `/dev/ttyUSB0` on Linux, or `COM5` on Windows. Console baud is 115200; flashing defaults to 230400.

First installation replaces the original firmware and partition layout. The local installer compares original calibration at `0xFFF000` before and after flashing and avoids NVS writes. Keep an original device backup privately if restoration is needed. Never install Puppy code on Arm. See the [complete source build guide](docs/FLASHING.md).

You may leave the compiled token empty and enter it after flashing through a hidden local USB prompt:

```sh
python3 tools/provision_rig.py --port YOUR_PUPPY_PORT --sdk-token
```

Personal binaries may contain credentials; keep generated files private. This project does not provide prebuilt binary downloads or GitHub Releases.

### 3. Pair in Muse

Open **Settings → Devices → Developer mode** in Muse, add `MuseGadget-RIG-XXXXXX`, follow Wi-Fi setup and press BOOT when physical confirmation is requested. The suffix comes from this Puppy's MAC address.

Once connected, ask Muse to read `rig.catalog` and `rig.status`. Put Puppy on a clear, level surface and try a greeting. After updating firmware, reconnect the device and start a fresh conversation if Muse still shows an old tool catalog.

### 4. Try an interaction

- “Let Puppy wave hello.” → `rig.perform` with `{"name":"greet"}`.
- “Take a photo through Puppy's camera.” → `camera.capture`.
- “Walk forward briefly.” → `rig.move` with `{"direction":"forward","speed":30,"duration_ms":500}`.
- “What's Puppy's battery voltage?” → `rig.battery`.
- Double-click BOOT while idle to record a voice note, then double-click to send. Single-click cancels recording; button presses during motion prioritize stopping.

Voice sending requires a working Muse connection. The recording, upload and feedback state machine is implemented; end-to-end voice-note behavior still needs broader field validation across networks/accounts.

## Optional LAN proxy

**The source defaults use direct access. A LAN proxy is optional.** Set your own host/port in the local build configuration before compiling, or use the USB override below after flashing. No maintainer network address is configured.

```sh
python3 tools/provision_rig.py --port YOUR_PUPPY_PORT --proxy YOUR_LAN_PROXY_HOST:7897
# Disable the proxy:
python3 tools/provision_rig.py --port YOUR_PUPPY_PORT --direct
```

For a proxy on a Mac/PC, keep the computer awake and the proxy running. Enable LAN access, bind the HTTP/mixed listener to a LAN-accessible interface and permit its port in the firewall. Puppy must be able to reach that host; using the same Wi-Fi/LAN is simplest. `127.0.0.1` refers to Puppy itself.

Saved USB settings override compiled settings and survive reflashing. Use `--direct` to force direct access or `--clear` to remove the USB override and restore compiled defaults after reboot.

HTTP CONNECT carries Muse API and synchronous TLS/WebSocket traffic, while retaining end-to-end hostname/certificate verification. SOCKS5, proxy authentication, UDP tunneling and a device-wide VPN are not implemented. An enabled proxy never silently falls back to direct access on failure. A phone VPN does not automatically carry Puppy traffic.

## Develop with an AI coding assistant

We recommend **Codex, Claude Code, or another repository-aware coding assistant** for environment setup, code changes and verification. The repository includes maintained project context:

- [AGENTS.md](AGENTS.md): architecture, build commands, validation and device-handling rules for coding agents.
- [CLAUDE.md](CLAUDE.md): Claude Code entry point, referencing the same project instructions.
- [Development guide](docs/DEVELOPMENT.md): configuration locations, capability extension points and example task briefs.
- [ESP32 SDK instructions](esp32/AGENTS.md): detailed upstream SDK workflows.

Open the repository in your assistant and ask:

> Read AGENTS.md and docs/FLASHING.md. Configure the ESP-IDF 6.0.1 environment, build RIG-Puppy from source, and show me how to edit my local token and optional proxy settings without exposing them. Report build and test results before flashing.

For code changes, ask the assistant to inspect the relevant backend, implement the behavior, run the appropriate checks and describe what still needs hardware validation. Credentials should be entered locally, never pasted into an assistant conversation.

| Path | Purpose |
|---|---|
| `esp32/main/boards/rig_puppy/` | Puppy hardware, protocol, actions, sensors and media |
| `esp32/main/rig_setup.*` | Local USB provisioning and boot-time settings |
| `esp32/main/rig_show.*`, `rig_pface.*` | Expression/sound synthesis and particle face engine |
| `esp32/devices/sdkconfig.rig-puppy` | Shared board defaults; personal settings belong in local generated config |
| `esp32/main/Kconfig.projbuild` | Available configuration options |
| `esp32/tests/` | SDK and RIG host tests |
| `docs/` | Source build instructions, development, provenance and demo media |
| `README.upstream.md`, `linux/`, `skills/` | Preserved upstream SDK documentation/functionality |

The firmware starts without an automatic motion routine, reads calibration without rewriting it, executes one body action at a time and stops on cancellation or stale feedback. Torque is telemetry, not software overload protection. Host tests and local catalog checks do not prove cloud registration or successful physical behavior; use the [technical reference](esp32/README_RIG.md) and validate on the intended board.

Planned work: richer interaction sequences, broader voice-note field testing, reproducible board validation and a separate RIG-Arm backend. Contributions should include the board, firmware version, reproduction steps and redacted logs. See [CONTRIBUTING.md](CONTRIBUTING.md) and [SECURITY.md](SECURITY.md).

## Credits and license

Based on [facebookincubator/muse-gadget-sdk](https://github.com/facebookincubator/muse-gadget-sdk), baseline `693cde9a884ad1edc87251b9f8944815f8de4809`. Puppy motion formulas and hardware definitions are adapted from Luwu's RIG-Omni implementation. RIG integration, expressions, tests and documentation are maintained by LuwuDynamics.

We retain the upstream [Apache-2.0 license](LICENSE) and copyright notices. `minimp3` retains CC0-1.0; `pixel_font.c` retains BSD-2-Clause. The upstream Jollybot avatar is outside Apache-2.0. See [NOTICE](NOTICE), [THIRD_PARTY.md](docs/THIRD_PARTY.md) and [the upstream README](README.upstream.md). Muse service access and SDK tokens are governed separately by the Gadget SDK Terms.
