#include "PoseProcessor.h"
#include <QElapsedTimer>
#include <QDateTime>
#include <cmath>
#include <optional>

// ============================================================
// PoseProcessor.cpp — 姿态与惯性导航处理器实现
// ============================================================

PoseProcessor::PoseProcessor(QObject* parent)
    : QObject(parent)
{
    reset();
}

// 重置所有状态：速度、位置、四元数、滤波器
void PoseProcessor::reset() {
    m_velocity    = {0, 0, 0};
    m_position    = {0, 0, 0};
    m_initialized = false;
    m_q           = {1, 0, 0, 0};
    m_qMadgwick   = {1, 0, 0, 0};
    m_qMahony     = {1, 0, 0, 0};
    m_mahonyIntegralFb = {0, 0, 0};
    m_lastTimestamp = 0;
    m_frameCount    = 0;
    m_baroRef.reset();

    // 从配置重新加载参数
    ConfigLoader& cfg = ConfigLoader::instance();
    m_gravity = cfg.gravityReference();

    InsConfig ins = cfg.getInsConfig();
    m_beta     = ins.madgwick.beta;
    m_mahonyKp = ins.mahony.kp;
    m_mahonyKi = ins.mahony.ki;
    m_kfEnabled  = ins.kalman.enabled;
    m_zuptEnabled= ins.zupt.enabled;

    // 预计算偏航角修正四元数（绕 Z 轴旋转 yawOffsetDeg）
    double halfYaw = (ins.filterYawOffsetDeg * M_PI / 180.0) / 2.0;
    m_qYawCorr = { std::cos(halfYaw), 0.0, 0.0, std::sin(halfYaw) };

    // 重新初始化滤波器
    m_vkf = VerticalKalmanFilter(ins.kalman.measurementNoiseR,
                                   ins.kalman.processNoiseSigma);
    m_zupt = ZUPTDetector(ins.zupt.accVarianceThreshold,
                           ins.zupt.gyroVarianceThreshold,
                           ins.zupt.windowSize);
    m_baroLpf = LowPassFilter(ins.baroLpfAlpha);
}

// ── 数据提取辅助 ─────────────────────────────────────────────

// 从数据数组中按配置映射提取 XYZ 三维向量
std::optional<Vec3d> PoseProcessor::extractVector(const PointConfig& cfg,
                                                    const QList<double>& data) const {
    int maxIdx = std::max({cfg.x.index, cfg.y.index, cfg.z.index});
    if (data.size() <= maxIdx) return std::nullopt;
    return Vec3d{
        data[cfg.x.index] * cfg.x.multiplier,
        data[cfg.y.index] * cfg.y.multiplier,
        data[cfg.z.index] * cfg.z.multiplier,
    };
}

// 从数据数组中按配置映射提取四元数 [w,x,y,z]
std::optional<Quat4d> PoseProcessor::extractQuaternion(const PointConfig& cfg,
                                                         const QList<double>& data) const {
    int maxIdx = std::max({cfg.w.index, cfg.x.index, cfg.y.index, cfg.z.index});
    if (data.size() <= maxIdx) return std::nullopt;
    Quat4d q = {
        data[cfg.w.index] * cfg.w.multiplier,
        data[cfg.x.index] * cfg.x.multiplier,
        data[cfg.y.index] * cfg.y.multiplier,
        data[cfg.z.index] * cfg.z.multiplier,
    };
    // 归一化
    double n = std::sqrt(q[0]*q[0]+q[1]*q[1]+q[2]*q[2]+q[3]*q[3]);
    if (n > 1e-12) { q[0]/=n; q[1]/=n; q[2]/=n; q[3]/=n; }
    return q;
}

// 从数据数组中提取气压计海拔值（仅当非零时返回）
std::optional<double> PoseProcessor::extractBarometer(const PointConfig& cfg,
                                                        const QList<double>& data) const {
    if (data.size() <= cfg.altitude.index) return std::nullopt;
    double val = data[cfg.altitude.index] * cfg.altitude.multiplier;
    if (val == 0.0) return std::nullopt;
    return val;
}

// ── 主处理入口 ───────────────────────────────────────────────

// 处理一帧传感器数据，执行完整 INS 管线
void PoseProcessor::process(const QString& source, const QString& prefix,
                              const QList<double>& data) {
    QList<PointConfig> pointsCfg = ConfigLoader::instance().getPoints();

    // 从配置点中提取各传感器分量
    std::optional<Vec3d>  accVec, gyrVec, magVec;
    std::optional<Quat4d> quatVec;
    std::optional<double> baroAlt;

    int matchedPoints = 0;
    for (const PointConfig& p : pointsCfg) {
        // 过滤数据来源（any 匹配所有来源）
        if (p.source != "any" && p.source != source) continue;

        // 过滤前缀（nullopt 或空字符串表示匹配无前缀数据）
        QString cfgPrefix = p.prefix.value_or(QString());
        if (cfgPrefix != prefix) continue;

        ++matchedPoints;

        if (p.purpose == "accelerometer")  accVec  = extractVector(p, data);
        else if (p.purpose == "gyroscope")      gyrVec  = extractVector(p, data);
        else if (p.purpose == "magnetic_field") magVec  = extractVector(p, data);
        else if (p.purpose == "quaternion")     quatVec = extractQuaternion(p, data);
        else if (p.purpose == "barometer")      baroAlt = extractBarometer(p, data);
    }

    // 无加速度数据时不处理（记录首帧警告）
    if (!accVec.has_value()) {
        if (m_frameCount == 0) {
            emit logMessage(QString("Warning: 收到来自 %1 的数据但未能提取加速度向量 (len=%2)")
                            .arg(source).arg(data.size()));
            if (matchedPoints == 0) {
                emit logMessage(QString("  -> 无配置点匹配 source='%1' prefix='%2'")
                                .arg(source).arg(prefix));
            }
        }
        return;
    }

    // ── 计算时间步长 dt ────────────────────────────────────────
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    double dt  = 0.01; // 首帧默认 dt（10ms）
    if (m_lastTimestamp > 0) {
        dt = (now - m_lastTimestamp) / 1000.0;
        if (dt > 0.1) dt = 0.1; // 限制最大 dt 防止大步长发散
    }
    m_lastTimestamp = now;
    ++m_frameCount;

    const Vec3d& acc = accVec.value();
    double accMag = std::sqrt(acc[0]*acc[0] + acc[1]*acc[1] + acc[2]*acc[2]);

    // ── 首帧姿态初始化（仅当加速度接近重力参考值时）─────────────
    bool hasGravity = std::abs(accMag - m_gravity) < 2.0 || accMag > 2.0;

    if (hasGravity && !m_initialized && !quatVec.has_value()) {
        Vec3d* magPtr = magVec.has_value() ? &magVec.value() : nullptr;
        auto [q, roll, pitch, yaw] = MathUtils::initializeOrientation(acc, magPtr);
        m_q = m_qMadgwick = m_qMahony = q;
        m_mahonyIntegralFb = {0, 0, 0};
        m_initialized = true;
        emit logMessage(QString("已初始化姿态 Roll=%1 Pitch=%2 Yaw=%3")
                        .arg(roll, 0, 'f', 1).arg(pitch, 0, 'f', 1).arg(yaw, 0, 'f', 1));
        return;
    }

    Vec3d linearAcc = acc; // 默认为原始加速度（未剥离重力）

    bool shouldLog = (m_frameCount % 10 == 0);
    QStringList debugMsgs;
    if (shouldLog) {
        debugMsgs << QString("DT:%1ms ACC:[%2,%3,%4] |a|=%5")
                       .arg(dt*1000, 0, 'f', 1)
                       .arg(acc[0],0,'f',2).arg(acc[1],0,'f',2).arg(acc[2],0,'f',2)
                       .arg(accMag, 0, 'f', 2);
    }

    if (hasGravity) {
        // ── AHRS 更新 ──────────────────────────────────────────
        if (quatVec.has_value()) {
            // 直接使用传感器模块提供的四元数
            m_q = quatVec.value();
            m_initialized = true;
            if (shouldLog) debugMsgs << "Q: 模块直出";
        } else if (gyrVec.has_value()) {
            // 使用 Madgwick/Mahony 计算主四元数
            const Vec3d& gyr = gyrVec.value();
            if (magVec.has_value()) {
                m_q = Ahrs::madgwickUpdate9dof(m_q, gyr, acc, magVec.value(), dt, m_beta);
            } else {
                m_q = Ahrs::madgwickUpdate6dof(m_q, gyr, acc, dt, m_beta);
            }
        }

        // 同时运行独立 Madgwick 和 Mahony 用于姿态显示
        if (gyrVec.has_value()) {
            const Vec3d& gyr = gyrVec.value();
            if (magVec.has_value()) {
                m_qMadgwick = Ahrs::madgwickUpdate9dof(m_qMadgwick, gyr, acc, magVec.value(), dt, m_beta);
                m_qMahony   = Ahrs::mahonyUpdate9dof(m_qMahony, gyr, acc, magVec.value(), dt,
                                                      m_mahonyKp, m_mahonyKi, &m_mahonyIntegralFb);
            } else {
                m_qMadgwick = Ahrs::madgwickUpdate6dof(m_qMadgwick, gyr, acc, dt, m_beta);
                m_qMahony   = Ahrs::mahonyUpdate6dof(m_qMahony, gyr, acc, dt,
                                                      m_mahonyKp, m_mahonyKi, &m_mahonyIntegralFb);
            }
        }

        // 将加速度旋转到世界坐标系，剥离重力
        Vec3d accEarth = MathUtils::rotateVector(acc, m_q);
        linearAcc = { accEarth[0], accEarth[1], accEarth[2] - m_gravity };

        if (shouldLog) {
            debugMsgs << QString("Q:[%1,%2,%3,%4]")
                           .arg(m_q[0],0,'f',2).arg(m_q[1],0,'f',2)
                           .arg(m_q[2],0,'f',2).arg(m_q[3],0,'f',2);
            debugMsgs << QString("AccEarth:[%1,%2,%3]")
                           .arg(accEarth[0],0,'f',2).arg(accEarth[1],0,'f',2)
                           .arg(accEarth[2],0,'f',2);
        }
    }

    // 发射已解析的传感器数据
    QList<double> linAccList = {linearAcc[0], linearAcc[1], linearAcc[2]};
    QList<double> gyrList, magList;
    if (gyrVec.has_value()) for (double v : gyrVec.value()) gyrList << v;
    if (magVec.has_value()) for (double v : magVec.value()) magList << v;
    emit parsedDataUpdated(source, prefix, linAccList, gyrList, magList);

    // ── ZUPT 零速检测 ─────────────────────────────────────────
    bool isStationary = false;
    if (m_zuptEnabled && gyrVec.has_value()) {
        double accNormSq = acc[0]*acc[0] + acc[1]*acc[1] + acc[2]*acc[2];
        const Vec3d& gyr = gyrVec.value();
        double gyroNorm  = std::sqrt(gyr[0]*gyr[0] + gyr[1]*gyr[1] + gyr[2]*gyr[2]);
        isStationary = m_zupt.update(accNormSq, gyroNorm);
    }

    // ── 垂直通道卡尔曼滤波 ────────────────────────────────────
    if (m_kfEnabled) {
        m_vkf.predict(dt, linearAcc[2]);

        if (baroAlt.has_value()) {
            double baroFiltered = m_baroLpf.update(baroAlt.value());
            if (!m_baroRef.has_value()) m_baroRef = baroFiltered;
            double deltaH = baroFiltered - m_baroRef.value();
            m_vkf.update(deltaH);
        }

        if (isStationary) m_vkf.applyZupt();

        m_position[2] = m_vkf.height();
        m_velocity[2] = m_vkf.velocity();
    } else {
        m_velocity[2] += linearAcc[2] * dt;
        m_position[2] += m_velocity[2] * dt;
    }

    // ── 水平通道积分 + ZUPT ────────────────────────────────────
    m_velocity[0] += linearAcc[0] * dt;
    m_velocity[1] += linearAcc[1] * dt;

    if (isStationary) {
        m_velocity[0] = 0.0;
        m_velocity[1] = 0.0;
        if (!m_kfEnabled) m_velocity[2] = 0.0;
        if (shouldLog) debugMsgs << "ZUPT: 静止检测，速度清零";
    }

    m_position[0] += m_velocity[0] * dt;
    m_position[1] += m_velocity[1] * dt;

    if (shouldLog) {
        debugMsgs << QString("Vel:[%1,%2,%3]")
                       .arg(m_velocity[0],0,'f',2).arg(m_velocity[1],0,'f',2).arg(m_velocity[2],0,'f',2);
        debugMsgs << QString("Pos:[%1,%2,%3]")
                       .arg(m_position[0],0,'f',2).arg(m_position[1],0,'f',2).arg(m_position[2],0,'f',2);
        emit logMessage(debugMsgs.join(" | "));
    }

    // ── 发射信号 ──────────────────────────────────────────────

    emit velocityUpdated(m_velocity[0], m_velocity[1], m_velocity[2]);
    emit positionUpdated("Displacement Path",
                          m_position[0], m_position[1], m_position[2]);

    // 发射 Madgwick/Mahony 四元数（施加偏航修正后）
    Quat4d qMw = MathUtils::quatMultiply(m_qYawCorr, m_qMadgwick);
    Quat4d qMh = MathUtils::quatMultiply(m_qYawCorr, m_qMahony);
    QList<double> madgwickList = {qMw[0], qMw[1], qMw[2], qMw[3]};
    QList<double> mahonyList   = {qMh[0], qMh[1], qMh[2], qMh[3]};
    emit filterQuaternionsUpdated(madgwickList, mahonyList);
}
