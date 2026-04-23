#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
udp_motion_simulator.py
────────────────────────────────────────────────────────────────────────
TracerTracker UDP 测试数据模拟器

按 ATK-MS901M 协议的 snapshot 字段顺序，通过 UDP CSV 文本协议
向 TracerTracker 发送符合客观运动规律的 IMU 数据。

特点：
  - 纯 Python 标准库实现（socket / math / time / argparse）
  - 多场景调度：静止 → 直线加减速 → 圆周 → 8 字 → 电梯升降 → 姿态摆动
  - 严格按运动学规律生成：位置-速度-加速度自洽、欧拉角速率与陀螺自洽
  - 加速度输出为 body 系（含重力），与传感器物理输出一致
  - 标准大气气压公式生成 pressure，与 altitude 同步
  - 与 TracerTracker 默认 config.json (UDP 127.0.0.1:8888) 即插即用

字段顺序（19 列，与 protocols/atkms901m.json 的 snapshot_order 一致）：
  ax, ay, az,            # 加速度 m/s²，body 系含重力
  gx, gy, gz,            # 角速度 rad/s，body 系
  q0, q1, q2, q3,        # 四元数 [w, x, y, z]，body→world
  mx, my, mz,            # 磁力计原始整数值，body 系
  temp,                  # 温度 °C
  roll, pitch, yaw,      # 欧拉角 度（ZYX 顺序）
  pressure, altitude     # 气压 Pa / 海拔 m

约定：
  - 世界系右手坐标 X-北 Y-东 Z-上（Z 轴朝上）
  - 重力在世界系 = (0, 0, +9.8)，故静止时 body 系 az = +9.8
  - yaw=0 表示 body X 轴指向世界 X 正方向（磁北）
  - 旋转顺序 ZYX：先 yaw（绕 Z），再 pitch（绕 Y'），再 roll（绕 X''）
"""

import argparse
import math
import random
import socket
import sys
import time
from dataclasses import dataclass
from typing import Callable, List, Tuple

# ── 物理常量 ─────────────────────────────────────────────────────────
GRAVITY = 9.80              # 重力加速度 m/s²，与 ConfigLoader.gravity_reference 默认一致
SEA_LEVEL_P = 101325.0      # 海平面气压 Pa
SEA_LEVEL_T = 288.15        # 海平面温度 K
LAPSE_RATE = 0.0065         # 温度递减率 K/m
BARO_EXP = 5.255            # 标准大气指数
TEMP_C = 25.0               # 模拟环境温度 °C

# 世界系磁场（μT 量级，X-北 Z-上 → 北向 + 下倾角）
MAG_WORLD = (250.0, 0.0, -400.0)

# ── 噪声参数 ────────────────────────────────────────────────────────
# 关键：ZUPT 用 |acc|² 与 |gyro| 在窗口内的方差判断静止
#       (acc_variance_threshold=0.5, gyro_variance_threshold=0.1)
# 真实运动数据若毫无噪声，圆周运动等场景下 body 系加速度恒定 → 方差为 0
# 会被误判为静止。这里按"传感器底噪 + 运动振动"两段加噪：
#   - 静止段：仅传感器底噪，|acc|² 方差远小于 0.5（不破坏 ZUPT 正常工作）
#   - 运动段：叠加显著抖动，|acc|² 方差 > 0.5（避免误检为静止）
ACC_NOISE_STATIC  = 0.008   # 静止段加速度白噪声 σ (m/s²)
GYRO_NOISE_STATIC = 0.0015  # 静止段陀螺白噪声 σ (rad/s)
MAG_NOISE_STATIC  = 0.5     # 静止段磁力计噪声 (LSB)
BARO_NOISE_STATIC = 1.0     # 静止段气压噪声 (Pa)
TEMP_NOISE_SIGMA  = 0.02    # 温度抖动 (°C)

ACC_NOISE_MOVING  = 0.10    # 运动段加速度噪声 σ (m/s²) → |a|² 方差 ~ (2·9.8·0.1)² ≈ 3.8 ≫ 0.5
GYRO_NOISE_MOVING = 0.012   # 运动段陀螺噪声 σ (rad/s)
MAG_NOISE_MOVING  = 4.0     # 运动段磁力计噪声 (LSB)
BARO_NOISE_MOVING = 5.0     # 运动段气压噪声 (Pa)

MOTION_THRESHOLD  = 0.05    # |vel| + 0.3·|acc_world| 超过此值视为运动


# ── 数学工具 ────────────────────────────────────────────────────────
def euler_to_quat(roll: float, pitch: float, yaw: float) -> Tuple[float, float, float, float]:
    """ZYX 欧拉角（弧度）→ 四元数 [w, x, y, z]，与 MathUtils::initializeOrientation 一致"""
    cr, sr = math.cos(roll * 0.5), math.sin(roll * 0.5)
    cp, sp = math.cos(pitch * 0.5), math.sin(pitch * 0.5)
    cy, sy = math.cos(yaw * 0.5), math.sin(yaw * 0.5)
    qw = cr * cp * cy + sr * sp * sy
    qx = sr * cp * cy - cr * sp * sy
    qy = cr * sp * cy + sr * cp * sy
    qz = cr * cp * sy - sr * sp * cy
    n = math.sqrt(qw * qw + qx * qx + qy * qy + qz * qz)
    if n < 1e-12:
        return (1.0, 0.0, 0.0, 0.0)
    return (qw / n, qx / n, qy / n, qz / n)


def rotate_world_to_body(v: Tuple[float, float, float],
                         roll: float, pitch: float, yaw: float) -> Tuple[float, float, float]:
    """用 ZYX 欧拉角构造旋转矩阵 R(body→world)，并左乘 R^T 把世界向量旋到 body 系"""
    cr, sr = math.cos(roll), math.sin(roll)
    cp, sp = math.cos(pitch), math.sin(pitch)
    cy, sy = math.cos(yaw), math.sin(yaw)

    # R = Rz(yaw) * Ry(pitch) * Rx(roll)（body→world），转置即 world→body
    r00 = cy * cp
    r01 = cy * sp * sr - sy * cr
    r02 = cy * sp * cr + sy * sr
    r10 = sy * cp
    r11 = sy * sp * sr + cy * cr
    r12 = sy * sp * cr - cy * sr
    r20 = -sp
    r21 = cp * sr
    r22 = cp * cr

    vx, vy, vz = v
    bx = r00 * vx + r10 * vy + r20 * vz
    by = r01 * vx + r11 * vy + r21 * vz
    bz = r02 * vx + r12 * vy + r22 * vz
    return (bx, by, bz)


def euler_rate_to_body_rate(roll: float, pitch: float,
                            roll_dot: float, pitch_dot: float, yaw_dot: float) -> Tuple[float, float, float]:
    """ZYX 欧拉角速率 → body 系角速度（rad/s）"""
    sp, cp = math.sin(pitch), math.cos(pitch)
    sr, cr = math.sin(roll), math.cos(roll)
    gx = roll_dot - sp * yaw_dot
    gy = cr * pitch_dot + sr * cp * yaw_dot
    gz = -sr * pitch_dot + cr * cp * yaw_dot
    return (gx, gy, gz)


def pressure_from_altitude(alt_m: float) -> float:
    """标准大气：海拔 m → 气压 Pa（alt 不应超过 11km）"""
    base = 1.0 - LAPSE_RATE * alt_m / SEA_LEVEL_T
    if base <= 0.0:
        return 0.0
    return SEA_LEVEL_P * (base ** BARO_EXP)


# ── 场景描述 ────────────────────────────────────────────────────────
# 每个场景函数：输入 (tau)，tau 是相对场景起点的时间秒；输出运动学状态。
# State = (pos, vel, acc_world_linear, euler, euler_dot)
#   pos/vel/acc 均为世界系 (x, y, z)
#   acc_world_linear 不含重力（重力在打包时再叠加进 body 系 az）
#   euler/euler_dot 为 (roll, pitch, yaw)，弧度 / (rad/s)

State = Tuple[
    Tuple[float, float, float],   # pos
    Tuple[float, float, float],   # vel
    Tuple[float, float, float],   # acc_world_linear
    Tuple[float, float, float],   # euler
    Tuple[float, float, float],   # euler_dot
]


def _zero_state(z0: float = 0.0) -> State:
    return ((0.0, 0.0, z0), (0.0, 0.0, 0.0), (0.0, 0.0, 0.0),
            (0.0, 0.0, 0.0), (0.0, 0.0, 0.0))


def scene_static(tau: float, ctx: dict) -> State:
    """完全静止，验证首帧姿态初始化与 ZUPT"""
    return _zero_state(ctx.get("z0", 0.0))


def scene_accel_x(tau: float, ctx: dict, T: float, a: float) -> State:
    """沿 +X 匀加速运动，加速度恒为 a m/s²"""
    x0 = ctx.get("x0", 0.0)
    v0 = ctx.get("vx0", 0.0)
    z0 = ctx.get("z0", 0.0)
    yaw = ctx.get("yaw0", 0.0)
    vx = v0 + a * tau
    x = x0 + v0 * tau + 0.5 * a * tau * tau
    return ((x, 0.0, z0), (vx, 0.0, 0.0), (a, 0.0, 0.0),
            (0.0, 0.0, yaw), (0.0, 0.0, 0.0))


def scene_cruise_x(tau: float, ctx: dict, T: float) -> State:
    """匀速直行（沿前一段末速度方向）"""
    x0 = ctx.get("x0", 0.0)
    v = ctx.get("vx0", 0.0)
    z0 = ctx.get("z0", 0.0)
    yaw = ctx.get("yaw0", 0.0)
    x = x0 + v * tau
    return ((x, 0.0, z0), (v, 0.0, 0.0), (0.0, 0.0, 0.0),
            (0.0, 0.0, yaw), (0.0, 0.0, 0.0))


def scene_decel_x(tau: float, ctx: dict, T: float, a: float) -> State:
    """沿 +X 匀减速到 0（a 应为负值）"""
    x0 = ctx.get("x0", 0.0)
    v0 = ctx.get("vx0", 0.0)
    z0 = ctx.get("z0", 0.0)
    yaw = ctx.get("yaw0", 0.0)
    vx = v0 + a * tau
    if (a < 0 and vx < 0) or (a > 0 and vx > 0 and v0 < 0):
        vx = 0.0
        a_eff = 0.0
    else:
        a_eff = a
    x = x0 + v0 * tau + 0.5 * a_eff * tau * tau
    return ((x, 0.0, z0), (vx, 0.0, 0.0), (a_eff, 0.0, 0.0),
            (0.0, 0.0, yaw), (0.0, 0.0, 0.0))


def scene_circle(tau: float, ctx: dict, T: float, R: float, omega: float) -> State:
    """水平面圆周运动，圆心 (cx, cy)，半径 R，角速度 omega（rad/s）"""
    cx = ctx.get("cx", 0.0)
    cy = ctx.get("cy", 0.0)
    z0 = ctx.get("z0", 0.0)
    phi0 = ctx.get("phi0", 0.0)
    phi = phi0 + omega * tau

    x = cx + R * math.cos(phi)
    y = cy + R * math.sin(phi)
    vx = -R * omega * math.sin(phi)
    vy = R * omega * math.cos(phi)
    ax = -R * omega * omega * math.cos(phi)  # 向心加速度世界系分量
    ay = -R * omega * omega * math.sin(phi)

    yaw = math.atan2(vy, vx)             # body X 始终对准切线方向
    yaw_dot = omega                      # 圆周匀速 yaw 速率即 omega
    return ((x, y, z0), (vx, vy, 0.0), (ax, ay, 0.0),
            (0.0, 0.0, yaw), (0.0, 0.0, yaw_dot))


def scene_figure8(tau: float, ctx: dict, T: float, A: float, omega: float) -> State:
    """Lissajous 8 字：x = A·sin(ω t), y = A·sin(2ω t)/2"""
    cx = ctx.get("cx", 0.0)
    cy = ctx.get("cy", 0.0)
    z0 = ctx.get("z0", 0.0)
    w = omega
    s1 = math.sin(w * tau)
    c1 = math.cos(w * tau)
    s2 = math.sin(2 * w * tau)
    c2 = math.cos(2 * w * tau)

    x = cx + A * s1
    y = cy + 0.5 * A * s2
    vx = A * w * c1
    vy = A * w * c2
    ax = -A * w * w * s1
    ay = -2 * A * w * w * s2

    speed = math.hypot(vx, vy)
    if speed < 1e-6:
        yaw = ctx.get("yaw0", 0.0)
        yaw_dot = 0.0
    else:
        yaw = math.atan2(vy, vx)
        # yaw_dot = (vx·ay - vy·ax) / (vx² + vy²)
        yaw_dot = (vx * ay - vy * ax) / (vx * vx + vy * vy)
    return ((x, y, z0), (vx, vy, 0.0), (ax, ay, 0.0),
            (0.0, 0.0, yaw), (0.0, 0.0, yaw_dot))


def scene_elevator(tau: float, ctx: dict, T: float, H: float) -> State:
    """电梯：先匀加速上升 H/2，再匀减速到顶部，悬停，再下降回原位
       简化为正弦剖面：z = z0 + H·sin²(π·tau / T) ⋅ sign 段"""
    z0 = ctx.get("z0", 0.0)
    yaw = ctx.get("yaw0", 0.0)
    # 用 sin²(π t / T) 平滑剖面：t∈[0, T/2] 上升到 H，t∈[T/2, T] 下降到 0
    half = T * 0.5
    if tau <= half:
        u = math.pi * tau / half
        z_off = H * 0.5 * (1 - math.cos(u))                 # 0 → H
        vz = H * 0.5 * math.sin(u) * (math.pi / half)
        az = H * 0.5 * math.cos(u) * (math.pi / half) ** 2
    else:
        s = tau - half
        u = math.pi * s / half
        z_off = H * 0.5 * (1 + math.cos(u))                 # H → 0
        vz = -H * 0.5 * math.sin(u) * (math.pi / half)
        az = -H * 0.5 * math.cos(u) * (math.pi / half) ** 2

    return ((0.0, 0.0, z0 + z_off), (0.0, 0.0, vz), (0.0, 0.0, az),
            (0.0, 0.0, yaw), (0.0, 0.0, 0.0))


def scene_tilt(tau: float, ctx: dict, T: float) -> State:
    """原地静止，roll/pitch 缓慢正弦摆动，验证姿态可视化"""
    z0 = ctx.get("z0", 0.0)
    yaw = ctx.get("yaw0", 0.0)
    w = 2 * math.pi / T * 1.5    # 1.5 个完整周期
    amp = math.radians(20.0)

    roll = amp * math.sin(w * tau)
    pitch = amp * math.sin(w * tau + math.pi / 2)
    roll_dot = amp * w * math.cos(w * tau)
    pitch_dot = amp * w * math.cos(w * tau + math.pi / 2)

    return ((0.0, 0.0, z0), (0.0, 0.0, 0.0), (0.0, 0.0, 0.0),
            (roll, pitch, yaw), (roll_dot, pitch_dot, 0.0))


# ── 场景串联调度器 ───────────────────────────────────────────────────
@dataclass
class Segment:
    name: str
    duration: float
    fn: Callable[[float, dict], State]


def build_demo_segments() -> List[Segment]:
    """完整演示：约 65s 串联所有动作"""
    return [
        Segment("static-1",   5.0, lambda tau, ctx: scene_static(tau, ctx)),
        Segment("accel_x",    3.0, lambda tau, ctx: scene_accel_x(tau, ctx, 3.0, a=1.0)),
        Segment("cruise_x",   3.0, lambda tau, ctx: scene_cruise_x(tau, ctx, 3.0)),
        Segment("decel_x",    3.0, lambda tau, ctx: scene_decel_x(tau, ctx, 3.0, a=-1.0)),
        Segment("static-2",   2.0, lambda tau, ctx: scene_static(tau, ctx)),
        Segment("circle",    10.0, lambda tau, ctx: scene_circle(tau, ctx, 10.0, R=2.0, omega=0.6)),
        Segment("static-3",   2.0, lambda tau, ctx: scene_static(tau, ctx)),
        Segment("figure8",   12.0, lambda tau, ctx: scene_figure8(tau, ctx, 12.0, A=2.0, omega=0.5)),
        Segment("static-4",   2.0, lambda tau, ctx: scene_static(tau, ctx)),
        Segment("elevator",   8.0, lambda tau, ctx: scene_elevator(tau, ctx, 8.0, H=2.0)),
        Segment("tilt",       5.0, lambda tau, ctx: scene_tilt(tau, ctx, 5.0)),
        Segment("static-end", 3.0, lambda tau, ctx: scene_static(tau, ctx)),
    ]


def build_single_scenario(name: str) -> List[Segment]:
    """单场景模式：前置 2s 静止帮助 PoseProcessor 初始化姿态"""
    presets = {
        "static":   [Segment("static",  10.0, lambda tau, ctx: scene_static(tau, ctx))],
        "accel":    [Segment("accel_x",  3.0, lambda tau, ctx: scene_accel_x(tau, ctx, 3.0, 1.0)),
                     Segment("cruise_x", 3.0, lambda tau, ctx: scene_cruise_x(tau, ctx, 3.0)),
                     Segment("decel_x",  3.0, lambda tau, ctx: scene_decel_x(tau, ctx, 3.0, -1.0))],
        "circle":   [Segment("circle",  20.0, lambda tau, ctx: scene_circle(tau, ctx, 20.0, 2.0, 0.6))],
        "figure8":  [Segment("figure8", 24.0, lambda tau, ctx: scene_figure8(tau, ctx, 24.0, 2.0, 0.5))],
        "elevator": [Segment("elevator", 8.0, lambda tau, ctx: scene_elevator(tau, ctx, 8.0, 2.0))],
        "tilt":     [Segment("tilt",    10.0, lambda tau, ctx: scene_tilt(tau, ctx, 10.0))],
    }
    if name not in presets:
        raise ValueError(f"未知场景: {name}")
    return [Segment("init_static", 2.0, lambda tau, ctx: scene_static(tau, ctx))] + presets[name]


# ── 数据打包 ────────────────────────────────────────────────────────
def _is_moving(vel: Tuple[float, float, float],
               acc_w: Tuple[float, float, float]) -> bool:
    """根据世界系速度与线加速度模长粗略判断是否处于运动状态"""
    vmag = math.sqrt(vel[0]*vel[0] + vel[1]*vel[1] + vel[2]*vel[2])
    amag = math.sqrt(acc_w[0]*acc_w[0] + acc_w[1]*acc_w[1] + acc_w[2]*acc_w[2])
    return (vmag + 0.3 * amag) > MOTION_THRESHOLD


def pack_snapshot(state: State, add_noise: bool = True) -> str:
    """把运动学状态打包成 19 列 CSV 行，单位与协议 snapshot_order 对应"""
    pos, vel, acc_w, euler, euler_dot = state
    roll, pitch, yaw = euler
    rd, pd, yd = euler_dot

    # 加速度：把世界系线性加速度叠加重力，再旋到 body 系
    ax, ay, az = rotate_world_to_body(
        (acc_w[0], acc_w[1], acc_w[2] + GRAVITY), roll, pitch, yaw
    )

    # 陀螺：欧拉角速率 → body 系角速度
    gx, gy, gz = euler_rate_to_body_rate(roll, pitch, rd, pd, yd)

    # 四元数（body→world）
    qw, qx, qy, qz = euler_to_quat(roll, pitch, yaw)

    # 磁力计：世界系磁场旋到 body 系，输出整数原始值
    mx, my, mz = rotate_world_to_body(MAG_WORLD, roll, pitch, yaw)

    # 气压与海拔
    altitude = pos[2]
    pressure = pressure_from_altitude(altitude)
    temp = TEMP_C

    # ── 噪声叠加 ───────────────────────────────────────────────────
    # 静止段噪声极小，运动段叠加传感器抖动，避免 ZUPT 在运动时误检
    if add_noise:
        if _is_moving(vel, acc_w):
            sa, sg, sm, sp = (ACC_NOISE_MOVING, GYRO_NOISE_MOVING,
                              MAG_NOISE_MOVING, BARO_NOISE_MOVING)
        else:
            sa, sg, sm, sp = (ACC_NOISE_STATIC, GYRO_NOISE_STATIC,
                              MAG_NOISE_STATIC, BARO_NOISE_STATIC)
        ax += random.gauss(0.0, sa)
        ay += random.gauss(0.0, sa)
        az += random.gauss(0.0, sa)
        gx += random.gauss(0.0, sg)
        gy += random.gauss(0.0, sg)
        gz += random.gauss(0.0, sg)
        mx += random.gauss(0.0, sm)
        my += random.gauss(0.0, sm)
        mz += random.gauss(0.0, sm)
        pressure += random.gauss(0.0, sp)
        temp += random.gauss(0.0, TEMP_NOISE_SIGMA)

    # 欧拉角输出度
    deg = 180.0 / math.pi
    fields = [
        ax, ay, az,
        gx, gy, gz,
        qw, qx, qy, qz,
        round(mx), round(my), round(mz),
        temp,
        roll * deg, pitch * deg, yaw * deg,
        pressure, altitude,
    ]
    # 控制精度：浮点 4 位、整数 0 位
    parts = []
    for i, v in enumerate(fields):
        if i in (10, 11, 12):              # mag 整数
            parts.append(f"{int(v)}")
        else:
            parts.append(f"{v:.5f}")
    return ",".join(parts)


# ── 主循环 ──────────────────────────────────────────────────────────
def run(args: argparse.Namespace) -> None:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    target = (args.ip, args.port)

    if args.scenario == "demo":
        segments = build_demo_segments()
    else:
        segments = build_single_scenario(args.scenario)

    total_dur = sum(s.duration for s in segments)
    period = 1.0 / args.rate

    print(f"[simulator] target={args.ip}:{args.port} rate={args.rate} Hz "
          f"scenario={args.scenario} duration≈{total_dur:.1f}s "
          f"loop={'on' if args.loop else 'off'} "
          f"noise={'on' if args.noise else 'off'}", flush=True)

    # 全局上下文：每段用上一段末态做初始位置/速度/朝向，保证连续
    ctx = {"x0": 0.0, "y0": 0.0, "z0": 0.0, "vx0": 0.0, "vy0": 0.0,
           "yaw0": 0.0, "phi0": 0.0, "cx": 0.0, "cy": 0.0}

    sent = 0
    t_start = time.perf_counter()
    next_tick = t_start

    try:
        while True:
            elapsed = 0.0
            for seg in segments:
                seg_t0 = time.perf_counter()
                seg_ctx = dict(ctx)
                last_state: State = _zero_state(ctx["z0"])
                tau = 0.0
                while tau <= seg.duration + 1e-9:
                    state = seg.fn(tau, seg_ctx)
                    line = pack_snapshot(state, add_noise=args.noise)
                    sock.sendto((line + "\n").encode("ascii"), target)
                    last_state = state
                    sent += 1

                    if not args.quiet and sent % args.rate == 0:
                        pos, _, _, eu, _ = state
                        print(f"[{seg.name:>11}] t={elapsed + tau:6.2f}s "
                              f"pos=({pos[0]:+.2f},{pos[1]:+.2f},{pos[2]:+.2f}) "
                              f"yaw={math.degrees(eu[2]):+6.1f}°",
                              flush=True)

                    next_tick += period
                    sleep_for = next_tick - time.perf_counter()
                    if sleep_for > 0:
                        time.sleep(sleep_for)
                    else:
                        # 落后则放弃赶进度，重置基线以避免雪崩
                        next_tick = time.perf_counter()
                    tau += period

                # 用末态更新全局上下文，保证下一段从此处接续
                pos, vel, _, eu, _ = last_state
                ctx["x0"], ctx["y0"], ctx["z0"] = pos
                ctx["vx0"], ctx["vy0"] = vel[0], vel[1]
                ctx["yaw0"] = eu[2]
                # 圆周相位：根据末态位置与圆心更新，便于下一圈无缝衔接
                if seg.name.startswith("circle"):
                    ctx["phi0"] = math.atan2(pos[1] - seg_ctx.get("cy", 0.0),
                                             pos[0] - seg_ctx.get("cx", 0.0))
                elapsed += seg.duration
                if not args.quiet:
                    print(f"[simulator] -- 段 '{seg.name}' 完成，用时 "
                          f"{time.perf_counter() - seg_t0:.2f}s --", flush=True)

            if not args.loop:
                break
            print("[simulator] 循环重启", flush=True)
            ctx = {"x0": 0.0, "y0": 0.0, "z0": 0.0, "vx0": 0.0, "vy0": 0.0,
                   "yaw0": 0.0, "phi0": 0.0, "cx": 0.0, "cy": 0.0}

    except KeyboardInterrupt:
        print("\n[simulator] 收到 Ctrl+C，退出", flush=True)
    finally:
        sock.close()
        dt = time.perf_counter() - t_start
        rate_eff = sent / dt if dt > 0 else 0.0
        print(f"[simulator] 共发送 {sent} 帧，实际平均速率 {rate_eff:.1f} Hz", flush=True)


def main() -> int:
    p = argparse.ArgumentParser(
        formatter_class=argparse.RawDescriptionHelpFormatter,
        description="向 TracerTracker 发送符合物理规律的 IMU UDP 测试数据",
        epilog="示例：\n"
               "  python tools/udp_motion_simulator.py\n"
               "  python tools/udp_motion_simulator.py --scenario circle --loop\n"
               "  python tools/udp_motion_simulator.py --ip 127.0.0.1 --port 8888 --rate 200\n",
    )
    p.add_argument("--ip", default="127.0.0.1", help="目标 IP（默认 127.0.0.1）")
    p.add_argument("--port", type=int, default=8888, help="目标端口（默认 8888）")
    p.add_argument("--rate", type=int, default=100, help="发送频率 Hz（默认 100）")
    p.add_argument("--scenario", default="demo",
                   choices=["demo", "static", "accel", "circle",
                            "figure8", "elevator", "tilt"],
                   help="场景：demo 为多场景串联演示，其余为单一场景")
    p.add_argument("--loop", action="store_true", help="循环播放（Ctrl+C 退出）")
    p.add_argument("--quiet", action="store_true", help="不打印每秒进度行")
    p.add_argument("--no-noise", dest="noise", action="store_false",
                   help="关闭传感器噪声（调试用，会让运动段触发 ZUPT 误检）")
    p.add_argument("--seed", type=int, default=None,
                   help="噪声随机种子（不指定则用系统时间，便于复现）")
    p.set_defaults(noise=True)
    args = p.parse_args()

    if args.rate <= 0 or args.rate > 1000:
        print("rate 必须在 1..1000 之间", file=sys.stderr)
        return 2
    if args.seed is not None:
        random.seed(args.seed)
    run(args)
    return 0


if __name__ == "__main__":
    sys.exit(main())
