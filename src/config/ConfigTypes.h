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

    // 与 QSerialPort::DataBits 枚举值一一对应（5/6/7/8）
    int     dataBits    = 8;
    // 与 QSerialPort::Parity 枚举值一一对应：0=No 2=Even 3=Odd 4=Mark 5=Space
    int     parity      = 0;
    // 与 QSerialPort::StopBits 枚举值一一对应：1=One 2=Two 3=OneAndHalf
    int     stopBits    = 1;
    // 与 QSerialPort::FlowControl 枚举值一一对应：0=No 1=Hardware 2=Software
    int     flowControl = 0;
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
    int    index      = 0;   // 数据数组下标（可由 field 在加载时解析得到）
    double multiplier = 1.0; // 换算乘数
    QString field;           // 协议字段名引用（可选，如 "ax"），加载时解析为 index
};

// 合法的 purpose 值集合
namespace PointPurpose {
    inline constexpr const char* Accelerometer = "accelerometer";
    inline constexpr const char* Gyroscope     = "gyroscope";
    inline constexpr const char* Quaternion     = "quaternion";
    inline constexpr const char* MagneticField  = "magnetic_field";
    inline constexpr const char* Barometer      = "barometer";
    // purpose 为空字符串表示纯可视化点

    inline bool isValid(const QString& p) {
        return p.isEmpty()
            || p == Accelerometer || p == Gyroscope
            || p == Quaternion    || p == MagneticField
            || p == Barometer;
    }

    inline bool isSensor(const QString& p) {
        return !p.isEmpty() && isValid(p);
    }
}

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

    // 判断此 point 是否匹配给定的数据来源与前缀
    bool matchesSource(const QString& src, const QString& pfx) const {
        if (source != "any" && source != src) return false;
        QString cfgPrefix = prefix.value_or(QString());
        return cfgPrefix == pfx;
    }

    // 提取 XYZ 轴所需的最大 data 索引 +1（即 data.size() 下界）
    int requiredSizeXYZ() const {
        return std::max({x.index, y.index, z.index}) + 1;
    }

    // 提取四元数 WXYZ 所需的最小 data 长度
    int requiredSizeQuat() const {
        return std::max({w.index, x.index, y.index, z.index}) + 1;
    }

    // 提取气压计所需的最小 data 长度
    int requiredSizeBaro() const {
        return std::max(altitude.index, pressure.index) + 1;
    }
};
