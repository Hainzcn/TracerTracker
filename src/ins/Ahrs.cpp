#include "Ahrs.h"
#include <cmath>

// ============================================================
// Ahrs.cpp — AHRS 滤波器实现
// 数值完全对应 Python 版 ahrs.py
// ============================================================

namespace Ahrs {

// ── Madgwick 6DOF ────────────────────────────────────────────

// Madgwick 6DOF 更新步骤：
//   1. 归一化加速度计向量
//   2. 计算目标函数梯度（将测量重力误差投影到四元数空间）
//   3. 归一化梯度，以 beta 作步长修正陀螺仪积分
//   4. 四元数微分方程积分，再归一化
Quat4d madgwickUpdate6dof(const Quat4d& q_in,
                           const Vec3d& gyr,
                           const Vec3d& acc,
                           double dt,
                           double beta) {
    double q0 = q_in[0], q1 = q_in[1], q2 = q_in[2], q3 = q_in[3];
    double gx = gyr[0], gy = gyr[1], gz = gyr[2];
    double ax = acc[0], ay = acc[1], az = acc[2];

    // 归一化加速度计（模为零则不更新）
    double norm = std::sqrt(ax*ax + ay*ay + az*az);
    if (norm == 0.0) return q_in;
    ax /= norm; ay /= norm; az /= norm;

    // 预计算常用乘积，减少重复运算
    double _2q0 = 2.0*q0, _2q1 = 2.0*q1, _2q2 = 2.0*q2, _2q3 = 2.0*q3;
    double _4q0 = 4.0*q0, _4q1 = 4.0*q1, _4q2 = 4.0*q2;
    double _8q1 = 8.0*q1, _8q2 = 8.0*q2;
    double q0q0 = q0*q0, q1q1 = q1*q1, q2q2 = q2*q2, q3q3 = q3*q3;

    // 目标函数梯度分量（对应 Python 版 s0..s3）
    double s0 = _4q0*q2q2 + _2q2*ax + _4q0*q1q1 - _2q1*ay;
    double s1 = _4q1*q3q3 - _2q3*ax + 4.0*q0q0*q1 - _2q0*ay - _4q1 + _8q1*q1q1 + _8q1*q2q2 + _4q1*az;
    double s2 = 4.0*q0q0*q2 + _2q0*ax + _4q2*q3q3 - _2q3*ay - _4q2 + _8q2*q1q1 + _8q2*q2q2 + _4q2*az;
    double s3 = 4.0*q1q1*q3 - _2q1*ax + 4.0*q2q2*q3 - _2q2*ay;

    // 归一化梯度
    norm = std::sqrt(s0*s0 + s1*s1 + s2*s2 + s3*s3);
    if (norm > 0.0) { s0/=norm; s1/=norm; s2/=norm; s3/=norm; }

    // 修正后四元数导数（陀螺仪积分 + 梯度修正）
    double qDot0 = 0.5*(-q1*gx - q2*gy - q3*gz) - beta*s0;
    double qDot1 = 0.5*( q0*gx + q2*gz - q3*gy) - beta*s1;
    double qDot2 = 0.5*( q0*gy - q1*gz + q3*gx) - beta*s2;
    double qDot3 = 0.5*( q0*gz + q1*gy - q2*gx) - beta*s3;

    // 积分更新四元数
    q0 += qDot0*dt; q1 += qDot1*dt; q2 += qDot2*dt; q3 += qDot3*dt;

    // 再次归一化
    norm = std::sqrt(q0*q0 + q1*q1 + q2*q2 + q3*q3);
    if (norm > 0.0) { q0/=norm; q1/=norm; q2/=norm; q3/=norm; }

    return {q0, q1, q2, q3};
}

// ── Madgwick 9DOF ────────────────────────────────────────────

// Madgwick 9DOF 更新步骤：在 6DOF 基础上增加磁力计参考场约束
// 磁力计模为零时回退到 6DOF
Quat4d madgwickUpdate9dof(const Quat4d& q_in,
                           const Vec3d& gyr,
                           const Vec3d& acc,
                           const Vec3d& mag,
                           double dt,
                           double beta) {
    double q0 = q_in[0], q1 = q_in[1], q2 = q_in[2], q3 = q_in[3];
    double gx = gyr[0], gy = gyr[1], gz = gyr[2];
    double ax = acc[0], ay = acc[1], az = acc[2];
    double mx = mag[0], my = mag[1], mz = mag[2];

    // 归一化加速度
    double norm = std::sqrt(ax*ax + ay*ay + az*az);
    if (norm == 0.0) return q_in;
    ax/=norm; ay/=norm; az/=norm;

    // 磁力计模为零时退化为 6DOF
    norm = std::sqrt(mx*mx + my*my + mz*mz);
    if (norm == 0.0) return madgwickUpdate6dof(q_in, gyr, acc, dt, beta);
    mx/=norm; my/=norm; mz/=norm;

    // 预计算乘积（与 Python 版完全对应）
    double _2q0mx = 2.0*q0*mx, _2q0my = 2.0*q0*my, _2q0mz = 2.0*q0*mz;
    double _2q1mx = 2.0*q1*mx;
    double _2q0   = 2.0*q0, _2q1 = 2.0*q1, _2q2 = 2.0*q2, _2q3 = 2.0*q3;
    double _2q0q2 = 2.0*q0*q2, _2q2q3 = 2.0*q2*q3;
    double q0q0 = q0*q0, q0q1 = q0*q1, q0q2 = q0*q2, q0q3 = q0*q3;
    double q1q1 = q1*q1, q1q2 = q1*q2, q1q3 = q1*q3;
    double q2q2 = q2*q2, q2q3 = q2*q3, q3q3 = q3*q3;

    // 参考磁场方向（在地球坐标系中的水平分量 bx 和垂直分量 bz）
    double hx = mx*q0q0 - _2q0my*q3 + _2q0mz*q2 + mx*q1q1 + _2q1*my*q2 + _2q1*mz*q3 - mx*q2q2 - mx*q3q3;
    double hy = _2q0mx*q3 + my*q0q0 - _2q0mz*q1 + _2q1mx*q2 - my*q1q1 + my*q2q2 + _2q2*mz*q3 - my*q3q3;
    double hz = -_2q0mx*q2 + _2q0my*q1 + mz*q0q0 - mz*q1q1 - mz*q2q2 + mz*q3q3;
    double bx = std::sqrt(hx*hx + hy*hy);
    double bz = hz;
    double _2bx = 2.0*bx, _2bz = 2.0*bz, _4bx = 4.0*bx, _4bz = 4.0*bz;

    // 目标函数梯度（9DOF 版）
    double s0 = -_2q2*(2.0*q1q3 - _2q0q2 - ax) + _2q1*(2.0*q0q1 + _2q2q3 - ay) - _2bz*q2*(_2bx*(0.5-q2q2-q3q3) + _2bz*(q1q3-q0q2) - mx) + (-_2bx*q3+_2bz*q1)*(_2bx*(q1q2-q0q3) + _2bz*(q0q1+q2q3) - my) + _2bx*q2*(_2bx*(q0q2+q1q3) + _2bz*(0.5-q1q1-q2q2) - mz);
    double s1 = _2q3*(2.0*q1q3 - _2q0q2 - ax) + _2q0*(2.0*q0q1 + _2q2q3 - ay) - 4.0*q1*(1-2.0*q1q1-2.0*q2q2-az) + _2bz*q3*(_2bx*(0.5-q2q2-q3q3) + _2bz*(q1q3-q0q2) - mx) + (_2bx*q2+_2bz*q0)*(_2bx*(q1q2-q0q3) + _2bz*(q0q1+q2q3) - my) + (_2bx*q3-_4bz*q1)*(_2bx*(q0q2+q1q3) + _2bz*(0.5-q1q1-q2q2) - mz);
    double s2 = -_2q0*(2.0*q1q3 - _2q0q2 - ax) + _2q3*(2.0*q0q1 + _2q2q3 - ay) - 4.0*q2*(1-2.0*q1q1-2.0*q2q2-az) + (-_4bx*q2-_2bz*q0)*(_2bx*(0.5-q2q2-q3q3) + _2bz*(q1q3-q0q2) - mx) + (_2bx*q1+_2bz*q3)*(_2bx*(q1q2-q0q3) + _2bz*(q0q1+q2q3) - my) + (_2bx*q0-_4bz*q2)*(_2bx*(q0q2+q1q3) + _2bz*(0.5-q1q1-q2q2) - mz);
    double s3 = _2q1*(2.0*q1q3 - _2q0q2 - ax) + _2q2*(2.0*q0q1 + _2q2q3 - ay) + (-_4bx*q3+_2bz*q1)*(_2bx*(0.5-q2q2-q3q3) + _2bz*(q1q3-q0q2) - mx) + (-_2bx*q0+_2bz*q2)*(_2bx*(q1q2-q0q3) + _2bz*(q0q1+q2q3) - my) + _2bx*q1*(_2bx*(q0q2+q1q3) + _2bz*(0.5-q1q1-q2q2) - mz);

    norm = std::sqrt(s0*s0 + s1*s1 + s2*s2 + s3*s3);
    if (norm > 0.0) { s0/=norm; s1/=norm; s2/=norm; s3/=norm; }

    double qDot0 = 0.5*(-q1*gx - q2*gy - q3*gz) - beta*s0;
    double qDot1 = 0.5*( q0*gx + q2*gz - q3*gy) - beta*s1;
    double qDot2 = 0.5*( q0*gy - q1*gz + q3*gx) - beta*s2;
    double qDot3 = 0.5*( q0*gz + q1*gy - q2*gx) - beta*s3;

    q0 += qDot0*dt; q1 += qDot1*dt; q2 += qDot2*dt; q3 += qDot3*dt;
    norm = std::sqrt(q0*q0 + q1*q1 + q2*q2 + q3*q3);
    if (norm > 0.0) { q0/=norm; q1/=norm; q2/=norm; q3/=norm; }

    return {q0, q1, q2, q3};
}

// ── Mahony 6DOF ──────────────────────────────────────────────

// Mahony 6DOF 更新步骤：
//   1. 归一化加速度计
//   2. 估算四元数旋转后的重力方向（旋转矩阵第三列）
//   3. 计算叉积误差（测量 - 估计）
//   4. 积分项（ki > 0 时）+比例修正（kp）修正陀螺仪
//   5. 四元数微分方程积分，归一化
Quat4d mahonyUpdate6dof(const Quat4d& q_in,
                         const Vec3d& gyr,
                         const Vec3d& acc,
                         double dt,
                         double kp,
                         double ki,
                         Vec3d* integralFb) {
    double q0 = q_in[0], q1 = q_in[1], q2 = q_in[2], q3 = q_in[3];
    double gx = gyr[0], gy = gyr[1], gz = gyr[2];
    double ax = acc[0], ay = acc[1], az = acc[2];

    double norm = std::sqrt(ax*ax + ay*ay + az*az);
    if (norm == 0.0) return q_in;
    ax/=norm; ay/=norm; az/=norm;

    // 四元数估算的重力方向（旋转矩阵第三列）
    double vx = 2.0*(q1*q3 - q0*q2);
    double vy = 2.0*(q0*q1 + q2*q3);
    double vz = q0*q0 - q1*q1 - q2*q2 + q3*q3;

    // 叉积误差：测量重力 × 估算重力
    double ex = ay*vz - az*vy;
    double ey = az*vx - ax*vz;
    double ez = ax*vy - ay*vx;

    // 积分项（若启用且指针非空）
    if (integralFb != nullptr && ki > 0.0) {
        (*integralFb)[0] += ki*ex*dt;
        (*integralFb)[1] += ki*ey*dt;
        (*integralFb)[2] += ki*ez*dt;
        gx += (*integralFb)[0];
        gy += (*integralFb)[1];
        gz += (*integralFb)[2];
    }

    // 比例修正
    gx += kp*ex; gy += kp*ey; gz += kp*ez;

    // 四元数微分方程积分
    double halfdt = 0.5*dt;
    q0 += (-q1*gx - q2*gy - q3*gz)*halfdt;
    q1 += ( q0*gx + q2*gz - q3*gy)*halfdt;
    q2 += ( q0*gy - q1*gz + q3*gx)*halfdt;
    q3 += ( q0*gz + q1*gy - q2*gx)*halfdt;

    norm = std::sqrt(q0*q0 + q1*q1 + q2*q2 + q3*q3);
    if (norm > 0.0) { q0/=norm; q1/=norm; q2/=norm; q3/=norm; }

    return {q0, q1, q2, q3};
}

// ── Mahony 9DOF ──────────────────────────────────────────────

// 在 6DOF 基础上增加磁力计约束：
//   误差 = 加速度叉积误差 + 磁力计叉积误差
Quat4d mahonyUpdate9dof(const Quat4d& q_in,
                         const Vec3d& gyr,
                         const Vec3d& acc,
                         const Vec3d& mag,
                         double dt,
                         double kp,
                         double ki,
                         Vec3d* integralFb) {
    double q0 = q_in[0], q1 = q_in[1], q2 = q_in[2], q3 = q_in[3];
    double gx = gyr[0], gy = gyr[1], gz = gyr[2];
    double ax = acc[0], ay = acc[1], az = acc[2];
    double mx = mag[0], my = mag[1], mz = mag[2];

    double norm = std::sqrt(ax*ax + ay*ay + az*az);
    if (norm == 0.0) return q_in;
    ax/=norm; ay/=norm; az/=norm;

    norm = std::sqrt(mx*mx + my*my + mz*mz);
    if (norm == 0.0) return mahonyUpdate6dof(q_in, gyr, acc, dt, kp, ki, integralFb);
    mx/=norm; my/=norm; mz/=norm;

    double q0q0 = q0*q0, q0q1 = q0*q1, q0q2 = q0*q2, q0q3 = q0*q3;
    double q1q1 = q1*q1, q1q2 = q1*q2, q1q3 = q1*q3;
    double q2q2 = q2*q2, q2q3 = q2*q3, q3q3 = q3*q3;

    // 磁力计旋转到地球坐标系，计算参考磁场方向
    double hx = 2.0*(mx*(0.5-q2q2-q3q3) + my*(q1q2-q0q3) + mz*(q1q3+q0q2));
    double hy = 2.0*(mx*(q1q2+q0q3) + my*(0.5-q1q1-q3q3) + mz*(q2q3-q0q1));
    double bx = std::sqrt(hx*hx + hy*hy);
    double bz = 2.0*(mx*(q1q3-q0q2) + my*(q2q3+q0q1) + mz*(0.5-q1q1-q2q2));

    // 估算重力和磁场方向
    double vx = 2.0*(q1q3-q0q2);
    double vy = 2.0*(q0q1+q2q3);
    double vz = q0q0-q1q1-q2q2+q3q3;
    double wx = 2.0*(bx*(0.5-q2q2-q3q3) + bz*(q1q3-q0q2));
    double wy = 2.0*(bx*(q1q2-q0q3)     + bz*(q0q1+q2q3));
    double wz = 2.0*(bx*(q0q2+q1q3)     + bz*(0.5-q1q1-q2q2));

    // 加速度叉积误差 + 磁力计叉积误差
    double ex = (ay*vz-az*vy) + (my*wz-mz*wy);
    double ey = (az*vx-ax*vz) + (mz*wx-mx*wz);
    double ez = (ax*vy-ay*vx) + (mx*wy-my*wx);

    if (integralFb != nullptr && ki > 0.0) {
        (*integralFb)[0] += ki*ex*dt;
        (*integralFb)[1] += ki*ey*dt;
        (*integralFb)[2] += ki*ez*dt;
        gx += (*integralFb)[0];
        gy += (*integralFb)[1];
        gz += (*integralFb)[2];
    }

    gx += kp*ex; gy += kp*ey; gz += kp*ez;

    double halfdt = 0.5*dt;
    q0 += (-q1*gx - q2*gy - q3*gz)*halfdt;
    q1 += ( q0*gx + q2*gz - q3*gy)*halfdt;
    q2 += ( q0*gy - q1*gz + q3*gx)*halfdt;
    q3 += ( q0*gz + q1*gy - q2*gx)*halfdt;

    norm = std::sqrt(q0*q0 + q1*q1 + q2*q2 + q3*q3);
    if (norm > 0.0) { q0/=norm; q1/=norm; q2/=norm; q3/=norm; }

    return {q0, q1, q2, q3};
}

} // namespace Ahrs
