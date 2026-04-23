# UDP 运动数据模拟器 (`udp_motion_simulator.py`)

向 TracerTracker 发送符合客观运动规律的 IMU 测试数据，用于演示
INS（惯性导航）、AHRS（姿态解算）、ZUPT（零速检测）与气压通道效果。

## 工作原理

- TracerTracker 的 UDP 接收器只支持 **CSV 文本** 协议
  （二进制 ATK-MS901M 仅串口启用）。
- 默认 `config.json` 的所有 `points` 不带前缀，因此本脚本发送 **不带前缀** 的 CSV 行。
- 每行 19 列，严格按 [`protocols/atkms901m.json`](../protocols/atkms901m.json)
  的 `snapshot_order` 字段顺序：

  ```
  ax, ay, az, gx, gy, gz, q0, q1, q2, q3,
  mx, my, mz, temp, roll, pitch, yaw, pressure, altitude
  ```

- 数据严格按运动学规律生成：
  - 世界系生成 `pos / vel / acc_world / euler / euler_dot`
  - 加速度叠加重力后旋转回 body 系，对应传感器物理输出
  - 陀螺由欧拉角速率经 ZYX 映射换算到 body 系
  - 气压根据海拔用标准大气公式实时反算

- **传感器噪声**（默认开启）：静止段加微小底噪、运动段叠加显著抖动。
  这是为避免合成数据过于平滑触发 ZUPT 误检（ZUPT 用 `|acc|²` 与 `|gyro|`
  在窗口内的方差判断静止）。可用 `--no-noise` 关闭做对比。

## 前置要求

- Python 3.7+（仅用标准库，无外部依赖）
- TracerTracker 已启动且 `config.json` 中 `udp.enabled=true`、`udp.port=8888`
  （这是默认值，无需改动）

## 快速使用

```bash
# 默认 demo 模式：依次串联静止 → 直线加减速 → 圆周 → 8 字 → 电梯升降 → 姿态摆动
python tools/udp_motion_simulator.py

# 单一场景循环播放
python tools/udp_motion_simulator.py --scenario circle --loop
python tools/udp_motion_simulator.py --scenario figure8 --loop
python tools/udp_motion_simulator.py --scenario elevator

# 自定义目标与频率
python tools/udp_motion_simulator.py --ip 127.0.0.1 --port 8888 --rate 200

# 静默模式（不打印每秒进度）
python tools/udp_motion_simulator.py --quiet
```

按 `Ctrl+C` 随时停止。

## 场景说明

| 场景 | 时长 | 说明 |
|------|------|------|
| `static` | 5–10s | 完全静止，触发 PoseProcessor 首帧姿态初始化与 ZUPT |
| `accel` | 9s | +X 匀加速 3s → 匀速 3s → 减速到 0，验证位移积分 |
| `circle` | 10–20s | 半径 2m、角速度 0.6 rad/s 水平圆周，yaw 跟随切线 |
| `figure8` | 12–24s | Lissajous 8 字，验证速度方向变化与 yaw 估计 |
| `elevator` | 8s | 平滑上升 2m 再下降，验证气压卡尔曼垂直通道 |
| `tilt` | 5–10s | 原地静止 + roll/pitch 缓慢摆动，验证姿态 3D 显示 |
| `demo` | ~60s | 上述所有场景按时间轴无缝串联（默认） |

## 命令行参数

| 参数 | 默认 | 说明 |
|------|------|------|
| `--ip` | `127.0.0.1` | 目标主机 IP |
| `--port` | `8888` | 目标 UDP 端口 |
| `--rate` | `100` | 发送频率 Hz（1–1000） |
| `--scenario` | `demo` | `demo / static / accel / circle / figure8 / elevator / tilt` |
| `--loop` | off | 循环播放，至 `Ctrl+C` 退出 |
| `--quiet` | off | 不打印每秒进度行 |
| `--no-noise` | off | 关闭传感器噪声（运动场景会被 ZUPT 误判为静止，仅调试用） |
| `--seed N` | – | 噪声随机种子（便于复现） |

## 验证

启动 TracerTracker → 在另一个终端运行模拟器，可观察：

- **3D 视图**：位移路径与场景轨迹一致（圆/8 字/直线/升降）
- **姿态指示**：四元数与欧拉角同步变化，`tilt` 场景下姿态明显摆动
- **传感器图表**：加速度、陀螺、磁力曲线随场景切换平滑过渡
- **垂直通道**：`elevator` 场景下高度估计跟随 ±2m 起伏

如果 3D 视图无轨迹但能看到原始数据，请确认 `config.json` 的 `points`
中 `accelerometer / gyroscope` 项使用 `field` 引用（`ax/gx` 等）而非
固定 `index`，且 `serial.protocol = "atkms901m"`（默认即满足）。
