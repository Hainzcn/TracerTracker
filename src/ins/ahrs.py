"""
AHRS（航姿参考系统）滤波器实现。

包含 Madgwick 和 Mahony 两种互补滤波算法，分别支持 6DOF 和 9DOF 模式。

四元数约定：[w, x, y, z]
"""

import math

import numpy as np


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
# Mahony 互补滤波器
# ---------------------------------------------------------------------------


def mahony_update_6dof(q, gyr, acc, dt, kp=1.0, ki=0.0, integral_fb=None):
    """适用于 6 自由度 IMU（加速度计 + 陀螺仪）的 Mahony AHRS 更新。

    参数
    ----------
    q : 数组类 [w, x, y, z]
    gyr : 数组类 [gx, gy, gz]，单位为 rad/s
    acc : 数组类 [ax, ay, az]
    dt : float – 以秒为单位的时间步长
    kp : float – 比例增益
    ki : float – 积分增益
    integral_fb : numpy.ndarray shape(3,) 或 None
        积分反馈项，由调用方维护状态，函数内部会就地修改。
        若为 None 则不使用积分项。

    返回
    -------
    numpy.ndarray – 更新后的单位四元数 [w, x, y, z]
    """
    q0, q1, q2, q3 = q
    gx, gy, gz = gyr
    ax, ay, az = acc

    norm = math.sqrt(ax * ax + ay * ay + az * az)
    if norm == 0:
        return np.array(q, dtype=float)
    ax /= norm
    ay /= norm
    az /= norm

    # 从四元数估计的重力方向（旋转矩阵第三列）
    vx = 2.0 * (q1 * q3 - q0 * q2)
    vy = 2.0 * (q0 * q1 + q2 * q3)
    vz = q0 * q0 - q1 * q1 - q2 * q2 + q3 * q3

    # 误差为测量重力与估计重力的叉积
    ex = ay * vz - az * vy
    ey = az * vx - ax * vz
    ez = ax * vy - ay * vx

    if integral_fb is not None and ki > 0:
        integral_fb[0] += ki * ex * dt
        integral_fb[1] += ki * ey * dt
        integral_fb[2] += ki * ez * dt
        gx += integral_fb[0]
        gy += integral_fb[1]
        gz += integral_fb[2]

    gx += kp * ex
    gy += kp * ey
    gz += kp * ez

    # 四元数微分方程积分
    halfdt = 0.5 * dt
    q0 += (-q1 * gx - q2 * gy - q3 * gz) * halfdt
    q1 += (q0 * gx + q2 * gz - q3 * gy) * halfdt
    q2 += (q0 * gy - q1 * gz + q3 * gx) * halfdt
    q3 += (q0 * gz + q1 * gy - q2 * gx) * halfdt

    norm = math.sqrt(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3)
    if norm > 0:
        q0 /= norm
        q1 /= norm
        q2 /= norm
        q3 /= norm

    return np.array([q0, q1, q2, q3])


def mahony_update_9dof(q, gyr, acc, mag, dt, kp=1.0, ki=0.0, integral_fb=None):
    """适用于 9 自由度 MARG（加速度计 + 陀螺仪 + 磁力计）的 Mahony AHRS 更新。
    当磁力计模为零时，回退到 6 自由度更新。

    参数
    ----------
    q : 数组类 [w, x, y, z]
    gyr : 数组类 [gx, gy, gz]，单位为 rad/s
    acc : 数组类 [ax, ay, az]
    mag : 数组类 [mx, my, mz]
    dt : float – 以秒为单位的时间步长
    kp : float – 比例增益
    ki : float – 积分增益
    integral_fb : numpy.ndarray shape(3,) 或 None
        积分反馈项，由调用方维护状态，函数内部会就地修改。

    返回
    -------
    numpy.ndarray – 更新后的单位四元数 [w, x, y, z]
    """
    q0, q1, q2, q3 = q
    gx, gy, gz = gyr
    ax, ay, az = acc
    mx, my, mz = mag

    norm = math.sqrt(ax * ax + ay * ay + az * az)
    if norm == 0:
        return np.array(q, dtype=float)
    ax /= norm
    ay /= norm
    az /= norm

    norm = math.sqrt(mx * mx + my * my + mz * mz)
    if norm == 0:
        return mahony_update_6dof(q, gyr, acc, dt, kp, ki, integral_fb)
    mx /= norm
    my /= norm
    mz /= norm

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

    # 将磁力计读数旋转到地球坐标系
    hx = (2.0 * (mx * (0.5 - q2q2 - q3q3) + my * (q1q2 - q0q3) + mz * (q1q3 + q0q2)))
    hy = (2.0 * (mx * (q1q2 + q0q3) + my * (0.5 - q1q1 - q3q3) + mz * (q2q3 - q0q1)))

    # 地球坐标系中的参考磁场方向
    bx = math.sqrt(hx * hx + hy * hy)
    bz = (2.0 * (mx * (q1q3 - q0q2) + my * (q2q3 + q0q1) + mz * (0.5 - q1q1 - q2q2)))

    # 从四元数估计的重力方向
    vx = 2.0 * (q1q3 - q0q2)
    vy = 2.0 * (q0q1 + q2q3)
    vz = q0q0 - q1q1 - q2q2 + q3q3

    # 从四元数估计的磁场方向
    wx = 2.0 * (bx * (0.5 - q2q2 - q3q3) + bz * (q1q3 - q0q2))
    wy = 2.0 * (bx * (q1q2 - q0q3) + bz * (q0q1 + q2q3))
    wz = 2.0 * (bx * (q0q2 + q1q3) + bz * (0.5 - q1q1 - q2q2))

    # 误差为加速度计和磁力计测量值与估计方向的叉积之和
    ex = (ay * vz - az * vy) + (my * wz - mz * wy)
    ey = (az * vx - ax * vz) + (mz * wx - mx * wz)
    ez = (ax * vy - ay * vx) + (mx * wy - my * wx)

    if integral_fb is not None and ki > 0:
        integral_fb[0] += ki * ex * dt
        integral_fb[1] += ki * ey * dt
        integral_fb[2] += ki * ez * dt
        gx += integral_fb[0]
        gy += integral_fb[1]
        gz += integral_fb[2]

    gx += kp * ex
    gy += kp * ey
    gz += kp * ez

    halfdt = 0.5 * dt
    q0 += (-q1 * gx - q2 * gy - q3 * gz) * halfdt
    q1 += (q0 * gx + q2 * gz - q3 * gy) * halfdt
    q2 += (q0 * gy - q1 * gz + q3 * gx) * halfdt
    q3 += (q0 * gz + q1 * gy - q2 * gx) * halfdt

    norm = math.sqrt(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3)
    if norm > 0:
        q0 /= norm
        q1 /= norm
        q2 /= norm
        q3 /= norm

    return np.array([q0, q1, q2, q3])
