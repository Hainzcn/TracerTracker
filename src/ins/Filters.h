#pragma once
#include <array>
#include <deque>
#include <optional>

// ============================================================
// Filters.h — INS 滤波器与检测器
// 包含：低通滤波器、垂直通道卡尔曼滤波器、零速检测器
// ============================================================

// ── 一阶低通滤波器（指数移动平均）────────────────────────────

class LowPassFilter {
public:
    // 构造函数：alpha 越小，截止频率越低（更平滑）
    explicit LowPassFilter(double alpha = 0.05);

    // 重置滤波器内部状态（下一次 update 将重新初始化）
    void reset();

    // 输入新的原始采样值，返回滤波后的值
    double update(double raw);

    // 获取当前滤波值（首次 update 前为 nullopt）
    std::optional<double> value() const;

private:
    double              m_alpha;  // 滤波系数
    std::optional<double> m_value; // 当前滤波状态
};

// ── 垂直通道卡尔曼滤波器 ─────────────────────────────────────
// 状态向量 x = [h, v, a]^T（高度、速度、加速度偏置）
// 观测量：气压计高度差 z = h + noise

class VerticalKalmanFilter {
public:
    // 构造函数：
    //   R        - 气压计测量噪声方差
    //   sigma_a  - 加速度过程噪声标准差
    VerticalKalmanFilter(double R = 0.5, double sigma_a = 0.5);

    // 重置为零初始状态
    void reset();

    // 预测步：使用 IMU 垂直加速度传播状态
    //   dt      - 时间步长（s）
    //   aImuZ   - 垂直线加速度（m/s²，已减去重力）
    void predict(double dt, double aImuZ);

    // 更新步：使用气压计高度观测值修正状态
    //   zAltitude - 气压计高度变化量（m，相对参考值）
    void update(double zAltitude);

    // 零速更新（ZUPT）：将速度状态强制归零并收缩协方差
    void applyZupt();

    // 获取状态向量各分量
    double height()   const { return m_x[0]; } // 高度（m）
    double velocity() const { return m_x[1]; } // 速度（m/s）
    double accelBias()const { return m_x[2]; } // 加速度偏置

private:
    std::array<double, 3>    m_x; // 状态向量 [h, v, a]
    std::array<double, 9>    m_P; // 3×3 协方差矩阵（行优先）
    double                   m_R;       // 测量噪声方差
    double                   m_sigmaA;  // 过程噪声标准差

    // 矩阵辅助：获取/设置 P[i][j]
    double& P(int i, int j)       { return m_P[i*3+j]; }
    double  P(int i, int j) const { return m_P[i*3+j]; }
};

// ── 零速检测器（ZUPT）────────────────────────────────────────

class ZUPTDetector {
public:
    // 构造函数：
    //   accThreshold  - 加速度方差阈值
    //   gyroThreshold - 陀螺仪方差阈值
    //   windowSize    - 滑动窗口大小（帧数）
    ZUPTDetector(double accThreshold  = 0.1,
                 double gyroThreshold = 0.01,
                 int    windowSize    = 20);

    // 清空历史缓冲区
    void reset();

    // 输入新的传感器样本，返回是否判定为静止状态
    //   accNormSq  - 加速度三轴平方和 (ax²+ay²+az²)
    //   gyroNorm   - 陀螺仪模 sqrt(gx²+gy²+gz²)
    bool update(double accNormSq, double gyroNorm);

private:
    double m_accThreshold;   // 加速度方差阈值
    double m_gyroThreshold;  // 陀螺仪方差阈值
    int    m_windowSize;     // 滑动窗口大小

    std::deque<double> m_accBuffer;  // 加速度平方和历史
    std::deque<double> m_gyroBuffer; // 陀螺仪模历史

    // 计算 deque 中数值的方差
    static double computeVariance(const std::deque<double>& buf);
};
