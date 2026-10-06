# Development guide / 代码修改指南

[Overview](../README.md) · [中文说明](../README.zh-CN.md) · [Source build and flash](FLASHING.md) · [Agent instructions](../AGENTS.md)

RIG-Muse separates the Muse transport from the selected robot backend. For Puppy work, start in `esp32/main/boards/rig_puppy/`; preserve the upstream SDK's pairing/network behavior unless the task specifically changes it. Normal builds do not require a sibling RIG-Omni checkout.

## Where to change behavior

All paths below are relative to `esp32/`.

| Goal / 需求 | Source / 修改位置 |
|---|---|
| Personal token, proxy or feature switches / 个人配置 | `build-rig-puppy/sdkconfig` through `menuconfig`; never commit real values |
| Shared hardware defaults / 共享板型默认值 | `devices/sdkconfig.rig-puppy`; option definitions in `main/Kconfig.projbuild` |
| Add or describe a Muse tool / 添加工具 | `main/boards/rig_puppy/puppy_robot.c`: command dispatch, registration and `rig.catalog` |
| Add an expressive preset / 新增互动预设 | `main/rig_show.*` catalog, Puppy `rig_media.*`, and corresponding action logic |
| Change original actions or gait / 动作与移动 | Puppy `puppy_actions.*`, `puppy_native.*`, `puppy_gait.*`; packet generation in `rig_motion.*` |
| Change eyes or sound / 表情音效 | `main/rig_pface.*`, `main/rig_show.*`, Puppy `rig_media.*` |
| Camera / 摄像头 | Puppy `puppy_camera.*`, tool `camera.capture` |
| IMU reactions / 惯性互动 | Puppy `puppy_imu.*`, `puppy_imu_model.c`, idle reaction handling in `puppy_robot.c` |
| Battery / 电池 | Servo voltage query in `rig_motion.c`, freshness/estimation in `puppy_robot.c` |
| Voice notes and buttons / 语音与按键 | Puppy `puppy_voice.*`, SDK button integration in `main/app.c` |
| Light accessory / 光剑 | Puppy `puppy_laser.*` |
| Local setup or proxy / 本地配置与代理 | `main/rig_setup.*`, `main/proxy_tls.c`, `tools/provision_rig.py` |
| Add a robot type / 新板型 | `main/boards/README.md`, a separate backend, Kconfig selection and build integration |

Arm is currently a placeholder. Its protocol, endianness, joint limits, IK and calibration interpretation differ from Puppy. See [the backend architecture](../esp32/main/boards/README.md) and [Arm notes](../esp32/main/boards/rig_arm/README.md) before implementing it.

## Capability changes

1. Inspect the existing tool schema and executor before editing.
2. Keep parameters bounded and validate them before enqueueing any physical work. Preserve one motion owner and immediate stop semantics.
3. Update tool registration, dispatch and `rig.catalog` together. If adding a preset, also update its description, timing and catalog entry.
4. Add meaningful checks for protocol, transitions and failure paths; update usage examples.
5. Build the Puppy profile and run host tests. Validate cloud discovery and physical behavior separately, on the intended board.

A motion job returning `accepted` is pending work. `completed` reflects the local timeline and feedback checks, not measured distance or navigation success. Battery percentage is an estimate; IMU pickup-like acceleration is not proof the robot remains held. Do not overstate these signals in tool descriptions.

## Working with Codex or Claude Code

The repository includes standard **`AGENTS.md`** instructions for Codex-compatible agents and **`CLAUDE.md`** for Claude Code. These files provide architecture, configuration, build, validation and hardware context; they do not replace review of code or on-device acceptance.

Start the assistant in the repository root. Recommended task briefs:

> Read AGENTS.md and docs/FLASHING.md. Check the ESP-IDF 6.0.1 environment and build RIG-Puppy. Show me where to edit my local SDK token and proxy settings without reading or printing them. Run the relevant checks and report the results before any hardware operation.

> Read the Puppy media catalog and executor. Add a short expressive interaction using existing motion primitives, animated eyes and sound effects. Keep stop/cancellation behavior intact, update tool discovery if needed, and report which checks are host-only and which require hardware.

中文示例：

> 先读 AGENTS.md 和代码修改指南，检查现有实现。请添加一个短互动，复用现有动作、表情和音效，保持停止与取消逻辑。更新目录和必要测试，编译通过后说明真机验收项目；不要自行烧录或启动动作。

Provide the desired behavior, target board and acceptance conditions. Enter credentials through local tools, not the assistant conversation. For an actual flash or motion test, explicitly include that scope and identify the connected hardware/placement conditions.

## Verification loop

From `esp32/`, with ESP-IDF 6.0.1 activated:

```sh
tools/board.sh rig-puppy build
tools/rig_host_tests.sh
python3 tools/prepare_flash.py
```

From the repository root:

```sh
python3 tools/audit_public.py --history
git diff --check
```

`prepare_flash.py` uses your compiled files to create an ignored local installation directory. It never uploads files. Keep configurations, binaries, logs and backups containing device data private.

For hardware checks, record the commit/configuration, board and firmware boot state. Check the local catalog, registration acknowledgment, service/app discovery and actual tool execution as separate stages. A green host test cannot prove a paw moved or that a voice note reached Muse.
