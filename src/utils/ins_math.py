"""
惯性导航系统 (INS) 数学工具。

用于姿态估计和坐标变换的纯函数，独立于任何 UI 框架。由 PoseProcessor 使用。

贯穿全文的四元数约定：[w, x, y, z]
"""

import math
from collections import deque

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


def rotate_vector(v, q):
    """使用从单位四元数 *q* 导出的旋转矩阵旋转向量 *v*。
    *v* 和返回值都是长度为 3 的数组。"""
    w, x, y, z = q
    vx, vy, vz = v

    rx = (1 - 2*y*y - 2*z*z) * vx + (2*x*y - 2*w*z) * vy + (2*x*z + 2*w*y) * vz
    ry = (2*x*y + 2*w*z) * vx + (1 - 2*x*x - 2*z*z) * vy + (2*y*z - 2*w*x) * vz
    rz = (2*x*z - 2*w*y) * vx + (2*y*z + 2*w*x) * vy + (1 - 2*x*x - 2*y*y) * vz

    return np.array([rx, ry, rz])


def madgwick_update_6dof(q, gyr, acc, dt, beta=0.1):
    """适用于 6 自由度 IMU（加速度计 + 陀螺仪）的 Madgwick AHRS 更新。

    参数
    ----------
    q : 数组类 [w, x, y, z]
    gyr : 数组类 [gx, gy, gz]，单位为 rad/s
    acc : 数组类 [ax, ay, az]
    dt : float – 以秒为单位的时间步长
    beta : float – 滤波器增益

    返回
    -------
    numpy.ndarray – 更新后的单位四元数 [w, x, y, z]
    """
    q0, q1, q2, q3 = q
    gx, gy, gz = gyr
    ax, ay, az = acc

    norm = math.sqrt(ax*ax + ay*ay + az*az)
    if norm == 0:
        return np.array(q, dtype=float)
    ax /= norm
    ay /= norm
    az /= norm

    _2q0 = 2.0 * q0
    _2q1 = 2.0 * q1
    _2q2 = 2.0 * q2
    _2q3 = 2.0 * q3
    _4q0 = 4.0 * q0
    _4q1 = 4.0 * q1
    _4q2 = 4.0 * q2
    _8q1 = 8.0 * q1
    _8q2 = 8.0 * q2
    q0q0 = q0 * q0
    q1q1 = q1 * q1
    q2q2 = q2 * q2
    q3q3 = q3 * q3

    s0 = _4q0 * q2q2 + _2q2 * ax + _4q0 * q1q1 - _2q1 * ay
    s1 = _4q1 * q3q3 - _2q3 * ax + 4.0 * q0q0 * q1 - _2q0 * ay - _4q1 + _8q1 * q1q1 + _8q1 * q2q2 + _4q1 * az
    s2 = 4.0 * q0q0 * q2 + _2q0 * ax + _4q2 * q3q3 - _2q3 * ay - _4q2 + _8q2 * q1q1 + _8q2 * q2q2 + _4q2 * az
    s3 = 4.0 * q1q1 * q3 - _2q1 * ax + 4.0 * q2q2 * q3 - _2q2 * ay

    norm = math.sqrt(s0*s0 + s1*s1 + s2*s2 + s3*s3)
    if norm > 0:
        s0 /= norm
        s1 /= norm
        s2 /= norm
        s3 /= norm

    qDot0 = 0.5 * (-q1 * gx - q2 * gy - q3 * gz) - beta * s0
    qDot1 = 0.5 * (q0 * gx + q2 * gz - q3 * gy) - beta * s1
    qDot2 = 0.5 * (q0 * gy - q1 * gz + q3 * gx) - beta * s2
    qDot3 = 0.5 * (q0 * gz + q1 * gy - q2 * gx) - beta * s3

    q0 += qDot0 * dt
    q1 += qDot1 * dt
    q2 += qDot2 * dt
    q3 += qDot3 * dt

    norm = math.sqrt(q0*q0 + q1*q1 + q2*q2 + q3*q3)
    if norm > 0:
        q0 /= norm
        q1 /= norm
        q2 /= norm
        q3 /= norm

    return np.array([q0, q1, q2, q3])


def madgwick_update_9dof(q, gyr, acc, mag, dt, beta=0.1):
    """适用于 9 自由度 MARG（加速度计 + 陀螺仪 + 磁力计）的 Madgwick AHRS 更新。
    当磁力计模为零时，回退到 6 自由度更新。

    参数
    ----------
    q : 数组类 [w, x, y, z]
    gyr : 数组类 [gx, gy, gz]，单位为 rad/s
    acc : 数组类 [ax, ay, az]
    mag : 数组类 [mx, my, mz]
    dt : float – 以秒为单位的时间步长
    beta : float – 滤波器增益

    返回
    -------
    numpy.ndarray – 更新后的单位四元数 [w, x, y, z]
    """
    q0, q1, q2, q3 = q
    gx, gy, gz = gyr
    ax, ay, az = acc
    mx, my, mz = mag

    norm = math.sqrt(ax*ax + ay*ay + az*az)
    if norm == 0:
        return np.array(q, dtype=float)
    ax /= norm
    ay /= norm
    az /= norm

    norm = math.sqrt(mx*mx + my*my + mz*mz)
    if norm == 0:
        return madgwick_update_6dof(q, gyr, acc, dt, beta)
    mx /= norm
    my /= norm
    mz /= norm

    _2q0mx = 2.0 * q0 * mx
    _2q0my = 2.0 * q0 * my
    _2q0mz = 2.0 * q0 * mz
    _2q1mx = 2.0 * q1 * mx
    _2q0 = 2.0 * q0
    _2q1 = 2.0 * q1
    _2q2 = 2.0 * q2
    _2q3 = 2.0 * q3
    _2q0q2 = 2.0 * q0 * q2
    _2q2q3 = 2.0 * q2 * q3
    q0q0 = q0 * q0
    q0q1 = q0 * q1
    q0q2 = q0 * q2
    q0q3 = q0 * q3
    q1q1 = q1 * q1
    q1q2 = q1 * q2
    q1q3 = q1 * q3
    q2q2 = q2 * q2
    q2q3 = q2 * q3
    q3q3 = q3 * q3

    hx = mx * q0q0 - _2q0my * q3 + _2q0mz * q2 + mx * q1q1 + _2q1 * my * q2 + _2q1 * mz * q3 - mx * q2q2 - mx * q3q3
    hy = _2q0mx * q3 + my * q0q0 - _2q0mz * q1 + _2q1mx * q2 - my * q1q1 + my * q2q2 + _2q2 * mz * q3 - my * q3q3
    hz = -_2q0mx * q2 + _2q0my * q1 + mz * q0q0 - mz * q1q1 - mz * q2q2 + mz * q3q3

    bx = math.sqrt(hx * hx + hy * hy)
    bz = hz

    _2bx = 2.0 * bx
    _2bz = 2.0 * bz
    _4bx = 4.0 * bx
    _4bz = 4.0 * bz

    s0 = -_2q2 * (2.0 * q1q3 - _2q0q2 - ax) + _2q1 * (2.0 * q0q1 + _2q2q3 - ay) - _2bz * q2 * (_2bx * (0.5 - q2q2 - q3q3) + _2bz * (q1q3 - q0q2) - mx) + (-_2bx * q3 + _2bz * q1) * (_2bx * (q1q2 - q0q3) + _2bz * (q0q1 + q2q3) - my) + _2bx * q2 * (_2bx * (q0q2 + q1q3) + _2bz * (0.5 - q1q1 - q2q2) - mz)
    s1 = _2q3 * (2.0 * q1q3 - _2q0q2 - ax) + _2q0 * (2.0 * q0q1 + _2q2q3 - ay) - 4.0 * q1 * (1 - 2.0 * q1q1 - 2.0 * q2q2 - az) + _2bz * q3 * (_2bx * (0.5 - q2q2 - q3q3) + _2bz * (q1q3 - q0q2) - mx) + (_2bx * q2 + _2bz * q0) * (_2bx * (q1q2 - q0q3) + _2bz * (q0q1 + q2q3) - my) + (_2bx * q3 - _4bz * q1) * (_2bx * (q0q2 + q1q3) + _2bz * (0.5 - q1q1 - q2q2) - mz)
    s2 = -_2q0 * (2.0 * q1q3 - _2q0q2 - ax) + _2q3 * (2.0 * q0q1 + _2q2q3 - ay) - 4.0 * q2 * (1 - 2.0 * q1q1 - 2.0 * q2q2 - az) + (-_4bx * q2 - _2bz * q0) * (_2bx * (0.5 - q2q2 - q3q3) + _2bz * (q1q3 - q0q2) - mx) + (_2bx * q1 + _2bz * q3) * (_2bx * (q1q2 - q0q3) + _2bz * (q0q1 + q2q3) - my) + (_2bx * q0 - _4bz * q2) * (_2bx * (q0q2 + q1q3) + _2bz * (0.5 - q1q1 - q2q2) - mz)
    s3 = _2q1 * (2.0 * q1q3 - _2q0q2 - ax) + _2q2 * (2.0 * q0q1 + _2q2q3 - ay) + (-_4bx * q3 + _2bz * q1) * (_2bx * (0.5 - q2q2 - q3q3) + _2bz * (q1q3 - q0q2) - mx) + (-_2bx * q0 + _2bz * q2) * (_2bx * (q1q2 - q0q3) + _2bz * (q0q1 + q2q3) - my) + _2bx * q1 * (_2bx * (q0q2 + q1q3) + _2bz * (0.5 - q1q1 - q2q2) - mz)

    norm = math.sqrt(s0*s0 + s1*s1 + s2*s2 + s3*s3)
    if norm > 0:
        s0 /= norm
        s1 /= norm
        s2 /= norm
        s3 /= norm

    qDot0 = 0.5 * (-q1 * gx - q2 * gy - q3 * gz) - beta * s0
    qDot1 = 0.5 * (q0 * gx + q2 * gz - q3 * gy) - beta * s1
    qDot2 = 0.5 * (q0 * gy - q1 * gz + q3 * gx) - beta * s2
    qDot3 = 0.5 * (q0 * gz + q1 * gy - q2 * gx) - beta * s3

    q0 += qDot0 * dt
    q1 += qDot1 * dt
    q2 += qDot2 * dt
    q3 += qDot3 * dt

    norm = math.sqrt(q0*q0 + q1*q1 + q2*q2 + q3*q3)
    if norm > 0:
        q0 /= norm
        q1 /= norm
        q2 /= norm
        q3 /= norm

    return np.array([q0, q1, q2, q3])


# ---------------------------------------------------------------------------
# 低通滤波器 / 卡尔曼滤波器 / 零速检测
# ---------------------------------------------------------------------------


class LowPassFilter:
    """一阶指数移动平均低通滤波器。

    alpha 越小，截止频率越低（平滑效果越强）。
    """

    def __init__(self, alpha=0.05):
        self.alpha = alpha
        self._value = None

    def reset(self):
        self._value = None

    def update(self, raw):
        if self._value is None:
            self._value = float(raw)
        else:
            self._value = self.alpha * raw + (1.0 - self.alpha) * self._value
        return self._value

    @property
    def value(self):
        return self._value


class VerticalKalmanFilter:
    """垂直通道卡尔曼滤波器。

    状态向量 x = [h, v, a]^T
        h — 高度 (m)
        v — 垂直速度 (m/s)
        a — 垂直加速度 (m/s^2)

    预测模型为牛顿运动学（匀加速假设）。
    观测量为气压计高度变化量 z = h + noise。
    """

    def __init__(self, R=0.5, sigma_a=0.5):
        self.x = np.zeros(3)
        self.P = np.eye(3)
        self.R = float(R)
        self.sigma_a = float(sigma_a)

    def reset(self):
        self.x = np.zeros(3)
        self.P = np.eye(3)

    def predict(self, dt, a_imu_z):
        """执行预测步，用 IMU 垂直加速度注入加速度状态分量。"""
        dt2 = dt * dt
        F = np.array([
            [1.0, dt, 0.5 * dt2],
            [0.0, 1.0, dt],
            [0.0, 0.0, 1.0],
        ])
        G = np.array([0.5 * dt2, dt, 1.0])
        Q = (self.sigma_a ** 2) * np.outer(G, G)

        self.x = F @ self.x
        self.x[2] = a_imu_z
        self.P = F @ self.P @ F.T + Q

    def update(self, z_altitude):
        """用气压高度观测值执行更新步。"""
        H = np.array([[1.0, 0.0, 0.0]])
        S = (H @ self.P @ H.T)[0, 0] + self.R
        K = (self.P @ H.T) / S
        y = z_altitude - self.x[0]
        self.x = self.x + (K * y).flatten()
        self.P = (np.eye(3) - K @ H) @ self.P

    def apply_zupt(self):
        """零速更新：将速度状态强制为零，并收缩对应协方差。"""
        self.x[1] = 0.0
        self.P[1, :] *= 0.01
        self.P[:, 1] *= 0.01
        self.P[1, 1] = 1e-4


class ZUPTDetector:
    """零速检测器。

    在滑动窗口内计算加速度模平方的方差和陀螺仪信号模的方差，
    当两者均低于阈值时判定为静止。
    """

    def __init__(self, acc_threshold=0.1, gyro_threshold=0.01, window_size=20):
        self.acc_threshold = acc_threshold
        self.gyro_threshold = gyro_threshold
        self.acc_buffer = deque(maxlen=window_size)
        self.gyro_buffer = deque(maxlen=window_size)

    def reset(self):
        self.acc_buffer.clear()
        self.gyro_buffer.clear()

    def update(self, acc_norm_sq, gyro_norm):
        """添加一个样本并返回是否处于静止状态。

        Parameters
        ----------
        acc_norm_sq : float
            原始加速度三轴平方和 (ax^2 + ay^2 + az^2)。
        gyro_norm : float
            陀螺仪三轴模 sqrt(gx^2 + gy^2 + gz^2)。

        Returns
        -------
        bool
            True 表示检测到静止。
        """
        self.acc_buffer.append(acc_norm_sq)
        self.gyro_buffer.append(gyro_norm)
        if len(self.acc_buffer) < self.acc_buffer.maxlen:
            return False
        acc_var = float(np.var(self.acc_buffer))
        gyro_var = float(np.var(self.gyro_buffer))
        return acc_var < self.acc_threshold and gyro_var < self.gyro_threshold
