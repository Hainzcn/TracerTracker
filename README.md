# TracerTracker

TracerTracker 是一个基于 Python 和 PySide6 (Qt) 开发的 3D 轨迹可视化工具。它能够实时接收来自串口或 UDP 的传感器数据，进行物理姿态解算，并在 3D 空间中动态绘制运动轨迹。

> **说明**: 本项目主要由 AI 辅助生成和编写。

## 项目结构

```
TracerTracker/
├── config.json                  # 运行时配置（必须位于项目根目录）
├── requirements.txt             # Python 依赖
├── README.md
├── src/
│   ├── main.py                  # 入口：日志、QApplication 初始化
│   ├── ui/
│   │   ├── main_window.py       # 主窗口：数据流串联、调试控制台、状态栏
│   │   ├── viewer_3d.py         # 3D 视图：坐标系、轨迹渲染、相机交互
│   │   ├── attitude_widget.py   # 右上角姿态叠加层（四元数/欧拉角 + 小立方体）
│   │   └── sensor_info_overlay.py  # 底部传感器叠加层（加速度、速度、气压高度）
│   └── utils/
│       ├── config_loader.py     # 单例配置加载/保存
│       ├── data_receiver.py     # UDP / 串口数据接收（含 ATK-MS901M 二进制协议）
│       ├── ins_math.py          # 惯性导航纯数学：Madgwick 滤波、四元数运算
│       ├── pose_processor.py    # 姿态处理器：重力剥离、积分、信号分发
│       └── atkms901m_resolver.py  # ATK-MS901M 协议帧解析与流式解码
└── tests/
    ├── test_udp_prefix.py       # UDP 前缀路由测试发送器
    └── test_udp_sender.py       # UDP 螺旋数据测试发送器
```

## 数据流架构

```
串口/UDP 原始数据
        │
        ▼
  DataReceiver          ──  接收线程，解析 CSV 或 ATK-MS901M 二进制帧
        │
        │  data_received(source, prefix, [float...])
        ▼
  PoseProcessor         ──  按 config points 提取 ACC/GYR/MAG/QUAT
        │                    调用 ins_math 进行姿态估计
        │                    剥离重力 → 欧拉积分 → 位移
        │
        │  position_updated / velocity_updated / parsed_data_updated
        ▼
  MainWindow
    ├── Viewer3D              全路径 / 速度尾迹渲染
    ├── AttitudeWidget         姿态显示
    └── SensorInfoOverlay      传感器数值显示
```

## 主要功能

- **实时数据接收**
  - 支持 **UDP** 和 **串口** 两种数据源，可同时启用。
  - CSV 文本协议（可带前缀，如 `G:1.0,2.0,3.0`）。
  - ATK-MS901M 二进制协议（自动帧同步与校验）。

- **内置姿态引擎**
  - 6-DOF / 9-DOF **Madgwick 滤波**，或直接使用模块输出的四元数。
  - 自动检测并剥离重力加速度。
  - 线性加速度二次积分输出位移轨迹。

- **3D 渲染与交互**
  - 左键拖动旋转、右键拖动平移、滚轮缩放、中键复位。
  - 全路径模式与速度尾迹模式，尾迹长度可调。
  - 多点独立渲染，颜色/大小可配置。

- **调试界面**
  - 双栏调试控制台：左侧原始数据 / 解析视图、右侧姿态引擎日志。
  - 底部状态栏实时显示连接状态与数据包统计。

## 安装

确保已安装 Python 3.8+，然后安装依赖：

```bash
pip install -r requirements.txt
```

## 运行

**必须从项目根目录启动**（`ConfigLoader` 从当前工作目录读取 `config.json`）：

```bash
python src/main.py
```

## 配置说明 (`config.json`)

首次运行且未找到配置文件时，程序自动生成默认配置。完整字段说明如下：

```jsonc
{
    // 重力加速度参考值 (m/s²)，用于剥离重力分量
    "gravity_reference": 9.80,

    "udp": {
        "enabled": true,          // 是否启用 UDP 接收
        "ip": "127.0.0.1",        // 监听地址
        "port": 8888              // 监听端口
    },

    "serial": {
        "enabled": true,
        "port": "COM8",           // 串口号
        "baudrate": 115200,
        "timeout": 1,
        "protocol": "atkms901m",  // "csv" 或 "atkms901m"
        "acc_fsr": 4,             // 加速度计满量程 (g)，仅 atkms901m 协议
        "gyro_fsr": 2000          // 陀螺仪满量程 (°/s)，仅 atkms901m 协议
    },

    "render_debug": {
        "enabled": false,         // 是否在日志中输出 3D 渲染调试信息
        "verbose_point_updates": false
    },

    // 数据点映射列表 —— 定义如何从原始数据流提取传感器轴
    "points": [
        {
            "name": "ACC",
            "source": "serial",       // "serial" | "udp" | "any"
            "purpose": "accelerometer", // 特殊用途: accelerometer / gyroscope
                                        //           magnetic_field / quaternion
            // 省略 "prefix" 表示匹配无前缀数据
            "x": { "index": 0, "multiplier": 1.0 },
            "y": { "index": 1, "multiplier": 1.0 },
            "z": { "index": 2, "multiplier": 1.0 }
        },
        {
            "name": "QUAT",
            "source": "serial",
            "purpose": "quaternion",
            "w": { "index": 6, "multiplier": 1.0 },
            "x": { "index": 7, "multiplier": 1.0 },
            "y": { "index": 8, "multiplier": 1.0 },
            "z": { "index": 9, "multiplier": 1.0 }
        },
        {
            "name": "Point G (Prefixed)",
            "source": "udp",
            "prefix": "G",            // 仅匹配带 "G:" 前缀的 UDP 数据
            "color": [0, 0, 255, 255], // RGBA 0-255, 可选
            "size": 15,                // 像素大小, 可选
            "x": { "index": 0, "multiplier": 1.0 },
            "y": { "index": 1, "multiplier": -1.0 },
            "z": { "index": 2, "multiplier": -1.0 }
        }
    ]
}
```

### `points` 字段说明

| 字段 | 类型 | 说明 |
|------|------|------|
| `name` | string | 显示名称 |
| `source` | string | 数据来源：`"serial"` / `"udp"` / `"any"` |
| `purpose` | string | 特殊用途。`accelerometer` / `gyroscope` / `magnetic_field` / `quaternion` 会被姿态引擎使用，不会直接渲染为独立点。省略或其他值则作为普通 3D 可视化点。 |
| `prefix` | string | 仅匹配带有该前缀的数据包（如 `"G"` 匹配 `G:1,2,3`）。省略则匹配无前缀数据。 |
| `color` | [R,G,B,A] | 渲染颜色，0-255。默认红色。 |
| `size` | number | 点像素大小。默认 10。 |
| `x/y/z` | object | 各轴配置：`index`（数据数组下标）和 `multiplier`（缩放因子）。 |
| `w` | object | 仅 quaternion 用途，四元数 w 分量的 index/multiplier。 |

## 模块简介

| 模块 | 职责 |
|------|------|
| `ins_math.py` | 纯数学函数：Madgwick 6DOF/9DOF 滤波、初始姿态估算、四元数旋转。无 Qt 依赖，可独立测试。 |
| `pose_processor.py` | Qt 信号编排层：从配置提取传感器数据、调用 `ins_math` 进行姿态更新、积分位移、发射信号。 |
| `atkms901m_resolver.py` | ATK-MS901M 传感器二进制协议：帧同步（`0x55 0x55`）、校验、多帧合并为 19 元素快照。 |
| `data_receiver.py` | 后台线程接收 UDP/串口数据，支持 CSV 文本和 ATK-MS901M 二进制两种协议。 |
| `config_loader.py` | 单例模式加载/保存 `config.json`，提供默认值回退。 |
| `viewer_3d.py` | pyqtgraph OpenGL 3D 视图，含自定义坐标轴、网格、轨迹渲染、相机动画。 |
| `main_window.py` | 主窗口：组装所有组件、信号路由、调试控制台、状态栏。 |
