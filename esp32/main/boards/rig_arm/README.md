# RIG-Arm 接入位置（尚未实现，不可烧录）

这里为后续 Arm 后端保留入口；当前没有 Arm 构建选项或可用固件，
不会把 Puppy 固件伪装成 Arm。接入步骤见 [板型后端](../README.md)。

已核对本地 RIG-Omni 的差异：

| 项目 | Puppy | Arm 参考实现 |
|---|---|---|
| UART TX/RX | GPIO3 / GPIO38 | GPIO46 / GPIO38 |
| 同步位置寄存器 | `0x35` | `0x2A` |
| 位置字节序 | 小端 | 大端 |
| 位置范围 | 本版保守 `200..2800` | 原版发送前限制 `1..1023` |
| 标定有效零点 | `200..2800` | `100..900` |
| 运动表达 | 四腿加腰部 | 五关节与 IK、末端姿态 |

来源：`RIG-Omni/main/boards/arm/{board_config.h,xgo.cc,xgo.h,arm_board.cc}`。
原版 Arm 的 `board_config.h` 有陈旧 Puppy 注释，不能按注释推断硬件。
同一 `0xFFF000` 地址不代表标定数据语义通用，绝不能跨板照搬零点。

实现时新增：`arm_robot.c`（导出 rig_selected_board）、`arm_protocol.c`、
`arm_kinematics.*`、`board_config.h`、`board.cmake`，并为夹爪/末端坐标等注册
板型自己的命令。Muse 配对、网络和公共命令分发保持共用。
示教录制/回放需要独立状态和持久化设计，不能复用 Puppy 的坐姿状态。
