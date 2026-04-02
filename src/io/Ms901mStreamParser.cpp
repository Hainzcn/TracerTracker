#include "Ms901mStreamParser.h"
#include <cmath>
#include <QString>

// ============================================================
// Ms901mStreamParser.cpp — ATK-MS901M 流式二进制解析器实现
// ============================================================

// 构造函数：初始化量程参数
Ms901mStreamParser::Ms901mStreamParser(int accFsr, int gyroFsr)
    : m_accFsr(accFsr), m_gyroFsr(gyroFsr)
{}

// 重置缓冲区和状态
void Ms901mStreamParser::reset() {
    m_buffer.clear();
    m_hasAttitude = m_hasQuaternion = m_hasGyroAcc = m_hasMagTemp = m_hasBaroAlt = false;
}

// 小端有符号 16 位整数转换（低字节在前）
int16_t Ms901mStreamParser::toInt16(uint8_t low, uint8_t high) {
    return static_cast<int16_t>((static_cast<uint16_t>(high) << 8) | low);
}

// 小端有符号 32 位整数转换（低字节在前）
int32_t Ms901mStreamParser::toInt32(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3) {
    return static_cast<int32_t>(
        (static_cast<uint32_t>(b3) << 24) |
        (static_cast<uint32_t>(b2) << 16) |
        (static_cast<uint32_t>(b1) <<  8) |
        static_cast<uint32_t>(b0)
    );
}

// 将字节追加到缓冲区，尝试解析所有完整帧
// 每个 0x03 帧触发一次快照合并
QList<Snapshot19> Ms901mStreamParser::feed(const QByteArray& data) {
    m_buffer.append(data);
    QList<Snapshot19> snapshots;

    while (true) {
        uint8_t   frameId;
        QByteArray payload;
        if (!tryExtractFrame(frameId, payload)) break;

        bool triggered = parseFrame(frameId, payload);
        if (triggered && m_hasGyroAcc) {
            snapshots.append(buildSnapshot());
        }
    }

    // 防止缓冲区无限增长（保留最后 2048 字节）
    if (m_buffer.size() > 4096) {
        m_buffer = m_buffer.right(2048);
    }

    return snapshots;
}

// 从缓冲区头部提取一个完整有效帧
// 自动跳过无效字节直到找到帧头 0x55 0x55
bool Ms901mStreamParser::tryExtractFrame(uint8_t& frameId, QByteArray& payload) {
    while (true) {
        // 在缓冲区中寻找帧头 0x55 0x55
        int idx = -1;
        for (int i = 0; i + 1 < m_buffer.size(); ++i) {
            if ((uint8_t)m_buffer[i] == 0x55 && (uint8_t)m_buffer[i+1] == 0x55) {
                idx = i;
                break;
            }
        }

        if (idx < 0) {
            // 未找到帧头，保留最后 1 字节（可能是不完整帧头）
            if (m_buffer.size() > 1)
                m_buffer = m_buffer.right(1);
            return false;
        }

        // 丢弃帧头前的垃圾字节
        if (idx > 0) m_buffer = m_buffer.mid(idx);

        // 需要至少 4 字节（帧头2 + ID1 + LEN1）
        if (m_buffer.size() < 4) return false;

        uint8_t fid    = (uint8_t)m_buffer[2];
        uint8_t dataLen= (uint8_t)m_buffer[3];
        int     total  = 4 + dataLen + 1; // 帧头2 + ID1 + LEN1 + DATA + CHECKSUM1

        // 数据长度异常（>64）跳过当前帧头
        if (dataLen > 64) {
            m_buffer = m_buffer.mid(2);
            continue;
        }

        // 缓冲区数据不足，等待更多数据
        if (m_buffer.size() < total) return false;

        // 校验和验证：sum(帧头到DATA末尾) & 0xFF
        uint8_t sum = 0;
        for (int i = 0; i < total - 1; ++i)
            sum += (uint8_t)m_buffer[i];

        uint8_t expected = sum;
        uint8_t actual   = (uint8_t)m_buffer[total - 1];

        if (expected != actual) {
            // 校验失败：丢弃 2 个字节（跳过当前帧头），继续搜索
            m_buffer = m_buffer.mid(2);
            continue;
        }

        // 提取载荷（data 段）
        frameId = fid;
        payload = m_buffer.mid(4, dataLen);
        m_buffer = m_buffer.mid(total); // 消费整帧
        return true;
    }
}

// 根据帧 ID 派发解析，0x03 帧返回 true（触发快照）
bool Ms901mStreamParser::parseFrame(uint8_t frameId, const QByteArray& payload) {
    switch (frameId) {
        case 0x01: parseAttitude(payload);   return false;
        case 0x02: parseQuaternion(payload); return false;
        case 0x03: parseGyroAcc(payload);    return true;  // 触发快照
        case 0x04: parseMagTemp(payload);    return false;
        case 0x05: parseBaroAlt(payload);    return false;
        default:                             return false;
    }
}

// 解析姿态角帧（ID=0x01，6 字节：roll/pitch/yaw 各 2 字节小端有符号）
bool Ms901mStreamParser::parseAttitude(const QByteArray& d) {
    if (d.size() != 6) return false;
    m_attitude.roll  = toInt16((uint8_t)d[0],(uint8_t)d[1]) / 32768.0 * 180.0;
    m_attitude.pitch = toInt16((uint8_t)d[2],(uint8_t)d[3]) / 32768.0 * 180.0;
    m_attitude.yaw   = toInt16((uint8_t)d[4],(uint8_t)d[5]) / 32768.0 * 180.0;
    m_hasAttitude = true;
    return true;
}

// 解析四元数帧（ID=0x02，8 字节：q0/q1/q2/q3 各 2 字节小端有符号）
bool Ms901mStreamParser::parseQuaternion(const QByteArray& d) {
    if (d.size() != 8) return false;
    m_quaternion.q0 = toInt16((uint8_t)d[0],(uint8_t)d[1]) / 32768.0;
    m_quaternion.q1 = toInt16((uint8_t)d[2],(uint8_t)d[3]) / 32768.0;
    m_quaternion.q2 = toInt16((uint8_t)d[4],(uint8_t)d[5]) / 32768.0;
    m_quaternion.q3 = toInt16((uint8_t)d[6],(uint8_t)d[7]) / 32768.0;
    m_hasQuaternion = true;
    return true;
}

// 解析陀螺仪+加速度计帧（ID=0x03，12 字节）
// 加速度：m/s²  = int16 / 32768 * accFsr * 9.8
// 陀螺仪：rad/s = int16 / 32768 * gyroFsr * (π/180)
bool Ms901mStreamParser::parseGyroAcc(const QByteArray& d) {
    if (d.size() != 12) return false;
    double fa = m_accFsr, fg = m_gyroFsr;
    constexpr double DEG2RAD = M_PI / 180.0;
    m_gyroAcc.ax = toInt16((uint8_t)d[0], (uint8_t)d[1])  / 32768.0 * fa * 9.8;
    m_gyroAcc.ay = toInt16((uint8_t)d[2], (uint8_t)d[3])  / 32768.0 * fa * 9.8;
    m_gyroAcc.az = toInt16((uint8_t)d[4], (uint8_t)d[5])  / 32768.0 * fa * 9.8;
    m_gyroAcc.gx = toInt16((uint8_t)d[6], (uint8_t)d[7])  / 32768.0 * fg * DEG2RAD;
    m_gyroAcc.gy = toInt16((uint8_t)d[8], (uint8_t)d[9])  / 32768.0 * fg * DEG2RAD;
    m_gyroAcc.gz = toInt16((uint8_t)d[10],(uint8_t)d[11]) / 32768.0 * fg * DEG2RAD;
    m_hasGyroAcc = true;
    return true;
}

// 解析磁力计+温度帧（ID=0x04，8 字节）
// 磁力计：原始整数值；温度：int16 / 100 (°C)
bool Ms901mStreamParser::parseMagTemp(const QByteArray& d) {
    if (d.size() != 8) return false;
    m_magTemp.mx   = static_cast<double>(toInt16((uint8_t)d[0],(uint8_t)d[1]));
    m_magTemp.my   = static_cast<double>(toInt16((uint8_t)d[2],(uint8_t)d[3]));
    m_magTemp.mz   = static_cast<double>(toInt16((uint8_t)d[4],(uint8_t)d[5]));
    m_magTemp.temp = toInt16((uint8_t)d[6],(uint8_t)d[7]) / 100.0;
    m_hasMagTemp = true;
    return true;
}

// 解析气压计+海拔帧（ID=0x05，10 字节）
// 气压：int32 Pa；海拔：int32 / 100 m；温度（忽略）
bool Ms901mStreamParser::parseBaroAlt(const QByteArray& d) {
    if (d.size() != 10) return false;
    m_baroAlt.pressure = static_cast<double>(
        toInt32((uint8_t)d[0],(uint8_t)d[1],(uint8_t)d[2],(uint8_t)d[3]));
    m_baroAlt.altitude = toInt32((uint8_t)d[4],(uint8_t)d[5],
                                  (uint8_t)d[6],(uint8_t)d[7]) / 100.0;
    m_hasBaroAlt = true;
    return true;
}

// 将各帧最新数据合并为 19 元素快照
// 至少需要 0x03（陀螺仪+加速度）帧才能构建快照
Snapshot19 Ms901mStreamParser::buildSnapshot() const {
    Snapshot19 s{};
    // [0-2] 加速度（m/s²）
    s[0] = m_gyroAcc.ax; s[1] = m_gyroAcc.ay; s[2] = m_gyroAcc.az;
    // [3-5] 陀螺仪（rad/s）
    s[3] = m_gyroAcc.gx; s[4] = m_gyroAcc.gy; s[5] = m_gyroAcc.gz;
    // [6-9] 四元数（[w,x,y,z]）
    s[6]  = m_hasQuaternion ? m_quaternion.q0 : 1.0;
    s[7]  = m_hasQuaternion ? m_quaternion.q1 : 0.0;
    s[8]  = m_hasQuaternion ? m_quaternion.q2 : 0.0;
    s[9]  = m_hasQuaternion ? m_quaternion.q3 : 0.0;
    // [10-12] 磁力计（原始值）
    s[10] = m_hasMagTemp ? m_magTemp.mx   : 0.0;
    s[11] = m_hasMagTemp ? m_magTemp.my   : 0.0;
    s[12] = m_hasMagTemp ? m_magTemp.mz   : 0.0;
    // [13] 温度（°C）
    s[13] = m_hasMagTemp ? m_magTemp.temp : 0.0;
    // [14-16] 欧拉角（°）
    s[14] = m_hasAttitude ? m_attitude.roll  : 0.0;
    s[15] = m_hasAttitude ? m_attitude.pitch : 0.0;
    s[16] = m_hasAttitude ? m_attitude.yaw   : 0.0;
    // [17] 气压（Pa）
    s[17] = m_hasBaroAlt ? m_baroAlt.pressure : 0.0;
    // [18] 海拔（m）
    s[18] = m_hasBaroAlt ? m_baroAlt.altitude : 0.0;
    return s;
}

// 将快照格式化为调试字符串
QString Ms901mStreamParser::formatDebug(const Snapshot19& s) {
    return QString("ACC(%1,%2,%3) GYR(%4,%5,%6) Q(%7,%8,%9,%10) "
                   "MAG(%11,%12,%13) T=%14C RPY(%15,%16,%17) P=%18Pa Alt=%19m")
        .arg(s[0],0,'f',2).arg(s[1],0,'f',2).arg(s[2],0,'f',2)
        .arg(s[3],0,'f',2).arg(s[4],0,'f',2).arg(s[5],0,'f',2)
        .arg(s[6],0,'f',3).arg(s[7],0,'f',3).arg(s[8],0,'f',3).arg(s[9],0,'f',3)
        .arg(s[10],0,'f',0).arg(s[11],0,'f',0).arg(s[12],0,'f',0)
        .arg(s[13],0,'f',1)
        .arg(s[14],0,'f',1).arg(s[15],0,'f',1).arg(s[16],0,'f',1)
        .arg(s[17],0,'f',0).arg(s[18],0,'f',2);
}
