#pragma once
#include "MathUtils.h"

// ============================================================
// Ahrs.h — AHRS（航姿参考系统）滤波器接口
// 包含 Madgwick 和 Mahony 两种互补滤波算法
// 四元数约定：[w, x, y, z]（标量在前）
// ============================================================

namespace Ahrs {

// ── Madgwick 滤波器 ─────────────────────────────────────────

// 6DOF Madgwick 更新（加速度计 + 陀螺仪）
// 参数：q=当前四元数, gyr=[gx,gy,gz](rad/s), acc=[ax,ay,az],
//       dt=时间步长(s), beta=梯度下降增益
// 返回：更新后的单位四元数
Quat4d madgwickUpdate6dof(const Quat4d& q,
                           const Vec3d& gyr,
                           const Vec3d& acc,
                           double dt,
                           double beta = 0.1);

// 9DOF Madgwick 更新（加速度计 + 陀螺仪 + 磁力计）
// 当磁力计模为零时自动回退到 6DOF 版本
// 参数：mag=[mx,my,mz]（额外输入）
// 返回：更新后的单位四元数
Quat4d madgwickUpdate9dof(const Quat4d& q,
                           const Vec3d& gyr,
                           const Vec3d& acc,
                           const Vec3d& mag,
                           double dt,
                           double beta = 0.1);

// ── Mahony 滤波器 ──────────────────────────────────────────

// 6DOF Mahony 更新（加速度计 + 陀螺仪）
// 参数：integralFb=积分反馈状态（由调用方维护，函数内就地修改）
// 返回：更新后的单位四元数
Quat4d mahonyUpdate6dof(const Quat4d& q,
                         const Vec3d& gyr,
                         const Vec3d& acc,
                         double dt,
                         double kp = 1.0,
                         double ki = 0.0,
                         Vec3d* integralFb = nullptr);

// 9DOF Mahony 更新（加速度计 + 陀螺仪 + 磁力计）
// 当磁力计模为零时自动回退到 6DOF 版本
// 返回：更新后的单位四元数
Quat4d mahonyUpdate9dof(const Quat4d& q,
                         const Vec3d& gyr,
                         const Vec3d& acc,
                         const Vec3d& mag,
                         double dt,
                         double kp = 1.0,
                         double ki = 0.0,
                         Vec3d* integralFb = nullptr);

} // namespace Ahrs
