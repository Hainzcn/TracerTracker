#include "Filters.h"
#include <cmath>
#include <numeric>
#include <algorithm>

// ============================================================
// Filters.cpp — INS 滤波器实现
// ============================================================

// ── LowPassFilter ─────────────────────────────────────────────

// 构造：保存滤波系数
LowPassFilter::LowPassFilter(double alpha)
    : m_alpha(alpha)
{}

// 重置内部状态（下次 update 将重新初始化）
void LowPassFilter::reset() {
    m_value.reset();
}

// 一阶 IIR 低通滤波：y[n] = alpha * x[n] + (1-alpha) * y[n-1]
// 首次调用直接以原始值初始化
double LowPassFilter::update(double raw) {
    if (!m_value.has_value()) {
        m_value = raw;
    } else {
        m_value = m_alpha * raw + (1.0 - m_alpha) * m_value.value();
    }
    return m_value.value();
}

// 返回当前滤波值（未初始化时为 nullopt）
std::optional<double> LowPassFilter::value() const {
    return m_value;
}

// ── VerticalKalmanFilter ──────────────────────────────────────

// 构造：初始化噪声参数并重置状态
VerticalKalmanFilter::VerticalKalmanFilter(double R, double sigma_a)
    : m_R(R), m_sigmaA(sigma_a)
{
    reset();
}

// 重置为零初始状态：x=[0,0,0]，P=I
void VerticalKalmanFilter::reset() {
    m_x.fill(0.0);
    m_P.fill(0.0);
    // P = I（单位矩阵）
    P(0,0) = 1.0; P(1,1) = 1.0; P(2,2) = 1.0;
}

// 预测步：牛顿运动学状态转移 + 过程噪声
// F = [[1, dt, 0.5*dt²], [0, 1, dt], [0, 0, 1]]
// G = [0.5*dt², dt, 1]（噪声驱动向量）
// Q = sigma_a² * G*G^T
void VerticalKalmanFilter::predict(double dt, double aImuZ) {
    double dt2 = dt * dt;
    // F 矩阵元素（仅非零非对角线元素）
    double F01 = dt, F02 = 0.5*dt2, F12 = dt;

    // 状态转移：x_new = F * x
    double h = m_x[0], v = m_x[1], a = m_x[2];
    m_x[0] = h + F01*v + F02*a;
    m_x[1] = v + F12*a;
    m_x[2] = aImuZ;  // 直接注入 IMU 加速度作为第三状态

    // 过程噪声矩阵 Q = sigma² * G*G^T
    double sa2 = m_sigmaA * m_sigmaA;
    double g0 = 0.5*dt2, g1 = dt, g2 = 1.0;
    double Q[3][3] = {
        {sa2*g0*g0, sa2*g0*g1, sa2*g0*g2},
        {sa2*g1*g0, sa2*g1*g1, sa2*g1*g2},
        {sa2*g2*g0, sa2*g2*g1, sa2*g2*g2},
    };

    // P_new = F * P * F^T + Q（手动展开 3×3 矩阵乘法）
    double Pold[3][3];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            Pold[i][j] = P(i, j);

    // F * P（F 是稀疏矩阵：只有对角线和上三角部分）
    double FP[3][3];
    FP[0][0] = Pold[0][0] + F01*Pold[1][0] + F02*Pold[2][0];
    FP[0][1] = Pold[0][1] + F01*Pold[1][1] + F02*Pold[2][1];
    FP[0][2] = Pold[0][2] + F01*Pold[1][2] + F02*Pold[2][2];
    FP[1][0] = Pold[1][0] + F12*Pold[2][0];
    FP[1][1] = Pold[1][1] + F12*Pold[2][1];
    FP[1][2] = Pold[1][2] + F12*Pold[2][2];
    FP[2][0] = Pold[2][0];
    FP[2][1] = Pold[2][1];
    FP[2][2] = Pold[2][2];

    // (F * P) * F^T + Q
    // F^T = [[1,0,0],[F01,1,0],[F02,F12,1]]
    P(0,0) = FP[0][0] + F01*FP[0][1] + F02*FP[0][2] + Q[0][0];
    P(0,1) = FP[0][1] + F12*FP[0][2] + Q[0][1];
    P(0,2) = FP[0][2]                 + Q[0][2];
    P(1,0) = FP[1][0] + F01*FP[1][1] + F02*FP[1][2] + Q[1][0];
    P(1,1) = FP[1][1] + F12*FP[1][2] + Q[1][1];
    P(1,2) = FP[1][2]                 + Q[1][2];
    P(2,0) = FP[2][0] + F01*FP[2][1] + F02*FP[2][2] + Q[2][0];
    P(2,1) = FP[2][1] + F12*FP[2][2] + Q[2][1];
    P(2,2) = FP[2][2]                 + Q[2][2];
}

// 更新步：卡尔曼增益 + 状态修正 + 协方差更新
// H = [1, 0, 0]（只观测高度）
void VerticalKalmanFilter::update(double zAltitude) {
    // 新息协方差 S = H*P*H^T + R = P[0][0] + R
    double S = P(0, 0) + m_R;
    if (S < 1e-12) return; // 防止除零

    // 卡尔曼增益 K = P*H^T / S = P 第 0 列 / S
    double K0 = P(0, 0) / S;
    double K1 = P(1, 0) / S;
    double K2 = P(2, 0) / S;

    // 状态更新：x = x + K * (z - H*x)
    double y = zAltitude - m_x[0]; // 新息
    m_x[0] += K0 * y;
    m_x[1] += K1 * y;
    m_x[2] += K2 * y;

    // 协方差更新：P = (I - K*H) * P
    // I - K*H = [[1-K0,0,0],[-K1,1,0],[-K2,0,1]]
    double p00 = P(0,0), p01 = P(0,1), p02 = P(0,2);
    double p10 = P(1,0), p11 = P(1,1), p12 = P(1,2);
    double p20 = P(2,0), p21 = P(2,1), p22 = P(2,2);
    P(0,0) = (1-K0)*p00;      P(0,1) = (1-K0)*p01;      P(0,2) = (1-K0)*p02;
    P(1,0) = -K1*p00 + p10;   P(1,1) = -K1*p01 + p11;   P(1,2) = -K1*p02 + p12;
    P(2,0) = -K2*p00 + p20;   P(2,1) = -K2*p01 + p21;   P(2,2) = -K2*p02 + p22;
}

// 零速更新（ZUPT）：强制速度为零，收缩相关协方差
void VerticalKalmanFilter::applyZupt() {
    m_x[1] = 0.0;
    // 收缩第 1 行和第 1 列（速度相关项）
    for (int j = 0; j < 3; ++j) { P(1,j) *= 0.01; P(j,1) *= 0.01; }
    P(1, 1) = 1e-4; // 速度方差设为极小值
}

// ── ZUPTDetector ──────────────────────────────────────────────

// 构造：保存阈值参数
ZUPTDetector::ZUPTDetector(double accThreshold, double gyroThreshold, int windowSize)
    : m_accThreshold(accThreshold)
    , m_gyroThreshold(gyroThreshold)
    , m_windowSize(windowSize)
{}

// 清空历史缓冲区
void ZUPTDetector::reset() {
    m_accBuffer.clear();
    m_gyroBuffer.clear();
}

// 计算滑动窗口方差
double ZUPTDetector::computeVariance(const std::deque<double>& buf) {
    if (buf.empty()) return 0.0;
    double mean = std::accumulate(buf.begin(), buf.end(), 0.0) / buf.size();
    double var  = 0.0;
    for (double v : buf) var += (v - mean) * (v - mean);
    return var / buf.size();
}

// 更新检测器，若窗口满且两个方差均低于阈值则返回 true（静止）
bool ZUPTDetector::update(double accNormSq, double gyroNorm) {
    // 保持滑动窗口大小
    m_accBuffer.push_back(accNormSq);
    m_gyroBuffer.push_back(gyroNorm);
    if ((int)m_accBuffer.size() > m_windowSize) m_accBuffer.pop_front();
    if ((int)m_gyroBuffer.size() > m_windowSize) m_gyroBuffer.pop_front();

    // 窗口未满时不判定
    if ((int)m_accBuffer.size() < m_windowSize) return false;

    double accVar  = computeVariance(m_accBuffer);
    double gyroVar = computeVariance(m_gyroBuffer);
    return (accVar < m_accThreshold) && (gyroVar < m_gyroThreshold);
}
