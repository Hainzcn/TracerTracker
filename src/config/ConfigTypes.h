#pragma once
#include <QString>
#include <QList>
#include <QColor>
#include <optional>

// ============================================================
// ConfigTypes.h — 配置数据结构定义
// 对应 Python 版 config.json 各节的 C++ 结构体
// ============================================================

// UDP 接收配置
struct UdpConfig {
    bool    enabled  = true;         // 是否启用 UDP 接收
    QString ip       = "127.0.0.1"; // 绑定 IP 地址
    int     port     = 8888;         // 绑定端口号
};

// 串口接收配置
struct SerialConfig {
    bool    enabled  = true;         // 是否启用串口接收
    QString port     = "COM5";       // 串口名称（如 COM3）
    int     baudrate = 115200;       // 波特率
    int     timeout  = 1;            // 超时秒数
    QString protocol = "atkms901m";  // 协议类型：csv 或 atkms901m
    int     accFsr   = 4;            // 加速度计满量程（G）
    int     gyroFsr  = 2000;         // 陀螺仪满量程（°/s）
};

// 渲染调试配置
struct RenderDebugConfig {
    bool enabled             = false; // 是否启用渲染调试输出
    bool verbosePointUpdates = false; // 是否输出点更新详细日志
};

// 卡尔曼滤波器配置
struct KalmanConfig {
    bool   enabled           = true; // 是否启用卡尔曼滤波
    double processNoiseSigma = 0.5;  // 过程噪声标准差 sigma_a
    double measurementNoiseR = 0.5;  // 测量噪声方差 R
};

// 零速检测（ZUPT）配置
struct ZuptConfig {
    bool   enabled              = true; // 是否启用 ZUPT
    double accVarianceThreshold = 0.5;  // 加速度方差阈值
    double gyroVarianceThreshold = 0.1; // 陀螺仪方差阈值
    int    windowSize           = 40;   // 滑动窗口大小（帧数）
};

// Madgwick 滤波器配置
struct MadgwickConfig {
    double beta = 0.05; // 梯度下降步长增益
};

// Mahony 滤波器配置
struct MahonyConfig {
    double kp = 1.0; // 比例增益
    double ki = 0.0; // 积分增益
};

// INS（惯性导航系统）总配置
struct InsConfig {
    KalmanConfig  kalman;
    ZuptConfig    zupt;
    MadgwickConfig madgwick;
    MahonyConfig  mahony;
    double baroLpfAlpha      = 0.1;  // 气压低通滤波系数
    double filterYawOffsetDeg = 90.0; // 偏航角修正偏移（度）
};

// 轴向分量索引与乘数
struct AxisMapping {
    int    index      = 0;   // 数据数组下标
    double multiplier = 1.0; // 换算乘数
};

// 单个数据点（point）配置条目
struct PointConfig {
    QString                  name;            // 点名称（唯一标识）
    QString                  source = "any";  // 数据来源："serial"/"udp"/"any"
    std::optional<QString>   prefix;          // CSV 前缀（空表示无前缀匹配）
    QString                  purpose;         // 用途：accelerometer/gyroscope/magnetic_field/quaternion/barometer/（空=可视化点）
    AxisMapping              x, y, z;         // XYZ 轴映射
    AxisMapping              w;               // 四元数 W 分量（仅 purpose=quaternion 时使用）
    AxisMapping              altitude;        // 气压计海拔映射（仅 purpose=barometer 时使用）
    AxisMapping              pressure;        // 气压计气压映射（仅 purpose=barometer 时使用）
    QColor                   color = QColor(255, 0, 0, 255); // 点颜色
    int                      size  = 10;      // 点大小（像素）
};
