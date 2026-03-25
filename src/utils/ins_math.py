"""
Inertial Navigation System math utilities.

Pure functions for attitude estimation and coordinate transforms,
independent of any UI framework.  Used by PoseProcessor.

Quaternion convention throughout: [w, x, y, z]
"""

import math
import numpy as np


def initialize_orientation(acc, mag=None):
    """Compute an initial orientation quaternion from accelerometer (and
    optionally magnetometer) readings.

    Returns (quaternion, roll_deg, pitch_deg, yaw_deg).
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
    """Rotate vector *v* by unit quaternion *q* using the rotation matrix
    derived from *q*.  Both *v* and the return value are length-3 arrays."""
    w, x, y, z = q
    vx, vy, vz = v

    rx = (1 - 2*y*y - 2*z*z) * vx + (2*x*y - 2*w*z) * vy + (2*x*z + 2*w*y) * vz
    ry = (2*x*y + 2*w*z) * vx + (1 - 2*x*x - 2*z*z) * vy + (2*y*z - 2*w*x) * vz
    rz = (2*x*z - 2*w*y) * vx + (2*y*z + 2*w*x) * vy + (1 - 2*x*x - 2*y*y) * vz

    return np.array([rx, ry, rz])


def madgwick_update_6dof(q, gyr, acc, dt, beta=0.1):
    """Madgwick AHRS update for 6-DOF IMU (accelerometer + gyroscope).

    Parameters
    ----------
    q : array-like [w, x, y, z]
    gyr : array-like [gx, gy, gz] in rad/s
    acc : array-like [ax, ay, az]
    dt : float – time step in seconds
    beta : float – filter gain

    Returns
    -------
    numpy.ndarray – updated unit quaternion [w, x, y, z]
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
    """Madgwick AHRS update for 9-DOF MARG (accelerometer + gyroscope +
    magnetometer).  Falls back to 6-DOF when the magnetometer norm is zero.

    Parameters
    ----------
    q : array-like [w, x, y, z]
    gyr : array-like [gx, gy, gz] in rad/s
    acc : array-like [ax, ay, az]
    mag : array-like [mx, my, mz]
    dt : float – time step in seconds
    beta : float – filter gain

    Returns
    -------
    numpy.ndarray – updated unit quaternion [w, x, y, z]
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
