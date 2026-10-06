# Build, configure and flash from source

[English overview](../README.md) · [中文说明](../README.zh-CN.md) · [Development guide](DEVELOPMENT.md)

RIG-Muse is distributed as source code. Clone the repository, configure it for your own Puppy and network, and compile locally. No prebuilt binaries or GitHub Releases are provided.

## 1. Development environment

Use **ESP-IDF v6.0.1**, Python 3 and Git. Follow Espressif's [official ESP-IDF installation guide](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32s3/get-started/index.html), then activate that installation:

```sh
git clone https://github.com/LuwuDynamics/RIG-Muse.git
cd RIG-Muse/esp32
. "$IDF_PATH/export.sh"
python3 -m pip install 'esptool>=5,<6' pyserial
idf.py --version
tools/board.sh rig-puppy build
```

If `IDF_PATH` is not set yet, source the `export.sh` in your ESP-IDF v6.0.1 installation instead. Verify the version; RIG-Omni's ESP-IDF 5.5 environment is not compatible with this SDK. On Windows, use the ESP-IDF terminal and run the shell helpers in an IDF-enabled Bash environment, or use the equivalent `idf.py` command below.

The initial build generates `build-rig-puppy/sdkconfig` and downloads managed components. A token is not required to compile; it is required before Muse pairing.

Equivalent manual build, with the same board, directory and configuration:

```sh
idf.py -B build-rig-puppy -DIDF_TARGET=esp32s3 \
  -DSDKCONFIG=build-rig-puppy/sdkconfig \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;devices/sdkconfig.rig-puppy" build
```

## 2. Your local configuration

Open the existing Puppy's build configuration:

```sh
idf.py -B build-rig-puppy \
  -DSDKCONFIG=build-rig-puppy/sdkconfig menuconfig
```

Under **ESP32 Device SDK**, configure the settings below. Alternatively, edit the corresponding entries in `build-rig-puppy/sdkconfig` locally.

| Setting | Purpose |
|---|---|
| `CONFIG_GADGET_SDK_TOKEN` | Your own Muse SDK token; empty if you will provision it over USB |
| `CONFIG_RIG_HTTP_PROXY` | Include optional HTTP CONNECT proxy support; enabled in the Puppy profile |
| `CONFIG_RIG_HTTP_PROXY_HOST` | Your reachable LAN proxy host; empty means direct connection when no saved USB override exists |
| `CONFIG_RIG_HTTP_PROXY_PORT` | Your HTTP/mixed proxy listener port; default 7897 |
| `CONFIG_HOMEHUB_WIFI_SSID`, `CONFIG_HOMEHUB_WIFI_PASSWORD` | Optional development overrides; normally keep empty and provision Wi-Fi in Muse |
| `CONFIG_RIG_PARTICLE_FACE`, `CONFIG_RIG_PARTICLE_FACE_STYLE` | Particle expression engine and reassembly style |

Configuration syntax for a **local file**, using placeholders:

```ini
CONFIG_GADGET_SDK_TOKEN="YOUR_OWN_SDK_TOKEN"
CONFIG_RIG_HTTP_PROXY=y
CONFIG_RIG_HTTP_PROXY_HOST="YOUR_LAN_PROXY_HOST"
CONFIG_RIG_HTTP_PROXY_PORT=7897
```

For a direct connection, leave the proxy host empty. Do not copy placeholder values literally. Obtain your token from [Muse SDK tokens](https://gadgets.muse.ai/settings/sdk-tokens); review the [SDK Terms](https://gadgets.muse.ai/sdk-terms).

**Do not put real credentials into tracked overlays, chat, issues or commits.** Generated `sdkconfig` and build files are ignored by Git. Tokens configured at compile time are embedded in your personal binary; keep that binary private. Blank token builds can instead use the hidden USB prompt after flashing.

`devices/sdkconfig.rig-puppy` holds shared hardware defaults; `main/Kconfig.projbuild` defines available settings. Once generated, `build-rig-puppy/sdkconfig` takes precedence over changes to the defaults. Use `menuconfig` for an existing build. To intentionally apply updated defaults, preserve your private settings outside the repository, remove only the generated configuration and reconfigure.

After any configuration or code changes, rebuild:

```sh
tools/board.sh rig-puppy build
tools/rig_host_tests.sh
python3 tools/prepare_flash.py
```

The last command copies your own compiled images into **`build-rig-puppy/flash-rig/`**, with a manifest and a local installer. It does not download firmware, produce a distribution ZIP or publish anything. Do not upload this directory.

## 3. Identify and flash your Puppy

Supported hardware: **RIG-Puppy, ESP32-S3, 16 MB flash, octal PSRAM and valid original servo calibration**. RIG-Arm is not supported. GPIO19/20 are used by the display; use the Puppy's USB UART bridge. Close other serial monitors first.

Port examples: macOS `/dev/cu.usbmodem...` or `/dev/cu.usbserial...`; Linux `/dev/ttyACM...` or `/dev/ttyUSB...`; Windows `COM...`. Identify the attached device rather than selecting the first matching port. Chip/flash identification alone cannot distinguish every robot variant.

First installation replaces the original firmware and partition layout. If restoration is needed, keep an original full flash backup **privately**, outside this repository:

```sh
python3 -m esptool --chip esp32s3 --port YOUR_PUPPY_PORT --baud 230400 \
  read-flash 0 0x1000000 puppy-original-private.bin
```

Install the images you just compiled:

```sh
python3 build-rig-puppy/flash-rig/flash.py --port YOUR_PUPPY_PORT
```

The installer validates hashes, sizes and the allowed layout, then compares the original calibration sector before and after writing. It writes five separate images:

| Address | Image |
|---|---|
| `0x00000` | `bootloader.bin` |
| `0x10000` | `partition-table.bin` |
| `0x17000` | `ota_data_initial.bin` |
| `0x19000` | `phy_init_data.bin` |
| `0x20000` | `muse-gadget.bin` |

It avoids NVS at `0x11000..0x16FFF` and original calibration at `0xFFF000..0xFFFFFF`. It does not erase the whole flash, write eFuses, copy another Puppy's calibration or initiate motion. Calibration is read-only; use the original manufacturer process if it is invalid. Do not merge these images into a full-flash image with padded gaps. This layout differs from RIG-Omni and is not an Omni OTA update.

For an existing RIG-Muse installation, an application-only update checks the installed partition table before writing:

```sh
python3 build-rig-puppy/flash-rig/flash.py --port YOUR_PUPPY_PORT --update
```

Use `--baud 115200` if transfer fails. If necessary, hold BOOT, tap RESET and release BOOT to enter the bootloader. Interrupted writes can be retried. OTA is disabled in the Puppy profile.

## 4. Provision locally and pair in Muse

If you left the build-time token empty, enter it through the hidden local USB prompt:

```sh
python3 tools/provision_rig.py --port YOUR_PUPPY_PORT --sdk-token
# Optional per-device proxy override:
python3 tools/provision_rig.py --port YOUR_PUPPY_PORT --proxy YOUR_LAN_PROXY_HOST:7897
# Explicitly override any build-time proxy with direct access:
python3 tools/provision_rig.py --port YOUR_PUPPY_PORT --direct
# Inspect settings without returning the token:
python3 tools/provision_rig.py --port YOUR_PUPPY_PORT
```

Saved USB token/proxy settings take precedence over build-time settings and apply after restart. Reflashing preserves NVS, so it also preserves those overrides. `--clear` removes USB settings; after restart the compiled defaults apply again. Normal BOOT setup reset clears Muse/Wi-Fi but preserves USB settings. NVS is unencrypted. To transfer a device, clear its local settings and reinstall your own source build with the compile-time credentials empty.

In Muse, open **Settings → Devices → Developer mode**, add `MuseGadget-RIG-XXXXXX`, configure 2.4 GHz Wi-Fi and press BOOT when requested. Check `rig.catalog` and `rig.status` before testing actions on a clear, level surface. Registration, completion feedback and observed physical behavior are separate checks.

A LAN proxy must accept connections from Puppy. Enable LAN access in the proxy application, keep the host awake and use its LAN address. `127.0.0.1` refers to Puppy itself. HTTP CONNECT preserves target TLS validation; SOCKS5, proxy authentication and UDP are not supported. An enabled proxy does not fall back to direct access on failure.

中文：本项目仅提供源码。先安装 ESP-IDF 6.0.1，用 Puppy 板型编译；在本地 `build-rig-puppy/sdkconfig` 修改 token、代理与表情配置，重新编译，再用本地生成的工具烧录。详见[中文 README](../README.zh-CN.md)与[代码修改指南](DEVELOPMENT.md)。
