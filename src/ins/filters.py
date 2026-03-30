"""
惯性导航系统 (INS) 滤波器与检测器。

包含低通滤波器、垂直通道卡尔曼滤波器和零速检测器。
"""

from collections import deque

import numpy as np


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
