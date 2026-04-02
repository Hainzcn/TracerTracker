#pragma once
#include <array>
#include <tuple>

// ============================================================
// MathUtils.h — INS 基础数学工具
// 四元数约定：[w, x, y, z]（标量在前）
// ============================================================

// 四元数类型别名：[w, x, y, z]
using Quat4d  = std::array<double, 4>;
// 三维向量类型别名：[x, y, z]
using Vec3d   = std::array<double, 3>;

namespace MathUtils {

// 根据加速度计（及可选磁力计）读数初始化姿态四元数
// 输入：acc=[ax,ay,az]，mag=[mx,my,mz]（可空）
// 返回：(quaternion[w,x,y,z], roll_deg, pitch_deg, yaw_deg)
std::tuple<Quat4d, double, double, double>
initializeOrientation(const Vec3d& acc, const Vec3d* mag = nullptr);

// Hamilton 积 p * q，两者均为 [w, x, y, z] 格式
// 返回：乘积四元数 [w, x, y, z]
Quat4d quatMultiply(const Quat4d& p, const Quat4d& q);

// 使用单位四元数 q 旋转三维向量 v
// 等价于旋转矩阵变换，不修改输入
// 返回：旋转后的三维向量
Vec3d rotateVector(const Vec3d& v, const Quat4d& q);

// 四元数归一化（防止数值累积漂移）
// 原地修改并返回归一化后的四元数
Quat4d normalize(Quat4d q);

} // namespace MathUtils
