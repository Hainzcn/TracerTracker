#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>
#include <QVariantMap>

// ============================================================
// GenericFrameParser.h — 配置驱动的通用二进制协议流式解析器
//
// 通过 JSON 协议定义文件驱动：分帧、字段解码、多帧合并。
// 设计目标：替代硬编码的 Ms901mStreamParser，使新增传感器
// 只需编写 JSON 而无需修改 C++ 代码。
//
// JSON 协议定义结构（简述）：
//   framing   — 分帧参数（sync 字节、ID/长度偏移、校验）
//   frames[]  — 各帧类型的字段定义（offset, type, scale）
//   snapshot_order[] — 最终 QList<double> 的字段排列
// ============================================================

class GenericFrameParser {
public:
    GenericFrameParser() = default;

    // 从 JSON 协议定义构造解析器
    // variableOverrides 用于覆盖协议默认变量（如 acc_fsr, gyro_fsr）
    static GenericFrameParser fromJson(const QJsonObject& protocolDef,
                                       const QVariantMap& variableOverrides = {});

    // 流式输入字节，返回本次调用产生的快照列表
    QList<QList<double>> feed(const QByteArray& data);

    // 字段名 → snapshot 数组下标（-1 表示未找到）
    int fieldIndex(const QString& fieldName) const;

    // 按 snapshot_order 顺序返回所有字段名
    QStringList fieldNames() const;

    // 将快照格式化为调试字符串
    QString formatDebug(const QList<double>& snapshot) const;

    // 重置缓冲区和帧状态
    void reset();

    bool isValid() const { return m_valid; }

private:
    // ── 字段数据类型 ──
    enum class FieldType {
        Int16LE, Int16BE,
        UInt16LE, UInt16BE,
        Int32LE, Int32BE,
        UInt32LE, UInt32BE,
        Float32LE, Float64LE,
    };

    // ── 校验类型 ──
    enum class ChecksumType { None, Sum8, XOR, CRC16 };

    // ── 单个字段定义 ──
    struct FieldDef {
        QString   name;
        int       offset = 0;
        FieldType type   = FieldType::Int16LE;
        double    scale  = 1.0;
    };

    // ── 单个帧类型定义 ──
    struct FrameDef {
        int              id      = 0;
        QString          name;
        bool             trigger = false;
        int              expectedLength = -1;
        QList<FieldDef>  fields;
    };

    // ── 分帧配置 ──
    struct FramingConfig {
        QByteArray   syncBytes;
        int          idOffset          = 2;
        int          idBytes           = 1;
        int          lengthOffset      = 3;
        int          lengthBytes       = 1;
        bool         lengthIncludesHeader = false;
        int          maxPayload        = 64;
        ChecksumType checksumType      = ChecksumType::Sum8;
    };

    bool m_valid = false;

    FramingConfig           m_framing;
    QList<FrameDef>         m_frames;
    QMap<int, int>          m_frameIdToIdx;
    QStringList             m_snapshotOrder;
    QMap<QString, int>      m_fieldIndexMap;

    // ── 流式缓冲区 ──
    QByteArray              m_buffer;
    int                     m_parseOffset = 0;
    QMap<QString, double>   m_latestValues;

    // ── 内部方法 ──
    bool tryExtractFrame(int& frameId, QByteArray& payload);
    void decodeFrame(const FrameDef& frame, const QByteArray& payload);
    QList<double> buildSnapshot() const;

    static double decodeFieldValue(const QByteArray& payload, const FieldDef& field);
    static FieldType parseFieldType(const QString& s);
    static ChecksumType parseChecksumType(const QString& s);

    // ── Scale 表达式求值 ──
    static double evalScaleExpr(const QJsonValue& scaleVal,
                                const QMap<QString, double>& vars);
    static double evalExprString(const QString& expr,
                                 const QMap<QString, double>& vars);
};
