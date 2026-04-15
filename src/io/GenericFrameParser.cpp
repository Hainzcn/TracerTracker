#include "GenericFrameParser.h"
#include <QJsonArray>
#include <QJsonValue>
#include <QDebug>
#include <cmath>
#include <cstring>
#include <algorithm>

// ============================================================
// GenericFrameParser.cpp — 通用二进制协议解析器实现
// ============================================================

// ── Scale 表达式求值 ─────────────────────────────────────────

// 将 "$var" 替换为变量值，然后求值简单算术表达式 (+-*/)
// 支持格式：纯数字、"$acc_fsr * 9.8 / 32768"、带括号表达式
double GenericFrameParser::evalScaleExpr(const QJsonValue& scaleVal,
                                          const QMap<QString, double>& vars)
{
    if (scaleVal.isDouble()) return scaleVal.toDouble(1.0);
    if (!scaleVal.isString()) return 1.0;
    return evalExprString(scaleVal.toString(), vars);
}

// 简单的递归下降表达式求值器
// 语法: expr = term (('+' | '-') term)*
//        term = factor (('*' | '/') factor)*
//        factor = NUMBER | '$' IDENT | '(' expr ')' | unary_minus factor
double GenericFrameParser::evalExprString(const QString& expr,
                                           const QMap<QString, double>& vars)
{
    struct Parser {
        const QString& s;
        int pos = 0;
        const QMap<QString, double>& vars;

        void skipSpaces() { while (pos < s.size() && s[pos].isSpace()) ++pos; }

        double parseNumber() {
            skipSpaces();
            int start = pos;
            if (pos < s.size() && (s[pos] == '-' || s[pos] == '+')) ++pos;
            while (pos < s.size() && (s[pos].isDigit() || s[pos] == '.')) ++pos;
            if (pos == start) return 0.0;
            bool ok;
            double val = s.mid(start, pos - start).toDouble(&ok);
            return ok ? val : 0.0;
        }

        double parseFactor() {
            skipSpaces();
            if (pos >= s.size()) return 0.0;

            if (s[pos] == '(') {
                ++pos;
                double val = parseExpr();
                skipSpaces();
                if (pos < s.size() && s[pos] == ')') ++pos;
                return val;
            }

            if (s[pos] == '$') {
                ++pos;
                int start = pos;
                while (pos < s.size() && (s[pos].isLetterOrNumber() || s[pos] == '_'))
                    ++pos;
                QString varName = s.mid(start, pos - start);
                if (vars.contains(varName)) return vars[varName];
                qWarning() << "GenericFrameParser: 未知变量 $" + varName;
                return 0.0;
            }

            if (s[pos] == '-') {
                ++pos;
                return -parseFactor();
            }

            return parseNumber();
        }

        double parseTerm() {
            double val = parseFactor();
            while (true) {
                skipSpaces();
                if (pos >= s.size()) break;
                if (s[pos] == '*') { ++pos; val *= parseFactor(); }
                else if (s[pos] == '/') {
                    ++pos;
                    double d = parseFactor();
                    val = (d != 0.0) ? val / d : 0.0;
                } else break;
            }
            return val;
        }

        double parseExpr() {
            double val = parseTerm();
            while (true) {
                skipSpaces();
                if (pos >= s.size()) break;
                if (s[pos] == '+') { ++pos; val += parseTerm(); }
                else if (s[pos] == '-') { ++pos; val -= parseTerm(); }
                else break;
            }
            return val;
        }
    };

    Parser p{expr, 0, vars};
    return p.parseExpr();
}

// ── 类型解析辅助 ─────────────────────────────────────────────

GenericFrameParser::FieldType GenericFrameParser::parseFieldType(const QString& s)
{
    static const QMap<QString, FieldType> map{
        {"int16_le",   FieldType::Int16LE},
        {"int16_be",   FieldType::Int16BE},
        {"uint16_le",  FieldType::UInt16LE},
        {"uint16_be",  FieldType::UInt16BE},
        {"int32_le",   FieldType::Int32LE},
        {"int32_be",   FieldType::Int32BE},
        {"uint32_le",  FieldType::UInt32LE},
        {"uint32_be",  FieldType::UInt32BE},
        {"float32_le", FieldType::Float32LE},
        {"float64_le", FieldType::Float64LE},
    };
    auto it = map.find(s.toLower());
    if (it != map.end()) return it.value();
    qWarning() << "GenericFrameParser: 未知字段类型" << s << "，默认 int16_le";
    return FieldType::Int16LE;
}

GenericFrameParser::ChecksumType GenericFrameParser::parseChecksumType(const QString& s)
{
    if (s == "sum8") return ChecksumType::Sum8;
    if (s == "xor")  return ChecksumType::XOR;
    if (s == "crc16") return ChecksumType::CRC16;
    if (s == "none") return ChecksumType::None;
    return ChecksumType::Sum8;
}

// ── 工厂方法：从 JSON 构建解析器 ─────────────────────────────

GenericFrameParser GenericFrameParser::fromJson(const QJsonObject& protocolDef,
                                                 const QVariantMap& variableOverrides)
{
    GenericFrameParser parser;

    // 合并变量：协议默认 + 外部覆盖
    QMap<QString, double> vars;
    QJsonObject varObj = protocolDef.value("variables").toObject();
    for (auto it = varObj.begin(); it != varObj.end(); ++it)
        vars[it.key()] = it.value().toDouble();
    for (auto it = variableOverrides.begin(); it != variableOverrides.end(); ++it)
        vars[it.key()] = it.value().toDouble();

    // 解析分帧配置
    QJsonObject framingObj = protocolDef.value("framing").toObject();
    {
        QString syncHex = framingObj.value("sync_bytes").toString();
        parser.m_framing.syncBytes = QByteArray::fromHex(syncHex.toLatin1());

        parser.m_framing.idOffset      = framingObj.value("id_offset").toInt(2);
        parser.m_framing.idBytes       = framingObj.value("id_bytes").toInt(1);
        parser.m_framing.lengthOffset  = framingObj.value("length_offset").toInt(3);
        parser.m_framing.lengthBytes   = framingObj.value("length_bytes").toInt(1);
        parser.m_framing.lengthIncludesHeader = framingObj.value("length_includes_header").toBool(false);
        parser.m_framing.maxPayload    = framingObj.value("max_payload").toInt(64);

        QJsonObject csObj = framingObj.value("checksum").toObject();
        parser.m_framing.checksumType = parseChecksumType(
            csObj.value("type").toString("sum8"));
    }

    // 解析帧定义
    QJsonArray framesArr = protocolDef.value("frames").toArray();
    for (const QJsonValue& fv : framesArr) {
        QJsonObject fo = fv.toObject();
        FrameDef frame;
        frame.id      = fo.value("id").toInt();
        frame.name    = fo.value("name").toString();
        frame.trigger = fo.value("trigger").toBool(false);
        frame.expectedLength = fo.value("expected_length").toInt(-1);

        QJsonArray fieldsArr = fo.value("fields").toArray();
        for (const QJsonValue& fieldVal : fieldsArr) {
            QJsonObject fieldObj = fieldVal.toObject();
            FieldDef field;
            field.name   = fieldObj.value("name").toString();
            field.offset = fieldObj.value("offset").toInt(0);
            field.type   = parseFieldType(fieldObj.value("type").toString("int16_le"));
            field.scale  = evalScaleExpr(fieldObj.value("scale"), vars);
            frame.fields.append(field);
        }

        int idx = parser.m_frames.size();
        parser.m_frames.append(frame);
        parser.m_frameIdToIdx[frame.id] = idx;
    }

    // 解析 snapshot_order
    QJsonArray orderArr = protocolDef.value("snapshot_order").toArray();
    for (int i = 0; i < orderArr.size(); ++i) {
        QString name = orderArr[i].toString();
        parser.m_snapshotOrder.append(name);
        parser.m_fieldIndexMap[name] = i;
    }

    parser.m_valid = !parser.m_framing.syncBytes.isEmpty()
                  && !parser.m_frames.isEmpty()
                  && !parser.m_snapshotOrder.isEmpty();

    if (!parser.m_valid)
        qWarning() << "GenericFrameParser: 协议定义不完整，解析器无效";

    return parser;
}

// ── 字段值解码 ───────────────────────────────────────────────

double GenericFrameParser::decodeFieldValue(const QByteArray& payload,
                                             const FieldDef& field)
{
    const uint8_t* d = reinterpret_cast<const uint8_t*>(payload.constData());
    int off = field.offset;
    int remaining = payload.size() - off;

    switch (field.type) {
    case FieldType::Int16LE:
        if (remaining < 2) return 0.0;
        return static_cast<double>(static_cast<int16_t>(
            (uint16_t(d[off+1]) << 8) | d[off])) * field.scale;

    case FieldType::Int16BE:
        if (remaining < 2) return 0.0;
        return static_cast<double>(static_cast<int16_t>(
            (uint16_t(d[off]) << 8) | d[off+1])) * field.scale;

    case FieldType::UInt16LE:
        if (remaining < 2) return 0.0;
        return static_cast<double>(
            (uint16_t(d[off+1]) << 8) | d[off]) * field.scale;

    case FieldType::UInt16BE:
        if (remaining < 2) return 0.0;
        return static_cast<double>(
            (uint16_t(d[off]) << 8) | d[off+1]) * field.scale;

    case FieldType::Int32LE:
        if (remaining < 4) return 0.0;
        return static_cast<double>(static_cast<int32_t>(
            (uint32_t(d[off+3]) << 24) | (uint32_t(d[off+2]) << 16) |
            (uint32_t(d[off+1]) << 8)  |  uint32_t(d[off]))) * field.scale;

    case FieldType::Int32BE:
        if (remaining < 4) return 0.0;
        return static_cast<double>(static_cast<int32_t>(
            (uint32_t(d[off]) << 24) | (uint32_t(d[off+1]) << 16) |
            (uint32_t(d[off+2]) << 8) | uint32_t(d[off+3]))) * field.scale;

    case FieldType::UInt32LE:
        if (remaining < 4) return 0.0;
        return static_cast<double>(
            (uint32_t(d[off+3]) << 24) | (uint32_t(d[off+2]) << 16) |
            (uint32_t(d[off+1]) << 8)  |  uint32_t(d[off])) * field.scale;

    case FieldType::UInt32BE:
        if (remaining < 4) return 0.0;
        return static_cast<double>(
            (uint32_t(d[off]) << 24) | (uint32_t(d[off+1]) << 16) |
            (uint32_t(d[off+2]) << 8) | uint32_t(d[off+3])) * field.scale;

    case FieldType::Float32LE: {
        if (remaining < 4) return 0.0;
        float val;
        uint32_t raw = (uint32_t(d[off+3]) << 24) | (uint32_t(d[off+2]) << 16) |
                       (uint32_t(d[off+1]) << 8)  |  uint32_t(d[off]);
        std::memcpy(&val, &raw, sizeof(float));
        return static_cast<double>(val) * field.scale;
    }

    case FieldType::Float64LE: {
        if (remaining < 8) return 0.0;
        double val;
        uint64_t raw = 0;
        for (int i = 7; i >= 0; --i)
            raw = (raw << 8) | uint64_t(d[off + i]);
        std::memcpy(&val, &raw, sizeof(double));
        return val * field.scale;
    }
    }
    return 0.0;
}

// ── 分帧提取 ─────────────────────────────────────────────────

bool GenericFrameParser::tryExtractFrame(int& frameId, QByteArray& payload)
{
    const int syncLen = m_framing.syncBytes.size();
    if (syncLen == 0) return false;

    while (true) {
        int bufSize = m_buffer.size();

        int idx = m_buffer.indexOf(m_framing.syncBytes, m_parseOffset);
        if (idx < 0) {
            m_parseOffset = std::max(0, bufSize - syncLen);
            return false;
        }
        m_parseOffset = idx;

        // 帧头最小长度：sync + max(id_end, length_end)
        int headerEnd = std::max(
            m_framing.idOffset + m_framing.idBytes,
            m_framing.lengthOffset + m_framing.lengthBytes);

        if (bufSize - idx < headerEnd) return false;

        const uint8_t* buf = reinterpret_cast<const uint8_t*>(m_buffer.constData());

        // 读取帧 ID
        int fid = 0;
        for (int i = 0; i < m_framing.idBytes; ++i)
            fid = (fid << 8) | buf[idx + m_framing.idOffset + i];

        // 读取载荷长度
        int dataLen = 0;
        for (int i = 0; i < m_framing.lengthBytes; ++i)
            dataLen = (dataLen << 8) | buf[idx + m_framing.lengthOffset + i];

        if (m_framing.lengthIncludesHeader)
            dataLen -= headerEnd;

        if (dataLen < 0 || dataLen > m_framing.maxPayload) {
            m_parseOffset = idx + syncLen;
            continue;
        }

        // 载荷起始位于 header 之后
        int payloadStart = idx + headerEnd;
        int checksumLen = (m_framing.checksumType != ChecksumType::None) ? 1 : 0;
        int totalFrame = headerEnd + dataLen + checksumLen;

        if (bufSize - idx < totalFrame) return false;

        // 校验
        if (m_framing.checksumType == ChecksumType::Sum8) {
            uint8_t sum = 0;
            for (int i = idx; i < idx + totalFrame - 1; ++i)
                sum += buf[i];
            if (sum != buf[idx + totalFrame - 1]) {
                m_parseOffset = idx + syncLen;
                continue;
            }
        } else if (m_framing.checksumType == ChecksumType::XOR) {
            uint8_t xorVal = 0;
            for (int i = idx; i < idx + totalFrame - 1; ++i)
                xorVal ^= buf[i];
            if (xorVal != buf[idx + totalFrame - 1]) {
                m_parseOffset = idx + syncLen;
                continue;
            }
        }

        frameId = fid;
        payload = m_buffer.mid(payloadStart, dataLen);
        m_parseOffset = idx + totalFrame;
        return true;
    }
}

// ── 帧解码 ───────────────────────────────────────────────────

void GenericFrameParser::decodeFrame(const FrameDef& frame, const QByteArray& payload)
{
    if (frame.expectedLength >= 0 && payload.size() != frame.expectedLength) {
        qWarning() << "GenericFrameParser: 帧" << frame.name
                   << "载荷长度" << payload.size()
                   << "!= 期望" << frame.expectedLength;
        return;
    }
    for (const FieldDef& field : frame.fields) {
        double val = decodeFieldValue(payload, field);
        m_latestValues[field.name] = val;
    }
}

// ── 快照构建 ─────────────────────────────────────────────────

QList<double> GenericFrameParser::buildSnapshot() const
{
    QList<double> snap;
    snap.reserve(m_snapshotOrder.size());
    for (const QString& name : m_snapshotOrder)
        snap.append(m_latestValues.value(name, 0.0));
    return snap;
}

// ── 流式输入 ─────────────────────────────────────────────────

QList<QList<double>> GenericFrameParser::feed(const QByteArray& data)
{
    if (!m_valid) return {};

    m_buffer.append(data);
    m_parseOffset = 0;
    QList<QList<double>> snapshots;

    while (true) {
        int frameId;
        QByteArray payload;
        if (!tryExtractFrame(frameId, payload)) break;

        auto it = m_frameIdToIdx.find(frameId);
        if (it == m_frameIdToIdx.end()) continue;

        const FrameDef& frame = m_frames[it.value()];
        decodeFrame(frame, payload);

        if (frame.trigger)
            snapshots.append(buildSnapshot());
    }

    // 紧缩缓冲区
    if (m_parseOffset > 0) {
        m_buffer = m_buffer.mid(m_parseOffset);
        m_parseOffset = 0;
    }
    if (m_buffer.size() > 4096)
        m_buffer = m_buffer.right(2048);

    return snapshots;
}

// ── 公共查询 ─────────────────────────────────────────────────

int GenericFrameParser::fieldIndex(const QString& fieldName) const
{
    return m_fieldIndexMap.value(fieldName, -1);
}

QStringList GenericFrameParser::fieldNames() const
{
    return m_snapshotOrder;
}

void GenericFrameParser::reset()
{
    m_buffer.clear();
    m_parseOffset = 0;
    m_latestValues.clear();
}

QString GenericFrameParser::formatDebug(const QList<double>& snapshot) const
{
    QStringList parts;
    int n = std::min(snapshot.size(), m_snapshotOrder.size());
    for (int i = 0; i < n; ++i)
        parts << QString("%1=%2").arg(m_snapshotOrder[i]).arg(snapshot[i], 0, 'f', 3);
    return parts.join(' ');
}
