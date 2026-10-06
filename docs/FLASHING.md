# Installing the public Puppy firmware / 公开固件烧录

This package targets **RIG-Puppy only: ESP32-S3, 16 MB flash, octal PSRAM**. Identify the actual board and USB UART port before writing. It is not a generic ESP32 or RIG-Arm image.

此固件仅适用于上述 Puppy 硬件。USB 转串口端口不等于板型身份，烧录前确认实际连接的是 Puppy。

## Prepare

Download the versioned ZIP from [Releases](https://github.com/LuwuDynamics/RIG-Muse/releases), extract it and enter its directory. Install Python 3 and the flashing dependencies. A virtual environment is recommended if your system Python disallows package installation:

```sh
python3 -m venv .venv
# macOS / Linux:
. .venv/bin/activate
# Windows PowerShell instead: .venv\Scripts\Activate.ps1
python -m pip install 'esptool>=5,<6' pyserial
```

Close other serial monitors before opening the port. On macOS use `/dev/cu.usbmodem...` or the USB UART's actual `/dev/cu.usbserial...` port; on Linux use `/dev/ttyACM...` or `/dev/ttyUSB...`; on Windows use `COM...`.

For a first installation, save a full original flash backup if you need a way back to the vendor firmware:

```sh
python -m esptool --chip esp32s3 --port YOUR_PUPPY_PORT --baud 230400 read-flash 0 0x1000000 puppy-original-private.bin
```

That backup contains device data and possibly credentials: keep it private, outside this repository. 首次安装会替换原固件和分区表；完整备份仅供本地恢复，不要上传 GitHub。

## First installation

```sh
python flash.py --port YOUR_PUPPY_PORT
```

The installer checks package hashes and flash size, reads the original calibration for comparison, writes only the five listed images and verifies that the calibration sector did not change. It never invokes `erase-flash`, writes eFuses, copies another Puppy's calibration or initiates a body action.

| Address | Image | Purpose |
|---|---|---|
| `0x00000` | `bootloader.bin` | ESP-IDF bootloader |
| `0x10000` | `partition-table.bin` | RIG-Muse partition layout |
| `0x17000` | `ota_data_initial.bin` | Initial app selection |
| `0x19000` | `phy_init_data.bin` | PHY data |
| `0x20000` | `muse-gadget.bin` | Puppy application |

`0x11000..0x16FFF` is the RIG-Muse NVS region; `0xFFF000..0xFFFFFF` is the read-only original calibration sector. Neither is included in the package. The layout differs from RIG-Omni; do not install this as an Omni OTA update.

Separate files intentionally avoid the padded gaps in a merged full-flash image, which could erase local configuration. Calibration validity is checked again by the firmware; an uncalibrated Puppy cannot perform body actions. 本固件不提供自动重新标定，请用原厂流程建立有效标定。

If serial transfer fails, retry at lower speed:

```sh
python flash.py --port YOUR_PUPPY_PORT --baud 115200
```

If the board will not enter its ROM bootloader, hold BOOT, tap RESET, then release BOOT and retry. Do not disconnect during writing. Interrupted writes can be retried using the same five-image installer.

## Existing RIG-Muse installation

For an application-only update, the tool first checks that the installed partition table matches this release:

```sh
python flash.py --port YOUR_PUPPY_PORT --update
```

This mode writes the app at `0x20000` and keeps local pairing, Wi-Fi and USB-provisioned settings. OTA is disabled in this developer preview; the app normally boots from `ota_0`.

## Provision and pair

Public firmware has **no embedded SDK token, Wi-Fi password or proxy endpoint**. Configure your own token before Muse pairing:

```sh
python provision_rig.py --port YOUR_PUPPY_PORT --sdk-token
# Optional HTTP CONNECT proxy:
python provision_rig.py --port YOUR_PUPPY_PORT --proxy YOUR_LAN_PROXY_HOST:7897
# Disable proxy:
python provision_rig.py --port YOUR_PUPPY_PORT --direct
# Inspect active settings without exposing the token:
python provision_rig.py --port YOUR_PUPPY_PORT
```

The tool restarts after acknowledged changes. In the Muse app, open Settings → Devices, enable Developer mode, add `MuseGadget-RIG-XXXXXX`, configure 2.4 GHz Wi-Fi and press BOOT when requested. Token acquisition and use are subject to the [SDK Terms](https://gadgets.muse.ai/sdk-terms).

Before giving the Puppy to somebody else, unpair/reset Muse/Wi-Fi using the normal device setup reset and separately remove the saved USB settings:

```sh
python provision_rig.py --port YOUR_PUPPY_PORT --clear
```

USB settings are device-level NVS values; BOOT's setup reset preserves them. NVS is not encrypted in this community profile. Public packaging contains no NVS data, but a physical device or full flash backup can contain local secrets. SDK tokens compiled into personal builds cannot be removed with a runtime command; install the credential-free public image instead.

发布固件与个人调试固件分开构建。公开版默认直连；配置代理后，电脑需保持代理运行且局域网可达。保存的代理失效不会自动绕过代理。
