# RIG-Muse

**给 Muse 一个机器狗身体。** 基于开源 Muse Gadget SDK，原生运行在 RIG-Puppy 的 ESP32-S3 上。

[English](README.md) · [下载固件](https://github.com/LuwuDynamics/RIG-Muse/releases) · [烧录说明](docs/FLASHING.md) · [技术文档](esp32/README_RIG.md)

<p align="center"><a href="docs/media/puppy-greeting.mp4"><img src="docs/media/puppy-greeting.gif" width="300" alt="Puppy 抬起前爪招手，然后回到站姿"></a><br><em>Muse 控制真机 Puppy 打招呼。点击查看 MP4。</em></p>

## 我们的目标

让 AI 伙伴从对话走进现实：用抬起的前爪打招呼，用表情和短音效回应互动，用摄像头分享眼前的世界。我们关注自然语言、感知与身体表达之间的连接，以及能让人开心、有回应的小互动。

Muse 负责对话和工具选择，固件负责具体硬件执行与状态反馈，不需要额外 Linux 网关。板型后端彼此独立，方便以后接入不同机器人；当前支持 Puppy，**RIG-Arm 仍待实现**。

这是 LuwuDynamics 维护的独立实验项目，基于 Meta 开源 SDK，不是 Meta 官方机器狗产品。当前发布为开发者预览。

## 当前能力

| 能力 | 实现 |
|---|---|
| 动作 | 原版 16 个动作；含别名、复位及纯媒体表演，共 24 个预设条目 |
| 移动 | 前进、后退、左转、右转，有限时长与速度 |
| 表情/声音 | 动态眼睛、粒子表情、短合成音效；不播放 TTS |
| 摄像头 | 按需拍摄 240×240 JPEG，接口为 `camera.capture` |
| IMU | 空闲时对摇晃、倾斜、拿起特征用表情和音效回应 |
| 电池 | 舵机回传电压及估算电量 |
| 光剑 | 定时开关及切换模式 |
| 语音留言 | 双击 BOOT 录音，再双击发送，回复在 Muse App 查看 |
| 状态反馈 | 任务 ID、完成/失败/取消、关节反馈和停止接口 |

`rig.catalog` 返回 24 个预设、4 个移动方向、12 个板型工具；SDK 另有 `device.health`。**24 是预设数量，不是全部能力数量。** 左右指原地转向，不是横移。录音、上传和反馈流程已实现，跨网络、跨账号的语音发送仍需更多实机验证。

## 开始使用

准备 RIG-Puppy（ESP32-S3、16 MB Flash、octal PSRAM、有效原舵机标定）、USB 数据线、Muse App/账号、自己的 [SDK token](https://gadgets.muse.ai/settings/sdk-tokens)，以及能访问 Muse 的 2.4 GHz Wi-Fi。先阅读 [SDK 服务条款](https://gadgets.muse.ai/sdk-terms)。

1. 从 [Releases](https://github.com/LuwuDynamics/RIG-Muse/releases) 下载、解压 `rig-muse-puppy-<版本>.zip`。
2. 在解压目录运行下面命令，将端口替换成当前 Puppy 的 USB UART 端口：

   ```sh
   python3 -m pip install 'esptool>=5,<6' pyserial
   python3 flash.py --port YOUR_PUPPY_PORT
   ```

   首次安装会替换原固件及分区表。若需恢复原固件，事先保存完整 Flash 备份。工具不写入 `0xFFF000` 标定扇区，并在烧录后核对；不会重新标定或执行动作。仅适用于 Puppy，不能烧到 Arm。

3. 通过 USB 配置自己的 SDK token。输入不回显，不进入命令行参数或 Shell 历史：

   ```sh
   python3 provision_rig.py --port YOUR_PUPPY_PORT --sdk-token
   ```

4. 在 Muse App 打开 **Settings → Devices → Developer mode**，添加 `MuseGadget-RIG-XXXXXX`，配上 Wi-Fi，并在提示时按 BOOT 确认。
5. 等待连接，让 Muse 查看 `rig.catalog` 和 `rig.status`。小狗放在平整、空旷的地面上，再让它打招呼或短步移动。

端口示例：macOS `/dev/cu.usbmodem...`，Linux `/dev/ttyACM0` 或 `/dev/ttyUSB0`，Windows `COM5`。控制台 115200 baud，烧录默认 230400 baud。详见 [烧录说明](docs/FLASHING.md)。

## 代理如何配置

**公开固件默认直连，不带作者的代理地址、SDK token 或 Wi-Fi 密码。** 固件包含可选代理支持，需要时通过 USB 设置：

```sh
python3 provision_rig.py --port YOUR_PUPPY_PORT --proxy YOUR_LAN_PROXY_HOST:7897
# 关闭代理，恢复直连：
python3 provision_rig.py --port YOUR_PUPPY_PORT --direct
```

若代理运行在 Mac/PC 上，需保持电脑运行，开启代理软件的局域网访问，使用局域网可访问的监听地址，并允许防火墙端口。小狗必须能访问这台电脑，通常连接同一局域网。`127.0.0.1` 指的是小狗自己。

采用 HTTP CONNECT，支持 Muse API 和同步 TLS/WebSocket，目标主机的 TLS 证书仍正常校验。不支持 SOCKS5、代理认证、UDP 或全设备 VPN。代理启用后连接失败，不会自动绕过代理直连。手机有 VPN，不代表小狗的流量会自动走手机 VPN。

## 可以怎么玩

- “小狗，跟大家打个招呼。”：招手、表情和音效。
- “用小狗的相机拍一张。”：通过 `camera.capture` 获取机器人视角。
- “往前走一小步，然后停下。”：短时前进，结束回到站姿。
- “现在电压多少？”：读取 `rig.battery`。
- 空闲且已连接时，双击 BOOT 录音，再双击发送。录音时单击取消，运动时按键优先停止。

动作返回 `accepted` 只表示接收任务，应继续检查 `rig.status` 到完成、失败或取消。完成依据关节反馈和时间线，不代表导航目标或移动距离已经达到。

## 从源码构建

使用 **ESP-IDF 6.0.1**：

```sh
git clone https://github.com/LuwuDynamics/RIG-Muse.git
cd RIG-Muse/esp32
. "$IDF_PATH/export.sh"
tools/board.sh rig-puppy build
tools/rig_host_tests.sh
# 构建不含个人配置的发布包：
tools/build_release.sh 0.1.0
```

普通板型构建也支持 USB 配置。个人构建可通过 `menuconfig` 设置编译时 token 和代理，但配置与生成的固件应保留在本地。发布构建使用独立的 `build-rig-puppy-release/`，打包时检查凭据为空，输出到 Git 忽略的 `dist/`；不发布 NVS、完整设备备份、ELF 或个人 `sdkconfig`。

## 后续方向与贡献

更丰富的连续互动、更广泛的语音留言验证、可复现的真机检查流程，以及独立 RIG-Arm 后端。上电不主动执行动作；原标定只读；同一时间只执行一个身体任务。扭矩仅作遥测，不作为软件过载保护。贡献方式见 [CONTRIBUTING.md](CONTRIBUTING.md)，隐私说明见 [SECURITY.md](SECURITY.md)。

## 来源与许可

基于 [facebookincubator/muse-gadget-sdk](https://github.com/facebookincubator/muse-gadget-sdk)，基线提交 `693cde9a884ad1edc87251b9f8944815f8de4809`。动作公式与硬件定义参考 Luwu RIG-Omni。保留上游 Apache-2.0 许可及版权说明；第三方文件和 Jollybot 素材保留原许可范围，见 [THIRD_PARTY.md](docs/THIRD_PARTY.md)、[NOTICE](NOTICE) 和 [上游 README](README.upstream.md)。Muse 服务使用和 SDK token 另受 SDK 服务条款约束。
