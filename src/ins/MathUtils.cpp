#include "MathUtils.h"
#include <cmath>

// ============================================================
// MathUtils.cpp — INS 基础数学工具实现
// ============================================================

namespace MathUtils {

// 四元数归一化
Quat4d normalize(Quat4d q) {
    double n = std::sqrt(q[0]*q[0] + q[1]*q[1] + q[2]*q[2] + q[3]*q[3]);
    if (n > 1e-12) {
        q[0] /= n; q[1] /= n; q[2] /= n; q[3] /= n;
    }
    return q;
}

// 根据加速度计（及可选磁力计）读数初始化方向四元数
// 算法：
//   1. 从 acc 计算 roll/pitch（不依赖磁力计）
//   2. 若提供磁力计，则经过倾斜补偿计算 yaw
//   3. 将欧拉角（ZYX 顺序）转换为四元数
std::tuple<Quat4d, double, double, double>
initializeOrientation(const Vec3d& acc, const Vec3d* mag) {
    double ax = acc[0], ay = acc[1], az = acc[2];

    // 横滚角（绕 X 轴）
    double roll  = std::atan2(ay, az);
    // 俯仰角（绕 Y 轴）
    double pitch = std::atan2(-ax, std::sqrt(ay*ay + az*az));

    double yaw = 0.0;
    if (mag != nullptr) {
        // 倾斜补偿磁力计偏航角计算
        double mx = (*mag)[0], my = (*mag)[1], mz = (*mag)[2];
        double Hy = my * std::cos(roll) - mz * std::sin(roll);
        double Hx = mx * std::cos(pitch)
                  + my * std::sin(pitch) * std::sin(roll)
                  + mz * std::sin(pitch) * std::cos(roll);
        yaw = std::atan2(-Hy, Hx);
    }

    // 欧拉角 → 四元数（ZYX 顺序，与 Python 版完全一致）
    double cy = std::cos(yaw   * 0.5);
    double sy = std::sin(yaw   * 0.5);
    double cp = std::cos(pitch * 0.5);
    double sp = std::sin(pitch * 0.5);
    double cr = std::cos(roll  * 0.5);
    double sr = std::sin(roll  * 0.5);

    Quat4d q = {
        cr * cp * cy + sr * sp * sy,  // w
        sr * cp * cy - cr * sp * sy,  // x
        cr * sp * cy + sr * cp * sy,  // y
        cr * cp * sy - sr * sp * cy   // z
    };
    q = normalize(q);

    constexpr double RAD2DEG = 180.0 / M_PI;
    return { q, roll * RAD2DEG, pitch * RAD2DEG, yaw * RAD2DEG };
}

// Hamilton 四元数乘积 p * q（[w,x,y,z] 格式）
// 公式展开与 Python numpy 版完全一致
Quat4d quatMultiply(const Quat4d& p, const Quat4d& q) {
    return {
        p[0]*q[0] - p[1]*q[1] - p[2]*q[2] - p[3]*q[3],  // w
        p[0]*q[1] + p[1]*q[0] + p[2]*q[3] - p[3]*q[2],  // x
        p[0]*q[2] - p[1]*q[3] + p[2]*q[0] + p[3]*q[1],  // y
        p[0]*q[3] + p[1]*q[2] - p[2]*q[1] + p[3]*q[0]   // z
    };
}

// 用四元数 q 旋转向量 v（通过展开旋转矩阵实现，避免两次四元数乘法）
// 旋转矩阵公式直接从四元数导出，与 Python 版数值完全一致
Vec3d rotateVector(const Vec3d& v, const Quat4d& q) {
    double w = q[0], x = q[1], y = q[2], z = q[3];
    double vx = v[0], vy = v[1], vz = v[2];

    double rx = (1 - 2*y*y - 2*z*z) * vx + (2*x*y - 2*w*z) * vy + (2*x*z + 2*w*y) * vz;
    double ry = (2*x*y + 2*w*z) * vx + (1 - 2*x*x - 2*z*z) * vy + (2*y*z - 2*w*x) * vz;
    double rz = (2*x*z - 2*w*y) * vx + (2*y*z + 2*w*x) * vy + (1 - 2*x*x - 2*y*y) * vz;

    return {rx, ry, rz};
}

} // namespace MathUtils
