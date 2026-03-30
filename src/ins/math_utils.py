"""
惯性导航系统 (INS) 基础数学工具。

四元数运算、向量旋转与方向初始化。

贯穿全文的四元数约定：[w, x, y, z]
"""

import math

import numpy as np


def initialize_orientation(acc, mag=None):
    """根据加速度计（以及可选的磁力计）读数计算初始方向四元数。

    返回 (quaternion, roll_deg, pitch_deg, yaw_deg)。
    """
    ax, ay, az = acc

    roll = math.atan2(ay, az)
    pitch = math.atan2(-ax, math.sqrt(ay * ay + az * az))

    yaw = 0.0
    if mag is not None:
        mx, my, mz = mag
        Hy = my * math.cos(roll) - mz * math.sin(roll)
        Hx = (mx * math.cos(pitch)
              + my * math.sin(pitch) * math.sin(roll)
              + mz * math.sin(pitch) * math.cos(roll))
        yaw = math.atan2(-Hy, Hx)

    cy = math.cos(yaw * 0.5)
    sy = math.sin(yaw * 0.5)
    cp = math.cos(pitch * 0.5)
    sp = math.sin(pitch * 0.5)
    cr = math.cos(roll * 0.5)
    sr = math.sin(roll * 0.5)

    w = cr * cp * cy + sr * sp * sy
    x = sr * cp * cy - cr * sp * sy
    y = cr * sp * cy + sr * cp * sy
    z = cr * cp * sy - sr * sp * cy

    q = np.array([w, x, y, z])
    norm = np.linalg.norm(q)
    if norm > 0:
        q /= norm

    return q, math.degrees(roll), math.degrees(pitch), math.degrees(yaw)


def quat_multiply(p, q):
    """Hamilton 积 p * q，两者均为 [w, x, y, z] 格式。"""
    return np.array([
        p[0]*q[0] - p[1]*q[1] - p[2]*q[2] - p[3]*q[3],
        p[0]*q[1] + p[1]*q[0] + p[2]*q[3] - p[3]*q[2],
        p[0]*q[2] - p[1]*q[3] + p[2]*q[0] + p[3]*q[1],
        p[0]*q[3] + p[1]*q[2] - p[2]*q[1] + p[3]*q[0],
    ])


def rotate_vector(v, q):
    """使用从单位四元数 *q* 导出的旋转矩阵旋转向量 *v*。
    *v* 和返回值都是长度为 3 的数组。"""
    w, x, y, z = q
    vx, vy, vz = v

    rx = (1 - 2*y*y - 2*z*z) * vx + (2*x*y - 2*w*z) * vy + (2*x*z + 2*w*y) * vz
    ry = (2*x*y + 2*w*z) * vx + (1 - 2*x*x - 2*z*z) * vy + (2*y*z - 2*w*x) * vz
    rz = (2*x*z - 2*w*y) * vx + (2*y*z + 2*w*x) * vy + (1 - 2*x*x - 2*y*y) * vz

    return np.array([rx, ry, rz])
