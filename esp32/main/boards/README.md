# RIG 板型后端

Muse 的 BLE 配对、Wi-Fi、TLS/代理、Noise 加密通道与命令分发仍是公共代码。
`CONFIG_RIG_ENABLED` 打开 RIG 接入，Kconfig 的 `RIG_BOARD_TYPE` 选择一个板型。
当前只有经过真机验证的 `RIG_PUPPY` 可以选择。

```text
app.c / noise_control.cpp / rig_console.c
              ↓
       rig_robot.c 公共入口
              ↓
       rig_board.h 后端接口
              ↓  编译时选择一个
  boards/rig_puppy/       boards/rig_arm/（待实现）
  UART 协议、硬件引脚     独立 UART 协议、IK、关节限制
  标定、动作、状态机      独立标定、工具和状态机
```

公共入口只转发 `init / set_connected / stop / command / register_commands`。
后端导出 `const rig_board_ops_t rig_selected_board`；同一镜像只允许链接一个。
公共层不假定 5 个关节、某个舵机寄存器、Puppy 姿势、标定格式或媒体硬件。
`rig_show.c` 中的表情渲染和音效合成是可复用的纯函数；目录内媒体驱动是板型专属。
`rig_pface.c` 是同样可复用的粒子表情引擎（纯 C，无 ESP-IDF 依赖，主机可测）；`rig_media.c` 用它驱动屏幕，
`CONFIG_RIG_PARTICLE_FACE=n` 即回到原来的平面眼睛。

## 增加新板型

1. 在 `main/boards/<board>/` 实现后端接口，以及自己的协议、引脚、标定读取、
   命令 schema、唯一硬件执行任务和取消逻辑。设备不支持的命令不要注册。
2. 提供 `board.cmake`，设置 `RIG_BOARD_INCLUDE` 并追加后端源文件。
   在 `main/Kconfig.projbuild` 的板型 choice 内新增选项；在 `main/CMakeLists.txt`
   的 RIG 分支选中对应 `board.cmake`。未接后端会在配置阶段报错。
3. 新增 `devices/sdkconfig.<board>`（含 `RIG_ENABLED=y`、唯一板型、板型自己的
   Flash/PSRAM/控制台/分区配置），并在 `tools/board.sh` 增加该板型。
   每个板型使用独立 `build-<board>/sdkconfig`，SDK token 不放入 overlay 或 Git。
4. 加后端协议、动作限位、取消/掉线和资源失败测试，更新设备表；编译后确认
   接入的真实板子身份，先备份再烧录，只在对应硬件上验收。

不复制网络栈，不在 app.c 中逐个添加 ARM/Puppy 动作分支；新增工具在该板型
`register_commands` 中注册，Muse 即可发现。

## RIG-Puppy

- `board_config.h`：UART/LCD/音频/摄像头/光剑引脚及标定扇区地址。
- `rig_motion.c`：Puppy 总线报文和反馈解析；不适用于 Arm。
- `puppy_native.c`：原版 16 个动作的纯轨迹公式，没有原版任务、UART 或全局执行状态。
- `puppy_actions.c`：24 个目录条目、动作别名、标定限位、有限时长的原版连续轨迹计划。
- `puppy_robot.c`：单一 UART 所有者、准备、连续轨迹、取消和完成反馈。
- `rig_media.c`：此板的 GC9A01 与 I2S 驱动，含粒子表情的双缓冲条带传输。
- `puppy_camera.c`：GC0308 注册到公共 camera 所有权接口，原始帧转 JPEG。
- `puppy_laser.c`：GPIO46 光剑及定时关闭，动作锁下调用，不另开 GPIO 写线程。
- `puppy_gait.c`：原版前后步态及左右转向，纯轨迹计算。
- `puppy_imu.c/puppy_imu_model.c`：QMI8658C 采样、事件去抖和侧翻检测。
- `puppy_voice.c`：I2S1 麦克风、有限录音、Muse 上传及确认状态。
- 电池电压读取与解析共用 `puppy_robot.c/rig_motion.c` 的唯一 UART 所有者。

`tools/import_puppy_actions.py <xgo_action.cc>` 可重新导入公式。
独立的 `tools/puppy_reference_vectors.py` 编译原版 C++ 来生成测试基准，
不能用修改后的移植代码生成“期望值”。正常编译/测试不依赖相邻 RIG-Omni 仓库。
