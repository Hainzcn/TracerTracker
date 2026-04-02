#pragma once
#include <QObject>
#include <QList>
#include <QString>
#include "MathUtils.h"
#include "Ahrs.h"
#include "Filters.h"
#include "config/ConfigLoader.h"

// ============================================================
// PoseProcessor.h — 姿态与惯性导航处理器
// 在主线程运行，通过信号/槽接收 DataReceiver 的原始数据
// 执行完整 INS 管线：AHRS → 重力剥离 → ZUPT → 卡尔曼 → 积分
// ============================================================

class PoseProcessor : public QObject {
    Q_OBJECT

public:
    explicit PoseProcessor(QObject* parent = nullptr);

    // 重置所有状态（位置/速度/四元数/滤波器）
    void reset();

signals:
    // 积分后的位移更新（name 用于 Viewer3D 识别轨迹，如 "Displacement Path"）
    void positionUpdated(const QString& name, double x, double y, double z);

    // 当前速度向量（m/s）
    void velocityUpdated(double vx, double vy, double vz);

    // 已提取的传感器数据（gyr/mag 为空列表表示无该分量）
    // linear_acc 是世界坐标系下剥离重力后的线加速度
    void parsedDataUpdated(const QString& source, const QString& prefix,
                            const QList<double>& linearAcc,
                            const QList<double>& gyr,
                            const QList<double>& mag);

    // Madgwick 和 Mahony 滤波四元数（各 4 个 double，[w,x,y,z]）
    void filterQuaternionsUpdated(const QList<double>& madgwickQ,
                                   const QList<double>& mahonyQ);

    // 节流调试日志消息
    void logMessage(const QString& msg);

public slots:
    // 处理来自 DataReceiver 的原始数据包
    // source: "serial"/"udp", prefix: 可选前缀, data: 浮点数组
    void process(const QString& source, const QString& prefix,
                 const QList<double>& data);

private:
    // ── 从配置中提取传感器向量的辅助函数 ───────────────────────

    // 根据 PointConfig 中的轴映射提取 XYZ 向量（可选）
    // 返回 nullopt 表示提取失败（索引越界等）
    std::optional<Vec3d> extractVector(const PointConfig& cfg,
                                        const QList<double>& data) const;

    // 根据 PointConfig 提取四元数 [w,x,y,z]（可选）
    std::optional<Quat4d> extractQuaternion(const PointConfig& cfg,
                                             const QList<double>& data) const;

    // 根据 PointConfig 提取气压计海拔值（可选）
    std::optional<double> extractBarometer(const PointConfig& cfg,
                                            const QList<double>& data) const;

    // ── 状态变量 ──────────────────────────────────────────────

    Vec3d  m_velocity = {0, 0, 0};  // 当前速度向量（m/s）
    Vec3d  m_position = {0, 0, 0};  // 当前位移向量（m）
    bool   m_initialized = false;    // 是否已完成首帧姿态初始化

    Quat4d m_q         = {1,0,0,0}; // 主四元数（用于重力剥离）
    Quat4d m_qMadgwick = {1,0,0,0}; // Madgwick 专用四元数
    Quat4d m_qMahony   = {1,0,0,0}; // Mahony 专用四元数
    Vec3d  m_mahonyIntegralFb = {0,0,0}; // Mahony 积分反馈

    // 偏航角修正四元数（在 reset() 中由配置预计算）
    Quat4d m_qYawCorr = {1,0,0,0};

    // INS 算法参数（由配置加载）
    double m_beta        = 0.05;  // Madgwick beta
    double m_mahonyKp    = 1.0;   // Mahony 比例增益
    double m_mahonyKi    = 0.0;   // Mahony 积分增益
    double m_gravity     = 9.80;  // 重力参考值（m/s²）
    bool   m_kfEnabled   = true;  // 是否启用卡尔曼滤波
    bool   m_zuptEnabled = true;  // 是否启用 ZUPT

    // 滤波器对象
    VerticalKalmanFilter m_vkf;     // 垂直通道卡尔曼滤波器
    ZUPTDetector         m_zupt;    // 零速检测器
    LowPassFilter        m_baroLpf; // 气压低通滤波器

    // 气压参考值（首次有效气压值）
    std::optional<double> m_baroRef;

    // 时间戳（用于计算 dt）
    qint64 m_lastTimestamp = 0; // 纳秒时间戳

    // 帧计数（用于节流日志输出）
    int m_frameCount = 0;
};
